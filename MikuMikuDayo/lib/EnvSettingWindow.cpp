#include "EnvSettingWindow.h"

#include <vector>
#include <format>

#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

#include "resource.h"

static EnvSettingWindow *g_Instance = NULL;		// ウィンドウインスタンス

// コンストラクタ/デストラクタ
EnvSettingWindow::EnvSettingWindow()
{
	hInstance = NULL;
	hWnd = NULL;
    hDlg_Wnd = NULL;
    hFont = NULL;
    m_EnvSetting = {1.0f, 1.0f};

    g_Instance = this;
}

EnvSettingWindow::~EnvSettingWindow()
{
}

// ダイアログの表示(モーダレスダイアログ)
void EnvSettingWindow::ShowDialog(HINSTANCE h_instance, HWND h_wnd)
{
	hInstance = h_instance;
	hWnd = h_wnd;

    if (hDlg_Wnd == NULL)
    {
    	// モーダレスダイアログとして作成
        hDlg_Wnd = CreateDialog(hInstance, MAKEINTRESOURCE(IDD_ENVSETTING_FORM), hWnd, EnvSettingProc);
    }
    ::ShowWindow(hDlg_Wnd, SW_SHOW);
}


// 設定内容の取得
void EnvSettingWindow::GetEnvEnvSetting(EnvSetting& envSetting)
{
    envSetting = m_EnvSetting;
}

// 設定内容の更新
void EnvSettingWindow::SetEnvEnvSetting(EnvSetting& envSetting)
{
    m_EnvSetting = envSetting;

    HWND hBar_V = GetDlgItem(hDlg_Wnd, IDC_SLIDER_Brightness);
    HWND hBar_S = GetDlgItem(hDlg_Wnd, IDC_SLIDER_Saturation);
    int Pos_V = g_Instance->m_EnvSetting.m_V_Gain * 1000;
    int Pos_S = g_Instance->m_EnvSetting.m_S_Gain * 1000;

    std::wstring out_str;
    out_str = std::format(L"{:.2f}", g_Instance->m_EnvSetting.m_V_Gain);
    SetWindowText(GetDlgItem(hDlg_Wnd, IDC_EDIT_Brightness), out_str.c_str());
    out_str = std::format(L"{:.2f}", g_Instance->m_EnvSetting.m_S_Gain);
    SetWindowText(GetDlgItem(hDlg_Wnd, IDC_EDIT_Saturation), out_str.c_str());

    SendMessage(hBar_V, TBM_SETPOS, TRUE, Pos_V);                   // 位置の設定
    SendMessage(hBar_S, TBM_SETPOS, TRUE, Pos_S);                   // 位置の設定
}

// ダイアログのハンドラの取得
HWND EnvSettingWindow::hDlg()
{
    return hDlg_Wnd;
}

// 文字列置換関数
std::wstring ReplaceString(
     std::wstring String1,  // 置き換え対象
     std::wstring String2,  // 検索対象
     std::wstring String3   // 置き換える内容
)
{
    std::wstring::size_type  Pos(String1.find(String2));

    while (Pos != std::string::npos)
    {
        String1.replace(Pos, String2.length(), String3);
        Pos = String1.find(String2, Pos + String3.length());
    }

    return String1;
}


// Infomation情報(≒Telop情報)の設定
void EnvSettingWindow::SetStatusText(std::wstring str)
{
    if (hDlg_Wnd != NULL) {
        //std::wstring str_buf = ReplaceString(str, L"\n", L"\r\n");
        std::wstring str_buf = str;
        SetWindowText(GetDlgItem(hDlg_Wnd, IDC_EDIT_Infomation), str_buf.c_str());
    }

}

// 環境設定ボックスのメッセージ ハンドラー
INT_PTR CALLBACK EnvSettingWindow::EnvSettingProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    std::wstring out_str;
    HWND hBar_V = GetDlgItem(hDlg, IDC_SLIDER_Brightness);
    HWND hBar_S = GetDlgItem(hDlg, IDC_SLIDER_Saturation);
    int Pos_V = g_Instance->m_EnvSetting.m_V_Gain * 1000;
    int Pos_S = g_Instance->m_EnvSetting.m_S_Gain * 1000;

    switch (message)
    {
    case WM_INITDIALOG:
        //g_Instance->m_EnvSetting.m_V_Gain = 1.0;
        //g_Instance->m_EnvSetting.m_S_Gain = 1.0;

        InitCommonControls();

        // フォント変更
        g_Instance->hFont = CreateFont(18, 0, 0, 0,
            FW_NORMAL, FALSE, FALSE, 0,
            ANSI_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH, L"Consolas");
        SendMessage(GetDlgItem(hDlg, IDC_EDIT_Infomation), WM_SETFONT, 
            (WPARAM)g_Instance->hFont, MAKELPARAM(FALSE, 0));

        out_str = std::format(L"{:.2f}", g_Instance->m_EnvSetting.m_V_Gain);
        SetWindowText(GetDlgItem(hDlg, IDC_EDIT_Brightness), out_str.c_str());
        out_str = std::format(L"{:.2f}", g_Instance->m_EnvSetting.m_S_Gain);
        SetWindowText(GetDlgItem(hDlg, IDC_EDIT_Saturation), out_str.c_str());

        SendMessage(hBar_V, TBM_SETRANGE, TRUE, MAKELPARAM(0, 2000));   // レンジを指定
        SendMessage(hBar_V, TBM_SETTICFREQ, 1, 0);                      // 目盛りの増分
        SendMessage(hBar_V, TBM_SETPOS, TRUE, Pos_V);                   // 位置の設定
        SendMessage(hBar_V, TBM_SETPAGESIZE, 0, 10);                    // クリック時の移動量
        SendMessage(hBar_S, TBM_SETRANGE, TRUE, MAKELPARAM(0, 2000));   // レンジを指定
        SendMessage(hBar_S, TBM_SETTICFREQ, 1, 0);                      // 目盛りの増分
        SendMessage(hBar_S, TBM_SETPOS, TRUE, Pos_S);                   // 位置の設定
        SendMessage(hBar_S, TBM_SETPAGESIZE, 0, 10);                    // クリック時の移動量

        return (INT_PTR)TRUE;
        break;
    case WM_HSCROLL:
        if (GetDlgItem(hDlg, IDC_SLIDER_Brightness) == (HWND)lParam)
        {
            Pos_V = (int)SendMessage(hBar_V, TBM_GETPOS, NULL, NULL); // 現在の値の取得
            g_Instance->SetTextFromNumber(GetDlgItem(hDlg, IDC_EDIT_Brightness), Pos_V, g_Instance->m_EnvSetting.m_V_Gain);
        }
        else if (GetDlgItem(hDlg, IDC_SLIDER_Saturation) == (HWND)lParam)
        {
            Pos_S = (int)SendMessage(hBar_S, TBM_GETPOS, NULL, NULL); // 現在の値の取得
            g_Instance->SetTextFromNumber(GetDlgItem(hDlg, IDC_EDIT_Saturation), Pos_S, g_Instance->m_EnvSetting.m_S_Gain);
        }
        break;
    case WM_CLOSE:
        DestroyWindow(g_Instance->hDlg_Wnd);
        break;
    case WM_DESTROY:
        DeleteObject(g_Instance->hFont);
        g_Instance->hDlg_Wnd = NULL;
        break;
    }
    return (INT_PTR)FALSE;
}

// 指定したウィンドウハンドルへ引数値(スライダーバーの整数値)を物理値に変換＆テキストボックスに設定
BOOL EnvSettingWindow::SetTextFromNumber(HWND handle, int input, float &output )
{
    // 0 : 0.0, 1000 : 1.0, 2000 : 2.0
    output = ((float)input / 1000.0f);
    std::wstring text_out = std::format(L"{:.2f}", ((float)input / 1000.0f));
    SetWindowText(handle, text_out.c_str());

    return TRUE;
}

