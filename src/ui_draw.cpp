// ui_draw.cpp —— GDI+ 绘制实现
#include "ui_draw.h"

#include <cmath>
#include <cstdlib>
#include <map>

using namespace Gdiplus;

// ---------------------------------------------------------------- 字体
static FontFamily* g_famSans = nullptr;
static FontFamily* g_famSerif = nullptr;
static std::map<int, Font*>* g_fonts = nullptr;

static bool FamilyAvailable(const wchar_t* name) {
    FontFamily f(name);
    return f.GetLastStatus() == Ok;
}

static FontFamily* PickFamily(bool serif) {
    if (serif) {
        const wchar_t* cands[] = {L"Noto Serif SC", L"Source Han Serif SC", L"Source Han Serif CN",
                                  L"SimSun", L"NSimSun", L"Microsoft YaHei UI"};
        for (auto c : cands)
            if (FamilyAvailable(c)) return new FontFamily(c);
        return FontFamily::GenericSerif()->Clone();
    }
    const wchar_t* cands[] = {L"Microsoft YaHei UI", L"Microsoft YaHei", L"Noto Sans SC",
                              L"PingFang SC", L"SimHei"};
    for (auto c : cands)
        if (FamilyAvailable(c)) return new FontFamily(c);
    return FontFamily::GenericSansSerif()->Clone();
}

Font* UiFont(int px, bool bold, bool serif) {
    if (px < 6) px = 6;
    if (!g_famSans) g_famSans = PickFamily(false);
    if (!g_famSerif) g_famSerif = PickFamily(true);
    if (!g_fonts) g_fonts = new std::map<int, Font*>();

    int key = (px << 2) | (bold ? 1 : 0) | (serif ? 2 : 0);
    auto it = g_fonts->find(key);
    if (it != g_fonts->end()) return it->second;

    FontFamily* fam = serif ? g_famSerif : g_famSans;
    Font* f = new Font(fam, (REAL)px, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    (*g_fonts)[key] = f;
    return f;
}

void ClearFontCache() {
    if (g_fonts) {
        for (auto& kv : *g_fonts) delete kv.second;
        delete g_fonts;
        g_fonts = nullptr;
    }
    delete g_famSans;
    g_famSans = nullptr;
    delete g_famSerif;
    g_famSerif = nullptr;
}

// ---------------------------------------------------------------- 几何
RectI Intersect(const RectI& a, const RectI& b) {
    int x1 = a.x > b.x ? a.x : b.x;
    int y1 = a.y > b.y ? a.y : b.y;
    int x2 = (a.Right() < b.Right() ? a.Right() : b.Right());
    int y2 = (a.Bottom() < b.Bottom() ? a.Bottom() : b.Bottom());
    if (x2 <= x1 || y2 <= y1) return RectI{0, 0, 0, 0};
    return RectI{x1, y1, x2 - x1, y2 - y1};
}

bool Empty(const RectI& r) { return r.w <= 0 || r.h <= 0; }

static void AddRoundRect(GraphicsPath& p, REAL x, REAL y, REAL w, REAL h, REAL r) {
    if (r < 0.5f) {
        p.StartFigure();
        p.AddLine(x, y, x + w, y);
        p.AddLine(x + w, y, x + w, y + h);
        p.AddLine(x + w, y + h, x, y + h);
        p.AddLine(x, y + h, x, y);
        p.CloseFigure();
        return;
    }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    REAL d = r * 2;
    p.StartFigure();
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}

void FillRect(Graphics& g, const RectI& r, Color c) {
    if (Empty(r)) return;
    SolidBrush b(c);
    g.FillRectangle(&b, r.x, r.y, r.w, r.h);
}

void FillRound(Graphics& g, const RectI& r, int radius, Color c) {
    if (Empty(r)) return;
    GraphicsPath p;
    AddRoundRect(p, (REAL)r.x, (REAL)r.y, (REAL)r.w, (REAL)r.h, (REAL)radius);
    SolidBrush b(c);
    g.FillPath(&b, &p);
}

void StrokeRound(Graphics& g, const RectI& r, int radius, Color c, float width) {
    if (Empty(r)) return;
    GraphicsPath p;
    AddRoundRect(p, (REAL)r.x + 0.5f, (REAL)r.y + 0.5f, (REAL)r.w - 1, (REAL)r.h - 1,
                 (REAL)radius);
    Pen pen(c, width);
    g.DrawPath(&pen, &p);
}

void StrokeRect(Graphics& g, const RectI& r, Color c, float width) {
    if (Empty(r)) return;
    Pen pen(c, width);
    g.DrawRectangle(&pen, r.x, r.y, r.w, r.h);
}

void HLine(Graphics& g, int x, int y, int w, Color c, float thick) {
    FillRect(g, RectI{x, y, w, (int)ceilf(thick)}, c);
}

void VLine(Graphics& g, int x, int y, int h, Color c, float thick) {
    FillRect(g, RectI{x, y, (int)ceilf(thick), h}, c);
}

void SoftShadow(Graphics& g, const RectI& r, int radius, int spread, int alpha) {
    for (int i = spread; i > 0; --i) {
        RectI rr{r.x - i, r.y - i + i / 2, r.w + i * 2, r.h + i * 2};
        FillRound(g, rr, radius + i, Color((BYTE)(alpha / (i + 1)), 60, 50, 35));
    }
}

// ---------------------------------------------------------------- 文本
static Graphics* MeasureGraphics() {
    static Bitmap*  bmp = nullptr;
    static Graphics* g = nullptr;
    if (!g) {
        bmp = new Bitmap(4, 4, PixelFormat32bppARGB);
        g = new Graphics(bmp);
        g->SetTextRenderingHint(TextRenderingHintAntiAlias);
    }
    return g;
}

int TextW(const std::wstring& s, int px, bool bold, bool serif) {
    if (s.empty()) return 0;
    Font*              f = UiFont(px, bold, serif);
    StringFormat       fmt(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    RectF box;
    MeasureGraphics()->MeasureString(s.c_str(), -1, f, PointF(0, 0), &fmt, &box);
    return (int)ceilf(box.Width);
}

void Text1(Graphics& g, const std::wstring& s, const RectI& r, int px, Color c, int align,
           bool bold, bool serif, bool ellipsis) {
    if (s.empty() || Empty(r)) return;
    Font* f = UiFont(px, bold, serif);
    StringFormat fmt;
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    fmt.SetAlignment(align == 1 ? StringAlignmentCenter
                                : align == 2 ? StringAlignmentFar : StringAlignmentNear);
    fmt.SetLineAlignment(StringAlignmentCenter);
    fmt.SetTrimming(ellipsis ? StringTrimmingEllipsisCharacter : StringTrimmingNone);
    SolidBrush br(c);
    g.DrawString(s.c_str(), -1, f, r.F(), &fmt, &br);
}

void TextWrap(Graphics& g, const std::wstring& s, const RectI& r, int px, Color c, int maxLines,
              int align, bool serif, bool bold, int lineGap) {
    if (s.empty() || Empty(r)) return;
    Font* f = UiFont(px, bold, serif);
    StringFormat fmt;
    fmt.SetFormatFlags(StringFormatFlagsLineLimit);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    fmt.SetAlignment(align == 1 ? StringAlignmentCenter
                                : align == 2 ? StringAlignmentFar : StringAlignmentNear);
    fmt.SetLineAlignment(StringAlignmentNear);
    (void)lineGap;
    SolidBrush br(c);
    g.DrawString(s.c_str(), -1, f, r.F(), &fmt, &br);
    (void)maxLines;
}

void Tracked(Graphics& g, const std::wstring& s, int x, int y, int px, Color c, float em,
             bool bold, bool serif) {
    Font*      f = UiFont(px, bold, serif);
    SolidBrush br(c);
    StringFormat fmt(StringFormatFlagsNoWrap);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    int cx = x;
    for (wchar_t ch : s) {
        wchar_t buf[2] = {ch, 0};
        g.DrawString(buf, -1, f, PointF((REAL)cx, (REAL)y), &fmt, &br);
        cx += TextW(buf, px, bold, serif) + (int)(em * px);
    }
}

int TrackedW(const std::wstring& s, int px, float em, bool bold, bool serif) {
    int w = 0;
    for (wchar_t ch : s) {
        wchar_t buf[2] = {ch, 0};
        w += TextW(buf, px, bold, serif) + (int)(em * px);
    }
    return w;
}

// ---------------------------------------------------------------- 图标
void Icon(Graphics& g, Ico id, const RectI& box, Color c, float stroke) {
    int side = box.w < box.h ? box.w : box.h;
    REAL s = (REAL)side / 24.0f;
    REAL ox = box.x + (box.w - 24 * s) / 2.0f;
    REAL oy = box.y + (box.h - 24 * s) / 2.0f;

    GraphicsPath p;
    auto X = [&](REAL v) { return ox + v * s; };
    auto Y = [&](REAL v) { return oy + v * s; };
    auto L = [&](REAL x1, REAL y1, REAL x2, REAL y2) {
        p.AddLine(X(x1), Y(y1), X(x2), Y(y2));
    };
    auto SF = [&]() { p.StartFigure(); };
    auto E = [&](REAL cx, REAL cy, REAL r) {
        p.AddEllipse(X(cx - r), Y(cy - r), 2 * r * s, 2 * r * s);
    };
    auto RR = [&](REAL x, REAL y, REAL w, REAL h, REAL r) {
        AddRoundRect(p, X(x), Y(y), w * s, h * s, r * s);
    };
    auto Closed = [&]() { p.CloseFigure(); };

    switch (id) {
    case Ico::Minus:
        SF(); L(5, 12, 19, 12);
        break;
    case Ico::Close:
        SF(); L(6, 6, 18, 18);
        SF(); L(18, 6, 6, 18);
        break;
    case Ico::ChevLeft:
        SF(); L(15, 18, 9, 12); L(9, 12, 15, 6);
        break;
    case Ico::ChevRight:
        SF(); L(9, 18, 15, 12); L(15, 12, 9, 6);
        break;
    case Ico::Download:
        SF(); L(12, 3, 12, 15);
        SF(); L(7, 10, 12, 15); L(12, 15, 17, 10);
        SF(); L(5, 20, 19, 20);
        break;
    case Ico::Calendar:
        RR(3, 5, 18, 16, 2);
        SF(); L(16, 3, 16, 7);
        SF(); L(8, 3, 8, 7);
        SF(); L(3, 10, 21, 10);
        break;
    case Ico::Search:
        SF(); E(11, 11, 7);
        SF(); L(16.2, 16.2, 20, 20);
        break;
    case Ico::Check:
        SF(); L(5, 12, 9, 16); L(9, 16, 19, 6);
        break;
    case Ico::Grid:
        RR(3, 3, 7, 7, 1);
        RR(14, 3, 7, 7, 1);
        RR(3, 14, 7, 7, 1);
        RR(14, 14, 7, 7, 1);
        break;
    case Ico::List:
        SF(); L(9, 6, 21, 6);
        SF(); L(9, 12, 21, 12);
        SF(); L(9, 18, 21, 18);
        SF(); E(4, 6, 1);
        SF(); E(4, 12, 1);
        SF(); E(4, 18, 1);
        break;
    case Ico::Folder:
        SF();
        L(3, 19, 3, 6);
        L(3, 6, 10, 6);
        L(10, 6, 12, 8);
        L(12, 8, 21, 8);
        L(21, 8, 21, 19);
        L(21, 19, 3, 19);
        Closed();
        break;
    case Ico::Archive:
        RR(3, 3, 18, 4, 0.5);
        RR(4, 7, 16, 13, 1);
        SF(); L(9, 11, 15, 11);
        break;
    case Ico::File:
        SF();
        L(6, 2, 14, 2);
        L(14, 2, 18, 6);
        L(18, 6, 18, 22);
        L(18, 22, 6, 22);
        L(6, 22, 6, 2);
        Closed();
        SF(); L(14, 2, 14, 6); L(14, 6, 18, 6);
        SF(); L(9, 12, 15, 12);
        SF(); L(9, 16, 15, 16);
        break;
    case Ico::Help:
        SF(); E(12, 12, 9);
        SF();
        p.AddBezier(X(9.6f), Y(9.6f), X(9.6f), Y(7.0f), X(14.4f), Y(7.0f), X(14.4f), Y(9.6f));
        p.AddBezier(X(14.4f), Y(9.6f), X(14.4f), Y(11.4f), X(12.0f), Y(11.2f), X(12.0f), Y(13.4f));
        SF(); E(12, 17, 0.7);
        break;
    case Ico::History:
        p.AddArc(X(3), Y(3), 18 * s, 18 * s, 200, 300);
        SF(); L(3, 3, 3, 8); L(3, 8, 8, 8);
        SF(); L(12, 7, 12, 12); L(12, 12, 15, 14);
        break;
    case Ico::Newspaper:
        SF();
        L(4, 4, 18, 4);
        L(18, 4, 18, 20);
        L(18, 20, 4, 20);
        L(4, 20, 4, 4);
        Closed();
        SF(); L(18, 8, 20, 8); L(20, 8, 20, 18); L(20, 18, 18, 20);
        SF(); L(7, 8, 15, 8);
        SF(); L(7, 12, 10, 12); L(12, 12, 15, 12);
        SF(); L(7, 16, 15, 16);
        break;
    case Ico::Settings: {
        SF(); E(12, 12, 3.2);
        for (int i = 0; i < 8; ++i) {
            double a = i * 3.14159265358979 / 4.0;
            REAL   c1 = (REAL)cos(a), s1 = (REAL)sin(a);
            SF();
            L(12 + c1 * 6.8f, 12 + s1 * 6.8f, 12 + c1 * 9.2f, 12 + s1 * 9.2f);
        }
        break;
    }
    case Ico::Info:
        SF(); E(12, 12, 9);
        SF(); L(12, 11, 12, 17);
        SF(); E(12, 7.6, 0.7);
        break;
    case Ico::Trash:
        SF(); L(3, 6, 21, 6);
        SF(); L(8, 6, 8, 3); L(8, 3, 16, 3); L(16, 3, 16, 6);
        SF(); L(5, 6, 6, 21); L(6, 21, 18, 21); L(18, 21, 19, 6);
        break;
    }

    Pen pen(c, 1.8f * s * stroke);
    pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    pen.SetLineJoin(LineJoinRound);
    g.DrawPath(&pen, &p);
}

// ---------------------------------------------------------------- 版面缩略图
static int MaxI(int a, int b) { return a > b ? a : b; }

void PaperThumb(Graphics& g, const RectI& box, const std::wstring& masthead, int pageNo,
                const std::wstring& section, const std::wstring& lead,
                const std::wstring& dateText, bool front) {
    if (Empty(box)) return;
    FillRect(g, box, GC(th::PaperBg));

    const int w = box.w;
    const int h = box.h;
    const int padX = MaxI(3, (int)(w * 0.052f));
    const int padT = MaxI(3, (int)(w * 0.042f));
    const int innerW = w - padX * 2;
    const int x0 = box.x + padX;
    int       y = box.y + padT;
    const int tiny = MaxI(4, (int)(w * 0.034f));
    const bool compact = (w < 92);

    // 顶行：日期 / 版号（小尺寸下过密，只保留细分隔线）
    if (compact) {
        HLine(g, x0, y, innerW, GC(th::PaperRule), 1);
        y += MaxI(2, (int)(w * 0.05f));
    } else {
        wchar_t nb[16];
        swprintf(nb, 16, L"第%02d版", pageNo);
        Text1(g, dateText, RectI{x0, y, (int)(innerW * 0.58f), tiny + 2}, tiny, GC(th::TextSub), 0);
        Text1(g, nb, RectI{x0 + (int)(innerW * 0.58f), y, (int)(innerW * 0.42f), tiny + 2}, tiny,
              GC(th::TextSub), 2);
        y += tiny + MaxI(2, (int)(w * 0.024f));
        HLine(g, x0, y, innerW, GC(th::PaperRule), 1);
        y += MaxI(2, (int)(w * 0.026f));
    }

    // 报头
    if (compact) {
        // 小尺寸下只保留居中报头，版名由行内文字承担，避免挤压
        int mp = MaxI(7, (int)(w * 0.16f));
        Text1(g, masthead, RectI{x0, y, innerW, (int)(mp * 1.25f)}, mp, GC(th::BrandRed), 1,
              false, true);
        y += (int)(mp * 1.3f);
    } else if (front) {
        int mp = MaxI(9, (int)(w * 0.185f));
        Text1(g, masthead, RectI{x0, y, innerW, (int)(mp * 1.2f)}, mp, GC(th::BrandRed), 1, true,
              true);
        y += (int)(mp * 1.28f);
    } else {
        int hp = MaxI(6, (int)(w * 0.080f));
        Text1(g, masthead, RectI{x0, y, (int)(innerW * 0.46f), hp + 4}, hp, GC(th::BrandRed), 0,
              false, true);
        if (!section.empty())
            Text1(g, section, RectI{x0 + (int)(innerW * 0.40f), y, (int)(innerW * 0.60f), hp + 4},
                  hp, GC(th::TextMain), 2, true, true);
        y += hp + MaxI(3, (int)(w * 0.034f));
    }

    // 版头双线
    HLine(g, x0, y, innerW, GC(th::Accent), 2);
    HLine(g, x0, y + 2, innerW, GC(th::Accent), 1);
    y += MaxI(3, (int)(w * 0.040f)) + 2;

    const int bottomLimit = box.y + h - MaxI(6, (int)(w * 0.085f));  // 留出页码区

    // 头条：真实标题，最多两行（紧凑模式下改为两行标题条）
    int leadPx = MaxI(6, (int)(w * (front ? 0.080f : 0.068f)));
    if (compact) {
        int barH = MaxI(1, (int)(w * 0.030f));
        int bw1 = (int)(innerW * 0.86f), bw2 = (int)(innerW * 0.62f);
        if (y + barH * 4 < bottomLimit) {
            FillRect(g, RectI{x0 + (innerW - bw1) / 2, y, bw1, barH}, GC(0x3A3733));
            FillRect(g, RectI{x0 + (innerW - bw2) / 2, y + barH * 2, bw2, barH}, GC(0x3A3733));
            y += barH * 4 + MaxI(2, (int)(w * 0.03f));
        }
    } else if (!lead.empty()) {
        int lineH = (int)(leadPx * 1.62f);
        int leadH = lineH * 2 + 2;
        if (y + leadH > bottomLimit) leadH = bottomLimit - y;
        if (leadH >= lineH) {
            TextWrap(g, lead, RectI{x0, y, innerW, leadH}, leadPx, GC(0x292723), 2, 1, true, true);
            y += leadH + MaxI(2, (int)(w * 0.026f));
        }
    }

    // 正文区：三栏 + 图片位
    int bodyH = bottomLimit - y;
    const int lineH = MaxI(1, (int)(w * (compact ? 0.028f : 0.0125f)));
    const int gap = MaxI(1, (int)(w * (compact ? 0.042f : 0.0155f)));
    const int step = lineH + gap;

    if (bodyH > step * 3) {
        int colL = (int)(innerW * 0.21f);
        int photoW = (int)(innerW * 0.50f);
        int colR = innerW - colL - photoW - gap * 2;
        int rows = (int)(bodyH * (compact ? 1.0f : 0.62f)) / step;
        if (rows < 3) rows = 3;

        // 左栏文字线
        for (int i = 0; i < rows; ++i)
            FillRect(g, RectI{x0, y + i * step, colL, lineH}, GC(th::PaperInk));

        // 中间图片位（示意）
        int phH = (int)(rows * step * 0.82f);
        RectI ph{x0 + colL + gap, y, photoW, phH};
        if (!Empty(ph)) {
            int sky = (int)(ph.h * 0.5f);
            FillRect(g, RectI{ph.x, ph.y, ph.w, sky}, GC(0xD5E0DF));
            FillRect(g, RectI{ph.x, ph.y + sky, ph.w, (int)(ph.h * 0.12f)}, GC(0xAEBCAD));
            FillRect(g, RectI{ph.x, ph.y + sky + (int)(ph.h * 0.12f), ph.w,
                              ph.h - sky - (int)(ph.h * 0.12f)},
                     GC(0xC9B99C));
            REAL   px0 = (REAL)ph.x, py0 = (REAL)ph.y;
            REAL   pw0 = (REAL)ph.w, ph0 = (REAL)ph.h;
            PointF peak[] = {PointF(px0, py0 + ph0 * 0.96f),
                             PointF(px0 + pw0 * 0.20f, py0 + ph0 * 0.36f),
                             PointF(px0 + pw0 * 0.38f, py0 + ph0 * 0.78f),
                             PointF(px0 + pw0 * 0.58f, py0 + ph0 * 0.28f),
                             PointF(px0 + pw0 * 0.78f, py0 + ph0 * 0.72f),
                             PointF(px0 + pw0, py0 + ph0 * 0.42f),
                             PointF(px0 + pw0, py0 + ph0),
                             PointF(px0, py0 + ph0)};
            GraphicsPath m;
            m.AddPolygon(peak, 8);
            SolidBrush mb(GC(0x6F8173));
            g.FillPath(&mb, &m);
            SolidBrush sb(GC(0xD8B178));
            g.FillEllipse(&sb, px0 + pw0 * 0.68f, py0 + ph0 * 0.09f, pw0 * 0.13f, pw0 * 0.13f);
            // 图片说明线
            int cy = ph.Bottom() + gap;
            for (int i = 0; i < 3 && cy + lineH < y + bodyH; ++i) {
                FillRect(g, RectI{ph.x, cy, ph.w, lineH}, GC(th::PaperInk));
                cy += step;
            }
        }

        // 右栏文字线
        int rx = x0 + colL + photoW + gap * 2;
        for (int i = 0; i < rows; ++i)
            FillRect(g, RectI{rx, y + i * step, colR, lineH}, GC(th::PaperInk));

        y += rows * step + MaxI(3, (int)(w * 0.026f));
    }

    // 底部两条小稿
    if (!compact && y + step * 4 < bottomLimit) {
        HLine(g, x0, y, innerW, GC(0x9F9990), 1);
        y += MaxI(2, (int)(w * 0.022f));
        int halfW = (innerW - gap) / 2;
        int rowsLeft = (bottomLimit - y) / step;
        for (int c = 0; c < 2; ++c) {
            int cx = x0 + c * (halfW + gap);
            int cy = y;
            FillRect(g, RectI{cx, cy, (int)(halfW * 0.78f), lineH + 1}, GC(0x504C46));
            cy += lineH + gap + 1;
            for (int i = 0; i < rowsLeft - 1; ++i) {
                if (cy + lineH > bottomLimit) break;
                FillRect(g, RectI{cx, cy, halfW, lineH}, GC(th::PaperInk));
                cy += step;
            }
        }
    }

    // 页码
    Text1(g, std::to_wstring(pageNo), RectI{box.x + w - padX - (int)(w * 0.12f),
                                            box.y + h - MaxI(8, (int)(w * 0.075f)),
                                            (int)(w * 0.12f), MaxI(7, (int)(w * 0.06f))},
          MaxI(5, (int)(w * 0.038f)), GC(th::TextSub), 2, false, true);
}

int GetEncoderClsid(const wchar_t* mime, CLSID* clsid) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    ImageCodecInfo* info = (ImageCodecInfo*)malloc(size);
    if (!info) return -1;
    GetImageEncoders(num, size, info);
    int found = -1;
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(info[i].MimeType, mime) == 0) {
            *clsid = info[i].Clsid;
            found  = (int)i;
            break;
        }
    }
    free(info);
    return found;
}
