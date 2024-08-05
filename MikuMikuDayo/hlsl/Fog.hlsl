#include "yrz.hlsli"

cbuffer ViewCB : register(b0)
{
	uint iFrame;		//�����J�n����̃t���[���ԍ��A0�X�^�[�g
	uint iTick;			//MMD�A�j���[�V�����̃t���[���ԍ�
	float2 resolution;	//�𑜓x
	float3 cameraRight;	//���[���h���W�n�ł̃J�����̉E����
	float fov;			//cot(������p/2)
	float3 cameraUp;	//�J�����̏����
	float skyboxPhi;	//skybox�̉�]�p
	float3 cameraForward;	//�J�����̑O����
	int nLights;		//���C�g�|���S���̐�
	float3 cameraPosition;	//�J�����̈ʒu
	int SpectralRendering;	//�������l�����ă����_�����O����?(bool)
	float SceneRadius;		//�V�[���S�̂̔��a(BDPT�ł̂ݎg�p)
	float LensR;			//�����Y�̔��a
	float Pint;		//�J�������s���g�̍����Ă�ʒu�܂ł̋���
	//�ǉ�����CB�v�f//
	float brigtnessGain;	//���邳�Q�C��												96
	float saturationGain;	//�ʓx�Q�C��												100
	int nLights_st;			//���C�g�|���S���̐�(�X�e�[�W)								104
	int DofEnable;			//DOF��L���ɂ��邩(bool)									108
	int FogEnable;			//FOG��L���ɂ��邩(bool)									112
	int ShadowEnable;		//�Ɩ�����̃V���h�E��L���ɂ��邩(bool)					116
	float3 lightPosition;	//�Ɩ�(�����z)�̈ʒu										128
	float4 fogColoer;		//�t�H�O�J���[												144
};

//���\�[�X�o�C���h
Texture2D<float4> CurrentFrameBuffer : register(t0);	//�ʏ�J���[�o�b�t�@ or �k���o�b�t�@
Texture2D<float>  DepthTex			 : register(t1);	//���C�g������ �[�x�o�b�t�@�o��

SamplerState smp : register(s0);						// �T���v���[

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

// �s�N�Z���V�F�[�_�[
float4 PSFog(VSO vso) : SV_TARGET
{
	float4 Color = CurrentFrameBuffer.Sample(smp, vso.uv);
	float depth = DepthTex.Sample(smp, vso.uv);

	// �t�H�O�̐F
	// float4 FogColor_white = float4(1.0f,1.0f,1.0f, 1.0f);
	// float4 FogColor_lightblue = float4(0.8f,1.0f,1.3f, 1.0f);
	float4 fogcolor = fogColoer;

	// �t�H�O�̖��x��ݒ�
	float fogDensity = 0.0001;
	
	// ���`�t�H�O
	if ( FogEnable == 1 && depth < 0.98e+5) {
		// �t�H�O�K�p��̐F���v�Z
		Color = lerp(Color, fogcolor, 1.0f - exp(-fogDensity * depth * depth));
		// ���u�����f�B���O�̂���, �ߋ����͓���, �������͕s������(Out = Src.rgb * Src.a + Dest.rgb * (1 - Dest.a))
		Color.a = clamp(1.0f - exp(-fogDensity  * depth), 0.0f, 0.8f);
	}

	return Color;
}
