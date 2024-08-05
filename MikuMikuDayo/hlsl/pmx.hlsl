/* bidirectional pathtracer版 */

#include "yrz.hlsli"
#include "PMXDefs.hlsli"

#include "PMXLight.hlsli"
#include "PMXBxDF.hlsli"

//#include "LensSystem.hlsl"
//#include "RandomWalkSSS.hlsl"

/***************************************************************************
	参考文献

Eric Veach氏 "Robust Monte Carlo Methods for Light Transport Simulation"
https://graphics.stanford.edu/papers/veach_thesis/

pbrt "16.3 Bidirectional Path Tracing"
https://pbr-book.org/3ed-2018/Light_Transport_III_Bidirectional_Methods/Bidirectional_Path_Tracing

****************************************************************************/


/***************************************************************************
	定数
****************************************************************************/
//最大探索回数
#define MAX_DEPTH 32
static float SkyRadius = 1e+5;	//skyboxの仮想半径
static float CLAMP_L = 100;	//ファイアフライ対策用1サンプルの最大輝度

/***************************************************************************
	構造体
****************************************************************************/
//散乱イベント, 論文での頂点y[i],z[i]に対応
//コメントでx[i]と書いたらyでもzでもどっちでもいい頂点を指す
struct Event {
	float3 beta;	//論文でのαi+1LまたはEに対応 i+1 である点に注意
	float PG;		//pdf[1/sr] * G(i-1→i) 前の頂点から自分がサンプルされるpdf[1/m²]
	float PGrev;	//pdf[1/sr] * G(i+1→i) 後の頂点から自分がサンプルされるpdf[1/m²]
	float3 P;		//位置
	float3x3 TBN;	//接空間(0:Tangent 1:Binormal, 2:Normal)(法線マップ・スムージング込み)
	float3 Ng;		//ポリゴン自体の法線(法線マップ・スムージング抜き)
	bool S;			//完全鏡面反射・透過フラグ
	bool sky;		//skyboxのどこかですフラグ
	float etaI,etaO;//入射前後の媒質屈折率
	Material m;		//マテリアル
	int iFace;		//面番号
	float rusQ;		//ロシアンルーレットによるPDFへの倍率
	bool SSSOut;	//表面下散乱の出口パッチであるフラグ
	bool SSSIn;		//表面下散乱の入口パッチであるフラグ
};

/***************************************************************************
	Miss, CHS, AnyHyit, Intersectionシェーダ
****************************************************************************/
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
	payload.miss = false;	//BDPTでは大事(ライトサブパスとカメラサブパスを作るので、フラグを消さないと片方のサブパスが作られ無くなる)
	payload.ID = InstanceID();
	payload.iFace = PrimitiveIndex();
	payload.uv = attr.uv;
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

	payload.ID = InstanceID();
	if(payload.ID == ID_CHARACTER) {
		v = GetVertex(triangleIndex, attr.uv);
		m = GetMaterial(triangleIndex, v.uv);
		face = -dot(normalize(WorldRayDirection()),GeometryNormal(PrimitiveIndex()));
	}
	else
	{
		v = GetVertex_st(triangleIndex, attr.uv);
		m = GetMaterial_st(triangleIndex, v.uv);
		face = -dot(normalize(WorldRayDirection()),GeometryNormal_st(PrimitiveIndex()));
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


/***************************************************************************
	ユーティリティ関数
****************************************************************************/
//可視判定抜きの幾何項
//xとyはどっちがどっちでも同じ値が返る
//gxとgyはx,yについてコサイン項を考慮するならtrue
//skyをtrueにすると、無限遠光源との接続という事で距離項を加味しない
float GeometryTerm(float3 x, float3 y, float3 Nx, float3 Ny, bool gx, bool gy, bool sky)
{
	float3 D = x-y;
	float3 w = normalize(D);
	
	float d2 = sky ? 1 : dot(D,D);
	float wox = gx ? abs(dot(w,Nx)) : 1;
	float woy = gy ? abs(dot(w,Ny)) : 1;

	return abs( wox * woy / max(1e-10,d2));
}

//コネクト時の可視項
//y:光源側頂点
//z:カメラ側頂点
//sideCheck : 光源の裏表のチェック、yがy0の時はtrue ... 現在はLightLe1でやっているので不要かも
float3 VisibilityTerm(inout Payload payload, Event y, Event z,bool sideCheck)
{
	float d;
	float3 L = LenDir(y.P-z.P, d);	//パッチ→光源

	//ライトポリゴンが片面材質の場合はライト法線の方から見えるのかチェック
	if (sideCheck && (dot(L, y.TBN[2])>0) && !y.m.twosided && !y.sky)
		return 0;

	//ライトポリゴンが遮られていないかテスト
	RayDesc shadowDesc;
	shadowDesc.Origin = z.P;	//zの表面からyに向けて粗く撃つ
	shadowDesc.Direction = L;
	float eps = max(1e-3, d * lerp(1e-3,1e-4,RNG.x));		//22日昼修正、ランダム性を戻した
	shadowDesc.TMin = eps;
	shadowDesc.TMax = d-eps;
	PayloadShadow payshadow;
	payshadow.xi = floor(RNG.x * 0x7FFFFFFF);
	payshadow.Tr = 1; 
	TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 2,0,2, shadowDesc, payshadow);

	if (any(payshadow.Tr != 1))
		return payshadow.Tr;
	
	//リーク対策、epsが大きい場合は近場からもう一回撃つ(z.Pの表面から短い範囲に正確に撃つ)
	if (eps > 0.01) {
		shadowDesc.TMin = 1e-3;
		shadowDesc.TMax = d - 1e-3;
		payshadow.Tr = 1;
		payshadow.xi = floor(RNG.x * 0x7FFFFFFF);
		TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 2,0,2, shadowDesc, payshadow);
	}
	return payshadow.Tr;
}


//Pσ(e→ne)をPA(e→ne)にする
float ConvertPDF(float pdf, Event e, Event ne)
{
	float3 v = ne.P - e.P;
	float3 w = normalize(v);
	float d2 = dot(v,v);
	float3 N = ne.TBN[2];

	if (!ne.sky)
		return pdf * abs(dot(N, w)) / d2;	//e.N・wが無いのは pσ(e→ne) = pσ⊥(e→ne)|e.N・w|だからpdfに含まれている
	else
		return pdf;
}

//x0→x1→x2方向にレイが飛ぶ確率密度(測度は面積) Pσ⊥(x1→x2)G(x0→x1→x2)
float PG(int waveCh, Event x0, Event x1, Event x2, bool forward)
{
	if (x1.S)
		return 0;

	float3 wi = normalize(x0.P-x1.P);
	float3 wo = normalize(x2.P-x1.P);
	float3 N = x1.TBN[2];
	N = dot(x1.Ng, wi) > 0 ? N : -N;
	float pdf = BxDFPDF(waveCh, x1.m, ComputeTBN(N), wi, x1.etaI, wo, x1.etaO, x1.SSSIn, x1.SSSOut) * x1.rusQ;
	return ConvertPDF(pdf, x1, x2);
}


//0だったら1にするだけの関数。S頂点のpdf計算をスルーさせるための関数
float Remap0(float x)
{
	if (x==0)
		return 1;
	return x;
}

//シェーディング法線使用の際にadjoint BSDFを補正する関数
float CorrectShadingNormal(float3 N, float3 Ng, float3 wi, float3 wo)
{
	float3 tmp = wi;
	wi = wo;
	wo = tmp;
	return abs(dot(wo,N) * dot(wi,Ng)) / max(1e-10,abs(dot(wo,Ng) * dot(wi,N)));
}


/***************************************************************************
	アイサブパス用
****************************************************************************/

//イメージセンサとレンズのサイズ
static float AspectRatio = resolution.x / resolution.y;
static float2 FilmSize = {0.3*AspectRatio, 0.3};	//縦24mmをMMD長さに変換したもの
static float FilmArea = FilmSize.x * FilmSize.y;
static float2 PixelOnFilmSize = FilmSize / resolution;	//イメージセンサ上の1画素のサイズ[MMD/pix]
static float PixelOnFilmArea = PixelOnFilmSize.x * PixelOnFilmSize.y;
static float FilmDistance = fov * FilmSize.y / 2;	//レンズ→センサ間距離[MMD]
static float LensArea = PI*sqr(LensR);
static float LensArea_noDOF = PI*sqr(0.0025);

//cameraPositionはどこか？... レンズの中心

//フィルム上の座標(カメラを手に持って構えて感光面を裏側から見た時の左上が0、右下が1)からビュー座標へ
float3 FilmToView(float2 f)
{
	return float3( (f - 0.5) * float2(FilmSize.x,-FilmSize.y), -FilmDistance);
}

void WritePixel(RWTexture2D<float4> rwtex, uint2 p, float4 c)
{
	uint2 b;
	rwtex.GetDimensions(b.x,b.y);
	if (all(p == clamp(p,0,b-1)))
		rwtex[p] += c;
}

//補間付書き込み…デノイズが効きづらくなるのでやめた
void WritePixelLerp(RWTexture2D<float4> rwtex, float2 pos, float4 c)
{
	if ( any(floor(pos)<0) || any(!isfinite(c)) )
		return;

	uint2 o = floor(pos);
	float2 f = pos-o;
	uint3 d = {1,1,0};

	WritePixel(rwtex, o        , c * (1-f.x) * (1-f.y));
	WritePixel(rwtex, o + d.xz , c * f.x * (1-f.y));
	WritePixel(rwtex, o + d.zy , c * (1-f.x) * f.y);
	WritePixel(rwtex, o + d.xy , c * f.x * f.y);
}


//t=1パス用、Pの位置から飛んでくるLtをレンズ上の点LensPを通してイメージセンサでキャッチ
void AddFlare(inout Payload payload, float3 Lt, float3 P, float3 LensP, int waveCh)
{
	float lensR;
	float3x3 camMat = {cameraRight, cameraUp, cameraForward};

	//カメラ座標系にする
	LensP = mul(camMat, LensP-cameraPosition);
	P = mul(camMat, P-cameraPosition);
	if (P.z <= 0.01)
		return;

	if(DofEnable==1) {
		// DOF有効
		lensR = LensR;
	}
	else {
		// DOF無効(影響を限りなく小さくする)
		lensR = 0.0025;
	}
	
	float2 lens = lensR ? LensP.xy/lensR : 0;	//レンズの範囲を-1～1とするレンズ座標 → 錯乱円上の位置

	//cos項(We0に入れられなかった分)
	Lt *= abs(normalize(P-LensP).z);	//5昼修正

	//錯乱円のフィルム上でのサイズを求める
	float m = FilmDistance / abs(length(P)-FilmDistance);	//像の拡大率
	float cocMMD = lensR * abs(Pint-length(P)) / Pint * m;	//錯乱円の半径[MMD]
	float2 coc = cocMMD / FilmSize.y * float2(lens.x,-lens.y);	//フィルムの大きさを1としてスケール
	float2 d = ViewToUV(P, fov, AspectRatio);	//Pのフィルム座標(左上が0、右下が1)

	float2 fp = (d+coc) * resolution;
	int2 pix = round(fp);	//Ltが格納されるピクセル

	//はみだしちゃった
	if (any(pix!=clamp(pix,0,resolution-1)))
		return;

	/* 分光 */
	if (SpectralRendering) {
		float3 xyz = mul(Lt,RGB2XYZ);
		xyz *= WaveMISWeight(waveCh);
		Lt = mul(xyz, XYZ2RGB);
	}

	//WritePixelLerp(FlareOutput, fp, float4(Lt,1));
	if (all(isfinite(Lt) == true))
		FlareOutput[pix].xyz += Lt;
}

//カメラ側からz0→z1の方向rdをサンプルするpdf Pσ(z0→z1)
//測度は立体角
float CameraPDFDirectionalRd(Event z0, float3 rd)
{
	float cosT = dot(z0.TBN[2], rd);

	//z1はカメラの裏側である
	if (cosT < 0)
		return 0;

	//We(0)にresolution.x,yを掛ける方法だと寄与の計算中にアンダーフローを起こしがちなので
	//pdfを小さくするようにした
	//return sqr(FilmDistance) / ( PixelOnFilmArea * cosT * cosT * cosT);
	//return sqr(FilmDistance) / ( FilmArea * cosT * cosT * cosT);
	
	//↑を正直に実装すると全体的に暗くなるのでとりあえずcos^4乗則分だけ入れることにした
	//フィルムを大きくしたり画角を上げ下げした時や解像度を上げた時の画素当たりの光の量の変化を表現できるので
	//活かしたい気もする
	return 1 / (cosT * cosT * cosT);
}

//z0からz1をサンプルするpdf Pσ⊥(z0→z1)G(z0⇔z1), 測度は面積
float CameraPDFDirectional(Event z0, Event z1)
{
	float3 d = z1.P-z0.P;
	float3 rd = normalize(d);
	float p = CameraPDFDirectionalRd(z0, rd);
	float g = abs(dot(z1.TBN[2],rd)) / max(1e-10,dot(d,d));
	return p*g;
}

//t=1パスのためのZ0サンプリング
//rdが不明な状態でレンズ上の一点をサンプリングする
//この時点ではrdが不明でcos項が分からないので、addFlareで後入れする
Event SampleZ0(inout Payload payload)
{
	Event e = (Event)0;
	float2 Xi = RNG.xy;
	float2 lens = CosSin(Xi.x*2*PI) * sqrt(Xi.y);

	//TODO なんかおかしい 24昼修正
	float2 auv = lens * 0.5 + 0.5;
	auv.y = 1-auv.y;
	float3 sibori = apertureTex.SampleLevel(samp, auv, 0).xyz;

	if(DofEnable==1) {
		// DOF有効
		e.P = cameraPosition + (lens.x*cameraRight + lens.y*cameraUp) * LensR;
		e.PG = LensArea ? 1/LensArea : 1;
	}
	else {
		// DOF無効(影響を限りなく小さくする)
		e.P = cameraPosition + (lens.x*cameraRight + lens.y*cameraUp) * 0.0025;
		e.PG = LensArea_noDOF ? 1/LensArea_noDOF : 1;
	}
	//本来↓はshibori / e.PGになる。レンズがデカくなれば入ってくる光の量が増えるという理屈
	e.beta = sibori;	//注意：cosθが入ってない

	e.etaI = e.etaO = 1;
	e.TBN = float3x3(cameraRight, cameraUp, cameraForward);
	e.Ng = e.TBN[2];
	e.rusQ = 1;
	return e;
}

//最初のカメラ頂点をサンプルする
Event SampleFirstCamera(inout Payload payload, out float3 rd, out float fs1, out float pdf1, out float2 writePos)
{
	float3x3 camMat = {cameraRight, cameraUp, cameraForward};
	Event e = (Event)0;
	
	//SampleZ0でレンズ上の位置をサンプル -> rdの決定 -> cos項をbetaに追加する
	e = SampleZ0(payload);

	writePos = DispatchRaysIndex().xy + RNG.xy;	//画素の書き込み位置[pix]
	float2 d = 1 - writePos / DispatchRaysDimensions().xy;	//画素に対応するフィルム座標(像が倒立するので反転する)
	float3 sensor = FilmToView(d);			//レイの出発点になるイメージセンサ上の位置(カメラ座標)
	float3 pinhall = -normalize(sensor);	//カメラ座標の原点にある仮想のピンホールを通って飛ぶレイ
	float3 fp = pinhall * Pint;				//ピントの合う位置(カメラ座標)
	rd = normalize(mul(fp,camMat) + cameraPosition - e.P);	//レンズ上のサンプル位置からfpを通るレイ(ワールド座標)
	e.beta *= dot(rd,cameraForward);	//cos項を入れる(pdfと合わせてcos^4)

	pdf1 = CameraPDFDirectionalRd(e,rd);	//Pσ(z0→z1)
	fs1 = 1;

	return e;
}

/***************************************************************************
	ライトサブパス作成用
****************************************************************************/
//ライトポリゴン・skyboxそれぞれを選択するpdf
static float LightSelectionPDF = nLights ? 0.5 : 0;
static float SkyboxSelectionPDF = 1 - LightSelectionPDF;
static float LightSelectionPDF_st = nLights_st ? 0.5 : 0;
static float SkyboxSelectionPDF_st = 1 - LightSelectionPDF_st;

//光源の発射点を選択するpdf[1/m2]を返す
//iFace:ライトポリゴンの面番号、skyboxの場合0未満の値
//rd:skyboxの場合のサンプリングした方角。ライトポリゴンの場合は未使用
float lightPDFOrigin(Payload payload, int iFace, float3 rd = 0)
{
	if(payload.ID == ID_CHARACTER) {
		if (iFace < 0) {
			//skyboxだった
			return SkyPDF(rd) * SkyboxSelectionPDF;
		} else {
			if (nLights == 0)
				return 0;
			
			int iLight = faces[iFace].iLight;
			if (iLight >= 0) {
				float area = LightArea(payload, iLight);
				return lights[iLight].pdf/max(1e-10,area) * LightSelectionPDF;
			} else
				return 0;
		}
	}
	else {
		if (iFace < 0) {
			//skyboxだった
			return SkyPDF(rd) * SkyboxSelectionPDF_st;
		} else {
			if (nLights_st == 0)
				return 0;
			
			int iLight = faces_st[iFace].iLight;
			if (iLight >= 0) {
				float area = LightArea(payload, iLight);
				return lights[iLight].pdf/max(1e-10,area) * LightSelectionPDF_st;
			} else
				return 0;
		}
	}

}

//光源の発射方向を選択するpdf[1/m2]を返す
//y0:光の発射点 y1:光の到達点
float lightPDFDirectioal(Event y0, Event y1)
{
	//そもそも発光体ではない
	if (all(y0.m.emission<1e-10))
		return 0;

	//Y0→Y1に飛んでくるpdf
	float3 d = y1.P - y0.P;
	float3 wi = normalize(d);
	float NoL0 = dot(wi,y0.TBN[2]);
	float NoL1 = dot(wi,y1.TBN[2]);
	float pdf;

	//skyboxからの光の場合
	if (y0.sky)
		return abs(NoL1) / (PI*SceneRadius*SceneRadius);
	
	//ライトポリゴンの場合
	//前提:どの方向から見ても輝度が一定になるには面光源の場合は
	//     光源の法線に対する射出方向θについて放射強度I(θ)=cosθ/πに比例する
	if (abs(NoL0) < y0.m.lightCosFalloff)	//コーンの外
		return 0;
	else if (y0.m.twosided)	//twosidedの場合は全球に拡散
		pdf = 1 / (4*PI*(1-y0.m.lightCosFalloff));
	else if (NoL0>=0)	//そうでない場合、y0の法線方向の半球へ拡散
		pdf = 1 / (2*PI*(1-y0.m.lightCosFalloff));
	else
		return 0;		//照射されない方向から眺めている
	
	//Pσ = Pσ⊥N・Lなので、pdf=Pσの中にNoL0は入ってる
	return pdf * abs(NoL1) / max(1e-10,dot(d,d));	//測度を立体角→面積に変換
}

//Lはy0→y1ベクトル(正規化済み)として、配光特性L1(y0→y1) = fs(y-1→y0→y1)を返す
float lightLe1L(Event y0, float3 L)
{
	//skyboxの場合は均等拡散あつかい
	if (y0.sky)
		return 1/PI;

	//片面光源を裏側から見てる
	float NoL = dot(y0.TBN[2], L);
	if (!y0.m.twosided && NoL<0)
		return 0;
	
	//コーンの外側から見ている
	float cosT = abs(NoL);
	float ctF = y0.m.lightCosFalloff;
	if (cosT <= ctF)
		return 0;

	float ctH = max(y0.m.lightCosHotspot,ctF);	//θH < θF → cosH > cosFを必ず満たすようにする
	float delta = (ctH == ctF) ? 1 : saturate((cosT-ctF)/(ctH-ctF));
	//float Le1 = sqr(delta) * sqr(delta) / (2*PI*(1-(ctF+ctH)/2));
	float Le1 = sqr(delta) * sqr(delta) /  ( ((1-sqr(ctF)) + (1-sqr(ctH))) / 2 );

	if (y0.m.twosided)
		Le1 /= 2;

	return Le1/PI;
}

//発光面y0から受光面y1への配光特性Le1(y0→y1)
float lightLe1(Event y0, Event y1)
{
	float3 L = normalize(y1.P-y0.P);
	return lightLe1L(y0, L);
}

//発光面y0の放射発散度を計算する
float3 lightLe0(Event y0)
{
	//skyboxの場合は均等拡散光源扱い、Le0=π emission, Le1(ω)はωによらず1/π ... skyboxに対するcosθは常に1
	if (y0.sky)
		return y0.m.emission * PI;
	
	//pbrtによればspoltlightの全放射束Φ ≒ I * 2π(1-(ctF+chH)/2) 但し、Iは減衰なしの場合の放射強度
	// I = Φ / 2π(1-(ctF+ctH)/2)
	// I(θ) = I * delta^4
	//
	// 面光源の場合 L(θ) = L cosθ f(θ) ... f(θ)はコーンによるθ毎の減衰係数、まずは θ<θF→1 else 0 と定義する
	// Le0 = ∫∫L f(θ) sinθcosθ dθdφ = 2πL ∫f(θ)sinθcosθ dθ, cosθ = μとおくと
	//      1                    1
	// 2πL ∫ f(μ) μ dμ = πL [μ^2]  = πL(1-μF^2)
	//      0                    μF

	//float3 Le0 = y0.m.emission * 2 * PI * (1 - (y0.m.lightCosFalloff+y0.m.lightCosHotspot)/2);
	float3 Le0 = y0.m.emission * PI * ((1-sqr(y0.m.lightCosFalloff)) + (1-sqr(y0.m.lightCosHotspot))) / 2;
	if (y0.m.twosided)
		Le0 *= 2;
	return Le0;
}

//光源側の最初の頂点y0を得て、ついでにy1の方向もサンプリングする
//入力 ro:カメラの位置(現状では何にも使われてない)
//出力 rd:y0→y1の方向, fs1:配光特性Le1(y0→y1), pdf1:rd選択時のPσ(y0→y1)
//返り値は頂点情報
Event SampleFirstLight(inout Payload payload, out float3 rd, out float fs1, out float pdf1)
{
	Event e = (Event)0;
	e.etaI = e.etaO = 1;
	e.PGrev = 1;	//仮に1と入れとく。後で決定する
	e.rusQ = 1;

	float4 Xi = RNG;
	float3 L;
	float skyboxSelectionPDF;
	if(payload.ID == ID_CHARACTER) {
		skyboxSelectionPDF = SkyboxSelectionPDF;
	}
	else {
		skyboxSelectionPDF = SkyboxSelectionPDF_st;
	}
	//skyboxかライトか
	if (Xi.x < skyboxSelectionPDF) {
		//skybox
		L = SampleSky(payload, 0, e.PG, e.m.emission, false);
		e.PG *= skyboxSelectionPDF;	//Pσ(L)*選択確率のまま入れとく、後でPGFwdの計算などの際に工夫する(理由は↓)
		//本来↑は、1/面積なので1/∞になる
		//skybox上の点y0をサンプルする面積についてのpdfが必要になるのはMISウェイトを計算する時だけ
		//MISウェイトを計算する時は常に PA(y0) / PA(y1→y0) という比率として計算される
		//分母のPA(x0)はskyboxの半径をlとすると、Pσ(L)/l^2
		//lは非常に大きいのでPA(y1→y0) = (Pσ⊥(y1→y0)G(y1⇔y0)) = (Pσ⊥(y1→y0) cosθ1/l^2 ) , θ1はLとy1.ngのなす角
		//l^2同士でキャンセルされて PA(y0)/PA(y1→y0) = Pσ(L)/ (Pσ⊥(y1→y0)cosθ1) となる 

		e.TBN = ComputeTBN(-L);
		//e.P = L * SkyRadius;	//これだとskyboxからの光が原点に向かって全部降り注ぐ事に
		float3 target = SampleConcentricDisk(0, SceneRadius, e.TBN, RNG.xy, pdf1);	//シーン内のどこかに飛ぶ事にする
		e.P = target + L*SkyRadius;	//シーン内のどこかめがけて飛ばすことのできるskybox上の位置
		e.Ng = -L;
		e.sky = true;
		e.iFace = -1;
		e.m.lightCosFalloff = 0;
		e.m.lightCosHotspot = 0;
		rd = -L;
	} else {
		float4 XiL = RNG;
		//y0についての情報
		int iLight = SampleLightIndex(payload, Xi.y);
		float area;
		e.P = SampleLightTriangle(payload, iLight, Xi.zw, area, e.TBN[2], e.m);
		e.TBN = ComputeTBN(e.TBN[2]);
		if(payload.ID == ID_CHARACTER) {
			e.Ng = GeometryNormal(lights[iLight].iFace);
			e.PG = lights[iLight].pdf/max(area,1e-10) * LightSelectionPDF;	//pdf = 選択率/面積
		}
		else {
			e.Ng = GeometryNormal_st(lights_st[iLight].iFace);
			e.PG = lights_st[iLight].pdf/max(area,1e-10) * LightSelectionPDF_st;	//pdf = 選択率/面積
		}
		e.sky = false;
		//以下、y0→y1について。配光特性Le1(y0→y1)と方向をサンプリングする際のpdf:Pσ(y0→y1)
		rd = SampleCone(e.TBN, XiL.xy, e.m.lightCosFalloff,  pdf1);
		//両面ライト
		if (e.m.twosided) {
			pdf1 /= 2;
			if (XiL.z < 0.5)
				rd = -rd;
		}
		
		if(payload.ID == ID_CHARACTER) {
			e.iFace = lights[iLight].iFace;
		}
		else {
			e.iFace = lights_st[iLight].iFace;
		}
		L = e.TBN[2];
	}

	fs1 = lightLe1L(e,L);
	e.beta = lightLe0(e) * abs(dot(e.TBN[2],rd)) / e.PG;	//Le0/PA(0)
	return e;
}


static bool bShadow_hit;			// シャドウヒットフラグ
static bool bShadow_hit_screen;		// シャドウヒットフラグ（アイパス）
/***************************************************************************
	サブパス作成共用
****************************************************************************/
//次のパッチを探す
//rd:eに入ってきたレイの向き
//e :元のパッチの位置
//ne:サンプルされる次のパッチ情報
//first : x0→x1の探索です、というフラグ
//fs1,pdf1:x1サンプル時のfs(=Le1(x0→x1))とpdf(Pσ(x0→x1))
//返り値 : 探索したパッチが終端だった場合はtrue
bool SampleNext(uniform int depth, inout Payload payload, inout float3 rd, inout Event e, out Event ne, float fs1, float pdf1, int waveCh, int sssCh, inout bool SSSPassed, bool adjoint)
{
	RayDesc ray;
	ray.Origin = e.P;
	ray.TMin = 1e-3;
	ray.TMax = 1e+5;
	ne = (Event)0;
	ne.rusQ = e.rusQ;
	BRDFOut B = (BRDFOut)0;

	[branch]
	if (depth == 0) {
		ne.beta = fs1/pdf1 * e.beta;
		ne.etaI = e.etaO;
		//最初の頂点x0からの探索では、rdの方向にレイを飛ばすだけ
		ray.Direction = rd;
	} else {
		//2番目以降の頂点からの探索では、頂点eに入ってきたレイが、eによってどこに散乱するかをBRDFサンプリングによって決定する
		float3 Ng = e.Ng;
		float3x3 TBN = e.TBN;
		if (dot(rd,Ng)>0) {
			TBN = -TBN;
			Ng = -Ng;
		}
		SampleBRDF(payload, e.m, e.P, TBN, Ng, -rd, e.etaI, waveCh, sssCh, SSSPassed, B, false);
		if (B.SSS) {
			//表面下散乱パスの場合、eを出口パッチの状態に合わせる
			e.P = B.POut;
			e.TBN = ComputeTBN(B.NOut);
			e.Ng = B.NgOut;
			e.SSSOut = true;
			ne.beta = e.beta * B.beta * (adjoint ? CorrectShadingNormal(e.TBN[2], e.Ng, -rd,B.L) : 1);
			e.beta *= B.betaSSS;	//出口パッチ時点でのβにする(albedoとSSSによる色合いが加味された状態で、出る時のフレネル透過率を加味する前)
		} else {
			//BSDF/BRDFだけで評価できるパス
			if (B.BSSRDF) {
				//表面下散乱材料の入り口でフレネル反射した場合のパス
				e.SSSIn = true;
				if (any(e.m.roughness < 1e-3))
					e.S = true;
			}
			ne.beta = e.beta * B.beta * (adjoint ? CorrectShadingNormal(TBN[2], Ng, -rd,B.L) : 1);
		}
		e.etaO = B.IOR;
		ne.etaI = e.etaO;
		ray.Direction = B.L;
	}

	//レイトレ
	TraceRay(tlas,RAY_FLAG_NONE,0xFF, 0,1,0,ray,payload);
	bool toTheSky = payload.miss;	//レイトレがミスってskyboxに行ったらトレース終了

	if (toTheSky) {
		//skyboxからの寄与を格納
		ne.m.emission = FetchSkybox(ray.Direction);
		ne.P = e.P + ray.Direction * SkyRadius;
		ne.Ng = -ray.Direction;
		ne.TBN = ComputeTBN(ne.Ng);
		ne.sky = true;
		ne.iFace = -1;
	} else {
		//パッチの情報を格納
		ne.P = e.P + ray.Direction * payload.t;
		if(payload.ID == ID_CHARACTER) {
			GetPatch(payload.iFace, -ray.Direction, payload.uv, ne.m, ne.TBN);
			ne.Ng = GeometryNormal(payload.iFace);
		}
		else {
			GetPatch_st(payload.iFace, -ray.Direction, payload.uv, ne.m, ne.TBN);
			ne.Ng = GeometryNormal_st(payload.iFace);
		}
		ne.S = (any(ne.m.roughness < 1e-3)) && (ne.m.category==CAT_METAL || ne.m.category==CAT_GLASS);
		ne.iFace = payload.iFace;
	}

	// シャドウレイヒット⇒影を描画
	if(payload.shadow_hit)
	{
		bShadow_hit = true;
	}

	//ロシアン
	float botsu = false;
	if (depth > 3) {
		float q = min(1,RGBtoY(B.beta));	//パスの継続確率
		if (q > RNG.x) {
			ne.beta /= q;
			ne.rusQ = e.rusQ * q;
		} else {
			botsu = true;
			ne.rusQ = e.rusQ * (1-q);
		}
	}

	rd = ray.Direction;
	return toTheSky || all(ne.beta<1e-10) || botsu;
}


//BxDFの評価
//ei→e→eoへの反射率
//waveCh : サンプリングしている波長
//NoL抜き、F入り
float3 BxDF(Event ei, Event e, Event eo, int waveCh, uniform bool adjoint)
{
	float3 wi = normalize(ei.P - e.P);
	float3 wo = normalize(eo.P - e.P);
	float3x3 TBN = e.TBN;
	float3 Ng = e.Ng;
	Material m = e.m;
	float etaO = MaterialIOR(e.m,waveCh,e.etaI);
	float etaI = e.etaI;

	//入射レイを基準に面の法線の向きを合わせる
	if (dot(wi,Ng) < 0) {
		TBN = -TBN;
		Ng = -Ng;
	}

	//BRDFではなくBTDFで評価してねフラグ
	bool needBTDF = sign(dot(wi,Ng)) != sign(dot(wo,Ng));

	//ガラス以外で透過が必要な場合は0で返る
	if (m.category!=CAT_GLASS && needBTDF) {
		return 0;
	}

	float3 H = normalize(wi+wo);
	float u = FresnelSchlick(abs(dot(wi, H)));
	float3 F = lerp(m.category==CAT_METAL ? m.albedo : IORtoF0(etaO/etaI),1,u);
	float2 a = sqr(m.roughness);

	float3 diffuse, specular;

	if (needBTDF)
		return GGXAnisoBTDF(a, wi,wo,TBN, etaI, etaO) * (1-F);

	if (e.SSSOut) {
		//SSSの出口パッチの場合、フレネル透過分(1-F)だけが寄与になる
		//albedo分とSSSによる吸収分は既にβに入ってる
		diffuse = saturate(1-F)/PI;	//出口パッチからの拡散反射分
		specular = 0;
	} else if (e.SSSIn) {
		//SSSの入口パッチの場合、鏡面反射だけが寄与になる
		diffuse = 0;
		specular = F * GGXAnisoBRDF(a, wi,wo, TBN);
	} else {
		diffuse = m.category==CAT_METAL ? 0 : saturate(1-F)*m.albedo/PI;
		float3 F0ms = m.category==CAT_METAL ? m.albedo : 1;
		specular = F * GGXAnisoBRDF(a, wi,wo, TBN);
	}

	return (diffuse + specular) * (adjoint ? CorrectShadingNormal(TBN[2],Ng, wi,wo) : 1);
}

/***************************************************************************
	RayGeneration
****************************************************************************/
[shader("raygeneration")]
void RayGeneration()
{
	Payload payload = (Payload)0;

	//露出値
	float exposure = 1;//1/RGBtoY(SkyboxSH[0].c[0].rgb);

	//パスの最初から終わりまで変わらない情報のセットアップ
	float3x3 camMat = {cameraRight, cameraUp, cameraForward};
	int waveCh = SampleWaveChannel(RNG.xy); //波長
	int sssCh = floor(RNG.x*3);	//SSS用のRGB波長

	Event Z[MAX_DEPTH];
	Event Y[MAX_DEPTH];
	int nL = 1 ,nE = 1;
	bool sssPassed;	//BSSRDFサンプル用フラグ

	//アイサブパス作成
	float3 rdE;
	float fs1E,pdf1E;
	float2 writePos;	//ピクセル書き込み位置(アンチエイリアス入り書き込み用だがデノイザが効かなくなるので現在は特に使ってない)
	Z[0] = SampleFirstCamera(payload, rdE, fs1E, pdf1E, writePos);
	sssPassed = false;
	for (int i=0; i<MAX_DEPTH-1; i++) {
		nE++;
		if (SampleNext(i, payload, rdE, Z[i], Z[i+1], fs1E, pdf1E, waveCh, sssCh, sssPassed, false))
			break;
	}
	
	// アイサブパスのシャドウヒットフラグを保存
	bShadow_hit_screen = bShadow_hit;
	bShadow_hit = false;

	//ライトサブパス作成
	float3 rdL;
	float fs1L,pdf1L;
	Y[0] = SampleFirstLight(payload, rdL,fs1L,pdf1L);
	//sssPassed = false;
	for (int i=0; i<MAX_DEPTH-1; i++){
		nL++;
		if (SampleNext(i, payload, rdL, Y[i], Y[i+1], fs1L, pdf1L, waveCh, sssCh, sssPassed, true))
			break;
	}
	

	//PG,PGrevの計算
	//X[0]のPGは各々の面積pdfが入ってる
	for (int i=2; i<=nE-1; i++) {
		Z[i].PG = PG(waveCh, Z[i-2], Z[i-1], Z[i], true);
		Z[i-2].PGrev = PG(waveCh, Z[i], Z[i-1], Z[i-2], false);
	}
	for (int i=2; i<=nL-1; i++) {
		Y[i].PG = PG(waveCh, Y[i-2], Y[i-1], Y[i], true);
		Y[i-2].PGrev = PG(waveCh, Y[i], Y[i-1], Y[i-2], false);
	}
	//pdf1Xの測度は立体角なので、面積に直す
	Z[1].PG = ConvertPDF(pdf1E, Z[0],Z[1]);
	Y[1].PG = ConvertPDF(pdf1L, Y[0],Y[1]);
	//この時点で不定なのは X[nX-1].PGrev, X[nX-2].PGrev これらはコネクト時に確定する

	//ライトサブパスの終端がミスってskybox方面に行った場合は接続できないので消しとく
	//アイサブパスの場合はskyboxからの寄与はs=0で得られるのでアイサブパス側にこの処理はない
	//この処理によってY[0]以外は sky=falseになる事が保証される
	if (Y[nL-1].sky)
		nL--;
	
	//s==1の時用, パスの長さk別にY0'を作っとく…キャッシュの効きが悪くなるのか凄く遅くなる割に収束は良くならないのでやめた
	//ていうかなんか間違っている気がする, ループの内側でy0dを都度サンプルするのが正しいような
	/*
	Event Y0d[MAX_DEPTH];
	float fs1Ld[MAX_DEPTH];
	float Y1fwd[MAX_DEPTH];
	float Y2fwd[MAX_DEPTH];
	for (int i=2; i<MAX_DEPTH; i++) {	//s=1,t=1以上でないと使われないのでk≧2
		float pdf1,__;	float3 ___;
		Y0d[i] = SampleFirstLight(payload, ro, ___, fs1Ld[i], pdf1);
		Y0d[i].PG = lightPDFDirectioal(Y0d[i],Y[1]);
		Y0d[i].PGrev = PGRev(waveCh, Y0d[i], Y[1], Y[2]);
		Y1fwd[i] = pdf1;
		Y2fwd[i] = PGFwd(waveCh, Y0d[i],Y[1],Y[2]);
	}
	for (int i=0; i<2; i++) {	//k=1の場合用→s=1,t=1なので今の所使われない
		Y0d[i] = Y[0];	//PGrevは計算済み
		fs1Ld[i] = fs1L;
		Y1fwd[i] = Y[1].PG;
		Y2fwd[i] = Y[2].PG;
	}
	*/

	//t=1パスの時はZ[0]をサンプルしなおすので、本来のZ[0]を保持しとく
	Event Z0original = Z[0];

	//Cs,tの評価
	float3 Lt = 0;
	for (int s=0; s<=nL; s++) {
		//t=0のパスについては一旦考えない事にする
		for (int t=1; t<=nE; t++) {
			int k = s+t-1;
			bool skipCalcWeight = false;	//ウェイトの計算抜きで100%寄与を得て良いよフラグ
			Z[0] = Z0original;
			/*
			Y[0] = Y0d[k];	//パスの長さ別にY0は違う所を参照する
			fs1L = fs1Ld[k];
			Y[1].PG = Y1fwd[k];
			Y[2].PG = Y2fwd[k];
			*/

			//辺が無い、または辺が多すぎる場合はノーカン
			if (k<1 || k >= MAX_DEPTH)
				continue;

			//アイサブパスの終端がskyboxに飛んで行ってる場合、ライトサブパスとは繋げない
			//ただし、s==0の場合はアイサブパスがskyboxと直接つながってるので寄与を得られる
			if (t>1)
				if (s!=0 && Z[t-1].sky)
					continue;
			
			//視点と光源が1点ずつのケースは、t=2, s=0に100%ウェイトを与えることで計算するので弾く
			//レンズフレアとかを計算したい場合はこのパスも考慮した方がいい？
			if (t==1 && s==1)
				continue;

			//t=1パスの場合、カメラ上の点をサンプルする
			if (t==1)
				Z[0] = SampleZ0(payload);
			
			float3 C;	//ウェイト無し寄与C*s,t
			if (s==0) {
				//アイサブパスが光源 or skybox直撃の場合は寄与を得る
				C = lightLe0(Z[t-1]) * lightLe1(Z[t-1],Z[t-2]) * Z[t-1].beta;

				//裏面から片面ポリゴンを見ていたりコーンの外側だったりして寄与0の場合
				if (all(C < 1e-10))
					continue;

				//ライトポリゴンリストに登録されていない弱いemissiveを持ったポリゴンはs=0パスでしか寄与を得られないので
				//あとでMISウェイトを1にセットするようフラグを立てる?
				/*
				if (!Z[t-1].sky) {
					if (lightPDFOrigin(payload, Z[t-1].iFace) == 0)
						skipCalcWeight = true;
				}
				*/
			} else {
				//ここを通るパスは↓の条件を満たしている
				//s≧1, t≧1, Z[t-1].sky = false

				//端点が片方でもS頂点(BSDFがδ関数)だったらつなげない
				if (Y[s-1].S || Z[t-1].S)
					continue;

				//コネクト！
				float3 fyz,fzy;	//fs(ys-2→ys-1→zt-1), fs(ys-1→zt-1→zt-2)
				float gyz;

				//ライト→カメラ反射率
				if (s==1)
					fyz = lightLe1(Y[s-1],Z[t-1]);
				else
					fyz = BxDF(Y[s-2], Y[s-1], Z[t-1] ,waveCh, true);
					//fyz = BxDF(Z[t-1], Y[s-1], Y[s-2] ,waveCh, true);

				//カメラ→ライト反射率
				if (t==1)
					fzy = fs1E;
				else
					//fzy = BxDF(Y[s-1], Z[t-1], Z[t-2] ,waveCh, false);
					fzy = BxDF(Z[t-2], Z[t-1], Y[s-1] ,waveCh, false);	//5昼修正、視点側がwiになるようにした
				
				//幾何項
				gyz = //GeometryTerm(Y[s-1].P,Z[t-1].P, Y[s-1].TBN[2], Z[t-1].TBN[2], !Y[s-1].sky,true, Y[0].sky);
					GeometryTerm(Y[s-1].P,Z[t-1].P, Y[s-1].TBN[2], Z[t-1].TBN[2], !Y[s-1].sky, true, Y[s-1].sky);
				
				float3 cst = fyz * gyz * fzy;	//コネクト係数(まだ可視項抜き)
				C = Y[s-1].beta * cst * Z[t-1].beta;
				
				if (all(C == 0))	//ここまででほぼ0になってたらレイトレしないで「寄与なし」とす
					continue;
				
				C *= VisibilityTerm(payload, Y[s-1],Z[t-1], s==1);	//レイトレ可視項
			}

			if (all(C == 0))
				continue;

			//ウェイトの計算
			float wst;
			if (skipCalcWeight) {
				wst = 1;
			} else if (s+t == 2) {
				//ここを通るのはs=0,t=2のみ
				wst = 1;
			} else {
				//この時点で不定なのは X[nX-1].PG, X[nX-1].PGrev, X[nX-2].PGrev
				float p = 1;		//p(s)=1としてpdfの相対的な値を以下のループによって足していく
				float p_sum = 1;	//pの合計。p(s)=1を最初に足しとく
				
				//ライトサブパス方向へ探る(s,tのs--,t++方向)
				for (int i=s-1; i>=0; i--) {
					//iとは何かというと、「もしY[s-1]とZ[t-1]でなく、Y[i]とY[i-1]の位置で接続されていたら？」という仮想接続エッジ
					//もし全パスのMISウェイトを1としたら、頂点が全く同じパス同士で、重複して寄与が得られてしまう
					//重複したパスには面積積測度あたりの確率密度p(i)の比に応じてウェイトを計算し、寄与に対する割合を算出する
					//Y[i]とY[i-1]では互いに可視である事は自明なので「仮想接続」に可視判定は必要ない
					float f = Y[i].PG;
					float r = Y[i].PGrev;

					//ここに来る時点でs≧1,t≧1,かつs+t≧3は保証されている
					//接続エッジに近傍の頂点のPGRevの再計算
					//fは元々計算してある値でOK
					if (i==s-1) {
						if (t==1)
							r = CameraPDFDirectional(Z[t-1],Y[s-1]);
						else
							r = PG(waveCh, Z[t-2],Z[t-1],Y[s-1], false);
					} else if (i==s-2) {
						r = PG(waveCh, Z[t-1],Y[s-1],Y[s-2], false);
					}

					//S頂点で反射した結果のPG,PGrevは0で記録してあるのでここで1に変換する
					//pの計算上S頂点は無かった扱いになる
					p *= Remap0(r)/Remap0(f);

					//S頂点、またはPGがS頂点由来ならp_sumに足さない(単純にスキップする)
					if (Y[i].S || ((i>0) && Y[i-1].S))
						continue;
					
					p_sum += p;
				}

				//アイサブパス方向へ(s,tのs++,t--方向)
				//s==0パスの時はZ[t-1]が光源上の点という事になる
				p = 1;
				for (int i=t-1; i>=1; i--) {

					float f = Z[i].PG;
					float r = Z[i].PGrev;

					if (i==t-1) {
						float3 L = normalize(Z[t-1].P - Z[t-2].P);	//skyboxをサンプルする方向
						if (s==0) {
							r = lightPDFOrigin(payload ,Z[t-1].iFace, L);	//skyboxの場合はiFaceに-1が入っている
						} else if (s==1) {
							r = lightPDFDirectioal(Y[0],Z[t-1]);
						} else
							r = PG(waveCh, Y[s-2],Y[s-1],Z[t-1], false);
					} else if (i==t-2) {
						r = (s>0) ? PG(waveCh, Y[s-1],Z[t-1],Z[t-2], false) : lightPDFDirectioal(Z[t-1],Z[t-2]);
					}

					p *= Remap0(r)/Remap0(f);

					if (Z[i].S || Z[i-1].S)
						continue;

					p_sum += p;
				}
				wst = 1/p_sum;
			}
			float3 Cst = wst * C;

			//チェック用をここに
			/*
			int2 filterIdx = floor(8 * DispatchRaysIndex().xy / DispatchRaysDimensions().xy);
			if (s==filterIdx.x && t==filterIdx.y)
				Cst = C;
			else
				Cst = 0;
			*/

			if (t==1) {
				AddFlare(payload, min(Cst * exposure, CLAMP_L), Y[s-1].P, Z[0].P, waveCh);
			}
			else {
				Lt += max(0, Cst);
			}
		}
	}

	// シャドウレイヒット⇒影を描画
	if(payload.shadow_hit || bShadow_hit_screen || bShadow_hit)
	{
		float shadow_Strength = 0.05;
		Lt = lerp(Lt*0.9 ,Lt*0.01, 1.0 - exp(-shadow_Strength * payload.shadow_t));
	}

	if (SpectralRendering) {
		float3 xyz = mul(Lt,RGB2XYZ);
		xyz *= WaveMISWeight(waveCh);
		// ステージを読み込むと発散してしまうので, ゲインをかけた成分を上乗せするだけにする
		xyz *= 0.25;
		float3 Lt_ofs = mul(xyz, XYZ2RGB);
		Lt += Lt_ofs;
	}

	//露出補正などをやって結果発表
	float3 result = Lt * exposure;
	result = clamp(result, -CLAMP_L,CLAMP_L);	//bias入りfirefly対策

	//マイナスの値もそのままアキュムレーションする(そうしないと分光の結果が赤っぽくなる)
	//WritePixelLerp(RTOutput, writePos, float4(result,1));
	RTOutput[DispatchRaysIndex().xy] = float4(result, 1);
}
