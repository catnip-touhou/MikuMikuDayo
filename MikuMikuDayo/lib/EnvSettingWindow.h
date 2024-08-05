#pragma once

#include <Windows.h>
#include <string>

struct EnvSetting
{
public:
	float m_V_Gain;		// 明度ゲイン値
	float m_S_Gain;		// 彩度ゲイン値
};

class EnvSettingWindow
{
private:
	HINSTANCE hInstance;
	HWND hWnd;
	HWND hDlg_Wnd;
	HFONT hFont;
	EnvSetting m_EnvSetting;

	// 環境設定ボックスのメッセージ ハンドラー
	static INT_PTR CALLBACK EnvSettingProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
	// 指定したウィンドウハンドルへ引数値(スライダーバーの整数値)を物理値に変換＆テキストボックスに設定
	BOOL SetTextFromNumber(HWND handle, int input, float& output);
public:
	EnvSettingWindow();
	~EnvSettingWindow();

	// ダイアログの表示(モーダレスダイアログ)
	void ShowDialog(HINSTANCE h_instance, HWND h_wnd);
	// 設定内容の取得
	void GetEnvEnvSetting(EnvSetting& envSetting);
	// 設定内容の更新
	void SetEnvEnvSetting(EnvSetting& envSetting);
	// ダイアログのハンドラの取得
	HWND hDlg();
	// 設定内容の取得
	void SetStatusText(std::wstring str);
};

