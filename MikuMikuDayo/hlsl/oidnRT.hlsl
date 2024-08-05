//Albedoマップと法線マップゲッター

//予約されたインスタンスID
#define	ID_CHARACTER	100
#define	ID_STAGE		200
#define ID_FOG			9999

static float PI = acos(-1);

struct HitInfo
{
	float3 Albedo;
	float3 Normal;
	float3 Tr;	//transmittance
};

struct Attributes 
{
	float2 uv;
};

struct Face {
	int iMaterial;	//マテリアル番号
	int iLight;		//光源リスト内での番号、光源でない場合は-1
};

struct Material
{
	int texture;
	int twosided;
	float autoNormal;
	int category;	//0:普通, 1:ガラス, 2:金属, 3:表面下散乱
	float3 albedo;
    float3 emission;
	float alpha;
	float2 roughness;	//tangent,binormal方向それぞれのroughness
	float2 IOR;			//IOR.xyでCauchyの分散係数ABを示す
	float4 cat;		//カテゴリ固有パラメータ
	float lightCosTMax;
	float lightCosFalloffStart;
};

struct Vertex {
    float3 position;
    float3 normal;
    float2 uv;
};

// ---[ Constant Buffers ]---

cbuffer ViewCB : register(b0)
{
	uint iFrame;		//収束開始からのフレーム番号、0スタート
	uint iTick;
	float2 resolution;	//解像度
	float3 cameraRight;	//ワールド座標系でのカメラの右方向
	float fov;			//cot(垂直画角/2)
	float3 cameraUp;
	float3 cameraForward;
	float3 cameraPosition;
	int nLights;		//ライトポリゴンの数
	int SpectralRendering;	//分光を考慮してレンダリングする
	float SceneRadius;		//シーン全体の半径(BDPTでのみ使用)
};

struct OIDNInput {
	float3 Color;
	float3 Albedo;
	float3 Normal;
};

// ---[ Resources ]---

RWStructuredBuffer<OIDNInput> 	oidn		: register(u0);
RaytracingAccelerationStructure SceneBVH	: register(t0);

//キャラクター
StructuredBuffer<Vertex> vertices			: register(t1);
StructuredBuffer<uint> indices				: register(t2);
StructuredBuffer<Material> materials 		: register(t3);
StructuredBuffer<Face> faces				: register(t4);
Texture2D<float4> textures[]				: register(t0, space1);
//ステージ
StructuredBuffer<Vertex> vertices_st		: register(t1, space2);
StructuredBuffer<uint> indices_st			: register(t2, space2);
StructuredBuffer<Material> materials_st		: register(t3, space2);
StructuredBuffer<Face> faces_st				: register(t4, space2);
Texture2D<float4> textures_st[]				: register(t0, space3);

Texture2D<float4> skybox					: register(t5);
Texture2D<float4> history					: register(t6);

SamplerState samp: register(s0);



//方向からskyboxのuvを返す
float2 LongRat(float3 v)
{
	return float2(atan2(v.z, v.x), acos(v.y)) / float2(-2 * PI, PI) - float2(0.25, 0);
}

float3 FetchSkybox(float3 rd)
{
	float3 t = skybox.SampleLevel(samp, LongRat(rd), 0, 0).xyz;
	//return InverseACESTonemap(pow(t, GAMMA));
	return t;
}

float3 AutoNormal(Texture2D<float4>texture, float2 uv, float scale = 0.25)
{
	float W,H;
	texture.GetDimensions(W,H);
    //本当はフットプリントにeを合わせたい
    float3 e = {1/256,1/256,0};
    float xp = texture.SampleLevel(samp, uv + e.xz, 0).g;
    float xn = texture.SampleLevel(samp, uv - e.xz, 0).g;
    float yp = texture.SampleLevel(samp, uv + e.zy, 0).g;
    float yn = texture.SampleLevel(samp, uv - e.zy, 0).g;
	float2 d = float2(xn-xp,yn-yp)*scale;
    float3 N = normalize(float3(d,1));
    return N;
}

[shader("raygeneration")]
void RayGeneration()
{
	uint2 LaunchIndex = DispatchRaysIndex().xy;
	uint2 LaunchDimensions = DispatchRaysDimensions().xy;

	//RTOutput[LaunchIndex.xy] = float4(cameraUp,1);
	//return;

	float2 jt = 0.5;//hash3(uint3(LaunchIndex, iFrame)).xy;			//jitter
	float2 uv = (LaunchIndex + jt) / resolution;
	float2 d =  uv*2-1;
	float aspectRatio = (resolution.x / resolution.y);

	// Setup the ray
	RayDesc ray;
	ray.Origin = cameraPosition;
	float3x3 camMat = {cameraRight, cameraUp, cameraForward};
	ray.Direction = mul(normalize(float3(d.x*aspectRatio,-d.y,fov)),camMat);
	//ray.Direction = normalize(float3(d.x,-d.y,1));
	ray.TMin = 1e-3;
	ray.TMax = 1e+5;

	// Trace the ray
	HitInfo payload;
	payload.Albedo = 0;
	payload.Normal = 0;
	payload.Tr = 1;

	TraceRay(SceneBVH,RAY_FLAG_NONE,0xFF,0,1,0,ray,payload);

	int idx = LaunchIndex.y * (int)resolution.x + LaunchIndex.x;
	oidn[idx].Color = history.SampleLevel(samp, uv, 0, 0).xyz;
	oidn[idx].Albedo = saturate(payload.Albedo);
	oidn[idx].Normal = clamp(payload.Normal,-1,1);
}

[shader("miss")]
void Miss(inout HitInfo payload)
{
    //float3 rd = normalize(WorldRayDirection());
    //float3 sky = FetchSkybox(rd);
    //payload.Albedo += saturate(sky);
}


//キャラクター
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
//ステージ
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

//キャラクター
Material GetMaterial(uint triangleIndex, float2 texcoord)
{
	Material m = materials[faces[triangleIndex].iMaterial];
	if (m.texture >= 0) {
		float4 tex = textures[m.texture].SampleLevel(samp, texcoord,0,0);
		m.albedo = lerp(m.albedo, m.albedo*pow(tex.rgb,2.2), tex.a);
		m.alpha *= tex.a;
	}
	return m;
}
//ステージ
Material GetMaterial_st(uint triangleIndex, float2 texcoord)
{
	Material m = materials_st[faces_st[triangleIndex].iMaterial];
	if (m.texture >= 0) {
		float4 tex = textures_st[m.texture].SampleLevel(samp, texcoord,0,0);
		m.albedo = lerp(m.albedo, m.albedo*pow(tex.rgb,2.2), tex.a);
		m.alpha *= tex.a;
	}
	return m;
}


//接空間生成、法線nから互いに直交する基底ベクトルを作る
float3x3 ComputeTBN(float3 n)
{
	float3 Z = abs(n.z) < 0.999999 ? float3(0, 0, 1) : float3(1, 0, 0);
	float3 X = cross(n, Z);
	float3 Y = normalize(cross(n, X));
	X = cross(n, Y);
	return float3x3(X,Y,n);
}


//(キャラクター)
void GetPatch(uint iFace, float2 st, out Material m, out float3x3 TBN, uniform bool simple = false)
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
	
	//接空間はてきとうでいい場合
	[branch]
	if (simple) {
		TBN = ComputeTBN(n);
		return;
	}

    float3 dp1 = v[1].position - v[0].position;
    float3 dp2 = v[2].position - v[0].position;
    float2 duv1 = v[1].uv - v[0].uv;
    float2 duv2 = v[2].uv - v[0].uv;

    //uvの変化が無い、又は接空間が作れないような張り方になっている場合
    if (dot(duv1,duv1) < 1e-16 || dot(duv2,duv2) < 1e-16) {
        TBN = ComputeTBN(n);
        return;
    }

    float3x3 M = float3x3(dp1, dp2, cross(dp1, dp2));
    float2x3 inverseM = float2x3(cross(M[1], M[2]), cross(M[2], M[0]));
    float3 Tangent = mul(float2(duv1.x, duv2.x), inverseM);
    float3 Binormal = mul(float2(duv1.y, duv2.y), inverseM);
	if (all(Tangent)==0 || all(Binormal)==0) {
		TBN = ComputeTBN(n);
        return;
    }
	Tangent = normalize(Tangent);
	Binormal = normalize(Binormal);
    
    //法線マップが必要な場合はここで実行してnを変化させるべし
    float3x3 TBNmap = float3x3(Tangent,Binormal,n);
	if (m.autoNormal && m.texture>=0)
	    n = mul(AutoNormal(textures[m.texture], uv, m.autoNormal), TBNmap);

    //直交化というより、法線のスムージングにあやかってT,Bもスムーズになって欲しいので
    Tangent = normalize(cross(Binormal, n));
    Binormal = cross(n, Tangent);

    //右手系になってしまってる場合、左手系にし直す
    if (dot(n, cross(Tangent,Binormal)) < 0)
        Tangent = -Tangent;


    //TBN = float3x3(Tangent,Binormal,n);
	TBN = ComputeTBN(n);
}
//ステージ
void GetPatch_st(uint iFace, float2 st, out Material m, out float3x3 TBN, uniform bool simple = false)
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
	
	//接空間はてきとうでいい場合
	[branch]
	if (simple) {
		TBN = ComputeTBN(n);
		return;
	}

    float3 dp1 = v[1].position - v[0].position;
    float3 dp2 = v[2].position - v[0].position;
    float2 duv1 = v[1].uv - v[0].uv;
    float2 duv2 = v[2].uv - v[0].uv;

    //uvの変化が無い、又は接空間が作れないような張り方になっている場合
    if (dot(duv1,duv1) < 1e-16 || dot(duv2,duv2) < 1e-16) {
        TBN = ComputeTBN(n);
        return;
    }

    float3x3 M = float3x3(dp1, dp2, cross(dp1, dp2));
    float2x3 inverseM = float2x3(cross(M[1], M[2]), cross(M[2], M[0]));
    float3 Tangent = mul(float2(duv1.x, duv2.x), inverseM);
    float3 Binormal = mul(float2(duv1.y, duv2.y), inverseM);
	if (all(Tangent)==0 || all(Binormal)==0) {
		TBN = ComputeTBN(n);
        return;
    }
	Tangent = normalize(Tangent);
	Binormal = normalize(Binormal);
    
    //法線マップが必要な場合はここで実行してnを変化させるべし
    float3x3 TBNmap = float3x3(Tangent,Binormal,n);
	if (m.autoNormal && m.texture>=0)
	    n = mul(AutoNormal(textures_st[m.texture], uv, m.autoNormal), TBNmap);

    //直交化というより、法線のスムージングにあやかってT,Bもスムーズになって欲しいので
    Tangent = normalize(cross(Binormal, n));
    Binormal = cross(n, Tangent);

    //右手系になってしまってる場合、左手系にし直す
    if (dot(n, cross(Tangent,Binormal)) < 0)
        Tangent = -Tangent;


    //TBN = float3x3(Tangent,Binormal,n);
	TBN = ComputeTBN(n);
}

[shader("closesthit")]
void ClosestHit(inout HitInfo payload, Attributes attrib)
{
	return; 
}

[shader("anyhit")]
void AnyHit(inout HitInfo payload, Attributes attrib)
{
	Material m;
	float3x3 TBN;
	if (InstanceID() == ID_CHARACTER) {
		GetPatch(PrimitiveIndex(),attrib.uv,m,TBN);
	}
	else if (InstanceID() == ID_STAGE) {
		GetPatch_st(PrimitiveIndex(),attrib.uv,m,TBN);
	}
	else {
		IgnoreHit();
	}
	float3 N = TBN[2];
	float3 rd = normalize(WorldRayDirection());

	if ((dot(normalize(WorldRayDirection()), N) > 0) && (m.twosided==0)) {
		IgnoreHit();
	}

	if (m.alpha < 0.1)
		IgnoreHit();
	
	payload.Albedo = payload.Tr * m.albedo;
	payload.Normal = N;
}