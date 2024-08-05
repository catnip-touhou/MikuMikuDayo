#include "yrz.hlsli"
#include "pmxDefs.hlsli"
#include "pmxBxDF.hlsli"
#include "pmxlight.hlsli"

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


[shader("miss")]
void Miss(inout Payload payload)
{
	payload.miss = true;
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
		//ガラス以外の場合、αに応じて透過・不透過の選択
		if ( (m.alpha < RNG.x) && (m.alpha<0.98))
			IgnoreHit();
		if (face<0 && !m.twosided)
			IgnoreHit();
	}
	//ガラスの場合、αや面の向きによらずヒットしたとみなす
}

/* callable shaderのテスト用
//ためしに構造体を小さくしてみる、うごいたのでデカくしてみた
//maxPayloadSizeまでの大きさに出来るらしい
struct Small {
	float4x4 big;
	float4x4 big1;
};

[shader("callable")]
void Callable(inout Small small){}

[shader("callable")]
void Callable2(inout Small small) {
	small.big = 0;
	small.big1= 1;
}
*/


static bool bShadow_hit;			// シャドウヒットフラグ

[shader("raygeneration")]
void RayGeneration()
{
	float3x3 camMat = {cameraRight, cameraUp, cameraForward};
	Payload payload = (Payload)0;

	/* テスト用
	Small smol;
	CallShader(1,smol);
	*/

	float2 d = (((DispatchRaysIndex().xy + RNG.xy) / DispatchRaysDimensions().xy) * 2 - 1);
	float aspectRatio = (DispatchRaysDimensions().x / (float)DispatchRaysDimensions().y);
	float3 rd = normalize(float3(d.x*aspectRatio,-d.y,fov));
	rd = mul(rd, camMat);

	RayDesc ray = {cameraPosition, 1e-3, rd, 1e+5};

	//パスの状態変数
	float3 Lt = 0, beta = 1;	//総寄与とスループット
	float eta = 1;	//現在レイの居る媒質の屈折率
	int waveCh = SampleWaveChannel(RNG.xy);	//サンプルされた波長
	bool SSSpassed = false;	//表面下散乱パスを通ったか？
	bool dispersed = false;	//分光するようなパスを通ったか？

	const int MAX_BOUNCE = 3;
	for (int i=0; i<MAX_BOUNCE; i++)
	{
		TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 0,1,0, ray, payload);

		// シャドウレイヒット⇒影を描画
		if(payload.shadow_hit)
		{
			bShadow_hit = true;
		}
		
		// ヒットミス
		if (payload.miss) {
			Lt += beta * FetchSkybox(rd);
			break;
		}
		
		Material m;
		float3x3 TBN;
		float3 Ng;
		if (payload.ID == ID_CHARACTER) {
			Ng = GeometryNormal(payload.iFace);
			GetPatch(payload.iFace,-rd,payload.uv, m, TBN);
		}
		else {
			Ng = GeometryNormal_st(payload.iFace);
			GetPatch_st(payload.iFace,-rd,payload.uv, m, TBN);
		}
		float Le1 = DirectLe1(m, -rd, TBN[2]);
		if (dot(Ng,rd) > 0) {
			Ng = -Ng;
			TBN = -TBN;
		}
		float3 P = ray.Origin + payload.t * rd;

		Lt += beta * m.emission * Le1;
		beta *= m.albedo;
		float pdf;
		rd = SampleCosWeightedHemisphere(TBN, RNG.xy, pdf);
		ray.Origin = P;

		/*
		BRDFOut B;
		SampleBRDF(payload, m, P, TBN, Ng, -rd, eta, waveCh, floor(RNG.x*3), SSSpassed, B);
		Lt += beta * m.emission * Le1;
		beta *= B.beta;
		eta = B.IOR;
		rd = B.L;
		dispersed |= B.dispersion;
		ray.Origin = B.SSS ? B.POut : P;
		*/

		ray.Direction = rd;

		//スループットがほぼ0になったら探索終了
		if (all(beta < 1e-10))
			break;
		
		//ロシアン
		if (i >= 3) {
			float q = clamp(RGBtoY(beta), 0.1,0.9);	//パスの継続確率
			if (q < RNG.x)
				break;
			else
				beta /= q;
		}
	}

	// シャドウレイヒット⇒影を描画
	if(payload.shadow_hit || bShadow_hit)
	{
		float shadow_Strength = 0.05f;
		Lt = lerp(Lt*0.8 ,Lt*0.01, 1.0 - exp(-shadow_Strength * payload.shadow_t));
	}

	// 分光
	if (dispersed && SpectralRendering) {
		float3 xyz = mul(Lt,RGB2XYZ);
		xyz *= WaveMISWeight(waveCh);
		Lt = mul(xyz, XYZ2RGB);
	}

	RTOutput[DispatchRaysIndex().xy] = float4(Lt, 1.f);
}


