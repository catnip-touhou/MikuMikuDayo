#ifndef PMXDEFS_HLSLI
#define PMXDEFS_HLSLI

#include "yrz.hlsli"

struct Payload
{
	uint xi;		//ハッシュ関数のシード、RNGマクロが呼ばれるたびに+1される
	bool miss;		//レイがミスった
	int ID;			//最後にぶっかった物体のinstanceID
	int iFace;		//↑の面番号
	float2 uv;		//↑のレイがぶつかった位置の重心座標
	float  t;		//レイの移動距離
	bool shadow_hit;//シャドウにぶつかったか
	float shadow_t; //シャドウレイの長さ
};

struct PayloadShadow {
	uint xi;
	bool shadow_hit;//シャドウにぶつかったか
	float3 Tr;		//Transmittance
	float shadow_t; //シャドウレイの長さ
};

//乱数を得る
#define RNG (Random4(iFrame,payload.xi))

//予約されたインスタンスID
#define	ID_CHARACTER	100
#define	ID_STAGE		200
#define ID_FOG			9999


/*マテリアルのカテゴリ*/

//金属でもガラスでもない「普通の」マテリアル
#define CAT_DEFAULT 0

//ガラス
//界面で屈折を起こし、内部で吸収を起こす。散乱は起こさない
//1.ポリゴンで閉じられた空間になっている事
//2.ガラス同士で貫通していない事
//以上の2つを満たしていなければならない
//光源からは不透明体と見なされる
//alphaは無視される (確率的に「閉じている」「閉じていない」が選択されると困るから)
//片面材質であっても両面張りとして扱われる
#define CAT_GLASS 1

//金属
//拡散反射を起こさない
#define CAT_METAL 2

//表面下散乱
#define CAT_SUBSURFACE 3

/* 光源のカテゴリ */
#define LIGHT_EMISSIVE	0
#define LIGHT_SKYBOX	1

//三角ポリゴン上の重心座標
struct Attribute
{
	float2 uv;
};

//PMX用レンダラーで想定しているマテリアル(PMXに入ってる情報からCPU側で変換される)
struct Material
{
	int texture;		//albedo,emission,alpha用テクスチャ
	int twosided;		//0:片面材質 1:両面材質
	float autoNormal;	//textureの明度勾配によるバンプマップの強さ
	int category;		//0:普通, 1:ガラス, 2:金属, 3:表面下散乱
	float3 albedo;		//反射率
    float3 emission;	//発光要素を減衰無しで観察した時の輝度
	float alpha;		//不透過度
	float2 roughness;	//tangent,binormal方向それぞれのroughness
	float2 IOR;			//IOR.xyでCauchyの分散係数ABを示す
	float4 cat;			//カテゴリ固有パラメータ(後述)
	float lightCosFalloff;	//スポットライトの範囲全体
	float lightCosHotspot;	//スポットライトの中心部、減衰なしの所
};

/* カテゴリ固有パラメータについて
現状ではcategory == CAT_SUBSURFACEの時だけ有効
cat.xyz = rgbチャンネルそれぞれの平均自由行程[0.1m]
cat.w = 上記に対するスケール, 0.01の時cat.xyzはmm単位になる
*/

//光源情報、個々の光源ポリゴンについての情報
struct Light
{
	float3 Le;	//輝度(Hotspot内から見た場合の)
	int iFace;	//この光源を持っているポリゴン番号
	float pdf;	//この光源が選択される確率
	float cdf;	//この光源までのどれかが選択される累積確率
};

struct Vertex {
    float3 position;
    float3 normal;
    float2 uv;
};

struct Face {
	int iMaterial;	//マテリアル番号
	int iLight;		//光源リスト内での番号、光源でない場合は-1
};

//BRDFサンプリングの結果格納用
struct BRDFOut {
	float3 beta;	//反射率/pdf
	float pdf;		//サンプリングした際のpdf
	float3 L;		//レイの出射方向(光の入射方向)
	float IOR;		//レイの出た先の媒質のIOR(屈折しなかった場合は元のIORと同じ)

	bool isMirror;	//完全鏡面反射パスを通った
	bool dispersion;	//分光を考慮しなければならないパスを通った
	bool diffuse;	//拡散反射パスを通った(SSSでもLambertでも)

	bool BSSRDF;	//BSSRDFの評価をした(表面下散乱マテリアルである)
	bool SSS;		//表面下散乱パスが選択された
	float3 POut;	//表面下散乱を起こして出た場所
	float3 NOut;	//表面下散乱を起こして出た場所の法線
	float3 NgOut;	//表面下散乱を起こして出た場所のポリゴン自体の法線
	float3 betaSSS;	//表面下散乱の結果、1ステップでbetaがどれだけ変わったか
};


struct Reservoir {
	float3 yPhat;	//主光源のBRDF・NoL
	float3 yLe;		//主光源の輝度
	float3 yPos;	//主光源上の位置
	float yPDF;
	bool yTwosided;	//主光源は片面か？
	float3 yN;		//主光源の向き
	float yw;		//主光源のウェイト
	float wsum;		//ウェイト合計
	int M;			//今までカウントしたライトの数
};

//汎用ビットスイッチ
struct GeneralBitSw {
	// [ToDo]ビットフィールドが使えるようになれば,ビットで32要素定義したい
	uint ulBITMAP;
};

// 汎用整数 or 浮動小数バッファ
struct GeneralDataBuf
{
	// [ToDo]共用体が使えるようになれば,共用体で32要素定義したい
	int		slBuf[4];
	float	flBuf[28];
};

// CPU側からConstantBufferに積まれる内容

cbuffer ViewCB : register(b0)
{
	//オリジナルCB要素//
	uint iFrame;			//収束開始からのフレーム番号、0スタート						4
	uint iTick;				//MMDアニメーションのフレーム番号							8
	float2 resolution;		//解像度													16
	float3 cameraRight;		//ワールド座標系でのカメラの右方向							28
	float fov;				//cot(垂直画角/2)											32
	float3 cameraUp;		//カメラの上方向											44
	float skyboxPhi;		//skyboxの回転角											48
	float3 cameraForward;	//カメラの前方向											60
	int nLights;			//ライトポリゴンの数(キャラクター①)						64
	float3 cameraPosition;	//カメラの位置												76
	int SpectralRendering;	//分光を考慮してレンダリングする?(bool)						80
	float SceneRadius;		//シーン全体の半径(BDPTでのみ使用)							84
	float LensR;			//レンズの半径												88
	float Pint;				//カメラ→ピントの合ってる位置までの距離					92
	//追加したCB要素//
	float brigtnessGain;	//明るさゲイン												96
	float saturationGain;	//彩度ゲイン												100
	int nLights_st;			//ライトポリゴンの数(ステージ)								104
	int DofEnable;			//DOFを有効にするか(bool)									108
	int FogEnable;			//FOGを有効にするか(bool)									112
	int ShadowEnable;		//照明からのシャドウを有効にするか(bool)					116
	float3 lightPosition;	//照明(≒太陽)の位置										128
	float4 fogColoer;		//フォグカラー												144
	//int Reserve;			 予備														～256
};

//リソースバインド

RWTexture2D<float4> RTOutput			: register(u0);	//レイトレ結果出力用
RWTexture2D<float4> FlareOutput			: register(u1);	//レンズフレア的な結果(現在でははBDPTのt=1パス)出力用
RaytracingAccelerationStructure tlas	: register(t0);	//TLAS

StructuredBuffer<Vertex> vertices		: register(t1);	//頂点バッファ(キャラクター)
StructuredBuffer<uint> indices			: register(t2);	//インデクスバッファ(キャラクター)
StructuredBuffer<Vertex> vertices_st	: register(t1, space2);	//頂点バッファ(ステージ)
StructuredBuffer<uint> indices_st		: register(t2, space2);	//インデクスバッファ(ステージ)

StructuredBuffer<Material> materials	: register(t3);	//マテリアル(キャラクター)
StructuredBuffer<Face> faces			: register(t4);	//面番号→マテリアル番号テーブル(キャラクター)
StructuredBuffer<Light>	lights			: register(t5);	//ライトポリゴン情報(キャラクター)
StructuredBuffer<Material> materials_st	: register(t3, space2);	//マテリアル(ステージ)
StructuredBuffer<Face> faces_st			: register(t4, space2);	//面番号→マテリアル番号テーブル(ステージ)
StructuredBuffer<Light>	lights_st		: register(t5, space2);	//ライトポリゴン情報(ステージ)

Texture2D<float4> skybox				: register(t6);	//skybox HDRI
Texture2D<float4> apertureTex			: register(t7);	//絞りテクスチャ
Texture2D<float4> textures[]			: register(t0, space1);	//albedo,emission,alphaマップ(キャラクター)
Texture2D<float4> textures_st[]			: register(t0, space3);	//albedo,emission,alphaマップ(ステージ)

Texture2D<float>SkyboxPDFRow : register(t8);		//pdf 行方向
Texture2D<float>SkyboxPDF : register(t9);			//pdf 各行分の列方向pdf
Texture2D<float>SkyboxCDFRow : register(t10);		//cdf 行方向
Texture2D<float>SkyboxCDF : register(t11);			//cdf 各行分の列方向cdf
StructuredBuffer<SHCoeff>SkyboxSH : register(t12);	//SH 現状、バッファだけど1要素だけ

SamplerState samp: register(s0);


//頂点バッファ(キャラクター)
Vertex GetVertex(uint triangleIndex, float2 uv)
{
	float3 barycentrics = float3((1.0f - uv.x - uv.y), uv.x, uv.y);

	Vertex v = (Vertex)0;
    for (int i=0; i<3; i++) {
        Vertex vi = vertices[indices[triangleIndex*3 + i]];
        v.position += vi.position * barycentrics[i];
        v.normal += vi.normal * barycentrics[i];
        v.uv += vi.uv * barycentrics[i];
    }
    v.normal = normalize(v.normal);
    return v;
}
//頂点バッファ(ステージ)
Vertex GetVertex_st(uint triangleIndex, float2 uv)
{
	float3 barycentrics = float3((1.0f - uv.x - uv.y), uv.x, uv.y);

	Vertex v = (Vertex)0;
    for (int i=0; i<3; i++) {
        Vertex vi = vertices_st[indices_st[triangleIndex*3 + i]];
        v.position += vi.position * barycentrics[i];
        v.normal += vi.normal * barycentrics[i];
        v.uv += vi.uv * barycentrics[i];
    }
    v.normal = normalize(v.normal);
    return v;
}

//ジオメトリ(キャラクター)
float3 GeometryNormal(uint triangleIndex)
{
	float3 p[3];
	for (int i=0; i<3; i++) {
		p[i] = vertices[indices[triangleIndex*3+i]].position;
	}
	return normalize(cross(p[0]-p[1],p[0]-p[2]));
}
//ジオメトリ(ステージ)
float3 GeometryNormal_st(uint triangleIndex)
{
	float3 p[3];
	for (int i=0; i<3; i++) {
		p[i] = vertices_st[indices_st[triangleIndex*3+i]].position;
	}
	return normalize(cross(p[0]-p[1],p[0]-p[2]));
}

//マテリアル(キャラクター)
Material GetMaterial(uint triangleIndex, float2 texcoord)
{
	Material m = materials[faces[triangleIndex].iMaterial];
	if (m.texture >= 0) {
		float4 tex = textures[m.texture].SampleLevel(samp, texcoord,0,0);
		float3 col = ToLinear(tex.rgb);
		m.albedo = lerp(m.albedo, m.albedo*col, tex.a);
		m.alpha *= tex.a;
		m.emission *= col;
	}
	return m;
}
//マテリアル(ステージ)
Material GetMaterial_st(uint triangleIndex, float2 texcoord)
{
	Material m = materials_st[faces_st[triangleIndex].iMaterial];
	if (m.texture >= 0) {
		float4 tex = textures_st[m.texture].SampleLevel(samp, texcoord,0,0);
		float3 col = ToLinear(tex.rgb);
		m.albedo = lerp(m.albedo, m.albedo*col, tex.a);
		m.alpha *= tex.a;
		m.emission *= col;
	}
	return m;
}

//albedoマップの輝度勾配から法線マップを作る
float3 AutoNormal(Texture2D<float4>texture, float2 uv, float scale = 0.25)
{
	float W,H;
	texture.GetDimensions(W,H);
    //本当はフットプリントにeを合わせたい
	//テクスチャの解像度からeを計算するのは良いアイディアのようだけど大体あんまりうまくいかない
    float3 e = {1.0/256,1.0/256,0};
    float xp = texture.SampleLevel(samp, uv + e.xz, 0).g;
    float xn = texture.SampleLevel(samp, uv - e.xz, 0).g;
    float yp = texture.SampleLevel(samp, uv + e.zy, 0).g;
    float yn = texture.SampleLevel(samp, uv - e.zy, 0).g;
	float2 d = float2(xn-xp,yn-yp)*scale;
    float3 N = normalize(float3(d,1));
    return N;
}

//三角形についての情報全部ゲット
//法線マップが要らない場合はsimple = trueにしよう
//(キャラクター)
void GetPatch(uint iFace, float3 wi, float2 st, out Material m, out float3x3 TBN, uniform bool simple = false)
{
    //紛らわしいので重心座標の方はst, テクスチャ座標の方はuvとす
	float3 bary = float3((1.0f - st.x - st.y), st.x, st.y);

    Vertex v[3];
    float3 n = 0;
    float2 uv = 0;
    for (int i=0; i<3; i++) {
        v[i] = vertices[indices[iFace*3+i]];
        n += v[i].normal * bary[i];
        uv += v[i].uv * bary[i];
    }
    n = normalize(n);
	m = GetMaterial(iFace, uv);
	
	[branch]
	if (simple) {
		//ノーマルマップなし版
		TBN = ComputeTBN(n);
		return;
	}

	//法線マップ用にdU = Tangent, dV = Binormalになる接空間を作る
	bool valid;
	TBN = ComputeTBN_UV(n, v[0].position, v[1].position, v[2].position, v[0].uv, v[1].uv, v[2].uv, valid);

    //法線マップが必要な場合はここで実行してnを変化させるべし
	if (valid) {
		float3x3 TBNmap = float3x3(TBN[1],TBN[2],n);
		if (m.autoNormal && m.texture>=0) {
			n = mul(AutoNormal(textures[m.texture], uv, m.autoNormal), TBNmap);
			n = normalize(n);	
			/*
			//法線マップから読み取る場合のコード
			float3 nmap = textures[m.texture].SampleLevel(samp,uv,0).rgb * 2 - 1;
			nmap = normalize(float3(nmap.x,nmap.y,10));
			n = normalize(mul(nmap,TBNmap));
			*/
		}
	}

	//法線マップ用の接空間そのままだと不連続になり、シェーディングに向かないので法線マップによって変化した法線からT,Bを求め直す
	TBN = ComputeTBN(n);
}
//(ステージ)
void GetPatch_st(uint iFace, float3 wi, float2 st, out Material m, out float3x3 TBN, uniform bool simple = false)
{
    //紛らわしいので重心座標の方はst, テクスチャ座標の方はuvとす
	float3 bary = float3((1.0f - st.x - st.y), st.x, st.y);

    Vertex v[3];
    float3 n = 0;
    float2 uv = 0;
    for (int i=0; i<3; i++) {
        v[i] = vertices_st[indices_st[iFace*3+i]];
        n += v[i].normal * bary[i];
        uv += v[i].uv * bary[i];
    }
    n = normalize(n);
	m = GetMaterial_st(iFace, uv);
	
	[branch]
	if (simple) {
		//ノーマルマップなし版
		TBN = ComputeTBN(n);
		return;
	}

	//法線マップ用にdU = Tangent, dV = Binormalになる接空間を作る
	bool valid;
	TBN = ComputeTBN_UV(n, v[0].position, v[1].position, v[2].position, v[0].uv, v[1].uv, v[2].uv, valid);

    //法線マップが必要な場合はここで実行してnを変化させるべし
	if (valid) {
		float3x3 TBNmap = float3x3(TBN[1],TBN[2],n);
		if (m.autoNormal && m.texture>=0) {
			n = mul(AutoNormal(textures_st[m.texture], uv, m.autoNormal), TBNmap);
			n = normalize(n);	
			/*
			//法線マップから読み取る場合のコード
			float3 nmap = textures_st[m.texture].SampleLevel(samp,uv,0).rgb * 2 - 1;
			nmap = normalize(float3(nmap.x,nmap.y,10));
			n = normalize(mul(nmap,TBNmap));
			*/
		}
	}

	//法線マップ用の接空間そのままだと不連続になり、シェーディングに向かないので法線マップによって変化した法線からT,Bを求め直す
	TBN = ComputeTBN(n);
}


//戦略1と2のpdfと累乗kから、戦略1,2のMISウェイトを返す
float2 PowerHeuristic(float2 pdf, float k)
{
	if (all(pdf==0))
		return 0;
	else if (pdf.x == 0)
		return float2(0,1);
	else if (pdf.y == 0)
		return float2(1,0);

	if (k==1) {
		return pdf/dot(pdf,1);
	} else {
		float2 p = pow(pdf,k);
		return p/dot(p,1);
	}
}


// Retrieve hit world position.
float3 HitWorldPosition()
{
    return WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
}
/**********************************************************************************
    光源サンプリング用のシャドウレイ
***********************************************************************************/
[shader("closesthit")]
void ClosestHitShadow(inout PayloadShadow payload, Attribute attr)
{
	//何もしない
}

[shader("anyhit")]
void AnyHitShadow(inout PayloadShadow payload, Attribute attr)
{
	int instanceID = InstanceID();
	if (instanceID == ID_FOG) {
		IgnoreHit();
		return;
	}

	Material m;
	if(instanceID == ID_CHARACTER ) {
		m = GetMaterial(PrimitiveIndex(), attr.uv);
	}
	else {
		m = GetMaterial_st(PrimitiveIndex(), attr.uv);
	}

	if (m.category != CAT_GLASS) {
		//半透明は確率的に透過。ただしα=0.98,0.99はフラグとして使われている事があるらしいので不透過とす
		if (m.alpha < RNG.x && m.alpha < 0.98) {
			IgnoreHit();
			return;
		}
		//ライトから見て裏向き面かつ片面描画材質ならミス
		//光源側から見えたら「見える」のか、パッチ側から見えたら「見える」のか？
		//BRDFサンプリングと揃えるため、「パッチ側からライトが見えるなら照明される」という扱いにする
		//WorldRayDirectionはパッチ→光源の方向なので、faceが正の時はパッチからライトは隠され、負の時はパッチからライトが見える
		float face = 0;
		if(instanceID == ID_CHARACTER) {
			face = -dot(normalize(WorldRayDirection()), GeometryNormal(PrimitiveIndex()));
		}
		else if(instanceID == ID_STAGE) {
			face = -dot(normalize(WorldRayDirection()), GeometryNormal_st(PrimitiveIndex()));
		}
		else {
			IgnoreHit();
		}

		if (face < 0 && !m.twosided) {
			IgnoreHit();
			return;
		}
		//ぶつかった
		payload.Tr = 0;
	} else {
		//ガラスはライトからは常に不透明扱い
		payload.Tr = 0;
	}

	payload.shadow_hit = true;
	payload.shadow_t = RayTCurrent();
}

[shader("miss")]
void MissShadow(inout PayloadShadow payload)
{
	payload.shadow_hit = false;
	payload.shadow_t = 0;
}

//ro(パッチの位置)からp(ライトの位置)の方向にレイを飛ばして見えるか返す
float3 Visibility(float3 ro, float3 p, float3 lightN, bool twosided, float eps = 1e-3)
{
	//ライトポリゴンが片面材質の場合はライト法線の方から見えるのかチェック
	if (dot(normalize(ro-p), lightN) < 0 && !twosided)
		return 0;

	float d = length(p-ro);
	float e = max(eps, d*1e-3);

	//ライトポリゴンが遮られていないかテスト
	RayDesc shadowDesc;
	shadowDesc.Origin = ro;
	shadowDesc.Direction = normalize(p-ro);
	shadowDesc.TMin = e;
	shadowDesc.TMax = d-eps;
	PayloadShadow payshadow;
	payshadow.xi = 0;	//TODO:決定論的になってしまってる?
	payshadow.Tr = 1; 
	TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 2,0,2, shadowDesc, payshadow);

	if (any(payshadow.Tr!=1))
		return payshadow.Tr;

	//ライトリーク防止のため近場からもう一回撃つ
	if (e > eps) {
		shadowDesc.TMin = eps;
		shadowDesc.TMax = min(d-eps,e+eps);
		payshadow.xi = 1;
		TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 2,0,2, shadowDesc, payshadow);
	}

	return payshadow.Tr;
}


#endif