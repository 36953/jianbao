// main.cpp —— 见报 · 电子报纸下载器（C++ / Win32 + GDI+ 原生实现）
//
// 运行方式：
//   1) 双击运行           → 打开窗口界面
//   2) --date YYYY-MM-DD  → 控制台下载指定日期
//   3) --latest           → 控制台下载最新一期
//   4) --render-ui a.png  → 离屏渲染界面截图（设计校对用）
#include "core.h"
#include "job.h"
#include "ui.h"

#include <shellapi.h>

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

// ---------------------------------------------------------------- 控制台输出
static HANDLE g_conOut      = INVALID_HANDLE_VALUE;
static bool   g_conIsConsole = false;

static void InitConsoleIO() {
    SetConsoleOutputCP(CP_UTF8);
    HANDLE h    = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD  type = (h && h != INVALID_HANDLE_VALUE) ? GetFileType(h) : FILE_TYPE_UNKNOWN;
    if (type == FILE_TYPE_CHAR) {
        g_conOut      = h;
        g_conIsConsole = true;
        return;
    }
    if (type == FILE_TYPE_PIPE || type == FILE_TYPE_DISK) {
        g_conOut      = h;
        g_conIsConsole = false;
        return;
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        HANDLE c = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                               0, nullptr);
        if (c != INVALID_HANDLE_VALUE) {
            g_conOut      = c;
            g_conIsConsole = true;
        }
    }
}

static void Out(const std::wstring& s) {
    if (g_conOut == INVALID_HANDLE_VALUE || s.empty()) return;
    DWORD written = 0;
    if (g_conIsConsole) {
        WriteConsoleW(g_conOut, s.c_str(), (DWORD)s.size(), &written, nullptr);
    } else {
        std::string u = WideToUtf8(s);
        WriteFile(g_conOut, u.data(), (DWORD)u.size(), &written, nullptr);
    }
}

static void OutLine(const std::wstring& s) { Out(s + L"\n"); }

static std::wstring Fmt(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vswprintf(buf, 2048, fmt, ap);
    va_end(ap);
    return buf;
}

// ---------------------------------------------------------------- 控制台模式
static int ConsoleMain(const std::vector<std::wstring>& args) {
    InitConsoleIO();

    JobOptions   opt;
    bool         latest = false, hasDate = false, help = false, listSources = false;
    opt.outDir = JoinPath(ExeDir(), L"报纸输出");

    for (size_t i = 0; i < args.size(); ++i) {
        const std::wstring& a = args[i];
        if (a == L"--source" && i + 1 < args.size()) {
            std::wstring v = args[++i];
            const SourceInfo* hit = nullptr;
            for (auto& s : AllSources())
                if (s.id == v || s.name == v) hit = &s;
            if (!hit) {
                OutLine(L"未知报纸来源：" + v + L"（可用 --sources 查看）");
                return 2;
            }
            opt.sourceId = hit->id;
        } else if (a == L"--sources") {
            listSources = true;
        } else if (a == L"--date" && i + 1 < args.size()) {
            if (!ParseIsoDate(args[++i], opt.date)) {
                OutLine(Fmt(L"日期格式错误：%s（示例 2024-10-09）", args[i].c_str()));
                return 2;
            }
            hasDate = true;
        } else if (a == L"--latest") {
            latest = true;
        } else if (a == L"--out" && i + 1 < args.size()) {
            opt.outDir = args[++i];
        } else if (a == L"--pages" && i + 1 < args.size()) {
            // 例如 --pages 1,2,5-8
            std::wstring spec = args[++i];
            std::wstring cur;
            spec += L',';
            for (wchar_t c : spec) {
                if (c == L',' || c == L'，') {
                    if (!cur.empty()) {
                        size_t dash = cur.find(L'-');
                        if (dash == std::wstring::npos) {
                            opt.pick.push_back(_wtoi(cur.c_str()));
                        } else {
                            int a1 = _wtoi(cur.substr(0, dash).c_str());
                            int a2 = _wtoi(cur.substr(dash + 1).c_str());
                            for (int p = a1; p <= a2; ++p) opt.pick.push_back(p);
                        }
                        cur.clear();
                    }
                } else {
                    cur.push_back(c);
                }
            }
        } else if (a == L"--info") {
            opt.infoOnly = true;
        } else if (a == L"--keep") {
            opt.keepPages = true;
        } else if (a == L"--help" || a == L"-h" || a == L"/?") {
            help = true;
        }
    }

    if (listSources) {
        OutLine(L"可用报纸来源：");
        for (auto& s : AllSources())
            OutLine(L"  " + s.id + L"\t" + s.name + L"\t" + s.note);
        return 0;
    }

    if (help) {
        OutLine(L"见报 · 电子报纸下载器（命令行模式）");
        OutLine(L"");
        OutLine(L"  --source ID         报纸来源，见 --sources（默认 rmrb）");
        OutLine(L"  --sources           列出已接入的报纸来源");
        OutLine(L"  --date YYYY-MM-DD   指定日期");
        OutLine(L"  --latest            自动探测并下载最新一期");
        OutLine(L"  --pages 1,2,5-8     只下载指定版面");
        OutLine(L"  --info              仅列出当日版面");
        OutLine(L"  --out DIR           输出目录（默认：程序目录\\报纸输出）");
        OutLine(L"  --keep              保留分版临时文件");
        OutLine(L"  不带参数运行则打开窗口界面");
        return 0;
    }

    if (!hasDate && !latest) {
        OutLine(L"请指定 --date 或 --latest（不带参数运行可打开窗口界面）");
        return 2;
    }

    const SourceInfo& src = FindSource(opt.sourceId) ? *FindSource(opt.sourceId) : DefaultSource();

    if (latest) {
        OutLine(L"正在探测" + src.name + L"当前可下载的最新一期 …");
        std::wstring err;
        if (!FindLatestDate(src, opt.date, err, nullptr)) {
            OutLine(L"✗ " + err);
            return 1;
        }
        OutLine(L"最新一期：" + IsoDateW(opt.date));
    }

    if (!EnsureDir(opt.outDir)) {
        OutLine(L"✗ 无法创建输出目录：" + opt.outDir);
        return 1;
    }

    JobResult res;
    RunJob(opt, [](const std::wstring& s) { OutLine(s); },
           [](int d, int t, const std::wstring&, long long) { Out(Fmt(L"%d/%d\r", d, t)); }, res);

    OutLine(L"");
    if (!res.ok) return 1;
    if (opt.infoOnly) return 0;
    if (res.skipped) {
        OutLine(L"– 已存在，未重复下载：" + res.outPdf);
        return 0;
    }
    OutLine(Fmt(L"✓ %s %d/%d 版  %s", src.name.c_str(), res.pagesOk, res.pagesTotal,
                HumanSize(res.sizeBytes).c_str()));
    for (auto& f : res.outFiles) OutLine(L"  " + f);
    return 0;
}

// ---------------------------------------------------------------- 入口
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int     argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    LocalFree(argv);

    if (args.empty()) {
        // 启用 Per-Monitor DPI 感知（失败则退化为系统缩放）
        using SetCtxFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
            auto fn = (SetCtxFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
            if (fn) fn((DPI_AWARENESS_CONTEXT)-4);  // PER_MONITOR_AWARE_V2
        }
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        int rc = RunGui();
        CoUninitialize();
        return rc;
    }

    // 离屏渲染：--render-ui out.png [宽 高] [YYYY-MM-DD]
    if (args[0] == L"--render-ui") {
        std::wstring outPng = args.size() > 1 ? args[1] : L"ui.png";
        int          w = args.size() > 2 ? _wtoi(args[2].c_str()) : 1280;
        int          h = args.size() > 3 ? _wtoi(args[3].c_str()) : 860;
        std::wstring iso   = args.size() > 4 ? args[4] : L"";
        std::wstring state = args.size() > 5 ? args[5] : L"download";
        std::wstring srcId = args.size() > 6 ? args[6] : L"";
        InitConsoleIO();
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        int rc = RenderUiPreview(outPng, w, h, iso, state, srcId);
        CoUninitialize();
        OutLine(rc == 0 ? L"✓ 已渲染 " + outPng : L"✗ 渲染失败");
        return rc;
    }

    return ConsoleMain(args);
}
