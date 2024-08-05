#define FXAA_PC 1
#define FXAA_HLSL_5 1

//いろいろ設定用/////////////////////////////////////////////////////////

//全体的な品質。39が最高らしいんだけど変えても正直良くわからない
#define FXAA_QUALITY__PRESET 12

//好みに応じて微妙に変えられます。見た目に一応分かる
// Only used on FXAA Quality.
// This used to be the FXAA_QUALITY__SUBPIX define.
// It is here now to allow easier tuning.
// Choose the amount of sub-pixel aliasing removal.
// This can effect sharpness.
//   1.00 - upper limit (softer)
//   0.75 - default amount of filtering
//   0.50 - lower limit (sharper, less sub-pixel aliasing removal)
//   0.25 - almost off
//   0.00 - completely off
#define QUALITYSUBPIX 0.75

//あんまり良くわからない
// Only used on FXAA Quality.
// This used to be the FXAA_QUALITY__EDGE_THRESHOLD define.
// It is here now to allow easier tuning.
// The minimum amount of local contrast required to apply algorithm.
//   0.333 - too little (faster)
//   0.250 - low quality
//   0.166 - default
//   0.125 - high quality 
//   0.063 - overkill (slower)
#define EDGETHRESHOLD 0.125

// これは割と効果が分かりやすい
// Only used on FXAA Quality.
// This used to be the FXAA_QUALITY__EDGE_THRESHOLD_MIN define.
// It is here now to allow easier tuning.
// Trims the algorithm from processing darks.
//   0.0833 - upper limit (default, the start of visible unfiltered edges)
//   0.0625 - high quality (faster)
//   0.0312 - visible limit (slower)
// Special notes when using FXAA_GREEN_AS_LUMA,
//   Likely want to set this to zero.
//   As colors that are mostly not-green
//   will appear very dark in the green channel!
//   Tune by looking at mostly non-green content,
//   then start at zero and increase until aliasing is a problem.
#define EDGETHRESHOLDMIN 0.0625



#include "Fxaa3_11.hlsli"

// FXAA
//
// NVIDIA社が公開しているTIMOTHY LOTTES氏作のコードをそのままMMDayoに組み込んだだけです
// コードそのもののライセンスなどについてはFxaa3_11.fxsubの先頭をご覧ください



//設定ここまで以下はコードです////////////////////////////////////////

struct VSO {
	float4 position:SV_POSITION;
	float2 uv:TEXCOORD;
};

Texture2D<float4> CurrentFrameBuffer : register(t0);

cbuffer CBuff0 : register(b0)
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
	float brigtnessGain;	//明るさゲイン												228
	float saturationGain;	//彩度ゲイン												232
	int nLights_st;			//ライトポリゴンの数(ステージ)								236
}
sampler samp : register(s0);


VSO VS( float4 pos : POSITION, float2 uv:TEXCOORD )
{
	VSO vso = (VSO)0;
	vso.position.x = pos.x;
	vso.position.y = pos.y;
	vso.position.z = pos.z;
	vso.position.w = 1;
	vso.uv.x = uv.x;
	vso.uv.y = uv.y;
	return vso;
}

float4 PS_Fxaa(VSO vso) : SV_TARGET
{
	FxaaTex InputFXAATex = { samp, CurrentFrameBuffer };
	//float4 Color = CurrentFrameBuffer.Sample(samp, vso.uv);
	//float alpha = Color.a;
	float4 Color = FxaaPixelShader(
		vso.uv.xy, FxaaFloat4(0.0f, 0.0f, 0.0f, 0.0f),
		InputFXAATex, InputFXAATex, InputFXAATex,
		1.0/resolution, FxaaFloat4(0.0f, 0.0f, 0.0f, 0.0f), FxaaFloat4(0.0f, 0.0f, 0.0f, 0.0f), FxaaFloat4(0.0f, 0.0f, 0.0f, 0.0f),
		QUALITYSUBPIX, EDGETHRESHOLD, EDGETHRESHOLDMIN,
		8, 0.125, 0.05, FxaaFloat4(0.0f, 0.0f, 0.0f, 0.0f));
	//Color.a = alpha;
	return Color;
}
