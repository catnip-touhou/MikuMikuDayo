#pragma once

// 共通
#include <vector>
#include <functional>
#include <mutex>
#include <condition_variable>

// Win32
#include <windows.h>

#pragma comment(lib,"gdi32.lib")

using BitmapCallback = std::function<void(const void* data, int width, int height)>;

class GdiCapture
{
private:
	
	// GDIを使用した指定ウィンドウハンドルの画像キャプチャ
	// Blt: [](HDC hscreen, HDC hdc) -> void
	template<class Blt>
	bool CaptureImpl(RECT rect, HWND hwnd, const BitmapCallback& callback, const Blt& blt);
public:
	
	//ウィンドウキャプチャ(コールバック関数をコール.動画用)
	bool CaptureWindow(HWND hwnd, const BitmapCallback& callback);
};


// ウィンドウキャプチャ＆PNGファイルに保存
extern void CaptureGdiAndSavePng(HWND hwnd, const TCHAR* png_filename);
