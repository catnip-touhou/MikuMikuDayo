/* ユーザー定義 サンプルエフェクト(画面をモノクロにする)*/
#include "yrz.hlsli"

//リソースバインド
Texture2D<float4> CurrentFrameBuffer : register(t0);	//通常カラーバッファ or 縮小バッファ
Texture2D<float4> HdrBaseFrameBuffer : register(t1);	//HdrBaseバッファ
Texture2D<float>  DepthTex			 : register(t2);	//レイトレ結果 深度バッファ出力

SamplerState smp : register(s0);						// サンプラー

struct VSO {
	float4 position:SV_POSITION;
	float2 uv:TEXCOORD;
};

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

// ピクセルシェーダー
float4 PSUSer(VSO vso) : SV_TARGET
{
    // グレースケールの色を出力
	float4 Color = CurrentFrameBuffer.Sample(smp, vso.uv);
	float gray = (Color.r + Color.g + Color.b) / 3.0f;

	return float4(gray, gray, gray, 1.0f);
}

