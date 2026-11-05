// ui_draw.h —— GDI+ 绘制原语（字体缓存、圆角矩形、文本、矢量图标、版面缩略图）
#pragma once

#include "ui_theme.h"

#include <string>

enum class Ico {
    Settings, Minus, Close, Download, History, Calendar, ChevLeft, ChevRight,
    Grid, List, Search, Check, Folder, Archive, File, Help, Newspaper, Info, Trash
};

// 字体缓存（尺寸已按 DPI 缩放）
Gdiplus::Font* UiFont(int px, bool bold = false, bool serif = false);
void           ClearFontCache();

void FillRect(Gdiplus::Graphics& g, const RectI& r, Gdiplus::Color c);
void FillRound(Gdiplus::Graphics& g, const RectI& r, int radius, Gdiplus::Color c);
void StrokeRound(Gdiplus::Graphics& g, const RectI& r, int radius, Gdiplus::Color c,
                 float width = 1.0f);
void StrokeRect(Gdiplus::Graphics& g, const RectI& r, Gdiplus::Color c, float width = 1.0f);
void HLine(Gdiplus::Graphics& g, int x, int y, int w, Gdiplus::Color c, float thick = 1.0f);
void VLine(Gdiplus::Graphics& g, int x, int y, int h, Gdiplus::Color c, float thick = 1.0f);

// align: 0 左 1 中 2 右；垂直恒居中
void Text1(Gdiplus::Graphics& g, const std::wstring& s, const RectI& r, int px,
           Gdiplus::Color c, int align = 0, bool bold = false, bool serif = false,
           bool ellipsis = true);
int  TextW(const std::wstring& s, int px, bool bold = false, bool serif = false);

// 多行文本，超出按行数截断并加省略号
void TextWrap(Gdiplus::Graphics& g, const std::wstring& s, const RectI& r, int px,
              Gdiplus::Color c, int maxLines = 2, int align = 0, bool serif = false,
              bool bold = false, int lineGap = 0);

// 字符级字距（对应 CSS letter-spacing）
void Tracked(Gdiplus::Graphics& g, const std::wstring& s, int x, int y, int px,
             Gdiplus::Color c, float em, bool bold = false, bool serif = false);
int  TrackedW(const std::wstring& s, int px, float em, bool bold = false, bool serif = false);

void Icon(Gdiplus::Graphics& g, Ico id, const RectI& box, Gdiplus::Color c,
          float stroke = 1.6f);

// 版面缩略图：front=true 绘制报头，否则绘制版名条；lead 为空时只画占位文本线
void PaperThumb(Gdiplus::Graphics& g, const RectI& box, const std::wstring& masthead,
                int pageNo, const std::wstring& section, const std::wstring& lead,
                const std::wstring& dateText, bool front);

// 近似阴影：以多层半透明圆角矩形模拟
void SoftShadow(Gdiplus::Graphics& g, const RectI& r, int radius, int spread, int alpha);

// 查找图像编码器（PNG 离屏渲染用）
int GetEncoderClsid(const wchar_t* mime, CLSID* clsid);
