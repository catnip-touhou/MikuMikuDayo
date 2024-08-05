
// BLOOM
//
// そぼろ氏が公開しているAutpluminousBasicのコードをもとに,MMDayoに組み込んだものです
// 

//リソースバインド
Texture2D<float4> CurrentFrameBuffer : register(t0);	//通常カラーバッファ or 縮小バッファ
Texture2D<float4> HdrBaseFrameBuffer : register(t1);	//HdrBaseバッファ
Texture2D<float>  DepthTex			 : register(t2);	//レイトレ結果 深度バッファ出力


SamplerState smp : register(s0);						// サンプラー
#define EmitterView (smp)								// ↑
#define	ScnSampY	(smp)								// ↑
#define ScnSampX	(smp)								// ↑

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
	int FogEnable;			//FOGを有効にするか(bool)									112
}

////////////////////////////////////////////////////////////////////////////////////////////////
// ユーザーパラメータ

// ぼかし範囲 (サンプリング数は固定のため、大きくしすぎると縞が出ます) 
// にじみ
#define	Extent_S	0.00050f
// ガウス
#define	Extent_G	0.00050f

//発光強度
#define	Strength_A	0.2f
#define	Strength_B	0.3f

//点滅周期、単位：フレーム、0で停止
#define	Interval	0

//編集中の点滅をフレーム数に同期
#define	SYNC	false

#define	PI		3.14159

// マテリアル色
#define	alpha1		1.0f					//: CONTROLOBJECT < string name = "(self)"; string item = "Tr"; >;
// スケール
#define	scaling0	10.0f					//: CONTROLOBJECT < string name = "(self)"; >;
#define	scaling 	(scaling0 * 0.1)

//時間
#define	ftime		(iTick / 30.0f)	//暫定/	//: TIME <bool SyncInEditMode = SYNC;>;
#define	timerate 	(Interval ? ((1 + cos(ftime * 2 * PI * 30 / (float)Interval)) * 0.4 + 0.2) : 1.0)

// ぼかし処理の重み係数：
//    ガウス関数 exp( -x^2/(2*d^2) ) を d=5, x=0～7 について計算したのち、
//    (WT_7 + WT_6 + … + WT_1 + WT_0 + WT_1 + … + WT_7) が 1 になるように正規化したもの
#define	WT_0	0.0920246
#define	WT_1	0.0902024
#define	WT_2	0.0849494
#define	WT_3	0.0768654
#define	WT_4	0.0668236
#define	WT_5	0.0558158
#define	WT_6	0.0447932
#define	WT_7	0.0345379


struct VSO {
	float4 position:SV_POSITION;
	float2 uv:TEXCOORD;
};


////////////////////////////////////////////////////////////////////////////////////////////////
// 白とび表現関数
float4 OverExposure(float4 color){
	float4 newcolor = color;

	//ある色が1を超えると、他の色にあふれる
	newcolor.gb += max(color.r - 1, 0) * float2(0.65, 0.6);
	newcolor.rb += max(color.g - 1, 0) * float2(0.5, 0.6);
	newcolor.rg += max(color.b - 1, 0) * float2(0.5, 0.6);

	return newcolor;
}


////////////////////////////////////////////////////////////////////////////////////////////////
//共通頂点シェーダ
//VSO VS_passDraw( float4 Pos : POSITION, float2 Tex : TEXCOORD0 ) {
//	VSO Out = (VSO)0; 
//
//	// スクリーンサイズ
//	float2 ViewportSize = resolution;							// : VIEWPORTPIXELSIZE;
//	float2 ViewportOffset = (float2(0.5,0.5)/ViewportSize);
//
//	Out.position = Pos;
//	Out.uv = Tex + float2(ViewportOffset.x, ViewportOffset.y);
//
//	return Out;
//}

//VSO VS_passDraw2( float4 Pos : POSITION, float2 Tex : TEXCOORD0 ) {
//	VSO Out = (VSO)0; 
//
//	// スクリーンサイズ
//	float2 ViewportSize = resolution;							// : VIEWPORTPIXELSIZE;
//	float2 ViewportOffset = (float2(0.5,0.5)/ViewportSize);
//
//	Out.position = Pos;
//	Out.uv = Tex + float2(ViewportOffset.x * 2, ViewportOffset.y * 2);
//
//	return Out;
//}


////////////////////////////////////////////////////////////////////////////////////////////////
// Passの流れ
//   (VS_passDraw,PS_first) -> (VS_passDraw2,PS_passSX) -> (VS_passDraw2,PS_passSY)
//		-> (VS_passDraw2,PS_passX) -> (VS_passDraw2,PS_passY<w/ blend>)
//
////////////////////////////////////////////////////////////////////////////////////////////////
// ファーストパス

float4 PS_first( VSO vso ) : SV_TARGET {
	float4 Color;
	float2 Tex = vso.uv;

	//<<高輝度抽出>>
	// メインレンダリングターゲットからカラーをサンプリング
	Color  = CurrentFrameBuffer.Sample(EmitterView, Tex);
	// サンプリングしたカラーの明るさを計算
    float Lt = dot(Color.xyz, float3(0.2125f, 0.7154f, 0.0721f));

    // clip()関数は引数の値がマイナスになると、以降の処理をスキップする
    // なので、マイナスになるとピクセルカラーは出力されない
    // 今回の実装はカラーの明るさが1以下ならピクセルキルする
    //clip(Lt - 1.0f);
	if((Lt - 1.0f) < 0.0f ) {
		// 高輝度領域はそのまま, 低輝度領域は黒透明にする.
		Color.rgb *= 0.0f;
		Color.a = 0.0f;
	}

	return Color;
}

////////////////////////////////////////////////////////////////////////////////////////////////
// X方向にじみ

float4 PS_passSX( VSO vso ) : SV_TARGET {   
	float4 Color;
	float2 Tex = vso.uv;

	// スクリーンサイズ
	float2 ViewportSize = resolution;							// : VIEWPORTPIXELSIZE;
	float2 SampStep2 = (float2(Extent_S,Extent_S)/ViewportSize*ViewportSize.y);
	float step = SampStep2.x * alpha1 * timerate;

	Color = CurrentFrameBuffer.Sample( ScnSampY, Tex );
    
	Color = max(Color, (7.0/8.0) * CurrentFrameBuffer.Sample( ScnSampY, Tex+float2(step     ,0)));
	Color = max(Color, (6.0/8.0) * CurrentFrameBuffer.Sample( ScnSampY, Tex+float2(step * 2 ,0)));
	Color = max(Color, (5.0/8.0) * CurrentFrameBuffer.Sample( ScnSampY, Tex+float2(step * 3 ,0)));
	Color = max(Color, (4.0/8.0) * CurrentFrameBuffer.Sample( ScnSampY, Tex+float2(step * 4 ,0)));
	Color = max(Color, (3.0/8.0) * CurrentFrameBuffer.Sample( ScnSampY, Tex+float2(step * 5 ,0)));
	Color = max(Color, (2.0/8.0) * CurrentFrameBuffer.Sample( ScnSampY, Tex+float2(step * 6 ,0)));
	Color = max(Color, (1.0/8.0) * CurrentFrameBuffer.Sample( ScnSampY, Tex+float2(step * 7 ,0)));
    
	Color = max(Color, (7.0/8.0) * CurrentFrameBuffer.Sample( ScnSampY, Tex-float2(step     ,0)));
	Color = max(Color, (6.0/8.0) * CurrentFrameBuffer.Sample( ScnSampY, Tex-float2(step * 2 ,0)));
	Color = max(Color, (5.0/8.0) * CurrentFrameBuffer.Sample( ScnSampY, Tex-float2(step * 3 ,0)));
	Color = max(Color, (4.0/8.0) * CurrentFrameBuffer.Sample( ScnSampY, Tex-float2(step * 4 ,0)));
	Color = max(Color, (3.0/8.0) * CurrentFrameBuffer.Sample( ScnSampY, Tex-float2(step * 5 ,0)));
	Color = max(Color, (2.0/8.0) * CurrentFrameBuffer.Sample( ScnSampY, Tex-float2(step * 6 ,0)));
	Color = max(Color, (1.0/8.0) * CurrentFrameBuffer.Sample( ScnSampY, Tex-float2(step * 7 ,0)));

	return Color;
}


////////////////////////////////////////////////////////////////////////////////////////////////
// Y方向にじみ

float4 PS_passSY( VSO vso ) : SV_TARGET {
	float4 Color;
	float2 Tex = vso.uv;

	// スクリーンサイズ
	float2 ViewportSize = resolution;							// : VIEWPORTPIXELSIZE;
	float2 SampStep2 = (float2(Extent_S,Extent_S)/ViewportSize*ViewportSize.y);
	float step = SampStep2.y * alpha1 * timerate;

    Color = CurrentFrameBuffer.Sample( ScnSampX, Tex );

	Color = max(Color, (7.0/8.0) * CurrentFrameBuffer.Sample( ScnSampX, Tex+float2(0, step    )));
	Color = max(Color, (6.0/8.0) * CurrentFrameBuffer.Sample( ScnSampX, Tex+float2(0, step * 2)));
	Color = max(Color, (5.0/8.0) * CurrentFrameBuffer.Sample( ScnSampX, Tex+float2(0, step * 3)));
	Color = max(Color, (4.0/8.0) * CurrentFrameBuffer.Sample( ScnSampX, Tex+float2(0, step * 4)));
	Color = max(Color, (3.0/8.0) * CurrentFrameBuffer.Sample( ScnSampX, Tex+float2(0, step * 5)));
	Color = max(Color, (2.0/8.0) * CurrentFrameBuffer.Sample( ScnSampX, Tex+float2(0, step * 6)));
	Color = max(Color, (1.0/8.0) * CurrentFrameBuffer.Sample( ScnSampX, Tex+float2(0, step * 7)));

	Color = max(Color, (7.0/8.0) * CurrentFrameBuffer.Sample( ScnSampX, Tex-float2(0, step    )));
	Color = max(Color, (6.0/8.0) * CurrentFrameBuffer.Sample( ScnSampX, Tex-float2(0, step * 2)));
	Color = max(Color, (5.0/8.0) * CurrentFrameBuffer.Sample( ScnSampX, Tex-float2(0, step * 3)));
	Color = max(Color, (4.0/8.0) * CurrentFrameBuffer.Sample( ScnSampX, Tex-float2(0, step * 4)));
	Color = max(Color, (3.0/8.0) * CurrentFrameBuffer.Sample( ScnSampX, Tex-float2(0, step * 5)));
	Color = max(Color, (2.0/8.0) * CurrentFrameBuffer.Sample( ScnSampX, Tex-float2(0, step * 6)));
	Color = max(Color, (1.0/8.0) * CurrentFrameBuffer.Sample( ScnSampX, Tex-float2(0, step * 7)));

	return Color;
}


////////////////////////////////////////////////////////////////////////////////////////////////
// X方向ぼかし

float4 PS_passX( VSO vso ) : SV_TARGET {   
	float4 Color;
	float2 Tex = vso.uv;

	// スクリーンサイズ
	float2 ViewportSize = resolution;							// : VIEWPORTPIXELSIZE;
	float2 SampStep = (float2(Extent_G,Extent_G)/ViewportSize*ViewportSize.y);
	float step = SampStep.x * alpha1 * timerate;

	Color  = WT_0 * CurrentFrameBuffer.Sample( ScnSampY, Tex );
	Color.rgb *= Strength_A;
	Color = OverExposure(Color);

	Color += WT_1 * ( CurrentFrameBuffer.Sample( ScnSampY, Tex+float2(step    ,0) ) + CurrentFrameBuffer.Sample( ScnSampY, Tex-float2(step    ,0) ) );
	Color += WT_2 * ( CurrentFrameBuffer.Sample( ScnSampY, Tex+float2(step * 2,0) ) + CurrentFrameBuffer.Sample( ScnSampY, Tex-float2(step * 2,0) ) );
	Color += WT_3 * ( CurrentFrameBuffer.Sample( ScnSampY, Tex+float2(step * 3,0) ) + CurrentFrameBuffer.Sample( ScnSampY, Tex-float2(step * 3,0) ) );
	Color += WT_4 * ( CurrentFrameBuffer.Sample( ScnSampY, Tex+float2(step * 4,0) ) + CurrentFrameBuffer.Sample( ScnSampY, Tex-float2(step * 4,0) ) );
	Color += WT_5 * ( CurrentFrameBuffer.Sample( ScnSampY, Tex+float2(step * 5,0) ) + CurrentFrameBuffer.Sample( ScnSampY, Tex-float2(step * 5,0) ) );
	Color += WT_6 * ( CurrentFrameBuffer.Sample( ScnSampY, Tex+float2(step * 6,0) ) + CurrentFrameBuffer.Sample( ScnSampY, Tex-float2(step * 6,0) ) );
	Color += WT_7 * ( CurrentFrameBuffer.Sample( ScnSampY, Tex+float2(step * 7,0) ) + CurrentFrameBuffer.Sample( ScnSampY, Tex-float2(step * 7,0) ) );

	return Color;
}

inline float Lerp(float a, float b, float t) { return a * (1 - t) + b * t; }
////////////////////////////////////////////////////////////////////////////////////////////////
// Y方向ぼかし

float4 PS_passY( VSO vso ) : SV_TARGET {
	float4 Color;
	float4 BaseColor;
	float2 Tex = vso.uv;
	float Depth;
	float Gain;

	// スクリーンサイズ
	float2 ViewportSize = resolution;							// : VIEWPORTPIXELSIZE;
	float2 SampStep = (float2(Extent_G,Extent_G)/ViewportSize*ViewportSize.y);
	float step = SampStep.y * alpha1 * timerate;


	Color  = WT_0 *   CurrentFrameBuffer.Sample( ScnSampX, Tex );
	Color += WT_1 * ( CurrentFrameBuffer.Sample( ScnSampX, Tex+float2(0,step    ) ) + CurrentFrameBuffer.Sample( ScnSampX, Tex-float2(0,step    ) ) );
	Color += WT_2 * ( CurrentFrameBuffer.Sample( ScnSampX, Tex+float2(0,step * 2) ) + CurrentFrameBuffer.Sample( ScnSampX, Tex-float2(0,step * 2) ) );
	Color += WT_3 * ( CurrentFrameBuffer.Sample( ScnSampX, Tex+float2(0,step * 3) ) + CurrentFrameBuffer.Sample( ScnSampX, Tex-float2(0,step * 3) ) );
	Color += WT_4 * ( CurrentFrameBuffer.Sample( ScnSampX, Tex+float2(0,step * 4) ) + CurrentFrameBuffer.Sample( ScnSampX, Tex-float2(0,step * 4) ) );
	Color += WT_5 * ( CurrentFrameBuffer.Sample( ScnSampX, Tex+float2(0,step * 5) ) + CurrentFrameBuffer.Sample( ScnSampX, Tex-float2(0,step * 5) ) );
	Color += WT_6 * ( CurrentFrameBuffer.Sample( ScnSampX, Tex+float2(0,step * 6) ) + CurrentFrameBuffer.Sample( ScnSampX, Tex-float2(0,step * 6) ) );
	Color += WT_7 * ( CurrentFrameBuffer.Sample( ScnSampX, Tex+float2(0,step * 7) ) + CurrentFrameBuffer.Sample( ScnSampX, Tex-float2(0,step * 7) ) );

	Color.rgb *= (Strength_B * scaling * timerate);
	Color = OverExposure(Color);

	// 深度を取得し, ピント距離より外側ではBloomを減衰する.
	//   ※ ボケ処理がRayシェーダーで行われる為, ポストエフェクトはボケに上書き更新してしまう.
	//	    なのでそのままだと遠方の高輝度点ほど不自然になってしまうのでBloomを距離に比例して減衰させて誤魔化す.
	Gain = 1.0f;
	if(DofEnable == 1)
	{
		Depth = DepthTex.Sample(EmitterView, Tex);
		if( Depth > Pint) {
			Depth = clamp(Depth - Pint, 0 ,3e+2);	// 0～3e+2の範囲にリミット
			Depth = Depth / 3e+2;					// 0～1の範囲に正規化

			Gain = Lerp( 1.0f, 0.5f, Depth);
		}
		Color.rgb *= Gain;
	}

	//ぼかされた高輝度領域(低輝度領域は黒透明)に、ベースの色を加算する.
	BaseColor = HdrBaseFrameBuffer.Sample( EmitterView, Tex );
	Color.rgb += BaseColor.rgb;
	Color.a = BaseColor.a;

	return Color;
}


