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

struct VSO {
	float4 position:SV_POSITION;
	float2 uv:TEXCOORD;
};

Texture2D<float>	DepthTex	: register(s0);			//レイトレ結果 深度バッファ出力
sampler samp : register(s0);

//VSO VS( float4 pos : POSITION, float2 uv:TEXCOORD )
//{
//	VSO vso = (VSO)0;
//	vso.position.x = pos.x;
//	vso.position.y = pos.y;
//	vso.position.z = pos.z;
//	vso.position.w = 1;
//	vso.uv.x = uv.x;
//	vso.uv.y = uv.y;
//	return vso;
//}

// デバッグ用 深度バッファ可視化
float4 PSDepthmap(VSO vso) : SV_TARGET
{
	float depth = DepthTex.Sample(samp, vso.uv);

	depth = clamp(depth ,0 ,3e+2);		// 0～3e+2の範囲にリミット
	depth = depth / 3e+2;				// 0～1の範囲に正規化

	float Lt = 1.0f - depth;			// 近く:白, 遠く:黒にマッピング
	float4 output = { Lt, Lt, Lt, 1.0f };

	return output;
}
