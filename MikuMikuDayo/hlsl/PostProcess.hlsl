#include "yrz.hlsli"

struct VSO {
	float4 position:SV_POSITION;
	float2 uv:TEXCOORD;
};

Texture2D<float4> CurrentFrameBuffer : register(t0);
Texture2D<float4> FlareBuffer : register(t1);
Texture2D<float4> HistoryBuffer : register(t2);		// HistoryBuffer or hdr_Post_RT


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
	float brigtnessGain;	//明るさゲイン												96
	float saturationGain;	//彩度ゲイン												100
	int nLights_st;			//ライトポリゴンの数(ステージ)								104
	int DofEnable;			//DOFを有効にするか(bool)									108
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


//アキュムレーション
float4 PSAcc(VSO vso) : SV_TARGET
{
	float4 current = float4(CurrentFrameBuffer.Sample(samp, vso.uv).rgb + FlareBuffer.Sample(samp, vso.uv).rgb, 1);
	float4 history = HistoryBuffer.Sample(samp,vso.uv);
	if (any(isnan(current)))
		return history;
	else {
		[branch]
		if ( iFrame == 0 ) {
			return current;		// 動画のレンダリングのため、iFrame=0のタイミングで累積をクリアする
		}
		else {
			return lerp(history, current, 1.0/(iFrame+1));
		}
	}
	//return max(0,lerp(HistoryBuffer.Sample(samp,vso.uv), CurrentFrameBuffer.Sample(samp, vso.uv), 1));
}

float4 PSTonemap(VSO vso) : SV_TARGET
{
	float4 o = HistoryBuffer.Sample(samp, vso.uv);	//実際にはhdr_Post_RT

	//HDR⇒SDRへのトーンマッピング&ガンマ補正
	o = clamp(o,0,1e+3);
	o.rgb = ACESCgTonemap(o.rgb);
	o.rgb = ToGamma(o.rgb);
	
	//色調調整
	float3 hsv = RGBtoHSV(o.rgb);
	hsv.y *= saturationGain;	// 彩度
	hsv.z *= brigtnessGain;		// 明度
	o.rgb = HSVtoRGB(hsv);
	
	return o;
}

float4 PSDirect(VSO vso) : SV_TARGET
{
	float4 o = HistoryBuffer.Sample(samp, vso.uv);	//実際にはhdr_Post_RT

	o.rgb = ToGamma(o.rgb);
	return o;
}

