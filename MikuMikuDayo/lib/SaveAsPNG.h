#pragma once

// PNGファイルに保存
bool SaveAsPNG(const wchar_t* path, int w, int h, int src_stride, const void* data, bool flip_y);