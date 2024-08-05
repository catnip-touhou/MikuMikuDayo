#pragma once

#include <Windows.h>
#include "Typedef.h"

struct MotionSetting
{
public:
	int m_Frame;	// ジャンプ先のフレームNo
};

class MotionSettingWindow
{
private:
	HINSTANCE hInstance;
	HWND hWnd;
	MotionSetting m_MotionSetting;

	DialogRet m_Result;

	// モーション設定ボックスのメッセージ ハンドラー
	static INT_PTR CALLBACK MotionSettingProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
	// 指定したウィンドウハンドルの入力値を取得
	BOOL GetNumberFromText(HWND handle, int& out);
public:
	MotionSettingWindow();
	~MotionSettingWindow();

	// ダイアログの表示
	DialogRet ShowDialog(HINSTANCE h_instance, HWND h_wnd, int NowFrame);
	// 設定内容の取得
	void GetMotionParameters(MotionSetting& motionSetting);
};

