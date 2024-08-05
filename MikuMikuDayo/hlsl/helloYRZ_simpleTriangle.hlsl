//対応エンコーディングはBOM付きUTF-8、またはSJISのようです。BOM無しUTF-8はエラー出ます

// TLAS のバッファを指定.
RaytracingAccelerationStructure gRtScene : register(t0);
// レイトレーシング結果バッファを指定.
RWTexture2D<float4> gOutput: register(u0);

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
    float3 col = payload.color;
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

