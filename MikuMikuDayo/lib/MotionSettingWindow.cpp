#include "MotionSettingWindow.h"

#include <string>
#include <vector>
#include <format>

#include "resource.h"

static MotionSettingWindow *g_Instance = NULL;		// ウィンドウインスタンス

// コンストラクタ/デストラクタ
MotionSettingWindow::MotionSettingWindow()
{
	hInstance = NULL;
	hWnd = NULL;
    m_MotionSetting = {};
    m_Result = DLG_CANCEL;

    g_Instance = this;
}

MotionSettingWindow::~MotionSettingWindow()
{
}


// ダイアログの表示
DialogRet MotionSettingWindow::ShowDialog(HINSTANCE h_instance, HWND h_wnd, int NowFrame)
{
	hInstance = h_instance;
	hWnd = h_wnd;
    m_MotionSetting.m_Frame = NowFrame;

    DialogBox(hInstance, MAKEINTRESOURCE(IDD_MOTIONSETTING_DIALOG), hWnd, MotionSettingProc);

    return m_Result;
}

// 設定内容の取得
void MotionSettingWindow::GetMotionParameters(MotionSetting& motionSetting)
{
    motionSetting = m_MotionSetting;
}

// モーション設定ボックスのメッセージ ハンドラー
INT_PTR CALLBACK MotionSettingWindow::MotionSettingProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    std::wstring out_str;

    switch (message)
    {
    case WM_INITDIALOG:
    	
        out_str = std::format(L"{}", g_Instance->m_MotionSetting.m_Frame);
        SetWindowText(GetDlgItem(hDlg, IDC_MOTION_FRAME_EDIT), out_str.c_str());

        return (INT_PTR)TRUE;
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_OK_MOTION_BUTTON)
        {
            MotionSetting output = {};
            BOOL bret = TRUE;
            bret &= g_Instance->GetNumberFromText(GetDlgItem(hDlg, IDC_MOTION_FRAME_EDIT), output.m_Frame);

            if (bret == TRUE)
            {
                g_Instance->m_MotionSetting = output;
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
        else if (LOWORD(wParam) == IDC_CANCEL_MOTION_BUTTON)
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
BOOL MotionSettingWindow::GetNumberFromText(HWND handle, int &out)
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

