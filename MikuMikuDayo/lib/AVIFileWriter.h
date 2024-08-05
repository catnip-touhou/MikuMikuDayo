#pragma once

#include <Windows.h>
#include <tchar.h>

#include <string>
#include <locale>
#include <codecvt>

#include <Vfw.h>
#pragma comment(lib, "vfw32.lib")

class AVIFileWriter
{
private:
	// VFW用メンバ変数(一部しか使用しない)
	PAVIFILE m_p_AviFile;					// AVIファイルハンドル
	PAVISTREAM m_p_AviStream;				// AVIファイルストリーム(RAW)へのハンドル
	PAVISTREAM m_p_AviStreamComp;			// AVIファイルストリーム(圧縮)へのハンドル
	AVICOMPRESSOPTIONS m_CompOpt;			// 圧縮フォーマット情報
	COMPVARS m_CompCv;						// ↑
	int m_CompCvCount;

	// OpenCV用メンバ変数     ※ opencv.hppをインクルードすると, dayo.cppで関数名のコンフリクトが起きるので, OpenCV VideoWriterのインスタンスはcpp本体でstaic変数にする
	//cv::VideoWriter *m_p_VideoWriter_Out;	// OpenCV VideoWriter

public:
	AVIFileWriter();
	~AVIFileWriter();

	// AVIファイルの作成（コーデックの選択を含む）
	bool CreateAVIFile(const TCHAR* aviFilename,int width, int height, int frameRate, int frameNum, int quality = -1);
	// DIBデータをAVIファイルに書込み
	bool StreamWrite(LONG frameNo, LPVOID lpDIBBuffer, LPBITMAPINFOHEADER lpBmi);
	// AVIファイルのクローズ
	void CloseAVIFile();
};

