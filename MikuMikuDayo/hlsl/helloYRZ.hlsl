//対応エンコーディングはBOM付きUTF-8、またはSJISのようです。BOM無しUTF-8はエラー出ます

// TLAS
RaytracingAccelerationStructure gRtScene : register(t0);
// レイトレーシング出力バッファ
RWTexture2D<float4> gOutput: register(u0);
//テクスチャ
Texture2D<float4> Dayo: register(t0, space3);


//定数レジスタ
cbuffer CB0 : register(b0)
{
    uint iFrame; 
};

struct Payload {
	float3 color;
};

struct MyAttribute {
    float2 barys;
};

[shader("raygeneration")]
void RayGeneration()
{
    uint2 launchIndex = DispatchRaysIndex().xy;

    float t = iFrame / 80.0;
    float2x2 m = { cos(t), sin(-t), sin(t), cos(t) };
    uint2 src = mul(int2(launchIndex)-int2(640,480), m) + int2(640,480);
    float3 o = Dayo.Load(int3(src & 0xFF,0)).rgb;
    gOutput[launchIndex.xy] = float4(o,1);
    return;

    float2 dims = float2(DispatchRaysDimensions().xy);
    float2 d = (launchIndex.xy + 0.5) / dims.xy * 2.0 - 1.0;
    RayDesc rayDesc;
    rayDesc.Origin = 0;
    rayDesc.Direction = normalize(float3(d.x, -d.y, 2));
    rayDesc.TMin = 0;
    rayDesc.TMax = 100000;
    Payload payload;
    payload.color = float3(0, 0, 0);
    RAY_FLAG flags = RAY_FLAG_NONE;
    uint rayMask = 0xFF;
    TraceRay(
    gRtScene,
    flags,
    rayMask,
    0, // ray index
    1, // MultiplierForGeometryContrib
    0, // miss index
    rayDesc,
    payload);
    float3 col = payload.color * abs(sin(iFrame/10.0));
    // 結果格納.
    gOutput[launchIndex.xy] = float4(col, 1);
}


[shader("miss")]
void Miss(inout Payload payload)
{
    payload.color = float3(0.1, 0.1, 0.2);
}


[shader("closesthit")]
void ClosestHit(inout Payload payload, MyAttribute attrib)
{
    float3 col = 0;
    col.xy = attrib.barys;
    col.z = 1.0 - col.x - col.y;
    payload.color = col;
}

