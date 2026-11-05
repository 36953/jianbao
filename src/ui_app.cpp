// ui_app.cpp —— 窗口、事件、后台任务与持久化
#include "ui.h"

#include <windowsx.h>

#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <thread>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

using namespace Gdiplus;

#define WM_APP_EVENT (WM_APP + 20)

static App                   g_app;
static HWND                  g_hwnd      = nullptr;
static HWND                  g_edit      = nullptr;
static WNDPROC               g_editProc  = nullptr;
static HFONT                 g_editFont  = nullptr;
static ULONG_PTR             g_gdiToken  = 0;
static std::atomic<int>      g_liveGen{0};
static bool                  g_painting  = false;

// ---------------------------------------------------------------- 事件
enum class EvKind { Latest, Pages, Leads, Log, Progress, Done };

struct Ev {
    EvKind                kind;
    int                   gen = 0;
    bool                  ok  = false;
    bool                  apply = false;   // Latest：是否直接切到该日期
    int                   done = 0, total = 0;
    long long             bytes = 0;
    std::wstring          text;
    Date                  date{};
    std::vector<PageInfo> pages;
    JobResult             res;
};

static void PostEv(Ev* e) {
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_EVENT, 0, (LPARAM)e);
    else delete e;
}

static void Invalidate() {
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
}

// ---------------------------------------------------------------- App 方法
bool App::IsSelected(int page) const {
    return std::find(selected.begin(), selected.end(), page) != selected.end();
}

void App::ToggleSelect(int page) {
    auto it = std::find(selected.begin(), selected.end(), page);
    if (it == selected.end()) selected.push_back(page);
    else selected.erase(it);
}

void App::ShowToast(const std::wstring& text, int ms) {
    toast = text;
    toastUntil = GetTickCount64() + ms;
}

const SourceInfo& App::Source() const {
    const SourceInfo* p = FindSource(sourceId);
    return p ? *p : DefaultSource();
}

// ---------------------------------------------------------------- 持久化
std::wstring AppDataDir() {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p))) {
        out = p;
        CoTaskMemFree(p);
    } else {
        out = ExeDir();
    }
    out = JoinPath(out, L"见报");
    EnsureDir(out);
    return out;
}

static std::wstring DefaultOutRoot() {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &p))) {
        out = p;
        CoTaskMemFree(p);
    } else {
        out = ExeDir();
    }
    out = JoinPath(out, L"见报");
    return out;
}

void LoadSettings(App& app) {
    app.settings.outRoot = DefaultOutRoot();
    app.settings.openAfter = true;
    app.settings.mergePdf = true;

    std::string raw;
    if (!ReadFileBytes(JoinPath(AppDataDir(), L"settings.ini"), raw)) return;
    std::wstring txt = Utf8ToWide(raw);
    size_t       pos = 0;
    while (pos < txt.size()) {
        size_t nl = txt.find(L'\n', pos);
        std::wstring line = txt.substr(pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos);
        pos = (nl == std::wstring::npos) ? txt.size() : nl + 1;
        line = TrimW(line);
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring k = TrimW(line.substr(0, eq));
        std::wstring v = TrimW(line.substr(eq + 1));
        if (k == L"outRoot" && !v.empty()) app.settings.outRoot = v;
        else if (k == L"openAfter") app.settings.openAfter = (v == L"1");
        else if (k == L"mergePdf") app.settings.mergePdf = (v == L"1");
        else if (k == L"source" && FindSource(v)) app.sourceId = v;
    }
    if (app.settings.outRoot.empty()) app.settings.outRoot = DefaultOutRoot();
}

void SaveSettings(const App& app) {
    std::wstring txt = L"outRoot=" + app.settings.outRoot + L"\n";
    txt += L"openAfter=" + std::wstring(app.settings.openAfter ? L"1" : L"0") + L"\n";
    txt += L"mergePdf=" + std::wstring(app.settings.mergePdf ? L"1" : L"0") + L"\n";
    txt += L"source=" + app.sourceId + L"\n";
    WriteFileBytes(JoinPath(AppDataDir(), L"settings.ini"), WideToUtf8(txt));
}

void LoadHistory(App& app) {
    app.history.clear();
    std::string raw;
    if (!ReadFileBytes(JoinPath(AppDataDir(), L"history.tsv"), raw)) return;
    std::wstring txt = Utf8ToWide(raw);
    size_t       pos = 0;
    while (pos < txt.size() && app.history.size() < 60) {
        size_t nl = txt.find(L'\n', pos);
        std::wstring line = txt.substr(pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos);
        pos = (nl == std::wstring::npos) ? txt.size() : nl + 1;
        if (line.empty()) continue;
        std::vector<std::wstring> f;
        size_t                    s = 0;
        while (true) {
            size_t t = line.find(L'\t', s);
            f.push_back(line.substr(s, t == std::wstring::npos ? std::wstring::npos : t - s));
            if (t == std::wstring::npos) break;
            s = t + 1;
        }
        if (f.size() < 5) continue;
        HistoryItem it;
        it.iso = f[0];
        it.when = f[1];
        it.pages = _wtoi(f[2].c_str());
        it.bytes = _wtoi64(f[3].c_str());
        it.path = f[4];
        if (it.path.empty()) continue;
        app.history.push_back(it);
    }
}

void SaveHistory(const App& app) {
    std::wstring txt;
    for (auto& it : app.history) {
        txt += it.iso + L"\t" + it.when + L"\t" + std::to_wstring(it.pages) + L"\t" +
               std::to_wstring(it.bytes) + L"\t" + it.path + L"\n";
    }
    WriteFileBytes(JoinPath(AppDataDir(), L"history.tsv"), WideToUtf8(txt));
}

// ---------------------------------------------------------------- 后台任务
static void WorkerLatest(std::wstring sourceId, bool apply) {
    const SourceInfo& src = FindSource(sourceId) ? *FindSource(sourceId) : DefaultSource();
    Date              d;
    std::wstring      err;
    bool              ok = FindLatestDate(src, d, err, nullptr);
    Ev*               e  = new Ev();
    e->kind  = EvKind::Latest;
    e->ok    = ok;
    e->apply = apply;
    e->date  = d;
    e->text  = err;
    PostEv(e);
}

static void WorkerPages(std::wstring sourceId, Date date, int gen) {
    const SourceInfo&     src = FindSource(sourceId) ? *FindSource(sourceId) : DefaultSource();
    std::vector<PageInfo> pages;
    std::wstring          err;
    FetchResult           fr = FetchPageList(src, date, pages, err);

    Ev* e = new Ev();
    e->kind = EvKind::Pages;
    e->gen  = gen;
    e->ok   = (fr == FetchResult::Ok);
    e->date = date;
    e->text = err;
    e->pages = pages;
    PostEv(e);

    if (fr != FetchResult::Ok || pages.empty()) return;
    // 人民日报旧版式版面页没有文章列表，头条抓取无意义
    if (src.id == L"rmrb" && pages[0].pageUrl.find(L"/nbs.D1100") != std::wstring::npos) return;

    std::vector<PageInfo> copy = pages;
    std::atomic<int>      next{0};
    int                   count = (int)copy.size();
    int                   nthread = count < 6 ? count : 6;
    std::vector<std::thread> pool;
    for (int i = 0; i < nthread; ++i) {
        pool.emplace_back([&]() {
            while (true) {
                int idx = next.fetch_add(1);
                if (idx >= count) break;
                if (g_liveGen.load() != gen) break;
                std::wstring e;
                FetchPageDetail(src, date, copy[idx], e);
            }
        });
    }
    for (auto& t : pool) t.join();
    if (g_liveGen.load() != gen) return;

    Ev* e2 = new Ev();
    e2->kind = EvKind::Leads;
    e2->gen  = gen;
    e2->pages = copy;
    PostEv(e2);
}

static void WorkerJob(JobOptions opt, int gen) {
    JobResult res;
    bool      ok = RunJob(
        opt,
        [gen](const std::wstring& s) {
            Ev* e = new Ev();
            e->kind = EvKind::Log;
            e->gen  = gen;
            e->text = s;
            PostEv(e);
        },
        [gen](int done, int total, const std::wstring& name, long long bytes) {
            Ev* e = new Ev();
            e->kind  = EvKind::Progress;
            e->gen   = gen;
            e->done  = done;
            e->total = total;
            e->text  = name;
            e->bytes = bytes;
            PostEv(e);
        },
        res);

    Ev* e = new Ev();
    e->kind = EvKind::Done;
    e->gen  = gen;
    e->ok   = ok;
    e->res  = res;
    PostEv(e);
}

// ---------------------------------------------------------------- 日期与加载
void App::SetDate(const Date& d) {
    date = d;
    dateReady = true;
    selected.clear();
    pages.clear();
    pagesError.clear();
    loading = true;
    contentScroll = 0;
    selScroll = 0;
    ++gen;
    g_liveGen.store(gen);
    std::thread(WorkerPages, sourceId, d, gen).detach();
    Invalidate();
}

void App::SetSource(const std::wstring& id) {
    if (id == sourceId) return;
    if (!FindSource(id)) return;
    sourceId = id;
    selected.clear();
    pages.clear();
    pagesError.clear();
    query.clear();
    searchFocus = false;
    contentScroll = 0;
    selScroll = 0;
    latestIso.clear();
    nav = 0;
    ++gen;
    g_liveGen.store(gen);
    loading = true;
    std::thread(WorkerPages, sourceId, date, gen).detach();
    std::thread(WorkerLatest, sourceId, false).detach();
    SaveSettings(*this);
    Invalidate();
}

static void OpenFolder(const std::wstring& path) {
    std::wstring p = path;
    EnsureDir(p);
    ShellExecuteW(g_hwnd, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static void PickFolder(std::wstring& out) {
    BROWSEINFOW bi = {0};
    bi.hwndOwner   = g_hwnd;
    bi.lpszTitle   = L"选择报纸保存目录";
    bi.ulFlags     = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    wchar_t buf[MAX_PATH * 2] = {0};
    if (SHGetPathFromIDListW(pidl, buf)) out = buf;
    CoTaskMemFree(pidl);
}

static void StartDownload(App& app) {
    if (app.running || app.selected.empty()) return;
    if (!EnsureDir(app.settings.outRoot)) {
        app.ShowToast(L"无法创建保存目录，请检查权限");
        Invalidate();
        return;
    }
    std::wstring iso  = IsoDateW(app.date);
    std::wstring dir  = JoinPath(app.settings.outRoot, iso);
    app.running    = true;
    app.progDone   = 0;
    app.progTotal  = (int)app.selected.size();
    app.liveBytes  = 0;
    app.statusLine.clear();

    JobOptions opt;
    opt.sourceId = app.sourceId;
    opt.date   = app.date;
    opt.outDir = dir;
    opt.pick   = app.selected;
    opt.merge  = app.settings.mergePdf;

    int gen = app.gen;
    std::thread(WorkerJob, opt, gen).detach();
    Invalidate();
}

// ---------------------------------------------------------------- 点击处理
static void RecordHistory(App& app, const JobResult& res) {
    if (!res.ok || res.outFiles.empty()) return;
    HistoryItem it;
    it.iso   = IsoDateW(app.date);
    it.path  = res.outPdf.empty() ? res.outFiles[0] : res.outPdf;
    it.pages = res.pagesOk;
    it.bytes = res.sizeBytes;

    SYSTEMTIME st;
    GetLocalTime(&st);
    Date today = Today();
    wchar_t when[64];
    if (CompareDate(app.date, today) == 0) {
        (void)st;
        wchar_t hhmm[16];
        swprintf(hhmm, 16, L"%02d:%02d", st.wHour, st.wMinute);
        when[0] = 0;
        swprintf(when, 64, L"今天 %s", hhmm);
    } else {
        swprintf(when, 64, L"%d月%d日", app.date.m, app.date.d);
    }
    it.when = when;
    app.history.insert(app.history.begin(), it);
    if (app.history.size() > 60) app.history.resize(60);
    SaveHistory(app);
}

static void HandleClick(App& app, int id, int mx, int my) {
    // 日历打开时优先处理
    if (app.showCalendar && !app.calRect.Has(mx, my)) {
        app.showCalendar = false;
        Invalidate();
        return;
    }
    if (app.showCalendar) {
        if (id >= HIT_CAL_DAY && id <= HIT_CAL_DAY + 31) {
            int day = id - HIT_CAL_DAY;
            Date d{app.calMonth.y, app.calMonth.m, day};
            app.showCalendar = false;
            app.SetDate(d);
            return;
        }
        if (id == HIT_CAL_PREV || id == HIT_CAL_NEXT) {
            int  delta = (id == HIT_CAL_PREV) ? -1 : 1;
            int  m = app.calMonth.m + delta, y = app.calMonth.y;
            if (m < 1) { m = 12; y--; }
            if (m > 12) { m = 1; y++; }
            app.calMonth = Date{y, m, 1};
            Invalidate();
            return;
        }
        if (id == HIT_CAL_TODAY) {
            app.showCalendar = false;
            app.SetDate(Today());
            return;
        }
        if (id == HIT_CAL_LATEST && !app.latestIso.empty()) {
            Date d;
            if (ParseIsoDate(app.latestIso, d)) {
                app.showCalendar = false;
                app.SetDate(d);
            }
            return;
        }
        Invalidate();
        return;
    }

    // 偏好设置弹窗
    if (app.showSettings && !app.modalRect.Has(mx, my)) {
        app.showSettings = false;
        Invalidate();
        return;
    }
    if (app.showSettings) {
        if (id == HIT_MODAL_CLOSE) {
            app.showSettings = false;
        } else if (id == HIT_MODAL_PATH) {
            std::wstring p = app.settings.outRoot;
            PickFolder(p);
            if (!p.empty()) app.settings.outRoot = p;
        } else if (id == HIT_TOGGLE_OPEN) {
            app.settings.openAfter = !app.settings.openAfter;
        } else if (id == HIT_TOGGLE_MERGE) {
            app.settings.mergePdf = !app.settings.mergePdf;
        } else if (id == HIT_MODAL_SAVE) {
            EnsureDir(app.settings.outRoot);
            SaveSettings(app);
            app.showSettings = false;
            app.ShowToast(L"设置已保存");
        }
        Invalidate();
        return;
    }

    if (id == HIT_TOAST_CLOSE) {
        app.toast.clear();
        Invalidate();
        return;
    }

    if (id >= HIT_CARD && id < HIT_CARD + 100) {
        app.ToggleSelect(id - HIT_CARD);
        Invalidate();
        return;
    }
    if (id >= HIT_SEL_ITEM && id < HIT_SEL_ITEM + 100) {
        app.ToggleSelect(id - HIT_SEL_ITEM);
        Invalidate();
        return;
    }
    if (id >= HIT_HIST_OPEN && id < HIT_HIST_OPEN + 200) {
        size_t idx = (size_t)(id - HIT_HIST_OPEN);
        if (idx < app.history.size()) {
            std::wstring p = app.history[idx].path;
            size_t slash = p.find_last_of(L"\\/");
            if (slash != std::wstring::npos) p = p.substr(0, slash);
            OpenFolder(p);
        }
        return;
    }

    if (id >= HIT_SOURCE && id < HIT_SOURCE + 40) {
        const auto& list = AllSources();
        size_t      idx  = (size_t)(id - HIT_SOURCE);
        if (idx < list.size()) {
            app.SetSource(list[idx].id);
            return;
        }
    }

    switch (id) {
    case HIT_TITLE_SETTINGS:
    case HIT_NAV_SETTINGS:
        app.showSettings = true;
        break;
    case HIT_TITLE_MIN:
        ShowWindow(g_hwnd, SW_MINIMIZE);
        break;
    case HIT_TITLE_CLOSE:
        PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
        break;
    case HIT_NAV_DOWNLOAD:
    case HIT_NAV_HISTORY:
        app.nav = (id == HIT_NAV_DOWNLOAD) ? 0 : 1;
        if (g_edit) ShowWindow(g_edit, app.nav == 0 ? SW_SHOW : SW_HIDE);
        break;
    case HIT_NAV_ABOUT:
    case HIT_HELP_ABOUT:
        app.ShowToast(std::wstring(L"见报 v") + kAppVersion + L" · 已接入 " +
                          std::to_wstring(AllSources().size()) + L" 家报纸来源",
                      5200);
        break;
    case HIT_SOURCE_MORE:
        app.ShowToast(L"如发现其他提供 PDF 的报纸，可反馈接入");
        break;
    case HIT_DATE_PREV: {
        Date d = AddDays(app.date, -1);
        app.SetDate(d);
        return;
    }
    case HIT_DATE_NEXT: {
        Date d = AddDays(app.date, 1);
        if (CompareDate(d, Today()) <= 0) app.SetDate(d);
        return;
    }
    case HIT_DATE_OPEN:
        app.calMonth = Date{app.date.y, app.date.m, 1};
        app.showCalendar = true;
        break;
    case HIT_SELECT_ALL: {
        bool allSel = !app.visible.empty();
        for (int no : app.visible)
            if (!app.IsSelected(no)) { allSel = false; break; }
        if (allSel) {
            for (int no : app.visible) {
                auto it = std::find(app.selected.begin(), app.selected.end(), no);
                if (it != app.selected.end()) app.selected.erase(it);
            }
        } else {
            for (int no : app.visible)
                if (!app.IsSelected(no)) app.selected.push_back(no);
        }
        break;
    }
    case HIT_VIEW_GRID: app.view = 0; app.contentScroll = 0; break;
    case HIT_VIEW_LIST: app.view = 1; app.contentScroll = 0; break;
    case HIT_SEARCH_BOX:
        if (g_edit) SetFocus(g_edit);
        return;
    case HIT_CLEAR_SEL: app.selected.clear(); break;
    case HIT_PICK_DIR: {
        std::wstring p = app.settings.outRoot;
        PickFolder(p);
        if (!p.empty()) {
            app.settings.outRoot = p;
            SaveSettings(app);
        }
        break;
    }
    case HIT_START:
        StartDownload(app);
        return;
    case HIT_OPEN_OUT:
        OpenFolder(app.settings.outRoot);
        break;
    case HIT_LATEST_JUMP:
        if (!app.latestIso.empty()) {
            Date d;
            if (ParseIsoDate(app.latestIso, d)) {
                app.SetDate(d);
                return;
            }
        }
        break;
    default:
        break;
    }
    Invalidate();
}

// ---------------------------------------------------------------- 绘制
static void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC         hdc = BeginPaint(hwnd, &ps);
    RECT        rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right - rc.left, H = rc.bottom - rc.top;
    if (W <= 0 || H <= 0) {
        EndPaint(hwnd, &ps);
        return;
    }

    static Bitmap*  buf   = nullptr;
    static Graphics* bg   = nullptr;
    if (!buf || (int)buf->GetWidth() != W || (int)buf->GetHeight() != H) {
        delete bg;
        delete buf;
        buf = new Bitmap(W, H, PixelFormat32bppPARGB);
        bg  = new Graphics(buf);
    }
    g_painting = true;
    DrawFrame(*bg, g_app, W, H);
    g_painting = false;

    Graphics screen(hdc);
    screen.SetCompositingMode(CompositingModeSourceCopy);
    screen.DrawImage(buf, 0, 0, W, H);
    EndPaint(hwnd, &ps);
}

// ---------------------------------------------------------------- 搜索框
static LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && (wp == VK_ESCAPE || wp == VK_RETURN)) {
        SetFocus(g_hwnd);
        return 0;
    }
    if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0;
    return CallWindowProcW(g_editProc, h, msg, wp, lp);
}

static void SyncSearchEdit(App& app) {
    if (!g_edit) return;
    static RectI last{0, 0, -1, -1};
    bool show = (app.nav == 0);
    ShowWindow(g_edit, show ? SW_SHOW : SW_HIDE);
    if (!show) return;
    const RectI& r = app.searchEditRect;
    if (r.x != last.x || r.y != last.y || r.w != last.w || r.h != last.h) {
        last = r;
        SetWindowPos(g_edit, nullptr, r.x, r.y, r.w, r.h, SWP_NOZORDER | SWP_NOACTIVATE);
        if (g_editFont) SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_editFont, TRUE);
    }
}

// ---------------------------------------------------------------- 窗口过程
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_hwnd = hwnd;
        g_app.hwnd = hwnd;
        g_app.dpi  = (int)GetDpiForWindow(hwnd);
        if (g_app.dpi <= 0) g_app.dpi = 96;

        // 自绘标题栏：圆角窗口
        DWORD pref = 2;  // DWMWCP_ROUND
        DwmSetWindowAttribute(hwnd, 33, &pref, sizeof(pref));

        g_editFont = CreateFontW(-MulDiv(9, g_app.dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        g_edit = CreateWindowExW(0, L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT, 0, 0, 0, 0,
                                 hwnd, (HMENU)9001, nullptr, nullptr);
        SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_editFont, TRUE);
        SendMessageW(g_edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
        g_editProc = (WNDPROC)SetWindowLongPtrW(g_edit, GWLP_WNDPROC, (LONG_PTR)EditProc);

        g_app.date = Today();
        g_app.dateReady = false;
        g_app.loading = true;
        g_app.settings.outRoot = DefaultOutRoot();
        LoadSettings(g_app);
        LoadHistory(g_app);
        EnsureDir(g_app.settings.outRoot);

        SetTimer(hwnd, 1, 400, nullptr);
        std::thread(WorkerLatest, g_app.sourceId, true).detach();
        return 0;
    }

    case WM_NCCALCSIZE:
        if (wp) return 0;
        break;

    case WM_NCHITTEST: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT  wr;
        GetWindowRect(hwnd, &wr);
        int b = g_app.S(6);
        bool maxed = IsZoomed(hwnd) != 0;
        if (!maxed) {
            bool left = pt.x < wr.left + b, right = pt.x >= wr.right - b;
            bool top = pt.y < wr.top + b, bottom = pt.y >= wr.bottom - b;
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
        }
        POINT c = pt;
        ScreenToClient(hwnd, &c);
        if (c.y >= 0 && c.y < g_app.S(th::TitleH)) {
            int id = g_app.Hit(c.x, c.y);
            if (id == HIT_TITLE_MIN || id == HIT_TITLE_CLOSE || id == HIT_TITLE_SETTINGS)
                return HTCLIENT;
            return HTCAPTION;
        }
        return HTCLIENT;
    }

    case WM_NCLBUTTONDBLCLK:
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = g_app.S(1100);
        mmi->ptMinTrackSize.y = g_app.S(700);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        Paint(hwnd);
        SyncSearchEdit(g_app);
        return 0;

    case WM_SIZE:
        Invalidate();
        return 0;

    case WM_DPICHANGED: {
        g_app.dpi = HIWORD(wp);
        RECT* r = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        ClearFontCache();
        if (g_editFont) DeleteObject(g_editFont);
        g_editFont = CreateFontW(-MulDiv(9, g_app.dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        Invalidate();
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int id = g_app.Hit(x, y);
        if (id != g_app.hover) {
            g_app.hover = id;
            Invalidate();
        }
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (g_app.hover != HIT_NONE) {
            g_app.hover = HIT_NONE;
            Invalidate();
        }
        return 0;

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        g_app.pressed = g_app.Hit(x, y);
        SetCapture(hwnd);
        return 0;
    }

    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int id = g_app.Hit(x, y);
        ReleaseCapture();
        if (id == g_app.pressed) {
            g_app.pressed = HIT_NONE;
            HandleClick(g_app, id, x, y);
        } else {
            g_app.pressed = HIT_NONE;
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        int step  = g_app.S(60) * delta / WHEEL_DELTA;
        RECT rc;
        GetClientRect(hwnd, &rc);
        int taskLeft = rc.right - g_app.S(th::TaskW);
        if (pt.x >= taskLeft) {
            g_app.selScroll -= step;
            if (g_app.selScroll < 0) g_app.selScroll = 0;
            if (g_app.selScroll > g_app.selMax) g_app.selScroll = g_app.selMax;
        } else {
            g_app.contentScroll -= step;
            if (g_app.contentScroll < 0) g_app.contentScroll = 0;
            if (g_app.contentScroll > g_app.contentMax) g_app.contentScroll = g_app.contentMax;
        }
        Invalidate();
        return 0;
    }

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            if (g_app.showCalendar) { g_app.showCalendar = false; Invalidate(); return 0; }
            if (g_app.showSettings) { g_app.showSettings = false; Invalidate(); return 0; }
        }
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id == 9001 && code == EN_CHANGE && g_edit) {
            wchar_t buf[256] = {0};
            GetWindowTextW(g_edit, buf, 256);
            g_app.query = TrimW(buf);
            g_app.contentScroll = 0;
            Invalidate();
        }
        return 0;
    }

    case WM_TIMER: {
        // 有动画时提高刷新率
        static int period = 400;
        int        want = (g_app.loading || g_app.running) ? 90 : 400;
        if (want != period) {
            period = want;
            SetTimer(hwnd, 1, period, nullptr);
        }
        if (g_app.loading || g_app.running) InvalidateRect(hwnd, nullptr, FALSE);
        bool need = false;
        if (!g_app.toast.empty() && GetTickCount64() > g_app.toastUntil) {
            g_app.toast.clear();
            need = true;
        }
        if (need) Invalidate();
        return 0;
    }

    case WM_APP_EVENT: {
        Ev* e = (Ev*)lp;
        if (!e) break;
        switch (e->kind) {
        case EvKind::Latest:
            if (e->ok) {
                g_app.latestIso = IsoDateW(e->date);
                if (e->apply) {
                    g_app.SetDate(e->date);
                    delete e;
                    return 0;
                }
                Invalidate();
            } else if (e->apply) {
                g_app.loading = false;
                g_app.pagesError = e->text.empty() ? L"无法连接站点，请检查网络" : e->text;
                g_app.dateReady = true;
                Invalidate();
            }
            break;
        case EvKind::Pages:
            if (e->gen == g_app.gen) {
                g_app.loading    = false;
                g_app.pages      = e->pages;
                g_app.pagesError = e->ok ? L"" : e->text;
                g_app.contentScroll = 0;
                if (!e->ok) g_app.selected.clear();
                Invalidate();
            }
            break;
        case EvKind::Leads:
            if (e->gen == g_app.gen) {
                g_app.pages = e->pages;
                Invalidate();
            }
            break;
        case EvKind::Log:
            if (e->gen == g_app.gen) {
                g_app.logTail = TrimW(e->text);
                Invalidate();
            }
            break;
        case EvKind::Progress:
            if (e->gen == g_app.gen) {
                g_app.progDone  = e->done;
                g_app.progTotal = e->total;
                if (e->bytes > 0) g_app.liveBytes = e->bytes;
                Invalidate();
            }
            break;
        case EvKind::Done:
            if (e->gen == g_app.gen) {
                g_app.running   = false;
                g_app.liveBytes = e->res.sizeBytes;
                g_app.progDone  = e->res.pagesTotal;
                g_app.progTotal = e->res.pagesTotal;
                if (e->ok) {
                    RecordHistory(g_app, e->res);
                    wchar_t msg[512];
                    swprintf(msg, 512, L"下载完成，%d 个版面已保存", e->res.pagesOk);
                    g_app.ShowToast(msg, 5200);
                    if (g_app.settings.openAfter) {
                        std::wstring dir = JoinPath(g_app.settings.outRoot, IsoDateW(g_app.date));
                        OpenFolder(dir);
                    }
                } else {
                    std::wstring err = e->res.error;
                    // 只保留首行，避免提示过长
                    size_t nl = err.find(L'\n');
                    if (nl != std::wstring::npos) err = err.substr(0, nl);
                    g_app.ShowToast(err.empty() ? L"下载失败" : err, 6000);
                }
                Invalidate();
            }
            break;
        }
        delete e;
        return 0;
    }

    case WM_CLOSE:
        if (g_app.running) {
            if (MessageBoxW(hwnd, L"下载仍在进行，确定要退出吗？", L"见报",
                            MB_ICONQUESTION | MB_YESNO) != IDYES)
                return 0;
            g_cancel = true;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        ClearFontCache();
        if (g_editFont) DeleteObject(g_editFont);
        if (g_gdiToken) GdiplusShutdown(g_gdiToken);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- 离屏渲染（设计校对用）
static void FillSampleData(App& app, const std::wstring& iso) {
    Date d;
    if (!iso.empty() && ParseIsoDate(iso, d)) app.date = d;
    app.dateReady = true;
    app.latestIso = IsoDateW(app.date);
    const wchar_t* names[] = {L"要闻", L"要闻", L"要闻", L"评论", L"政治", L"经济", L"社会", L"国际"};
    for (int i = 1; i <= 8; ++i) {
        PageInfo p;
        p.number = i;
        p.name = std::wstring(names[i - 1]);
        app.pages.push_back(p);
    }
    app.selected = {1, 2, 3};
}

int RenderUiPreview(const std::wstring& outPng, int w, int h, const std::wstring& iso,
                    const std::wstring& state, const std::wstring& sourceId) {
    GdiplusStartupInput in;
    if (GdiplusStartup(&g_gdiToken, &in, nullptr) != Ok) return 1;

    g_app.dpi = 96;
    if (!sourceId.empty() && FindSource(sourceId)) g_app.sourceId = sourceId;
    g_app.settings.outRoot = L"C:\\Users\\Administrator\\Downloads\\见报";
    FillSampleData(g_app, iso);
    if (g_app.pages.size() >= 1) {
        g_app.pages[0].lead = L"凝心聚力　奋进强国建设新征程";
        g_app.pages[1].lead = L"推动经济持续回升向好";
        g_app.pages[2].lead = L"以高水平开放促进互利共赢";
        g_app.pages[3].lead = L"在实干中创造新的时代荣光";
    }

    if (state == L"history") {
        g_app.nav = 1;
        std::wstring base = g_app.settings.outRoot + L"\\2026-11-03\\人民日报_2026-11-03.pdf";
        g_app.history.push_back(HistoryItem{L"2026-11-03", L"今天 09:41", base, 3, 8 * 1024LL * 1024});
        g_app.history.push_back(HistoryItem{L"2026-11-02", L"昨天 20:16", base, 20,
                                            68 * 1024LL * 1024});
        g_app.history.push_back(HistoryItem{L"2026-10-28", L"10月28日 08:32", base, 1,
                                            3 * 1024LL * 1024});
    } else if (state == L"settings") {
        g_app.showSettings = true;
    } else if (state == L"calendar") {
        g_app.showCalendar = true;
        g_app.calMonth = Date{g_app.date.y, g_app.date.m, 1};
    } else if (state == L"loading") {
        g_app.pages.clear();
        g_app.selected.clear();
        g_app.loading = true;
    } else if (state == L"error") {
        g_app.pages.clear();
        g_app.selected.clear();
        g_app.loading = false;
        g_app.pagesError = L"该日期没有报纸（404）\n提示：站点当前最新可下载日期为 2026-11-05。";
    } else if (state == L"list") {
        g_app.view = 1;
    } else if (state == L"running") {
        g_app.running = true;
        g_app.progDone = 3;
        g_app.progTotal = 8;
        g_app.liveBytes = 6 * 1024LL * 1024 + 300 * 1024;
    }

    int rc = 1;
    {
        Bitmap   bmp(w, h, PixelFormat32bppPARGB);
        Graphics g(&bmp);
        DrawFrame(g, g_app, w, h);
        CLSID png;
        if (GetEncoderClsid(L"image/png", &png) >= 0 && bmp.Save(outPng.c_str(), &png, nullptr) == Ok)
            rc = 0;
    }
    ClearFontCache();
    GdiplusShutdown(g_gdiToken);
    g_gdiToken = 0;
    return rc;
}

// ---------------------------------------------------------------- 入口
int RunGui() {
    HINSTANCE inst = GetModuleHandleW(nullptr);

    GdiplusStartupInput gi;
    if (GdiplusStartup(&g_gdiToken, &gi, nullptr) != Ok) return 1;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"JianBaoWindow";
    wc.hIcon = (HICON)LoadImageW(inst, L"APPICON", IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    int w = MulDiv(1280, 96, 96), h = MulDiv(860, 96, 96);
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    if (w > sw - 80) w = sw - 80;
    if (h > sh - 80) h = sh - 80;

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"见报 · 电子报纸下载器",
                                WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                                (sw - w) / 2, (sh - h) / 2, w, h, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
