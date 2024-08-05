#include "AVISettingWindow.h"

#include <string>
#include <vector>
#include <format>

#include "resource.h"

static AVISettingWindow *g_Instance = NULL;		// ウィンドウインスタンス

// コンストラクタ/デストラクタ
AVISettingWindow::AVISettingWindow()
{
	hInstance = NULL;
	hWnd = NULL;
    m_AVISetting = {};
    m_AVISetting.m_Avi_Width = 1600;
    m_AVISetting.m_Avi_height = 900;
    m_AVISetting.m_StartFrame = 0;
    m_AVISetting.m_FPS = 30;
    m_AVISetting.m_AccumFrame = 2;
    m_AVISetting.m_EndFrame = 100;
    m_Result = DLG_CANCEL;

    g_Instance = this;
}

AVISettingWindow::~AVISettingWindow()
{
}


// ダイアログの表示
DialogRet AVISettingWindow::ShowDialog(HINSTANCE h_instance, HWND h_wnd, int EndFrame, int Fps)
{
	hInstance = h_instance;
	hWnd = h_wnd;
    m_AVISetting.m_EndFrame = EndFrame;
    m_AVISetting.m_FPS = Fps;

    DialogBox(hInstance, MAKEINTRESOURCE(IDD_AVISETTING_DIALOG), hWnd, AviSettingProc);

    return m_Result;
}

// 設定内容の取得
void AVISettingWindow::GetAviParameters(AVISetting& aviSetting)
{
    aviSetting = m_AVISetting;
}


// 設定内容の更新
void AVISettingWindow::SetAviParameters(AVISetting& aviSetting)
{
    m_AVISetting = aviSetting;
}

// AVI 出力設定ボックスのメッセージ ハンドラー
INT_PTR CALLBACK AVISettingWindow::AviSettingProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    std::wstring out_str;

    switch (message)
    {
    case WM_INITDIALOG:

        out_str = std::format(L"{}", g_Instance->m_AVISetting.m_Avi_Width);
        SetWindowText(GetDlgItem(hDlg, IDC_EDIT_Width), out_str.c_str());
        out_str = std::format(L"{}", g_Instance->m_AVISetting.m_Avi_height);
        SetWindowText(GetDlgItem(hDlg, IDC_EDIT_Height), out_str.c_str());
        out_str = std::format(L"{}", g_Instance->m_AVISetting.m_StartFrame);
        SetWindowText(GetDlgItem(hDlg, IDC_EDIT_StartFrame), out_str.c_str());
        out_str = std::format(L"{}", g_Instance->m_AVISetting.m_EndFrame);
        SetWindowText(GetDlgItem(hDlg, IDC_EDIT_EndFrame), out_str.c_str());
        out_str = std::format(L"{}", g_Instance->m_AVISetting.m_FPS);
        SetWindowText(GetDlgItem(hDlg, IDC_EDIT_FPS), out_str.c_str());
        out_str = std::format(L"{}", g_Instance->m_AVISetting.m_AccumFrame);
        SetWindowText(GetDlgItem(hDlg, IDC_EDIT_AccmFrame), out_str.c_str());

        // FPSは現状30FPSしか対応していないので、入力を無効化
        EnableWindow(GetDlgItem(hDlg, IDC_EDIT_FPS), FALSE);

        return (INT_PTR)TRUE;
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_OK_BUTTON)
        {
            AVISetting output = {};
            BOOL bret = TRUE;
            bret &= g_Instance->GetNumberFromText(GetDlgItem(hDlg, IDC_EDIT_Width), output.m_Avi_Width);
            bret &= g_Instance->GetNumberFromText(GetDlgItem(hDlg, IDC_EDIT_Height), output.m_Avi_height);
            bret &= g_Instance->GetNumberFromText(GetDlgItem(hDlg, IDC_EDIT_StartFrame), output.m_StartFrame);
            bret &= g_Instance->GetNumberFromText(GetDlgItem(hDlg, IDC_EDIT_EndFrame), output.m_EndFrame);
            bret &= g_Instance->GetNumberFromText(GetDlgItem(hDlg, IDC_EDIT_FPS), output.m_FPS);
            bret &= g_Instance->GetNumberFromText(GetDlgItem(hDlg, IDC_EDIT_AccmFrame), output.m_AccumFrame);

            if (bret == TRUE)
            {
                g_Instance->m_AVISetting = output;
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
        else if (LOWORD(wParam) == IDC_CANCEL_BUTTON)
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
BOOL AVISettingWindow::GetNumberFromText(HWND handle, int &out)
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

