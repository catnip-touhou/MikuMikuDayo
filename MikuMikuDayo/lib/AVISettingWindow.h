#pragma once

#include <Windows.h>
#include "Typedef.h"

struct AVISetting
{
public:
	int m_Avi_Width;		// 出力AVIファイル 幅pixel
	int m_Avi_height;		// 出力AVIファイル 高さpixel
	int m_StartFrame;		// AVIファイルに出力する, 開始フレーム
	int m_EndFrame;			// AVIファイルに出力する, 終了フレーム
	int m_FPS;				// AVIファイルの出力FPS
	int m_AccumFrame;		// 1フレームを作成するのに, 何回レンダリング結果を累積するか,の数
};

class AVISettingWindow
{
private:
	HINSTANCE hInstance;
	HWND hWnd;
	AVISetting m_AVISetting;

	DialogRet m_Result;

	// AVI 出力設定ボックスのメッセージ ハンドラー
	static INT_PTR CALLBACK AviSettingProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
	// 指定したウィンドウハンドルの入力値を取得
	BOOL GetNumberFromText(HWND handle, int& out);
public:
	AVISettingWindow();
	~AVISettingWindow();

	// ダイアログの表示
	DialogRet ShowDialog(HINSTANCE h_instance, HWND h_wnd, int EndFrame = 100, int Fps = 30);
	// 設定内容の取得
	void GetAviParameters(AVISetting& aviSetting);
	// 設定内容の更新
	void SetAviParameters(AVISetting& aviSetting);
};

