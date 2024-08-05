#pragma once

#include <Windows.h>
#include "Typedef.h"

struct WindowSetting
{
public:
	int m_Width;		//メインウィンドウ 幅
	int m_Height;		//メインウィンドウ 高さ
};

class WindowSettingWindow
{
private:
	HINSTANCE hInstance;
	HWND hWnd;
	WindowSetting m_WindowSetting;

	DialogRet m_Result;

	// ウィンドウ設定ボックスのメッセージ ハンドラー
	static INT_PTR CALLBACK WindowSettingProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
	// 指定したウィンドウハンドルの入力値を取得
	BOOL GetNumberFromText(HWND handle, int& out);
public:
	WindowSettingWindow();
	~WindowSettingWindow();

	// ダイアログの表示
	DialogRet ShowDialog(HINSTANCE h_instance, HWND h_wnd, int NowWidth, int NowHeight);
	// 指定したウィンドウハンドルの入力値を取得
	void GetWindowParameters(WindowSetting& windowSetting);
};

