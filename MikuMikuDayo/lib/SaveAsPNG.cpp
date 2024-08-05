#include "SaveAsPNG.h"

#include <vector>
#include <string>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define __STDC_LIB_EXT1__
#include "stb_image_write.h"

// wchar_t文字列をUTF-8に変換する関数
std::string ConvertTCHARtoUTF8(const wchar_t* tcharString);

// 参考文献
// 
// ・i-saint氏 "Windows の 3 種類のスクリーンキャプチャ API を検証する"
//    https://qiita.com/i_saint/items/ad5b0545873d0cff4604
//    https://github.com/i-saint/ScreenCaptureTest/tree/master

// PNGファイルに保存
bool SaveAsPNG(const wchar_t* path, int w, int h, int src_stride, const void* data, bool flip_y)
{
    std::vector<unsigned char> buf(w * h * 4);
    int dst_stride = w * 4;
    auto src = (const unsigned char*)data;
    auto dst = (unsigned char*)buf.data();
    if (flip_y) {
    	// 上下反転, RGBAの並び入れ替え
        for (int i = 0; i < h; ++i) {
            auto s = src + (src_stride * (h - i - 1));
            auto d = dst + (dst_stride * i);
            for (int j = 0; j < w; ++j) {
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
                d[3] = s[3];
                s += 4;
                d += 4;
            }
        }
    }
    else {
    	// 上下反転せず, RGBAの並び入れ替え
        for (int i = 0; i < h; ++i) {
            auto s = src + (src_stride * i);
            auto d = dst + (dst_stride * i);
            for (int j = 0; j < w; ++j) {
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
                d[3] = s[3];
                s += 4;
                d += 4;
            }
        }
    }

	// Unicodeファイル名をUTF8ファイル名に変換
    std::string char_path = ConvertTCHARtoUTF8(path);
	
	// stbi_write_pngコンポーネントを呼び出し, PNGファイルに保存
    return stbi_write_png(char_path.c_str(), w, h, 4, buf.data(), dst_stride);
}

// wchar_t文字列をUTF-8に変換する関数
std::string ConvertTCHARtoUTF8(const wchar_t* tcharString) {
    // TCHAR文字列をUTF-16に変換
    std::wstring utf16String(tcharString);

    // UTF-16からUTF-8に変換
    std::string utf8String;
    for (wchar_t ch : utf16String) {
        if (ch <= 0x7F) {
            utf8String += static_cast<char>(ch);
        }
        else if (ch <= 0x7FF) {
            utf8String += static_cast<char>(0xC0 | ((ch >> 6) & 0x1F));
            utf8String += static_cast<char>(0x80 | (ch & 0x3F));
        }
        else {
            utf8String += static_cast<char>(0xE0 | ((ch >> 12) & 0x0F));
            utf8String += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
            utf8String += static_cast<char>(0x80 | (ch & 0x3F));
        }
    }

    return utf8String;
}