// ui_paint.cpp —— 整帧渲染：严格对照设计稿的配色、字号与间距
#include "job.h"
#include "ui.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace Gdiplus;

// ---------------------------------------------------------------- 小工具
static void AddHit(App& a, RectI r, int id) {
    if (!Empty(r)) a.hits.push_back(Hotspot{r, id});
}

static bool Hov(const App& a, int id) { return a.hover == id && id != HIT_NONE; }

static int WeekdayOf(const Date& d) {  // 0 = 周日
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int              y = d.y;
    if (d.m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[d.m - 1] + d.d) % 7;
}

static const wchar_t* kWeekCn[] = {L"星期日", L"星期一", L"星期二", L"星期三",
                                   L"星期四", L"星期五", L"星期六"};

// 从成品文件名反推报纸名："人民日报_2026-11-05_01-03版.pdf" -> "人民日报"
static std::wstring PaperNameFromPath(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    std::wstring f = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    size_t us = f.find(L'_');
    if (us == std::wstring::npos || us == 0) return L"报纸";
    return f.substr(0, us);
}

// "05版：评论" -> "评论"
static std::wstring SectionOf(const std::wstring& name) {    size_t p = name.find(L"版：");
    if (p != std::wstring::npos) return name.substr(p + 2);
    p = name.find(L"版:");
    if (p != std::wstring::npos) return name.substr(p + 2);
    // 纯数字版名（如 "05版"）不显示为版名
    if (!name.empty() && name.size() <= 4 && name.back() == L'版') return L"";
    return name;
}

static std::wstring DateCn(const Date& d) {
    wchar_t buf[64];
    swprintf(buf, 64, L"%d年%d月%d日", d.y, d.m, d.d);
    return buf;
}

static std::wstring Num2(int v) {
    wchar_t buf[8];
    swprintf(buf, 8, L"%02d", v);
    return buf;
}

// ---------------------------------------------------------------- 标题栏
void DrawTitlebar(Graphics& g, App& app, int W) {
    int h = app.S(th::TitleH);
    FillRect(g, RectI{0, 0, W, h}, GC(th::TitleBg));

    // 品牌图标
    int mark = app.S(28);
    RectI mk{app.S(16), (h - mark) / 2, mark, mark};
    FillRound(g, mk, app.S(7), GC(th::BrandRed));
    Icon(g, Ico::Newspaper, mk, GC(0xFFFFFF), 1.5f);

    // 品牌文字
    int tx = mk.Right() + app.S(10);
    int brandPx = app.S(15);
    int by = (h - app.S(18)) / 2;
    Tracked(g, L"见报", tx, by, brandPx, GC(th::TitleText), 0.12f, true, true);
    int after = tx + TrackedW(L"见报", brandPx, 0.12f, true, true) + app.S(3);
    Text1(g, L"电子报纸下载器", RectI{after, by, app.S(140), app.S(18)}, app.S(11),
          GC(th::TitleMuted), 0);

    // 右侧窗口按钮
    int closeW = app.S(46), minW = app.S(46), setW = app.S(40);
    int cx = W - closeW;
    RectI rc{cx, 0, closeW, h};
    if (Hov(app, HIT_TITLE_CLOSE)) FillRect(g, rc, GC(th::BrandRed));
    Icon(g, Ico::Close, RectI{cx + (closeW - app.S(15)) / 2, (h - app.S(15)) / 2, app.S(15),
                              app.S(15)},
         GC(Hov(app, HIT_TITLE_CLOSE) ? 0xFFFFFF : 0xAAAAAA), 1.5f);
    AddHit(app, rc, HIT_TITLE_CLOSE);

    int mx = cx - minW;
    RectI rm{mx, 0, minW, h};
    if (Hov(app, HIT_TITLE_MIN)) FillRect(g, rm, GC(th::TitleHover));
    Icon(g, Ico::Minus, RectI{mx + (minW - app.S(15)) / 2, (h - app.S(15)) / 2, app.S(15),
                              app.S(15)},
         GC(Hov(app, HIT_TITLE_MIN) ? 0xFFFFFF : 0xAAAAAA), 1.5f);
    AddHit(app, rm, HIT_TITLE_MIN);

    int sx = mx - setW;
    RectI rs{sx, 0, setW, h};
    if (Hov(app, HIT_TITLE_SETTINGS)) FillRect(g, rs, GC(th::TitleHover));
    Icon(g, Ico::Settings, RectI{sx + (setW - app.S(16)) / 2, (h - app.S(16)) / 2, app.S(16),
                                 app.S(16)},
         GC(Hov(app, HIT_TITLE_SETTINGS) ? 0xFFFFFF : 0xAAAAAA), 1.5f);
    AddHit(app, rs, HIT_TITLE_SETTINGS);
}

// ---------------------------------------------------------------- 侧栏
void DrawSidebar(Graphics& g, App& app, int W, int H) {
    int   w = app.S(th::SideW);
    int   top = app.S(th::TitleH);
    RectI bar{0, top, w, H - top};
    FillRect(g, bar, GC(th::SideBg));
    VLine(g, w - 1, top, H - top, GC(th::SideLine));

    int px = app.S(13), py = top + app.S(25);
    int innerW = w - px * 2;

    Tracked(g, L"工作台", px + app.S(12), py, app.S(10), GC(th::TextMuted), 0.13f, true);
    py += app.S(22);

    struct NavItem {
        int          id;
        Ico          ico;
        const wchar_t* text;
        int          badge;
    };
    NavItem items[] = {
        {HIT_NAV_DOWNLOAD, Ico::Download, L"报纸下载", -1},
        {HIT_NAV_HISTORY, Ico::History, L"下载记录", (int)app.history.size()},
    };
    for (auto& it : items) {
        bool  active = (app.nav == (it.id == HIT_NAV_DOWNLOAD ? 0 : 1));
        RectI r{px, py, innerW, app.S(42)};
        if (active) {
            FillRound(g, r, app.S(7), GC(th::SideActive));
            FillRound(g, RectI{r.x, r.y, app.S(3), r.h}, app.S(2), GC(th::Accent));
        } else if (Hov(app, it.id)) {
            FillRound(g, r, app.S(7), GC(th::SideHover));
        }
        Icon(g, it.ico, RectI{r.x + app.S(12), r.y + (r.h - app.S(18)) / 2, app.S(18), app.S(18)},
             GC(active ? th::Accent : th::TextBody));
        Text1(g, it.text, RectI{r.x + app.S(41), r.y, r.w - app.S(80), r.h}, app.S(13),
              GC(active ? th::Accent : th::TextBody), 0, active);
        if (it.badge > 0) {
            std::wstring b = it.badge > 99 ? L"99+" : std::to_wstring(it.badge);
            int          bw = TextW(b, app.S(10)) + app.S(12);
            RectI        br{r.Right() - app.S(12) - bw, r.y + (r.h - app.S(16)) / 2, bw, app.S(16)};
            FillRound(g, br, app.S(8), GC(th::SideBadge));
            Text1(g, b, br, app.S(10), GC(th::TextBody), 1);
        }
        AddHit(app, r, it.id);
        py += app.S(42) + app.S(2);
    }

    py += app.S(26);
    Tracked(g, L"报纸来源", px + app.S(12), py, app.S(10), GC(th::TextMuted), 0.13f, true);
    py += app.S(22);

    // 已接入的报纸来源（全部为公开、免登录、按日提供 PDF 的数字报）
    const auto& sources = AllSources();
    for (size_t i = 0; i < sources.size(); ++i) {
        const SourceInfo& s      = sources[i];
        bool              active = (app.sourceId == s.id);
        RectI             r{px, py, innerW, app.S(44)};
        if (active) {
            FillRound(g, r, app.S(7), GC(th::SideActive));
            FillRound(g, RectI{r.x, r.y + app.S(9), app.S(3), r.h - app.S(18)}, app.S(2),
                      GC(th::Accent));
        } else if (Hov(app, HIT_SOURCE + (int)i)) {
            FillRound(g, r, app.S(7), GC(th::SideHover));
        }
        RectI box{r.x + app.S(12), r.y + (r.h - app.S(24)) / 2, app.S(24), app.S(24)};
        FillRound(g, box, app.S(5), GC(active ? th::Accent : th::SideBadge));
        Text1(g, s.name.substr(0, 1), box, app.S(12), GC(active ? 0xFFFFFF : 0x6E6C67), 1, true);
        Text1(g, s.name, RectI{box.Right() + app.S(10), r.y, innerW - app.S(74), r.h}, app.S(12),
              GC(active ? th::TextMain : th::TextBody), 0, active);
        if (active)
            Text1(g, L"当前", RectI{r.Right() - app.S(40), r.y, app.S(28), r.h}, app.S(9),
                  GC(th::Accent), 2);
        AddHit(app, r, HIT_SOURCE + (int)i);
        py += r.h + app.S(2);
    }

    // 底部
    int  bottomPad = app.S(14);
    int  vy = H - bottomPad - app.S(14);
    RectI vrow{px + app.S(10), vy, innerW - app.S(20), app.S(14)};
    SolidBrush dot(GC(th::OkDotRing));
    g.FillEllipse(&dot, (REAL)(vrow.x - app.S(0)), (REAL)(vrow.y + app.S(4)), (REAL)app.S(10),
                  (REAL)app.S(10));
    SolidBrush dot2(GC(th::OkDot));
    g.FillEllipse(&dot2, (REAL)(vrow.x + app.S(2)), (REAL)(vrow.y + app.S(6)), (REAL)app.S(6),
                  (REAL)app.S(6));
    Text1(g, L"服务运行正常", RectI{vrow.x + app.S(16), vrow.y, vrow.w, vrow.h}, app.S(9),
          GC(0x99968F), 0);
    Text1(g, L"v1.1.1", vrow, app.S(9), GC(0x99968F), 2);

    int by = vy - app.S(10);
    struct BotItem {
        int            id;
        Ico            ico;
        const wchar_t* text;
    };
    BotItem bots[] = {{HIT_NAV_SETTINGS, Ico::Settings, L"偏好设置"},
                      {HIT_NAV_ABOUT, Ico::Help, L"帮助与关于"}};
    by -= app.S(36) * 2;
    HLine(g, px, by - app.S(10), innerW, GC(0xDCD8D1));
    for (auto& b : bots) {
        RectI r{px, by, innerW, app.S(36)};
        if (Hov(app, b.id)) FillRound(g, r, app.S(7), GC(th::SideHover));
        Icon(g, b.ico, RectI{r.x + app.S(12), r.y + (r.h - app.S(17)) / 2, app.S(17), app.S(17)},
             GC(th::TextBody));
        Text1(g, b.text, RectI{r.x + app.S(41), r.y, r.w - app.S(50), r.h}, app.S(13),
              GC(th::TextBody), 0);
        AddHit(app, r, b.id);
        by += app.S(36);
    }
}

// ---------------------------------------------------------------- 版面卡
static void DrawCard(Graphics& g, App& app, const PageInfo& p, RectI card, bool selected,
                     bool hovered) {
    int pad = app.S(9);
    int coverH = card.h - app.S(40);
    RectI cover{card.x, card.y, card.w, coverH};
    if (hovered && !selected) {
        cover.y -= app.S(2);
        SoftShadow(g, cover, app.S(3), app.S(4), 26);
    }
    FillRect(g, cover, GC(th::CoverBg));

    RectI paper{cover.x + 1 + pad, cover.y + 1 + pad, cover.w - 2 - pad * 2,
                cover.h - 2 - pad * 2};
    g.SetClip(paper.F());
    PaperThumb(g, paper, app.Source().name, p.number, SectionOf(p.name), p.lead,
               DateCn(app.date), p.number == 1);
    g.ResetClip();

    if (selected) {
        StrokeRound(g, cover, app.S(3), GC(th::Accent), (float)app.S(2));
        int cm = app.S(21);
        RectI ck{cover.Right() - cm + app.S(7), cover.y - app.S(7), cm, cm};
        SolidBrush ring(GC(th::ContentBg));
        g.FillEllipse(&ring, (REAL)ck.x - 2, (REAL)ck.y - 2, (REAL)ck.w + 4, (REAL)ck.h + 4);
        FillRound(g, ck, cm / 2, GC(th::Accent));
        Icon(g, Ico::Check, ck, GC(0xFFFFFF), 1.5f);
    } else {
        StrokeRound(g, cover, app.S(3), GC(hovered ? th::CardHover : th::CardBorder), 1.0f);
        if (hovered) {
            FillRect(g, RectI{cover.x + 1, cover.Bottom() - app.S(29), cover.w - 2, app.S(28)},
                     GC(0x262522, 217));
            Text1(g, L"点击选择", RectI{cover.x, cover.Bottom() - app.S(29), cover.w, app.S(28)},
                  app.S(10), GC(0xFFFFFF), 1);
        }
    }

    // 版号 + 版名
    int iy = cover.Bottom() + app.S(10);
    std::wstring no = L"第 " + Num2(p.number) + L" 版";
    int          nw = TextW(no, app.S(12), true, true);
    Text1(g, no, RectI{card.x + app.S(2), iy, nw + 4, app.S(18)}, app.S(12), GC(th::TextMain), 0,
          true, true);
    std::wstring sz = p.bytes > 0 ? (L"PDF · " + HumanSize(p.bytes)) : std::wstring(L"PDF");
    int          szX = card.w - TextW(sz, app.S(8)) - app.S(2);

    std::wstring sec = SectionOf(p.name);
    if (!sec.empty()) {
        int secX = card.x + app.S(2) + nw + app.S(7);
        int secW = card.x + szX - app.S(6) - secX;
        if (secW < app.S(16)) secW = app.S(16);
        Text1(g, sec, RectI{secX, iy, secW, app.S(18)}, app.S(10), GC(0x8F8C85), 0, false, false,
              true);
    }
    Text1(g, sz, RectI{card.x, iy + app.S(2), card.w - app.S(2), app.S(14)}, app.S(8),
          GC(th::TextFaint), 2);

    AddHit(app, RectI{card.x, card.y, card.w, card.h}, HIT_CARD + p.number);
}

// ---------------------------------------------------------------- 下载页
void DrawDownloadPage(Graphics& g, App& app, int W, int H) {
    int sideW = app.S(th::SideW);
    int taskW = app.S(th::TaskW);
    int top = app.S(th::TitleH);
    RectI area{sideW, top, W - sideW - taskW, H - top};
    FillRect(g, area, GC(th::ContentBg));

    int padX = app.S(th::ContentPadX), padT = app.S(th::ContentPadT);
    int ix = area.x + padX, iy = area.y + padT;
    int iw = area.w - padX * 2;

    // ---- 页头
    const SourceInfo& src = app.Source();
    Tracked(g, src.eyebrow.c_str(), ix, iy, app.S(9), GC(th::Accent), 0.18f, true);
    Text1(g, src.name, RectI{ix, iy + app.S(16), iw, app.S(36)}, app.S(27), GC(th::TextMain), 0,
          true, true);
    Text1(g, L"选择日期与版面，获取高清电子版",
          RectI{ix, iy + app.S(56), iw, app.S(18)}, app.S(12), GC(th::TextSub), 0);

    // ---- 日期步进器
    int stepW = app.S(38) * 2 + app.S(188);
    RectI step{ix + iw - stepW, iy + app.S(16), stepW, app.S(43)};
    SoftShadow(g, step, app.S(7), app.S(3), 20);
    FillRound(g, step, app.S(7), GC(0xFFFFFF));
    StrokeRound(g, step, app.S(7), GC(0xDEDBD5), 1.0f);

    RectI prev{step.x, step.y, app.S(38), step.h};
    if (Hov(app, HIT_DATE_PREV)) FillRound(g, prev, app.S(6), GC(0xF2F0EC));
    Icon(g, Ico::ChevLeft, RectI{prev.x + (prev.w - app.S(16)) / 2, prev.y + (prev.h - app.S(16)) / 2,
                                 app.S(16), app.S(16)},
         GC(0x817E77));
    AddHit(app, prev, HIT_DATE_PREV);

    bool canNext = CompareDate(app.date, Today()) < 0;
    RectI next{step.Right() - app.S(38), step.y, app.S(38), step.h};
    if (canNext && Hov(app, HIT_DATE_NEXT)) FillRound(g, next, app.S(6), GC(0xF2F0EC));
    Icon(g, Ico::ChevRight,
         RectI{next.x + (next.w - app.S(16)) / 2, next.y + (next.h - app.S(16)) / 2, app.S(16),
               app.S(16)},
         GC(canNext ? 0x817E77 : 0xD1CEC8));
    if (canNext) AddHit(app, next, HIT_DATE_NEXT);

    RectI center{prev.Right(), step.y, step.w - app.S(76), step.h};
    VLine(g, center.x, center.y + app.S(9), center.h - app.S(18), GC(0xE9E6E0));
    VLine(g, center.Right() - 1, center.y + app.S(9), center.h - app.S(18), GC(0xE9E6E0));
    if (Hov(app, HIT_DATE_OPEN)) FillRound(g, center, app.S(4), GC(0xF7F5F1));
    std::wstring dtext = app.dateReady ? DateCn(app.date) : L"正在获取…";
    int          dw = TextW(dtext, app.S(13), true, true);
    int          wdw = app.dateReady ? TextW(kWeekCn[WeekdayOf(app.date)], app.S(9)) : 0;
    int          groupW = app.S(17) + app.S(8) + dw + (wdw ? app.S(8) + wdw : 0);
    int          gx = center.x + (center.w - groupW) / 2;
    Icon(g, Ico::Calendar, RectI{gx, center.y + (center.h - app.S(17)) / 2, app.S(17), app.S(17)},
         GC(0x403F3B));
    Text1(g, dtext, RectI{gx + app.S(25), center.y, dw + 2, center.h}, app.S(13), GC(0x403F3B), 0,
          true, true);
    if (app.dateReady)
        Text1(g, kWeekCn[WeekdayOf(app.date)],
              RectI{gx + app.S(25) + dw + app.S(8) - 2, center.y, wdw + 4, center.h}, app.S(9),
              GC(th::TextFaint), 0);
    AddHit(app, center, HIT_DATE_OPEN);

    // ---- 过滤（工具栏与卡片共用同一结果）
    std::vector<const PageInfo*> shown;
    shown.reserve(app.pages.size());
    for (auto& p : app.pages) {
        if (app.query.empty()) {
            shown.push_back(&p);
            continue;
        }
        std::wstring sec = SectionOf(p.name);
        std::wstring no  = Num2(p.number);
        if (sec.find(app.query) != std::wstring::npos ||
            p.name.find(app.query) != std::wstring::npos ||
            no.find(app.query) != std::wstring::npos ||
            p.lead.find(app.query) != std::wstring::npos)
            shown.push_back(&p);
    }
    app.visible.clear();
    for (auto* p : shown) app.visible.push_back(p->number);

    // ---- 工具栏
    int tbY = iy + app.S(78) + app.S(26);
    HLine(g, area.x, tbY, area.w, GC(th::Line));
    HLine(g, area.x, tbY + app.S(42), area.w, GC(th::Line));

    RectI tb{ix, tbY, iw, app.S(42)};
    // 全选
    bool allSel = !app.visible.empty();
    for (int no : app.visible)
        if (!app.IsSelected(no)) { allSel = false; break; }
    RectI cbox{tb.x, tb.y + (tb.h - app.S(14)) / 2, app.S(14), app.S(14)};
    FillRound(g, cbox, app.S(3), GC(allSel ? th::Accent : 0xFFFFFF));
    StrokeRound(g, cbox, app.S(3), GC(allSel ? th::Accent : 0xCFCBC3), 1.0f);
    if (allSel) Icon(g, Ico::Check, cbox, GC(0xFFFFFF), 1.3f);
    int tx = cbox.Right() + app.S(8);
    Text1(g, L"全选", RectI{tx, tb.y, app.S(40), tb.h}, app.S(12), GC(th::TextMain), 0, true);
    std::wstring countText = L"共 " + std::to_wstring(app.pages.size()) + L" 个版面";
    if (!app.query.empty())
        countText += L" · 匹配 " + std::to_wstring(app.visible.size());
    Text1(g, countText, RectI{tx + app.S(40), tb.y, app.S(190), tb.h}, app.S(12),
          GC(th::TextFaint), 0);
    AddHit(app, RectI{cbox.x - app.S(6), tb.y, app.S(150), tb.h}, HIT_SELECT_ALL);

    // 搜索框 + 视图切换
    int vsW = app.S(60);
    RectI vs{tb.Right() - vsW, tb.y + (tb.h - app.S(30)) / 2, vsW, app.S(30)};
    FillRound(g, vs, app.S(5), GC(0xEBE8E2));
    RectI vg{vs.x + app.S(2), vs.y + app.S(2), app.S(28), app.S(26)};
    RectI vl{vs.x + app.S(30), vs.y + app.S(2), app.S(28), app.S(26)};
    if (app.view == 0) {
        FillRound(g, vg, app.S(4), GC(0xFFFFFF));
    }
    Icon(g, Ico::Grid, RectI{vg.x + app.S(6), vg.y + app.S(5), app.S(16), app.S(16)},
         GC(app.view == 0 ? 0x4D4B46 : 0x9C9992));
    Icon(g, Ico::List, RectI{vl.x + app.S(5), vl.y + app.S(5), app.S(17), app.S(17)},
         GC(app.view == 1 ? 0x4D4B46 : 0x9C9992));
    AddHit(app, vg, HIT_VIEW_GRID);
    AddHit(app, vl, HIT_VIEW_LIST);

    RectI sb{vs.x - app.S(10) - app.S(150), tb.y + (tb.h - app.S(30)) / 2, app.S(150), app.S(30)};
    FillRound(g, sb, app.S(5), GC(0xFFFFFF));
    StrokeRound(g, sb, app.S(5), GC(0xE0DDD6), 1.0f);
    Icon(g, Ico::Search, RectI{sb.x + app.S(9), sb.y + (sb.h - app.S(16)) / 2, app.S(16), app.S(16)},
         GC(th::TextFaint));
    app.searchEditRect =
        RectI{sb.x + app.S(31), sb.y + app.S(4), sb.w - app.S(39), sb.h - app.S(8)};
    AddHit(app, sb, HIT_SEARCH_BOX);

    // ---- 版面区
    int gridTop = tbY + app.S(42) + app.S(20);
    RectI view{ix, gridTop, iw, area.Bottom() - app.S(40) - gridTop};
    if (view.h < app.S(60)) view.h = app.S(60);

    if (app.pages.empty()) {
        int cy = view.y + view.h / 2 - app.S(56);
        if (app.loading) {
            int   d = app.S(26);
            RectI box{view.x + view.w / 2 - d / 2, cy, d, d};
            float startA = (float)((GetTickCount64() / 3) % 360);
            GraphicsPath arc;
            arc.AddArc((REAL)box.x, (REAL)box.y, (REAL)box.w, (REAL)box.h, startA, 260.0f);
            Pen pen(GC(th::Accent), (float)app.S(2));
            pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
            g.DrawPath(&pen, &arc);
            Text1(g, L"正在获取该日期的版面信息…",
                  RectI{view.x, cy + app.S(40), view.w, app.S(20)}, app.S(12), GC(th::TextSub), 1);
        } else {
            Icon(g, Ico::Info,
                 RectI{view.x + view.w / 2 - app.S(14), cy, app.S(28), app.S(28)}, GC(0xC7C2B9),
                 1.4f);
            std::wstring msg = app.pagesError.empty() ? L"请选择要下载的日期" : app.pagesError;
            TextWrap(g, msg, RectI{view.x, cy + app.S(42), view.w, app.S(46)}, app.S(12),
                     GC(th::TextSub), 2, 1);
            if (!app.latestIso.empty()) {
                wchar_t buf[96];
                swprintf(buf, 96, L"最近一期：%s", app.latestIso.c_str());
                Text1(g, buf, RectI{view.x, cy + app.S(94), view.w, app.S(16)}, app.S(10),
                      GC(th::TextFaint), 1);
                int   bw = app.S(136);
                RectI btn{view.x + (view.w - bw) / 2, cy + app.S(118), bw, app.S(32)};
                FillRound(g, btn, app.S(6),
                          GC(Hov(app, HIT_LATEST_JUMP) ? th::AccentHover : th::Accent));
                Text1(g, L"跳转到最近一期", btn, app.S(11), GC(0xFFFFFF), 1, true);
                AddHit(app, btn, HIT_LATEST_JUMP);
            }
        }
    } else if (shown.empty()) {
        int cy = view.y + view.h / 2 - app.S(40);
        Icon(g, Ico::Search, RectI{view.x + view.w / 2 - app.S(13), cy, app.S(26), app.S(26)},
             GC(0xC7C2B9), 1.4f);
        Text1(g, L"没有匹配的版面", RectI{view.x, cy + app.S(38), view.w, app.S(20)}, app.S(12),
              GC(th::TextSub), 1);
        Text1(g, L"试试其他关键词，或清空搜索", RectI{view.x, cy + app.S(60), view.w, app.S(16)},
              app.S(10), GC(th::TextFaint), 1);
    } else if (app.view == 0) {
        int cols = (W < app.S(1240)) ? 3 : 4;
        int gapX = app.S(18), gapY = app.S(25);
        int cardW = (view.w - gapX * (cols - 1)) / cols;
        int coverH = (int)((cardW - 2 - app.S(18)) / 0.72f) + app.S(18) + 2;
        int cardH = coverH + app.S(40);
        int rows = ((int)shown.size() + cols - 1) / cols;
        int total = rows * cardH + (rows - 1) * gapY;
        app.contentMax = total > view.h ? total - view.h : 0;
        if (app.contentScroll > app.contentMax) app.contentScroll = app.contentMax;
        if (app.contentScroll < 0) app.contentScroll = 0;

        g.SetClip(view.F());
        for (size_t i = 0; i < shown.size(); ++i) {
            int row = (int)i / cols, col = (int)i % cols;
            RectI card{view.x + col * (cardW + gapX),
                       view.y + row * (cardH + gapY) - app.contentScroll, cardW, cardH};
            if (card.Bottom() < view.y - app.S(10) || card.y > view.Bottom() + app.S(10))
                continue;
            bool sel = app.IsSelected(shown[i]->number);
            DrawCard(g, app, *shown[i], card, sel, Hov(app, HIT_CARD + shown[i]->number));
        }
        g.ResetClip();

        if (app.contentMax > 0) {
            int trackH = view.h;
            int thumbH = trackH * view.h / total;
            if (thumbH < app.S(28)) thumbH = app.S(28);
            int ty = view.y + (trackH - thumbH) * app.contentScroll / app.contentMax;
            FillRound(g, RectI{area.Right() - app.S(6), ty, app.S(3), thumbH}, app.S(2),
                      GC(0xC9C4BB));
        }
    } else {
        int rowH = app.S(100), gapY = app.S(8);
        int total = (int)shown.size() * (rowH + gapY);
        app.contentMax = total > view.h ? total - view.h : 0;
        if (app.contentScroll > app.contentMax) app.contentScroll = app.contentMax;
        if (app.contentScroll < 0) app.contentScroll = 0;

        g.SetClip(view.F());
        for (size_t i = 0; i < shown.size(); ++i) {
            const PageInfo& p = *shown[i];
            RectI r{view.x, view.y + (int)i * (rowH + gapY) - app.contentScroll, view.w, rowH};
            if (r.Bottom() < view.y || r.y > view.Bottom()) continue;
            bool sel = app.IsSelected(p.number);
            bool hv = Hov(app, HIT_CARD + p.number);
            FillRound(g, r, app.S(6), GC(0xFFFFFF));
            StrokeRound(g, r, app.S(6), GC(sel ? th::Accent : th::Line), sel ? 2.0f : 1.0f);
            (void)hv;
            RectI cov{r.x + app.S(8), r.y + app.S(8), app.S(62), app.S(84)};
            RectI pp{cov.x + app.S(3), cov.y + app.S(3), cov.w - app.S(6), cov.h - app.S(6)};
            FillRect(g, cov, GC(th::CoverBg));
            g.SetClip(pp.F());
            PaperThumb(g, pp, app.Source().name, p.number, SectionOf(p.name), p.lead,
                       DateCn(app.date), p.number == 1);
            g.ResetClip();
            g.SetClip(view.F());
            std::wstring no = L"第 " + Num2(p.number) + L" 版";
            std::wstring sec = SectionOf(p.name);
            Text1(g, no + (sec.empty() ? L"" : L" · " + sec),
                  RectI{cov.Right() + app.S(15), r.y + app.S(22), view.w - app.S(180), app.S(20)},
                  app.S(12), GC(th::TextMain), 0, true, true);
            if (!p.lead.empty())
                Text1(g, p.lead,
                      RectI{cov.Right() + app.S(15), r.y + app.S(46), view.w - app.S(180),
                            app.S(18)},
                      app.S(10), GC(th::TextFaint), 0);
            Text1(g, p.bytes > 0 ? (L"PDF · " + HumanSize(p.bytes)) : std::wstring(L"PDF"),
                  RectI{r.Right() - app.S(120), r.y, app.S(100), r.h}, app.S(9), GC(th::TextFaint),
                  2);
            AddHit(app, r, HIT_CARD + p.number);
        }
        g.ResetClip();
    }
}

// ---------------------------------------------------------------- 任务面板
void DrawTaskPanel(Graphics& g, App& app, int W, int H) {
    int   top = app.S(th::TitleH);
    int   taskW = app.S(th::TaskW);
    RectI panel{W - taskW, top, taskW, H - top};
    FillRect(g, panel, GC(th::PanelBg));
    VLine(g, panel.x, top, panel.h, GC(th::SideLine));

    int padX = app.S(th::TaskPadX), padT = app.S(th::TaskPadT);
    int ix = panel.x + padX, iy = top + padT;
    int iw = taskW - padX * 2;
    int padB = app.S(20);

    Tracked(g, L"DOWNLOAD TASK", ix, iy, app.S(9), GC(th::Accent), 0.18f, true);
    Text1(g, L"下载任务", RectI{ix, iy + app.S(15), iw - app.S(34), app.S(28)}, app.S(19),
          GC(th::TextMain), 0, true, true);
    // 计数徽标
    RectI badge{ix + iw - app.S(25), iy + app.S(18), app.S(25), app.S(25)};
    FillRound(g, badge, app.S(13), GC(th::Accent));
    Text1(g, std::to_wstring(app.selected.size()), badge, app.S(11), GC(0xFFFFFF), 1, true);

    // 当前日期
    RectI dc{ix, iy + app.S(62), iw, app.S(51)};
    FillRound(g, dc, app.S(6), GC(0xE5E1DA));
    RectI ib{dc.x + app.S(12), dc.y + app.S(11), app.S(29), app.S(29)};
    FillRound(g, ib, app.S(5), GC(0xF5F2ED));
    Icon(g, Ico::Calendar, RectI{ib.x + app.S(6), ib.y + app.S(6), app.S(17), app.S(17)},
         GC(th::Accent));
    Text1(g, L"当前日期", RectI{ib.Right() + app.S(11), dc.y + app.S(9), iw, app.S(12)}, app.S(9),
          GC(0x99958E), 0);
    wchar_t dbuf[64];
    swprintf(dbuf, 64, L"%04d / %02d / %02d", app.date.y, app.date.m, app.date.d);
    Text1(g, dbuf, RectI{ib.Right() + app.S(11), dc.y + app.S(21), iw, app.S(18)}, app.S(12),
          GC(th::TextMain), 0, true, true);

    // 已选版面
    int sumY = dc.Bottom() + app.S(20);
    Text1(g, L"已选版面", RectI{ix, sumY, iw / 2, app.S(16)}, app.S(11), GC(th::TextMain), 0,
          true);
    RectI clearBtn{ix + iw - app.S(40), sumY, app.S(40), app.S(16)};
    Text1(g, L"清空", clearBtn, app.S(9),
          GC(Hov(app, HIT_CLEAR_SEL) ? th::Accent : th::TextFaint), 2);
    AddHit(app, clearBtn, HIT_CLEAR_SEL);
    HLine(g, ix, sumY + app.S(22), iw, GC(0xDCD8D1));

    // 底部固定区（自下而上）
    int hintY    = panel.Bottom() - padB - app.S(12);
    int btnY     = hintY - app.S(9) - app.S(42);
    int sizeRowY = btnY - app.S(12) - app.S(16);

    // 列表下方依次为：输出文件名、文件格式/保存位置、（进度）。自下而上预留高度
    const int outBlockH = app.S(16) + app.S(34);
    const int optBlockH = app.S(18) * 2;
    const int progBlockH = app.running ? app.S(52) : 0;
    int reserved = app.S(16) + outBlockH + app.S(16) + optBlockH + progBlockH + app.S(16);

    int listTop      = sumY + app.S(23);
    int availForList = sizeRowY - app.S(14) - reserved - listTop;
    int needH = app.selected.empty() ? app.S(92) : (int)app.selected.size() * app.S(60);
    int listH = needH < app.S(92) ? app.S(92) : needH;
    if (listH > app.S(300)) listH = app.S(300);
    if (listH > availForList) listH = availForList > app.S(60) ? availForList : app.S(60);
    // 选中列表按整行裁切，避免出现半截行
    if (!app.selected.empty()) {
        int unit = app.S(60);
        if (listH >= unit) listH = (listH / unit) * unit;
        else listH = unit;
    }

    int listBottom = listTop + listH;

    RectI listRect{ix, listTop, iw, listH};
    if (app.selected.empty()) {
        Icon(g, Ico::File, RectI{ix + iw / 2 - app.S(11), listTop + app.S(24), app.S(22), app.S(22)},
             GC(0xC2BEB6), 1.4f);
        Text1(g, L"尚未选择版面", RectI{ix, listTop + app.S(52), iw, app.S(16)}, app.S(10),
              GC(th::TextFaint), 1);
    } else {
        int  itemH = app.S(60);
        int  total = (int)app.selected.size() * itemH;
        app.selMax = total > listRect.h ? total - listRect.h : 0;
        if (app.selScroll > app.selMax) app.selScroll = app.selMax;
        if (app.selScroll < 0) app.selScroll = 0;
        g.SetClip(listRect.F());
        std::vector<int> sorted = app.selected;
        std::sort(sorted.begin(), sorted.end());
        for (size_t i = 0; i < sorted.size(); ++i) {
            int          page = sorted[i];
            const PageInfo* info = nullptr;
            for (auto& p : app.pages)
                if (p.number == page) { info = &p; break; }
            RectI r{listRect.x, listRect.y + (int)i * itemH - app.selScroll, listRect.w, itemH};
            if (r.Bottom() < listRect.y || r.y > listRect.Bottom()) continue;

            RectI mini{r.x, r.y + app.S(10), app.S(31), app.S(39)};
            FillRect(g, mini, GC(0xFFFFFF));
            StrokeRect(g, mini, GC(0xD9D4CB), 1.0f);
            Text1(g, L"人民", RectI{mini.x, mini.y + app.S(9), mini.w, app.S(9)}, app.S(7),
                  GC(th::Accent), 1, false, true);
            HLine(g, mini.x + app.S(7), mini.y + app.S(20), mini.w - app.S(14), GC(th::Accent));
            Text1(g, Num2(page), RectI{mini.x, mini.y + app.S(22), mini.w, app.S(12)}, app.S(9),
                  GC(0x77736B), 1);

            std::wstring sec = info ? SectionOf(info->name) : L"";
            Text1(g, L"第 " + Num2(page) + L" 版" + (sec.empty() ? L"" : L" · " + sec),
                  RectI{mini.Right() + app.S(9), r.y + app.S(12), r.w - app.S(60), app.S(15)},
                  app.S(10), GC(th::TextMain), 0, true);
            std::wstring lead = info ? info->lead : L"";
            if (!lead.empty())
                Text1(g, lead,
                      RectI{mini.Right() + app.S(9), r.y + app.S(28), r.w - app.S(60), app.S(14)},
                      app.S(8), GC(0xA09C95), 0);
            RectI del{r.Right() - app.S(26), r.y + app.S(18), app.S(20), app.S(20)};
            if (Hov(app, HIT_SEL_ITEM + page)) {
                FillRound(g, del, app.S(4), GC(0xE0DCD5));
            }
            Icon(g, Ico::Close, del, GC(0xAAA69F), 1.4f);
            AddHit(app, del, HIT_SEL_ITEM + page);
            HLine(g, r.x, r.Bottom(), r.w, GC(0xE0DCD5));
        }
        g.ResetClip();

        // 列表滚动条（选中版面较多时）
        if (app.selMax > 0) {
            int totalH = (int)sorted.size() * itemH;
            int thumbH = listRect.h * listRect.h / totalH;
            if (thumbH < app.S(24)) thumbH = app.S(24);
            int ty = listRect.y + (listRect.h - thumbH) * app.selScroll / app.selMax;
            FillRound(g, RectI{listRect.Right() - app.S(4), ty, app.S(3), thumbH}, app.S(2),
                      GC(0xC9C4BB));
        }
    }

    // 输出文件名预览
    int outY = listBottom + app.S(16);
    Text1(g, app.settings.mergePdf ? L"输出文件" : L"输出文件（逐版保存）",
          RectI{ix, outY, iw, app.S(14)}, app.S(9), GC(0x99958E), 0);
    RectI ob{ix, outY + app.S(16), iw, app.S(34)};
    FillRound(g, ob, app.S(5), GC(0xF7F5F1));
    StrokeRound(g, ob, app.S(5), GC(0xE2DFD9), 1.0f);
    Icon(g, Ico::File, RectI{ob.x + app.S(9), ob.y + app.S(10), app.S(14), app.S(14)},
         GC(0x8E8A82), 1.4f);
    std::wstring outName =
        app.selected.empty()
            ? std::wstring(L"选择版面后显示文件名")
            : BuildOutputName(app.Source().name, app.date, app.selected, app.settings.mergePdf);
    Text1(g, outName, RectI{ob.x + app.S(29), ob.y, ob.w - app.S(36), ob.h}, app.S(9),
          GC(app.selected.empty() ? 0xAAA69F : 0x4B4945), 0);

    int opt1Y = ob.Bottom() + app.S(16);
    int opt2Y = opt1Y + app.S(18);

    // 选项
    Text1(g, L"文件格式", RectI{ix, opt1Y, iw / 2, app.S(16)}, app.S(10), GC(0x918E87), 0);
    Text1(g, app.settings.mergePdf ? L"PDF 合并版" : L"PDF 分版",
          RectI{ix + iw / 2, opt1Y, iw / 2, app.S(16)}, app.S(10), GC(0x4B4945), 2);

    Text1(g, L"保存位置", RectI{ix, opt2Y, iw / 2, app.S(16)}, app.S(10), GC(0x918E87), 0);
    RectI pathBtn{ix + iw / 2, opt2Y - app.S(2), iw / 2, app.S(20)};
    std::wstring root = app.settings.outRoot;
    size_t       slash = root.find_last_of(L"\\/");
    std::wstring leaf = (slash == std::wstring::npos) ? root : root.substr(slash + 1);
    if (leaf.empty()) leaf = root;
    Icon(g, Ico::Folder, RectI{pathBtn.x, pathBtn.y + app.S(3), app.S(14), app.S(14)},
         GC(0x595752));
    Text1(g, leaf, RectI{pathBtn.x + app.S(19), pathBtn.y, pathBtn.w - app.S(19), app.S(20)},
          app.S(10), GC(0x595752), 0);
    AddHit(app, pathBtn, HIT_PICK_DIR);

    // 进度
    if (app.running) {
        int py = opt2Y + app.S(30);
        int pct = app.progTotal > 0 ? app.progDone * 100 / app.progTotal : 0;
        Text1(g, L"正在下载…", RectI{ix, py, iw / 2, app.S(14)}, app.S(9), GC(0x77736C), 0);
        Text1(g, std::to_wstring(pct) + L"%",
              RectI{ix + iw / 2, py, iw / 2, app.S(14)}, app.S(9), GC(0x77736C), 2, true);
        RectI bar{ix, py + app.S(18), iw, app.S(4)};
        FillRound(g, bar, app.S(2), GC(0xD9D5CE));
        if (pct > 0) FillRound(g, RectI{bar.x, bar.y, bar.w * pct / 100, bar.h}, app.S(2),
                               GC(th::Accent));
    }

    // 底部
    HLine(g, ix, sizeRowY - app.S(14), iw, GC(0xD9D5CE));
    Text1(g, L"已下载大小", RectI{ix, sizeRowY, iw / 2, app.S(16)}, app.S(10), GC(0x99958D), 0);
    Text1(g, app.liveBytes > 0 ? HumanSize(app.liveBytes) : L"—",
          RectI{ix + iw / 2, sizeRowY, iw / 2, app.S(16)}, app.S(10), GC(0x46443F), 2, true);

    RectI btn{ix, btnY, iw, app.S(42)};
    bool   can = !app.selected.empty() && !app.running;
    Color  btnBg = can ? (Hov(app, HIT_START) ? GC(th::AccentHover) : GC(th::Accent))
                       : GC(th::Disabled);
    FillRound(g, btn, app.S(6), btnBg);
    std::wstring btnText;
    if (app.running)
        btnText = L"正在下载";
    else if (app.selected.empty())
        btnText = L"请先选择版面";
    else
        btnText = L"下载 " + std::to_wstring(app.selected.size()) + L" 个版面";
    int tw = TextW(btnText, app.S(12), true) + app.S(26);
    int bx = btn.x + (btn.w - tw) / 2;
    Icon(g, Ico::Download, RectI{bx, btn.y + (btn.h - app.S(18)) / 2, app.S(18), app.S(18)},
         GC(0xFFFFFF), 1.5f);
    Text1(g, btnText, RectI{bx + app.S(26), btn.y, tw, btn.h}, app.S(12), GC(0xFFFFFF), 0, true);
    if (can) AddHit(app, btn, HIT_START);

    Icon(g, Ico::Archive,
         RectI{ix + iw / 2 - app.S(52), hintY + app.S(1), app.S(13), app.S(13)}, GC(th::TextFaint));
    Text1(g, L"自动按日期创建文件夹",
          RectI{ix + iw / 2 - app.S(34), hintY, app.S(88), app.S(14)}, app.S(8), GC(th::TextFaint),
          0);
}

// ---------------------------------------------------------------- 下载记录
void DrawHistoryPage(Graphics& g, App& app, int W, int H) {
    int   top = app.S(th::TitleH);
    RectI area{app.S(th::SideW), top, W - app.S(th::SideW), H - top};
    FillRect(g, area, GC(th::ContentBg));

    int ix = area.x + app.S(46), iy = area.y + app.S(40);
    int iw = area.w - app.S(92);
    if (iw > app.S(900)) iw = app.S(900);

    Tracked(g, L"DOWNLOAD HISTORY", ix, iy, app.S(9), GC(th::Accent), 0.18f, true);
    Text1(g, L"下载记录", RectI{ix, iy + app.S(16), iw, app.S(36)}, app.S(27), GC(th::TextMain), 0,
          true, true);
    Text1(g, L"最近下载的报纸文件与任务", RectI{ix, iy + app.S(56), iw, app.S(18)}, app.S(12),
          GC(th::TextSub), 0);

    int cardY = iy + app.S(92);
    if (app.history.empty()) {
        Icon(g, Ico::Archive, RectI{ix + iw / 2 - app.S(14), cardY + app.S(30), app.S(28),
                                    app.S(28)},
             GC(0xC7C2B9), 1.4f);
        Text1(g, L"还没有下载记录", RectI{ix, cardY + app.S(66), iw, app.S(18)}, app.S(12),
              GC(th::TextSub), 1);
        Text1(g, L"下载完成后会自动记录在这里", RectI{ix, cardY + app.S(88), iw, app.S(16)},
              app.S(10), GC(th::TextFaint), 1);
        return;
    }

    int rowH = app.S(78);
    int rows = (int)app.history.size();
    int cardH = rows * rowH;
    FillRound(g, RectI{ix, cardY, iw, cardH}, app.S(8), GC(0xFFFFFF));
    StrokeRound(g, RectI{ix, cardY, iw, cardH}, app.S(8), GC(0xE1DED7), 1.0f);

    for (int i = 0; i < rows; ++i) {
        const HistoryItem& it = app.history[i];
        RectI r{ix, cardY + i * rowH, iw, rowH};
        if (i == rows - 1) {
            // 末行不留分隔线
        } else {
            HLine(g, r.x + app.S(20), r.Bottom(), r.w - app.S(40), GC(th::LineSoft));
        }
        RectI box{r.x + app.S(20), r.y + (rowH - app.S(38)) / 2, app.S(38), app.S(38)};
        FillRound(g, box, app.S(6), GC(th::AccentSoft));
        Icon(g, Ico::File, RectI{box.x + app.S(9), box.y + app.S(9), app.S(20), app.S(20)},
             GC(th::Accent), 1.4f);

        int tx = box.Right() + app.S(14);
        Text1(g, PaperNameFromPath(it.path) + L" · " + [&] {
                  // 2026-11-03 -> 2026年11月3日
                  if (it.iso.size() < 10) return it.iso;
                  return std::to_wstring(_wtoi(it.iso.substr(0, 4).c_str())) + L"年" +
                         std::to_wstring(_wtoi(it.iso.substr(5, 2).c_str())) + L"月" +
                         std::to_wstring(_wtoi(it.iso.substr(8, 2).c_str())) + L"日";
              }(),
              RectI{tx, r.y + app.S(22), iw - app.S(340), app.S(18)}, app.S(12), GC(0x33322F), 0,
              true);
        // 副标题：时间 + 文件名，便于区分同一天的不同版面组合
        std::wstring leafName = it.path;
        size_t       sl2      = leafName.find_last_of(L"\\/");
        if (sl2 != std::wstring::npos) leafName = leafName.substr(sl2 + 1);
        std::wstring sub = it.when;
        if (!leafName.empty()) sub += L"  ·  " + leafName;
        Text1(g, sub, RectI{tx, r.y + app.S(44), iw - app.S(340), app.S(16)}, app.S(11),
              GC(th::TextFaint), 0);
        Text1(g, std::to_wstring(it.pages) + L" 个版面",
              RectI{r.x + iw - app.S(300), r.y, app.S(80), rowH}, app.S(11), GC(0x78756F), 0);
        Text1(g, HumanSize(it.bytes), RectI{r.x + iw - app.S(215), r.y, app.S(70), rowH},
              app.S(11), GC(0x78756F), 0);
        RectI btn{r.x + iw - app.S(130), r.y + (rowH - app.S(30)) / 2, app.S(100), app.S(30)};
        FillRound(g, btn, app.S(5), GC(Hov(app, HIT_HIST_OPEN + i) ? 0xF7F5F1 : 0xFFFFFF));
        StrokeRound(g, btn, app.S(5), GC(th::LineStrong), 1.0f);
        Icon(g, Ico::Folder, RectI{btn.x + app.S(18), btn.y + app.S(8), app.S(14), app.S(14)},
             GC(0x64615B));
        Text1(g, L"打开目录", RectI{btn.x + app.S(37), btn.y, app.S(56), btn.h}, app.S(10),
              GC(0x64615B), 0);
        AddHit(app, btn, HIT_HIST_OPEN + i);
    }
}

// ---------------------------------------------------------------- 偏好设置
void DrawSettingsModal(Graphics& g, App& app, int W, int H) {
    int top = app.S(th::TitleH);
    FillRect(g, RectI{0, top, W, H - top}, GC(0x24231F, 112));
    AddHit(app, RectI{0, top, W, H - top}, HIT_MODAL_BACKDROP);

    int mw = app.S(430), mh = app.S(372);
    RectI m{W / 2 - mw / 2, top + (H - top) / 2 - mh / 2, mw, mh};
    app.modalRect = m;
    SoftShadow(g, m, app.S(9), app.S(10), 60);
    FillRound(g, m, app.S(9), GC(th::ContentBg));

    int px = m.x + app.S(24), py = m.y + app.S(24);
    int iw = mw - app.S(48);

    Tracked(g, L"PREFERENCES", px, py, app.S(9), GC(th::Accent), 0.18f, true);
    Text1(g, L"偏好设置", RectI{px, py + app.S(15), iw - app.S(30), app.S(26)}, app.S(19),
          GC(th::TextMain), 0, true, true);
    RectI xb{px + iw - app.S(24), py + app.S(4), app.S(24), app.S(24)};
    Icon(g, Ico::Close, xb, GC(Hov(app, HIT_MODAL_CLOSE) ? th::TextMain : 0x88847D), 1.5f);
    AddHit(app, xb, HIT_MODAL_CLOSE);

    int ry = py + app.S(58);
    HLine(g, px, ry, iw, GC(th::Line));

    // 行 1：保存位置
    Text1(g, L"默认保存位置", RectI{px, ry + app.S(14), iw - app.S(160), app.S(16)}, app.S(11),
          GC(th::TextMain), 0, true);
    Text1(g, L"下载的报纸将保存在这里", RectI{px, ry + app.S(33), iw - app.S(160), app.S(14)},
          app.S(9), GC(th::TextFaint), 0);
    std::wstring leaf = app.settings.outRoot;
    size_t       sl = leaf.find_last_of(L"\\/");
    if (sl != std::wstring::npos) leaf = leaf.substr(sl + 1);
    RectI pb{px + iw - app.S(150), ry + app.S(19), app.S(150), app.S(30)};
    FillRound(g, pb, app.S(5), GC(0xFFFFFF));
    StrokeRound(g, pb, app.S(5), GC(0xDEDAD3), 1.0f);
    Icon(g, Ico::Folder, RectI{pb.x + app.S(10), pb.y + app.S(8), app.S(15), app.S(15)},
         GC(0x5B5852));
    Text1(g, leaf.empty() ? L"选择目录" : leaf,
          RectI{pb.x + app.S(30), pb.y, pb.w - app.S(36), pb.h}, app.S(9), GC(0x5B5852), 0);
    AddHit(app, pb, HIT_MODAL_PATH);
    ry += app.S(68);
    HLine(g, px, ry, iw, GC(0xE7E4DE));

    // 行 2/3：开关
    struct Row {
        int            id;
        const wchar_t* title;
        const wchar_t* hint;
        bool           on;
    };
    Row rows[] = {
        {HIT_TOGGLE_OPEN, L"下载完成后打开目录", L"方便快速查看下载文件", app.settings.openAfter},
        {HIT_TOGGLE_MERGE, L"自动合并多个版面", L"将所选版面合并为单个 PDF",
         app.settings.mergePdf},
    };
    for (auto& rw : rows) {
        Text1(g, rw.title, RectI{px, ry + app.S(14), iw - app.S(70), app.S(16)}, app.S(11),
              GC(th::TextMain), 0, true);
        Text1(g, rw.hint, RectI{px, ry + app.S(33), iw - app.S(70), app.S(14)}, app.S(9),
              GC(th::TextFaint), 0);
        RectI tg{px + iw - app.S(34), ry + app.S(25), app.S(34), app.S(19)};
        FillRound(g, tg, app.S(10), GC(rw.on ? th::Accent : 0xD5D1C9));
        int kw = app.S(15);
        RectI knob{rw.on ? tg.Right() - kw - app.S(2) : tg.x + app.S(2),
                   tg.y + app.S(2), kw, kw};
        FillRound(g, knob, kw / 2, GC(0xFFFFFF));
        AddHit(app, RectI{tg.x - app.S(150), ry, app.S(190), app.S(68)}, rw.id);
        ry += app.S(68);
        HLine(g, px, ry, iw, GC(0xE7E4DE));
    }

    RectI save{px, ry + app.S(20), iw, app.S(38)};
    FillRound(g, save, app.S(5), GC(Hov(app, HIT_MODAL_SAVE) ? th::AccentHover : th::Accent));
    Text1(g, L"保存设置", save, app.S(11), GC(0xFFFFFF), 1, true);
    AddHit(app, save, HIT_MODAL_SAVE);
}

// ---------------------------------------------------------------- 日历
void DrawCalendar(Graphics& g, App& app, int W, int H) {
    int   cw = app.S(272), chh = app.S(308);
    int   stepW = app.S(264);
    int   sideW = app.S(th::SideW), taskW = app.S(th::TaskW);
    int   contentW = W - sideW - taskW;
    int   stepRight = sideW + th::ContentPadX + (contentW - app.S(th::ContentPadX) * 2);
    int   sx = stepRight - stepW;
    int   sy = app.S(th::TitleH) + th::ContentPadT + app.S(16) + app.S(43) + app.S(6);
    RectI cal{sx, sy, cw, chh};
    app.calRect = cal;

    SoftShadow(g, cal, app.S(8), app.S(8), 55);
    FillRound(g, cal, app.S(8), GC(0xFFFFFF));
    StrokeRound(g, cal, app.S(8), GC(0xDEDBD5), 1.0f);
    AddHit(app, cal, HIT_NONE);  // 遮挡下层，避免点穿

    int px = cal.x + app.S(12), iw = cal.w - app.S(24);
    RectI head{px, cal.y + app.S(10), iw, app.S(28)};
    wchar_t mbuf[32];
    swprintf(mbuf, 32, L"%d年%d月", app.calMonth.y, app.calMonth.m);
    Text1(g, mbuf, head, app.S(13), GC(th::TextMain), 1, true, true);
    RectI pv{head.x, head.y, app.S(26), head.h};
    RectI nx{head.Right() - app.S(26), head.y, app.S(26), head.h};
    if (Hov(app, HIT_CAL_PREV)) FillRound(g, pv, app.S(4), GC(0xF2F0EC));
    if (Hov(app, HIT_CAL_NEXT)) FillRound(g, nx, app.S(4), GC(0xF2F0EC));
    Icon(g, Ico::ChevLeft, RectI{pv.x + app.S(5), pv.y + app.S(6), app.S(16), app.S(16)},
         GC(0x817E77));
    Icon(g, Ico::ChevRight, RectI{nx.x + app.S(5), nx.y + app.S(6), app.S(16), app.S(16)},
         GC(0x817E77));
    AddHit(app, pv, HIT_CAL_PREV);
    AddHit(app, nx, HIT_CAL_NEXT);

    HLine(g, px, head.Bottom() + app.S(6), iw, GC(th::LineSoft));

    int cellW = iw / 7;
    int rowTop = head.Bottom() + app.S(12);
    int cellH = app.S(30);
    const wchar_t* wk[] = {L"日", L"一", L"二", L"三", L"四", L"五", L"六"};
    for (int i = 0; i < 7; ++i)
        Text1(g, wk[i], RectI{px + i * cellW, rowTop, cellW, app.S(18)}, app.S(9),
              GC(th::TextFaint), 1);

    // 当月首日星期
    Date first{app.calMonth.y, app.calMonth.m, 1};
    int  fdow = WeekdayOf(first);
    int  dim = 31;
    static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    dim = mdays[app.calMonth.m - 1];
    bool leap = (app.calMonth.y % 4 == 0 && app.calMonth.y % 100 != 0) ||
                (app.calMonth.y % 400 == 0);
    if (app.calMonth.m == 2 && leap) dim = 29;

    int gy = rowTop + app.S(20);
    Date today = Today();
    for (int day = 1; day <= dim; ++day) {
        int  idx = fdow + day - 1;
        int  row = idx / 7, col = idx % 7;
        RectI cell{px + col * cellW, gy + row * cellH, cellW, cellH};
        bool  isSel = (day == app.date.d && app.calMonth.m == app.date.m &&
                       app.calMonth.y == app.date.y);
        bool  isToday = (day == today.d && app.calMonth.m == today.m &&
                        app.calMonth.y == today.y);
        int   id = HIT_CAL_DAY + day;
        if (isSel) {
            RectI cir{cell.x + cellW / 2 - app.S(13), cell.y + 2, app.S(26), app.S(26)};
            FillRound(g, cir, app.S(13), GC(th::Accent));
        } else if (Hov(app, id)) {
            RectI cir{cell.x + cellW / 2 - app.S(13), cell.y + 2, app.S(26), app.S(26)};
            FillRound(g, cir, app.S(13), GC(0xF2EFEA));
        } else if (isToday) {
            RectI cir{cell.x + cellW / 2 - app.S(13), cell.y + 2, app.S(26), app.S(26)};
            StrokeRound(g, cir, app.S(13), GC(0xD8B3B3), 1.0f);
        }
        Text1(g, std::to_wstring(day), RectI{cell.x, cell.y + 2, cell.w, app.S(26)}, app.S(11),
              GC(isSel ? 0xFFFFFF : (isToday ? th::Accent : 0x403F3B)), 1, isSel);
        AddHit(app, cell, id);
    }

    // 底部快捷
    int fy = cal.Bottom() - app.S(40);
    HLine(g, px, fy - app.S(8), iw, GC(th::LineSoft));
    RectI b1{px, fy, (iw - app.S(8)) / 2, app.S(28)};
    RectI b2{b1.Right() + app.S(8), fy, (iw - app.S(8)) / 2, app.S(28)};
    FillRound(g, b1, app.S(5), GC(Hov(app, HIT_CAL_TODAY) ? 0xF2EFEA : 0xF7F5F1));
    Text1(g, L"今天", b1, app.S(10), GC(0x595752), 1);
    AddHit(app, b1, HIT_CAL_TODAY);
    FillRound(g, b2, app.S(5), GC(Hov(app, HIT_CAL_LATEST) ? 0xF2EFEA : 0xF7F5F1));
    Text1(g, L"最近一期", b2, app.S(10), GC(0x595752), 1);
    AddHit(app, b2, HIT_CAL_LATEST);
}

// ---------------------------------------------------------------- 吐司
void DrawToast(Graphics& g, App& app, int W, int H) {
    int  tw = app.S(330), thh = app.S(46);
    int  tw2 = TextW(app.toast, app.S(11)) + app.S(90);
    if (tw2 > tw) tw = tw2;
    RectI t{W / 2 - tw / 2, H - app.S(28) - thh, tw, thh};
    SoftShadow(g, t, app.S(7), app.S(7), 70);
    FillRound(g, t, app.S(7), GC(0x292926));

    AddHit(app, t, HIT_NONE);
    RectI cir{t.x + app.S(10), t.y + (thh - app.S(23)) / 2, app.S(23), app.S(23)};
    FillRound(g, cir, app.S(12), GC(th::OkGreen));
    Icon(g, Ico::Check, cir, GC(0xFFFFFF), 1.5f);
    Text1(g, app.toast, RectI{cir.Right() + app.S(10), t.y, t.w - app.S(80), thh}, app.S(11),
          GC(0xFFFFFF), 0);
    RectI xb{t.Right() - app.S(30), t.y + (thh - app.S(20)) / 2, app.S(20), app.S(20)};
    Icon(g, Ico::Close, xb, GC(0xAAAAAA), 1.4f);
    AddHit(app, xb, HIT_TOAST_CLOSE);
}

// ---------------------------------------------------------------- 整帧
void DrawFrame(Graphics& g, App& app, int W, int H) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.SetCompositingQuality(CompositingQualityHighQuality);
    app.hits.clear();

    FillRect(g, RectI{0, 0, W, H}, GC(th::AppBg));
    DrawTitlebar(g, app, W);
    DrawSidebar(g, app, W, H);

    if (app.nav == 0) {
        DrawDownloadPage(g, app, W, H);
        DrawTaskPanel(g, app, W, H);
    } else {
        DrawHistoryPage(g, app, W, H);
    }

    if (app.showCalendar) DrawCalendar(g, app, W, H);
    if (app.showSettings) DrawSettingsModal(g, app, W, H);
    if (!app.toast.empty()) DrawToast(g, app, W, H);
}
