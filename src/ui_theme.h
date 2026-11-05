// ui_theme.h —— 设计规范（对照 Figma 导出的设计稿取值）
#pragma once

#include "core.h"

#include <objidl.h>
#include <gdiplus.h>

namespace th {

// ---------------------------------------------------------------- 颜色
inline COLORREF Rgb(unsigned v) { return RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF); }

const COLORREF AppBg       = Rgb(0xF6F5F2);  // 窗口底色
const COLORREF TitleBg     = Rgb(0x272725);
const COLORREF TitleHover  = Rgb(0x373735);
const COLORREF TitleText   = Rgb(0xFFFFFF);
const COLORREF TitleMuted  = Rgb(0x989894);
const COLORREF BrandRed    = Rgb(0xB82222);

const COLORREF SideBg      = Rgb(0xEEECE7);
const COLORREF SideLine    = Rgb(0xDEDBD4);
const COLORREF SideHover   = Rgb(0xE5E2DC);
const COLORREF SideActive  = Rgb(0xDEDAD2);
const COLORREF SideBadge   = Rgb(0xD6D1C9);

const COLORREF ContentBg   = Rgb(0xF8F7F4);
const COLORREF PanelBg     = Rgb(0xF0EEE9);

const COLORREF Accent      = Rgb(0xA52021);
const COLORREF AccentHover = Rgb(0x8E1718);
const COLORREF AccentSoft  = Rgb(0xF4ECE9);

const COLORREF TextMain    = Rgb(0x292927);
const COLORREF TextBody    = Rgb(0x686762);
const COLORREF TextSub     = Rgb(0x94918A);
const COLORREF TextMuted   = Rgb(0x9A9892);
const COLORREF TextFaint   = Rgb(0xAAA69F);

const COLORREF Line        = Rgb(0xE2DFD9);
const COLORREF LineSoft    = Rgb(0xEBE8E2);
const COLORREF LineStrong  = Rgb(0xDEDBD4);

const COLORREF PaperBg     = Rgb(0xFFFEF9);
const COLORREF PaperInk    = Rgb(0xBBB6AC);
const COLORREF PaperRule   = Rgb(0xC7C1B6);

const COLORREF OkGreen     = Rgb(0x397353);
const COLORREF OkDot       = Rgb(0x4E9B6D);
const COLORREF OkDotRing   = Rgb(0xDCE8DF);
const COLORREF Disabled    = Rgb(0xC5C0B8);
const COLORREF CardBorder  = Rgb(0xDEDBD5);
const COLORREF CardHover   = Rgb(0xC7C2B9);
const COLORREF CoverBg     = Rgb(0xE9E6DF);

// ---------------------------------------------------------------- 尺寸（96 DPI 基准）
const int TitleH      = 48;
const int SideW       = 210;
const int TaskW       = 286;
const int ContentPadX = 32;
const int ContentPadT = 31;
const int TaskPadX    = 22;
const int TaskPadT    = 31;

}  // namespace th

// COLORREF -> Gdiplus::Color
inline Gdiplus::Color GC(COLORREF c, int alpha = 255) {
    return Gdiplus::Color((BYTE)alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

struct RectI {
    int x = 0, y = 0, w = 0, h = 0;
    bool Has(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    int  Right() const { return x + w; }
    int  Bottom() const { return y + h; }
    Gdiplus::RectF F() const {
        return Gdiplus::RectF((Gdiplus::REAL)x, (Gdiplus::REAL)y, (Gdiplus::REAL)w,
                              (Gdiplus::REAL)h);
    }
    RectI Inset(int v) const { return RectI{x + v, y + v, w - 2 * v, h - 2 * v}; }
};

RectI Intersect(const RectI& a, const RectI& b);
bool  Empty(const RectI& r);
