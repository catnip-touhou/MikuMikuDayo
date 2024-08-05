/* ユーザー定義 コンピュート シェーダー サンプルエフェクト */

#include "yrz.hlsli"

#define	PI	3.14159

//MikuMikuDayoのカメラ・照明情報
struct CameraAndLight {
	float3 camera_up;		//カメラの上方向
	float3 camera_right;	//ワールド座標系でのカメラの右方向
	float3 camera_forward;	//カメラの前方向
	float3 camera_position;	//カメラの位置
	float  camera_pint;		//カメラのピント位置
	float3 light_position;	//光源(≒太陽)の位置
};

//MikuMikuDayoのワールド情報
struct WorldInfomation {
	uint iFrame;			//収束開始からのフレーム番号、0スタート
	uint iTick;				//MMDアニメーションのフレーム番号
};

RWStructuredBuffer<CameraAndLight> OutCAL : register(u0);	//変換後のカメラ・照明バッファ

StructuredBuffer<CameraAndLight> CAL : register(t0);		//変換前のカメラ・照明バッファ
StructuredBuffer<WorldInfomation> WI : register(t1);		//MikuMikuDayoのワールド情報


[numthreads(1, 1, 1)]
void CS_User( uint3 id : SV_DispatchThreadID )
{
	uint idx = id.x;
	CameraAndLight cal = CAL[idx];
	WorldInfomation wi = WI[idx];
	
	float time = (1.0 / 30.0f) * (float)wi.iTick;
	//float time = (1.0 / 30.0f) * (float)wi.iFrame;

	// 光源位置 設定
	cal.light_position.x =  3000.0f;
	cal.light_position.y =  10000.0f;
	cal.light_position.z =  5000.0f;

	// カメラ補正 設定
	//cal.camera_position.y += 1.0 * sin(2.0 * PI * 0.25f * time);
	//cal.camera_position.x += 1.0 * cos(2.0 * PI * 0.35f * time);

    OutCAL[id.x] = cal;
}

