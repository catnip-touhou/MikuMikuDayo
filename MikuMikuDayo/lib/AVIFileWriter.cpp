#include "AVIFileWriter.h"

#include <GdiPlus.h>
#pragma comment(lib, "gdiplus.lib")

#include <opencv2/opencv.hpp>

#ifdef _DEBUG
#pragma comment(lib, "opencv_world490d.lib")
#else
#pragma comment(lib, "opencv_world490.lib")
#endif

using namespace ::Gdiplus;

// 参考文献
//
// ・kimoto氏 "無圧縮AVIを、任意のコーデックで圧縮するコード"
//    https://gist.github.com/kimoto/995031
//
// ・kelbird氏 "【Visual Studio】OpenCVで動画出力ファイル作成"
//    https://qiita.com/kelbird/items/4dbca8cae8b5d05ab33f


// OpenCV VideoWriter
static cv::VideoWriter m_p_VideoWriter_Out;	

/*
*	エラー出力用のメッセージボックス
*/
#define ERRMSGBOX_BUFFER_SIZE 256
void ErrorMessageBox(LPCTSTR format, ...)
{
	va_list arg;
	va_start(arg, format);

	TCHAR buffer[ERRMSGBOX_BUFFER_SIZE];
	::_vsnwprintf_s(buffer, ERRMSGBOX_BUFFER_SIZE, _TRUNCATE, format, arg);
	::MessageBoxW(NULL, buffer, L"Error", MB_OK);
	va_end(arg);
}
void ErrorMessageBoxA(LPCSTR format, ...)
{
	va_list arg;
	va_start(arg, format);

	CHAR buffer[ERRMSGBOX_BUFFER_SIZE];
	::_vsnprintf_s(buffer, ERRMSGBOX_BUFFER_SIZE, _TRUNCATE, format, arg);
	::MessageBoxA(NULL, buffer, "Error", MB_OK);
	va_end(arg);
}


AVIFileWriter::AVIFileWriter()
{
	m_p_AviFile = NULL;
	m_p_AviStream = NULL;
	m_p_AviStreamComp = NULL;
	memset(&m_CompOpt, 0, sizeof(AVICOMPRESSOPTIONS));
	memset(&m_CompCv, 0, sizeof(COMPVARS));
	m_CompCvCount = 0;
}

AVIFileWriter::~AVIFileWriter()
{
	CloseAVIFile();
}

// AVIファイルストリームへのハンドル
bool AVIFileWriter::CreateAVIFile(const TCHAR* aviFilename, int width, int height, int frameRate, int frameNum,int quality)
{
	BITMAPINFOHEADER bmih;
	ULONG sizeimage;

	::AVIFileInit();

	//sizeimage = height * ((3 * width + 3) / 4) * 4;
	//bmih = { sizeof(BITMAPINFOHEADER),width,height,1,24,BI_RGB,sizeimage,0,0,0,0 };
	sizeimage = width * height * 4;
	bmih = { sizeof(BITMAPINFOHEADER),width,height,1,32,BI_RGB,sizeimage,0,0,0,0 };

	AVISTREAMINFO streaminfo;
	ZeroMemory(&streaminfo, sizeof(AVISTREAMINFO));
	streaminfo.fccType = streamtypeVIDEO;	// 動画
	streaminfo.fccHandler = comptypeDIB;	// デバイス非依存ビットマップ
	streaminfo.dwScale = 1;
	streaminfo.dwRate = frameRate;			// フレームレート
	streaminfo.dwLength = frameNum;			// フレーム数
	streaminfo.dwQuality = quality;			// クオリティ(関数コール時に指定を省略するとデフォルト設定=>-1)

	// ユーザーに圧縮フォーマットを選択してもらう
	m_CompCv.cbSize = sizeof(COMPVARS);
	m_CompCv.dwFlags = ICMF_COMPVARS_VALID;
	m_CompCv.fccHandler = comptypeDIB;
	m_CompCv.lQ = ICQUALITY_DEFAULT;
	if (!::ICCompressorChoose(NULL, ICMF_CHOOSE_DATARATE | ICMF_CHOOSE_KEYFRAME,
								&bmih, NULL, &m_CompCv, NULL))
	{
		::ErrorMessageBox(L"No codec selected. Please select a codec");
		CloseAVIFile();
		return false;
	}
	m_CompCvCount++;

	// 選択された圧縮フォーマットに基づく設定を反映
	streaminfo.fccHandler = m_CompCv.fccHandler;
	m_CompOpt.fccType = streamtypeVIDEO;
	m_CompOpt.fccHandler = m_CompCv.fccHandler;
	m_CompOpt.dwKeyFrameEvery = m_CompCv.lKey;
	m_CompOpt.dwQuality = m_CompCv.lQ;
	m_CompOpt.dwBytesPerSecond = m_CompCv.lDataRate;
	m_CompOpt.dwFlags = (m_CompCv.lDataRate > 0 ? AVICOMPRESSF_DATARATE : 0)
					  | (m_CompCv.lKey > 0 ? AVICOMPRESSF_KEYFRAMES : 0);
	m_CompOpt.lpFormat = NULL;
	m_CompOpt.cbFormat = 0;
	m_CompOpt.lpParms = m_CompCv.lpState;
	m_CompOpt.cbParms = m_CompCv.cbState;
	m_CompOpt.dwInterleaveEvery = 0;

	// ファイル名をUnicodeからUTF-8に変換
	std::wstring str_filenameW(aviFilename);
	unsigned int strSize = str_filenameW.length();
	char* str_filename_buf = (char*)::GlobalAlloc(GMEM_FIXED, sizeof(char) * strSize);
	std::string str_filename;
	if (str_filename_buf != NULL)
	{
		int ret = WideCharToMultiByte(
			CP_UTF8, 0, str_filenameW.c_str(), -1, str_filename_buf, strSize + 1, NULL, NULL);
		if (ret <= 0)
		{
			GlobalFree(str_filename_buf);
			return false;
		}

		str_filename.append(str_filename_buf);
		GlobalFree(str_filename_buf);
	}
	else
	{
		ErrorMessageBox(L"Failed to create AVI file");
		return false;
	}

	// OpenCV VideoWriterの作成
	cv::Size cvsize(width, height);
	float fps_double = (float)streaminfo.dwRate;
	bool bret = m_p_VideoWriter_Out.open(str_filename,m_CompOpt.fccHandler, fps_double, cvsize, true);
	if (bret == false || m_p_VideoWriter_Out.isOpened() == false)
	{
		ErrorMessageBox(L"Failed to create AVI file");
		return false;
	}
	//MessageBoxA(NULL, m_p_VideoWriter_Out->getBackendName().c_str(), "debug", MB_OK);

// VFWでの実装. 書き出せるファイルサイズに制約がある為、没
#if FALSE
	// AVIファイルをオープン
	if (::AVIFileOpen(&m_p_AviFile, aviFilename, OF_CREATE | OF_WRITE | OF_SHARE_DENY_NONE, NULL) != 0)
	{
		CloseAVIFile();
		return false;
	}

	// AVIファイル書き込み用のVIDEOストリームを作成
	if (::AVIFileCreateStream(m_p_AviFile, &m_p_AviStream, &streaminfo) != 0)
	{
		CloseAVIFile();
		return false;
	}

	// 圧縮済みデータ格納用のストリームを作成
	if (::AVIMakeCompressedStream(&m_p_AviStreamComp, m_p_AviStream, &m_CompOpt, NULL) != AVIERR_OK)
	{
		CloseAVIFile();
		return false;
	}

	// 圧縮済みデータ格納用のストリームのデータ情報を設定
	if (::AVIStreamSetFormat(m_p_AviStreamComp, 0, &bmih, sizeof(BITMAPINFOHEADER)) != 0) {
		CloseAVIFile();
		return false;
	}
#endif

	return true;
}

bool AVIFileWriter::StreamWrite(LONG frameNo, LPVOID lpDIBBuffer, LPBITMAPINFOHEADER lpBmi)
{
	// OpenCV用の 1フレームのバッファを作成
	cv::Mat frame(lpBmi->biHeight, lpBmi->biWidth, CV_8SC3);	// 24ビットRGB
	//cv::Mat frame(lpBmi->biHeight, lpBmi->biWidth, CV_8SC4);	// 32ビットARGB　透過フレームはOpenCVでは書き込めなかった
	for (int y = 0; y < lpBmi->biHeight; ++y) {
		for (int x = 0; x < lpBmi->biWidth; ++x) {
			int index = (lpBmi->biHeight - y - 1) *lpBmi->biWidth + x;
			BYTE* frameData = (BYTE*)lpDIBBuffer;
			unsigned char b = frameData[index * 4];		// blueチャネル
			unsigned char g = frameData[index * 4 + 1];	// greenチャネル
			unsigned char r = frameData[index * 4 + 2];	// redチャネル
			unsigned char a = frameData[index * 4 + 3]; // アルファチャンネル
			frame.at< cv::Vec3b>(y, x) = cv::Vec3b(b, g, r);	 // 24ビットRGB
			//frame.at<cv::Vec4b>(y, x) = cv::Vec4b(b, g, r, a); // 32ビットARGB　透過フレームはOpenCVでは書き込めなかった
		}
	}
	// 1フレーム書き込み
	try
	{
		if (m_p_VideoWriter_Out.isOpened()) {
			m_p_VideoWriter_Out << frame;
		}
		else {
			std::exception err("VideoWriter is not opened.");
			throw err;
		}
	}
	catch (const std::exception &err)
	{
		ErrorMessageBoxA(err.what());
		return false;
	}

	// VFWでの実装. 書き出せるファイルサイズに制約がある為、没
#if FALSE
	if (AVIStreamWrite(m_p_AviStreamComp, frameNo, 1, lpDIBBuffer, lpBmi->biSizeImage, AVIIF_KEYFRAME, NULL, NULL) != 0)
		return false;
#endif

	return true;
}

// AVIファイルのクローズ
void AVIFileWriter::CloseAVIFile()
{
	if (m_p_AviStreamComp != NULL)
	{
		::AVIStreamRelease(m_p_AviStreamComp);
		m_p_AviStreamComp = NULL;
	}
	if (m_p_AviStream != NULL)
	{
		::AVIStreamRelease(m_p_AviStream);
		m_p_AviStream = NULL;
	}
	if (m_p_AviFile != NULL)
	{
		::AVIFileRelease(m_p_AviFile);
		m_p_AviFile = NULL;
	}
	if (m_CompCvCount > 0)
	{
		::ICCompressorFree(&m_CompCv);
		m_CompCvCount--;
	}
	if (m_p_VideoWriter_Out.isOpened())
	{
		m_p_VideoWriter_Out.release();
	}

	AVIFileExit();
}