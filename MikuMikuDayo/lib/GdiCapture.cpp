#include "GdiCapture.h"
#include "SaveAsPNG.h"

// 参考文献
// 
// ・i-saint氏 "Windows の 3 種類のスクリーンキャプチャ API を検証する"
//    https://qiita.com/i_saint/items/ad5b0545873d0cff4604
//    https://github.com/i-saint/ScreenCaptureTest/tree/master


// GDIを使用した指定ウィンドウハンドルの画像キャプチャ
// Blt: [](HDC hscreen, HDC hdc) -> void
template<class Blt>
bool GdiCapture::CaptureImpl(RECT rect, HWND hwnd, const BitmapCallback& callback, const Blt& blt)
{
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    info.bmiHeader.biSizeImage = width * height * 4;

    bool ret = false;
    HDC hscreen = ::GetDC(hwnd);
    HDC hdc = ::CreateCompatibleDC(hscreen);
    void* data = nullptr;
    if (HBITMAP hbmp = ::CreateDIBSection(hdc, &info, DIB_RGB_COLORS, &data, NULL, NULL)) {
        ::SelectObject(hdc, hbmp);
        blt(hscreen, hdc);
        callback(data, width, height);
        ::DeleteObject(hbmp);
        ret = true;
    }
    ::DeleteDC(hdc);
    ::ReleaseDC(hwnd, hscreen);

    return ret;
}

//ウィンドウキャプチャ(コールバック関数をコール.動画用)
bool GdiCapture::CaptureWindow(HWND hwnd, const BitmapCallback& callback)
{
    //sctProfile("CaptureWindow");
    RECT rect{};
    ::GetWindowRect(hwnd, &rect);

    return CaptureImpl(rect, hwnd, callback, [&](HDC hscreen, HDC hdc) {
        // BitBlt() can't capture Chrome, Edge, etc. PrintWindow() with PW_RENDERFULLCONTENT can do it.
        //::BitBlt(hdc, 0, 0, width, height, hscreen, 0, 0, SRCCOPY);
        ::PrintWindow(hwnd, hdc, PW_RENDERFULLCONTENT);
        });
}

// ウィンドウキャプチャ＆PNGファイルに保存
void CaptureGdiAndSavePng(HWND hwnd, const TCHAR* png_filename)
{
    static TCHAR* pngFilename = (TCHAR*)png_filename;

    HWND target = hwnd;
    GdiCapture gdiCapture;
    gdiCapture.CaptureWindow(target, [](const void* data, int w, int h) {
        SaveAsPNG(pngFilename, w, h, w * 4, data, true);
        });
}