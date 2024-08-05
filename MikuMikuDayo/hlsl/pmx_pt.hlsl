/* MIS Unidirectional Pathtracer版 */

#include "yrz.hlsli"
#include "PMXDefs.hlsli"

#include "PMXLight.hlsli"
#include "PMXBxDF.hlsli"

//#include "LensSystem.hlsl"

//レンズシステムによるro,rdへの変更
float3 LensSystem(float2 d, float2 Xi, out float3 ro, inout float3 rd, float3x3 camMat, int waveCh, out float2 flarePos, out float3 flareBeta)
{
	float aspectRatio = (resolution.x / resolution.y);
	rd = normalize(float3(d.x*aspectRatio,-d.y,fov));	//イメージセンサ上の1点からピンホールを通って出るレイ
	rd = mul(rd, camMat);

	float3 fp = cameraPosition + Pint * rd;	//ピントの合ってる位置
	float2 aperture;
	if(DofEnable == 1) {
		// DOF有効
		aperture = CosSin(Xi.x*2*PI) * sqrt(Xi.y) * LensR;
	}
	else {
		// DOF無効(影響を限りなく小さくする)
		aperture = CosSin(Xi.x*2*PI) * sqrt(Xi.y) * 0.0025;
	}
	ro = cameraPosition + aperture.x * cameraRight + aperture.y * cameraUp;	//開口上の点
	rd = normalize(fp-ro);	//開口上の点を通って焦点を通るレイを生成する

	flarePos = 0;
	flareBeta = 0;
	float2 auv = aperture / LensR / 2 + 0.5;	//絞りテクスチャ
	auv.y = 1-auv.y;
	return apertureTex.SampleLevel(samp, auv, 0).rgb * pow(dot(rd,cameraForward),4);	//cos四乗則
}

/* 基本のBRDFサンプリング用シェーダ */

[shader("miss")]
void Miss(inout Payload payload)
{
	payload.miss = true;
	payload.shadow_hit = false;
	payload.t = RayTCurrent();
}


[shader("closesthit")]
void ClosestHit(inout Payload payload, Attribute attr)
{
	payload.miss = false;
	payload.iFace = PrimitiveIndex();
	payload.uv = attr.uv;
	payload.ID = InstanceID();
	payload.t = RayTCurrent();

	// shadowの描画用 シャドウレイ発射
	payload.shadow_hit = false;
	if(ShadowEnable == 1) {
		// Check if the shadow ray intersects any geometry (except the original surface)
		float3 hitPosition = HitWorldPosition();
		float3 shadowRayDir = normalize(lightPosition - hitPosition);
		RayDesc shadowRay;
		shadowRay.Origin = hitPosition + shadowRayDir * 0.001; // Slightly offset to avoid self-intersection
		shadowRay.Direction = shadowRayDir;
		PayloadShadow shadowPayload;
		TraceRay(tlas, RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
			0xFF, 2, 1, 2, shadowRay, shadowPayload);	// shadow用のシェーダーインデックスは2
		payload.shadow_hit = shadowPayload.shadow_hit;
		payload.shadow_t = shadowPayload.shadow_t;
	}
}

[shader("anyhit")]
void AnyHit(inout Payload payload, Attribute attr)
{
	uint triangleIndex = PrimitiveIndex();

	Vertex v;
	Material m;
	float face;

	if (InstanceID() == ID_CHARACTER) {
		v = GetVertex(triangleIndex, attr.uv);
		m = GetMaterial(triangleIndex, v.uv);
		face = -dot(normalize(WorldRayDirection()),GeometryNormal(PrimitiveIndex()));
	}
	else if (InstanceID() == ID_STAGE) {
		v = GetVertex_st(triangleIndex, attr.uv);
		m = GetMaterial_st(triangleIndex, v.uv);
		face = -dot(normalize(WorldRayDirection()),GeometryNormal_st(PrimitiveIndex()));
	}
	else {
		IgnoreHit();
	}

	if (m.category != CAT_GLASS) {
		//SLABの場合、αに応じて透過・不透過の選択
		if ( (m.alpha < RNG.x) && (m.alpha<0.98))
			IgnoreHit();
		if ( face<0 && !m.twosided)
			IgnoreHit();
	}
	//ガラスの場合、αや面の向きによらずヒットしたとみなす
}

/* fog版BRDFサンプリング用シェーダ */
[shader("closesthit")]
void ClosestHitFog(inout Payload payload, Attribute attr)
{
	payload.t = RayTCurrent();
	payload.ID = InstanceID();
	payload.iFace = -1;
}

[shader("intersection")]
void IntersectFog()
{
	//とりあえずフォグは後回し
	return;

	Attribute attr = (Attribute)0;
	float3 rd = normalize(WorldRayDirection());
	float3 P = WorldRayOrigin() + RayTCurrent() * rd;

	float4 Xi = Hash4(uint4(DispatchRaysIndex().xy, iFrame, 0) + uint4(0,P*1000));
	float d = -log(1-Xi.x) * 100;
	ReportHit(d, 0, attr);
}


static bool bShadow_hit;			// シャドウヒットフラグ

[shader("raygeneration")]
void RayGeneration()
{
	Payload payload = (Payload)0;

	//パスの最初から終わりまでで変わらない情報のセットアップ
	float2 d = (((DispatchRaysIndex().xy + RNG.xy) / DispatchRaysDimensions().xy) * 2 - 1);
	float3x3 camMat = {cameraRight, cameraUp, cameraForward};
	int waveCh = SampleWaveChannel(RNG.xy); //波長
	int sssCh = floor(RNG.x*3);	//SSS用のRGB波長

	//パスの状態
	float3 Lt = 0;		//パスの寄与
	float3 beta = 1;	//スループット
	float IOR = 1;
	float3 ro,rd;
	bool SSSPassed = false;
	bool disperse = false;
	bool isMirror = false;

	float3 filteredLt = 0;	//モニター用。特定のバウンス回で得た寄与を求める時にどうぞ

	//レンズシステムからレイの発射位置・方向を決める
	float2 flarePos = 0;
	float3 flareBeta = 0;
	//LensSystemなし版
	//ro = cameraPosition;
	//float aspectRatio = (resolution.x / resolution.y);
	//rd = normalize(float3(d.x*aspectRatio,-d.y,fov));	//イメージセンサ上の1点からピンホールを通って出るレイ
	//rd = mul(rd, camMat);
	//あり版
	beta = LensSystem(d, RNG.xy, ro, rd, camMat, waveCh, flarePos, flareBeta);
	//絞りに遮られた
	if (all(beta==0))
		return;

	//最後にレイがぶっかったパッチについての情報
	float3 P = ro;				//位置
	float3x3 TBN = 0;			//接空間
	Material m = (Material)0;	//マテリアル
	BRDFOut B = (BRDFOut)0;		//パッチ表面からどっちにレイが反射するかBRDFサンプリングした結果格納用
	float3 prevBeta = beta;
	float3 V;

	//MISによる寄与の計算用
	float pdf_light = 0;
	float pdf_brdf = 0;

	//出発！
	for (int i=0; i<32; i++) {
		bool toTheSky = false;

		//「今のパッチ」から「次のパッチ」へレイを飛ばす
		RayDesc ray;
		ray.Origin = ro; ray.Direction = rd;
		ray.TMin = 1e-3; ray.TMax = 1e+5;
		TraceRay(tlas,RAY_FLAG_NONE,0xFF, 0,1,0,ray,payload);

		// シャドウレイヒット⇒影を描画
		if(payload.shadow_hit)
		{
			bShadow_hit = true;
		}

		//「次のパッチ」についての情報
		float3 nP,nN,Ng;
		float3x3 nTBN;
		Material nM;
		float3 nV = -rd;
		float Le1;

		//ミスってたらskyboxからの寄与を得て終了フラグを立てる
		if (payload.miss) {
			Le1 = 1;
			nM.emission = FetchSkybox(rd);
			toTheSky = true;
		} else {
			//レイがぶっかった点(次のパッチ)のマテリアルを得る
			if(payload.ID == ID_CHARACTER) {
				GetPatch(payload.iFace, nV, payload.uv, nM, nTBN);
			}
			else {
				GetPatch_st(payload.iFace, nV, payload.uv, nM, nTBN);
			}
			//法線を反転させる前に配光特性の計算
			Le1 = DirectLe1(nM,nV,nTBN[2]);
			//面をレイの方向に反転させる
			if(payload.ID == ID_CHARACTER) {
				Ng = GeometryNormal(payload.iFace);
			}
			else {
				Ng = GeometryNormal_st(payload.iFace);
			}
			if (dot(nV,Ng) < 0)
				nTBN = -nTBN;
			nN = nTBN[2];
			nP = ro + rd * payload.t;
		}

		if (i==0 || B.isMirror) {
			//発光体にカメラから出たレイが直撃 or 完全鏡面反射パスなのでNEE不可
			Lt += beta * nM.emission * Le1;
		} else {
			//NEE ... 今のパッチについて 次のパッチの発光要素から寄与を得る戦略と、どこか別のライトからの寄与を得る戦略でMIS

			//BRDFサンプリング：次のパッチの発光要素から寄与を得る戦略
			float3 f_brdf = B.beta * pdf_brdf * nM.emission * Le1;

			//光源サンプリング：どこか別のライトからの寄与を得る戦略
			float3 lightPos;	//サンプルされた光源の位置
			float3 f_light;
			float lightSelectionPDF;
			int nlights;
			if(payload.ID == ID_CHARACTER) {
				lightSelectionPDF = nLights > 0 ? 0.5 : 1;	//光源ポリゴンが1つでも有る場合は、光源ポリゴン50%、skybox50%でサンプリングする
				nlights = nLights;
			}
			else {
				lightSelectionPDF = nLights_st > 0 ? 0.5 : 1;	//光源ポリゴンが1つでも有る場合は、光源ポリゴン50%、skybox50%でサンプリングする
				nlights = nLights_st;
			}
			bool sky;
			if (nlights>0 && RNG.x<0.5) {
				sky = false;
				f_light =  DirectLight(LIGHT_EMISSIVE, payload, waveCh, m, B, P, TBN, V, IOR, pdf_light, lightPos);
			} else {
				sky = true;
				f_light =  DirectLight(LIGHT_SKYBOX, payload, waveCh, m, B, P, TBN, V, IOR, pdf_light, lightPos);
			}
			pdf_light*=lightSelectionPDF;

			//今わかっているのは
			//f_brdf : BRDFサンプリングの結果, pdf_brdf : BRDFサンプリングのpdf
			//f_light : 光源サンプリングの結果, pdf_light : 光源サンプリングのpdf

			//MISにさらに必要なのは
			//BRDFサンプリングによって、さっきサンプリングされた光源をサンプルするpdf : pdf_b
			//光源サンプリングによって、発光ポリゴンをサンプルするpdf : pdf_l

			//pdf_bの計算
			float3 L = normalize(lightPos-P);	//光源サンプリングの結果がskyboxかどうかに関わらずこれで光源の方向は求まる
			float pdf_b;
			pdf_b = BxDFPDF(waveCh, m, TBN, V, IOR, L, 1, !B.SSS && B.BSSRDF, B.SSS);
			
			//pdf_lの計算
			float pdf_l = 0;
			if (toTheSky) {
				pdf_l = SkyPDF(rd) * lightSelectionPDF;
			} else {
				if(payload.ID == ID_CHARACTER) {
					int iLight = faces[payload.iFace].iLight;
					if (iLight >= 0) {
						float area = LightArea(payload, iLight);
						float3 dl = nP-P;
						pdf_l = dot(dl,dl) / max(area * abs(dot(nN, L)),1e-10) * lights[iLight].pdf * lightSelectionPDF;
					}
				}
				else {
					int iLight = faces_st[payload.iFace].iLight;
					if (iLight >= 0) {
						float area = LightArea(payload, iLight);
						float3 dl = nP-P;
						pdf_l = dot(dl,dl) / max(area * abs(dot(nN, L)),1e-10) * lights[iLight].pdf * lightSelectionPDF;
					}
				}
			}

			//バランスヒューリスティック
			float3 cb = f_brdf / (pdf_brdf + pdf_l);	//brdfサンプリングによるMISウェイト込みの寄与、の半分
			float3 cl = f_light / (pdf_b + pdf_light);	//光源サンプリングによるMISウェイト込みの寄与、の半分
			Lt += (cb+cl) * prevBeta;	//寄与の半分ずつが得られるので足す
			//Lt += f_brdf / pdf_brdf * prevBeta;
			//Lt += f_light / pdf_light * prevBeta;
		}
		
		if (toTheSky)
			break;

		//BRDFサンプリングで次の次のパッチへの方向を決める
		SampleBRDF(payload, nM, nP, nTBN, Ng, nV, IOR, waveCh, sssCh, SSSPassed, B);

		//パスの状態更新
		pdf_brdf = B.pdf;
		ro = B.SSS ? B.POut : nP;
		rd = B.L;
		IOR = B.IOR;
		prevBeta = beta;
		beta *= B.beta;
		disperse = disperse || B.dispersion;
		isMirror = any(m.roughness < 1e-3) && (m.category == CAT_GLASS || m.category == CAT_METAL);
		P = ro;
		TBN = nTBN;
		m = nM;
		V = nV;

		//ロシアン
		if (i>=3) {
			float rr = min(1,RGBtoY(beta));
			if (RNG.x > rr)
				break;
			else 
				beta /= rr;
		}

		//打ち切り処理
		if (all(beta == 0))
			break;
		
	}

	//後は結果の格納！

	//分光
	if (disperse && SpectralRendering) {
		float3 xyz = mul(Lt,RGB2XYZ);
		xyz *= WaveMISWeight(waveCh);
		// ステージを読み込むと発散してしまうので, ゲインをかけた成分を上乗せするだけにする
		xyz *= 0.25;
		float3 Lt_ofs = mul(xyz, XYZ2RGB);
		Lt += Lt_ofs;
	}

	// シャドウレイヒット⇒影を描画
	if(payload.shadow_hit || bShadow_hit)
	{
		float shadow_Strength = 0.05f;
		Lt = lerp(Lt*0.8 ,Lt*0.01, 1.0 - exp(-shadow_Strength * payload.shadow_t));
	}

	//露出補正などをやって結果発表
	float exposure = 1;//1/RGBtoY(SkyboxSH[0].c[0].rgb);
	float3 result = Lt * exposure;
	result = clamp(result, /*-100*/0,100);	//bias入りfirefly対策

	//マイナスの値もそのままアキュムレーションする(そうしないと分光の結果が赤っぽくなる)
	// ⇒マイナスの値をアキュムレーションすると、累積を取ったときに真っ黒になってしまうため、0でクランプする
	if (all(isnan(result) == false))
		RTOutput[DispatchRaysIndex().xy] = float4(result, 1);

	//レンズフレアの値を足す
	if (all(flarePos == clamp(flarePos,-1,1))) {
		float2 fp = clamp(((flarePos+1)*0.5) * DispatchRaysDimensions().xy, 0, DispatchRaysDimensions().xy-1);
		float3 xyz = mul(flareBeta,RGB2XYZ);
		xyz *= WaveMISWeight(waveCh);
		flareBeta = mul(xyz, XYZ2RGB);
		FlareOutput[fp] = float4(flareBeta * exposure, 1);
	}
}
