#include "yrz.hlsli"

cbuffer ViewCB : register(b0)
{
	uint iFrame;		//収束開始からのフレーム番号、0スタート
	uint iTick;			//MMDアニメーションのフレーム番号
	float2 resolution;	//解像度
	float3 cameraRight;	//ワールド座標系でのカメラの右方向
	float fov;			//cot(垂直画角/2)
	float3 cameraUp;	//カメラの上方向
	float skyboxPhi;	//skyboxの回転角
	float3 cameraForward;	//カメラの前方向
	int nLights;		//ライトポリゴンの数
	float3 cameraPosition;	//カメラの位置
	int SpectralRendering;	//分光を考慮してレンダリングする?(bool)
	float SceneRadius;		//シーン全体の半径(BDPTでのみ使用)
	float LensR;			//レンズの半径
	float Pint;		//カメラ→ピントの合ってる位置までの距離
};

//乱数を得る
#define RNG (Random4(iFrame,payload.xi))

RWTexture2D<float>	DepthTex	: register(u0);			//レイトレ結果出力用(深度バッファ出力用)
RaytracingAccelerationStructure tlas	: register(t0);	//TLAS

struct Payload {
	uint xi;		//ハッシュ関数のシード、RNGマクロが呼ばれるたびに+1される
    bool miss;		//レイがミスった
    float t;		//レイの移動距離
} ;

struct Attribute {
    float2 uv;
};

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
	payload.t = RayTCurrent();
}


inline float Lerp(float a, float b, float t) { return a * (1 - t) + b * t; }

// 深度バッファ作成 RayGen
[shader("raygeneration")]
void RayGeneration()
{
	float3x3 camMat = {cameraRight, cameraUp, cameraForward};
	Payload payload = (Payload)0;
	
	float2 d = ((float2(DispatchRaysIndex().xy) / float2(DispatchRaysDimensions().xy)) * 2.0f - 1.0f);
	
	float aspectRatio = (DispatchRaysDimensions().x / (float)DispatchRaysDimensions().y);
	float3 rd = normalize(float3(d.x*aspectRatio,-d.y,fov));
	rd = mul(rd, camMat);

	RayDesc ray = {cameraPosition, 1e-3, rd, 1e+5};

	TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 0,1,0, ray, payload);
    
    //skyboxまで飛んでった場合は適当に遠くにしとく
    if (payload.miss)
        payload.t = 1e+5;

    DepthTex[DispatchRaysIndex().xy] = payload.t;
}
