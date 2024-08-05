#include "WindowSettingWindow.h"

#include <string>
#include <vector>
#include <format>

#include "resource.h"

static WindowSettingWindow *g_Instance = NULL;;		// ウィンドウインスタンス

// コンストラクタ/デストラクタ
WindowSettingWindow::WindowSettingWindow()
{
	hInstance = NULL;
	hWnd = NULL;
    m_WindowSetting = {};
    m_Result = DLG_CANCEL;

    g_Instance = this;
}

WindowSettingWindow::~WindowSettingWindow()
{
}


// ダイアログの表示
DialogRet WindowSettingWindow::ShowDialog(HINSTANCE h_instance, HWND h_wnd, int NowWidth, int NowHeight)
{
	hInstance = h_instance;
	hWnd = h_wnd;
    m_WindowSetting.m_Width = NowWidth;
	m_WindowSetting.m_Height = NowHeight;

    DialogBox(hInstance, MAKEINTRESOURCE(IDD_WINDOWSIZE_DIALOG), hWnd, WindowSettingProc);

    return m_Result;
}

// 設定内容の取得
void WindowSettingWindow::GetWindowParameters(WindowSetting& windowSetting)
{
    windowSetting = m_WindowSetting;
}

// ウィンドウ設定ボックスのメッセージ ハンドラー
INT_PTR CALLBACK WindowSettingWindow::WindowSettingProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    std::wstring out_str;

    switch (message)
    {
    case WM_INITDIALOG:
    	
        out_str = std::format(L"{}", g_Instance->m_WindowSetting.m_Width);
        SetWindowText(GetDlgItem(hDlg, IDC_WINDOW_WIDTH_EDIT), out_str.c_str());

        out_str = std::format(L"{}", g_Instance->m_WindowSetting.m_Height);
        SetWindowText(GetDlgItem(hDlg, IDC_WINDOW_HEIGHT_EDIT), out_str.c_str());

        return (INT_PTR)TRUE;
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_OK_WINDOW_BUTTON)
        {
            WindowSetting output = {};
            BOOL bret = TRUE;
            bret &= g_Instance->GetNumberFromText(GetDlgItem(hDlg, IDC_WINDOW_WIDTH_EDIT), output.m_Width);
        	bret &= g_Instance->GetNumberFromText(GetDlgItem(hDlg, IDC_WINDOW_HEIGHT_EDIT), output.m_Height);

            if (bret == TRUE)
            {
                g_Instance->m_WindowSetting = output;
                g_Instance->m_Result = DLG_OK;
                EndDialog(hDlg, LOWORD(wParam));
            }
            else
            {
                MessageBox(g_Instance->hWnd, L"Invalid number detected. Please enter a valid number",
                    L"Error", MB_OK | MB_ICONERROR);
            }

            return (INT_PTR)TRUE;
        }
        else if (LOWORD(wParam) == IDC_CANCEL_WINDOW_BUTTON)
        {
            g_Instance->m_Result = DLG_CANCEL;

            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

// 指定したウィンドウハンドルの入力値を取得
BOOL WindowSettingWindow::GetNumberFromText(HWND handle, int &out)
{
    int length = ::GetWindowTextLength(handle);
    if (length <= 0) return FALSE;

    std::vector<TCHAR> buffer(length + 1, 0);
    int result = ::GetWindowText(handle, reinterpret_cast<TCHAR*>(&buffer[0]), (int)buffer.size());
    if(result <= 0) return FALSE;

    std::wstring ws(&buffer[0]);

    out = std::stoi(ws);

    return TRUE;
}

