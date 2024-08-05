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
	//追加したCB要素//
	float brigtnessGain;	//明るさゲイン												96
	float saturationGain;	//彩度ゲイン												100
	int nLights_st;			//ライトポリゴンの数(ステージ)								104
	int DofEnable;			//DOFを有効にするか(bool)									108
	int FogEnable;			//FOGを有効にするか(bool)									112
	int ShadowEnable;		//照明からのシャドウを有効にするか(bool)					116
	float3 lightPosition;	//照明(≒太陽)の位置										128
	float4 fogColoer;		//フォグカラー												144
};

//リソースバインド
Texture2D<float4> CurrentFrameBuffer : register(t0);	//通常カラーバッファ or 縮小バッファ
Texture2D<float>  DepthTex			 : register(t1);	//レイトレ結果 深度バッファ出力

SamplerState smp : register(s0);						// サンプラー

struct VSO {
	float4 position:SV_POSITION;
	float2 uv:TEXCOORD;
};

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

// ピクセルシェーダー
float4 PSFog(VSO vso) : SV_TARGET
{
	float4 Color = CurrentFrameBuffer.Sample(smp, vso.uv);
	float depth = DepthTex.Sample(smp, vso.uv);

	// フォグの色
	// float4 FogColor_white = float4(1.0f,1.0f,1.0f, 1.0f);
	// float4 FogColor_lightblue = float4(0.8f,1.0f,1.3f, 1.0f);
	float4 fogcolor = fogColoer;

	// フォグの密度を設定
	float fogDensity = 0.0001;
	
	// 線形フォグ
	if ( FogEnable == 1 && depth < 0.98e+5) {
		// フォグ適用後の色を計算
		Color = lerp(Color, fogcolor, 1.0f - exp(-fogDensity * depth * depth));
		// αブレンディングのため, 近距離は透明, 遠距離は不透明に(Out = Src.rgb * Src.a + Dest.rgb * (1 - Dest.a))
		Color.a = clamp(1.0f - exp(-fogDensity  * depth), 0.0f, 0.8f);
	}

	return Color;
}
