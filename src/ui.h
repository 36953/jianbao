// ui.h —— 界面状态、命中区与整帧渲染入口
#pragma once

#include "core.h"
#include "job.h"
#include "ui_draw.h"

#include <string>
#include <vector>

// ---------------------------------------------------------------- 持久化数据
struct Settings {
    std::wstring outRoot;          // 保存位置根目录
    bool         openAfter = true; // 完成后打开目录
    bool         mergePdf  = true; // 合并为单个 PDF
};

struct HistoryItem {
    std::wstring iso;       // 2026-11-03
    std::wstring when;      // 今天 09:41
    std::wstring path;      // 文件或目录
    int          pages = 0;
    long long    bytes = 0;
};

// ---------------------------------------------------------------- 命中区
enum HitId {
    HIT_NONE = 0,
    HIT_TITLE_SETTINGS = 1, HIT_TITLE_MIN, HIT_TITLE_CLOSE,
    HIT_NAV_DOWNLOAD, HIT_NAV_HISTORY, HIT_NAV_SETTINGS, HIT_NAV_ABOUT,
    HIT_SOURCE_MORE,
    HIT_DATE_PREV, HIT_DATE_NEXT, HIT_DATE_OPEN,
    HIT_SELECT_ALL, HIT_VIEW_GRID, HIT_VIEW_LIST, HIT_SEARCH_BOX,
    HIT_CLEAR_SEL, HIT_PICK_DIR, HIT_START, HIT_OPEN_OUT,
    HIT_TOAST_CLOSE,
    HIT_MODAL_BACKDROP, HIT_MODAL_CLOSE, HIT_MODAL_PATH, HIT_TOGGLE_OPEN, HIT_TOGGLE_MERGE,
    HIT_MODAL_SAVE,
    HIT_CAL_PREV, HIT_CAL_NEXT, HIT_CAL_TODAY, HIT_CAL_LATEST,
    HIT_LATEST_JUMP, HIT_HELP_ABOUT,
    HIT_CARD      = 1000,
    HIT_SEL_ITEM  = 2000,
    HIT_HIST_OPEN = 3000,
    HIT_CAL_DAY   = 4000,
    HIT_SOURCE    = 5000,   // + 来源序号（AllSources 下标）
};

struct Hotspot {
    RectI r;
    int   id = HIT_NONE;
};

// ---------------------------------------------------------------- 应用状态
// 初值占位，运行时由 Today() 覆盖
#define TODAY_YEAR  2026
#define TODAY_MONTH 11
#define TODAY_DAY   5

struct App {
    HWND hwnd    = nullptr;
    int  dpi     = 96;
    int  S(int v) const { return MulDiv(v, dpi, 96); }

    // 设置与历史
    Settings                 settings;
    std::vector<HistoryItem> history;

    // 当期数据
    std::wstring         sourceId = L"rmrb";   // 当前报纸来源
    Date                 date{TODAY_YEAR, TODAY_MONTH, TODAY_DAY};
    bool                 dateReady = false;
    std::vector<PageInfo> pages;
    std::wstring         pagesError;
    bool                 loading = false;
    int                  gen     = 0;   // 请求代数，用于丢弃过期结果

    // 交互状态
    std::vector<int> selected;
    std::vector<int> visible;   // 当前过滤后可见的版号
    std::wstring     query;
    int              view          = 0;  // 0 网格 1 列表
    int              nav           = 0;  // 0 下载 1 记录
    int              contentScroll = 0;
    int              contentMax    = 0;
    int              selScroll     = 0;
    int              selMax        = 0;
    int              hover         = HIT_NONE;
    int              pressed       = HIT_NONE;
    bool             searchFocus   = false;

    // 任务状态
    bool         running   = false;
    int          progDone  = 0;
    int          progTotal = 0;
    long long    liveBytes = 0;
    std::wstring statusLine;   // 右侧状态提示
    std::wstring logTail;      // 最近一条日志

    // 浮层
    bool         showSettings = false;
    bool         showCalendar = false;
    Date         calMonth{};
    std::wstring toast;
    ULONGLONG    toastUntil = 0;

    // 其他
    std::wstring latestIso;   // 最近一期，用于提示
    ULONGLONG    spinTick = 0;

    std::vector<Hotspot> hits;
    RectI                searchEditRect;
    RectI                calRect;
    RectI                modalRect;
    RectI                winRect;

    int  Hit(int px, int py) const {
        for (auto it = hits.rbegin(); it != hits.rend(); ++it)
            if (it->r.Has(px, py)) return it->id;
        return HIT_NONE;
    }
    bool IsSelected(int page) const;
    void ToggleSelect(int page);
    void SetDate(const Date& d);
    void SetSource(const std::wstring& id);
    const SourceInfo& Source() const;
    void ShowToast(const std::wstring& text, int ms = 4200);
};

// 当前日期（编译期给出可用初值，运行时由 Today() 覆盖）
void DrawFrame(Gdiplus::Graphics& g, App& app, int W, int H);

// 子页面绘制
void DrawTitlebar(Gdiplus::Graphics& g, App& app, int W);
void DrawSidebar(Gdiplus::Graphics& g, App& app, int W, int H);
void DrawDownloadPage(Gdiplus::Graphics& g, App& app, int W, int H);
void DrawTaskPanel(Gdiplus::Graphics& g, App& app, int W, int H);
void DrawHistoryPage(Gdiplus::Graphics& g, App& app, int W, int H);
void DrawSettingsModal(Gdiplus::Graphics& g, App& app, int W, int H);
void DrawCalendar(Gdiplus::Graphics& g, App& app, int W, int H);
void DrawToast(Gdiplus::Graphics& g, App& app, int W, int H);

// 持久化
void LoadSettings(App& app);
void SaveSettings(const App& app);
void LoadHistory(App& app);
void SaveHistory(const App& app);
std::wstring AppDataDir();

int RunGui();
int RenderUiPreview(const std::wstring& outPng, int w, int h, const std::wstring& iso,
                    const std::wstring& state, const std::wstring& sourceId = L"");
