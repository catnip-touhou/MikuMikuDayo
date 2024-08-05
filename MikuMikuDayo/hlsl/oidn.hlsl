#include "yrz.hlsli"

struct VSO {
	float4 position:SV_POSITION;
	float2 uv:TEXCOORD;
};

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

StructuredBuffer<float3> oidn : register(t0);
sampler samp : register(s0);

VSO VS( float4 pos : POSITION, float2 uv:TEXCOORD )
{
	VSO vso = (VSO)0;
	vso.position = pos;
	vso.position.w = 1;
	vso.uv = uv;
	return vso;
}

float4 PSConvOIDN(VSO vso) : SV_TARGET
{
	// OIDNバッファ形式から, テクスチャに変換
	int2 pix = floor(vso.uv * resolution);
	int idx = pix.y * round(resolution.x) + pix.x;
	float4 o = float4(oidn[idx],1);

	//
	// トーンマッピング,ガンマ補正, 色調調整は, PSTonemapでまとめて実施するので, コメントアウト
	//
	//HDR⇒SDRへのトーンマッピング&ガンマ補正
	//o.rgb = ACESCgTonemap(o.rgb);
	//o.rgb = ToGamma(o.rgb);

	//色調調整
	//float3 hsv = RGBtoHSV(o.rgb);
	//hsv.y *= saturationGain;		// 彩度
	//hsv.z *= brigtnessGain;		// 明度
	//o.rgb = HSVtoRGB(hsv);

	return o;
}

