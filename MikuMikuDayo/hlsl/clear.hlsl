
RWTexture2D<float4> FlareOutput : register(u0);
RWTexture2D<float4> DXROutput : register(u1);

struct Payload {
    int dmy;
};

struct Attribute {
    float2 uv;
};

[shader("raygeneration")]
void Clear()
{
	FlareOutput[DispatchRaysIndex().xy] = float4(0,0,0,1);
	DXROutput[DispatchRaysIndex().xy] = float4(0,0,0,1);
}

[shader("miss")]
void Miss(inout Payload payload)
{
	//特に何もしない
}


[shader("closesthit")]
void ClosestHit(inout Payload payload, Attribute attr)
{
	//特に何もしない
}
