// job.cpp —— 下载、合并与命名
#include "job.h"

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <thread>

std::atomic<bool> g_cancel{false};

std::wstring HumanSize(long long bytes) {
    wchar_t buf[64];
    if (bytes >= 1024LL * 1024)
        swprintf(buf, 64, L"%.2f MB", bytes / 1024.0 / 1024.0);
    else if (bytes >= 1024)
        swprintf(buf, 64, L"%.0f KB", bytes / 1024.0);
    else
        swprintf(buf, 64, L"%lld B", bytes);
    return buf;
}

std::wstring PageRangeLabel(const std::vector<int>& numbers) {
    if (numbers.empty()) return L"";
    std::vector<int> v = numbers;
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());

    std::wstring out;
    for (size_t i = 0; i < v.size();) {
        size_t j = i;
        while (j + 1 < v.size() && v[j + 1] == v[j] + 1) ++j;
        wchar_t buf[32];
        if (j > i)
            swprintf(buf, 32, L"%02d-%02d", v[i], v[j]);
        else
            swprintf(buf, 32, L"%02d", v[i]);
        if (!out.empty()) out += L",";
        out += buf;
        i = j + 1;
    }
    return out;
}

std::wstring BuildOutputName(const std::wstring& paper, const Date& date,
                             const std::vector<int>& pick, bool merge) {
    std::wstring base = paper + L"_" + IsoDateW(date);
    if (pick.empty()) return merge ? base + L".pdf" : base;
    if (merge) return base + L"_" + PageRangeLabel(pick) + L"版.pdf";
    return base;
}

// 取文件字节数
static long long FileSizeOf(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER li;
    long long     sz = 0;
    if (GetFileSizeEx(h, &li)) sz = li.QuadPart;
    CloseHandle(h);
    return sz;
}

// 探测最新一期并生成中文失败提示
static std::wstring LatestHint(const SourceInfo& src, const Date& date, bool forbidden) {
    Date         latest;
    std::wstring err;
    if (!FindLatestDate(src, latest, err, nullptr)) return L"";
    if (CompareDate(latest, date) == 0) return L"";
    if (forbidden)
        return L"\n  提示：本站仅公开最近约两年的电子版，更早的日期无法下载。当前最新一期为 " +
               IsoDateW(latest) + L"。";
    return L"\n  提示：本站当前最新可下载日期为 " + IsoDateW(latest) + L"，可改选该日期后重试。";
}

bool RunJob(const JobOptions& opt, const LogFn& log, const ProgressFn& progress,
            JobResult& result) {
    result = JobResult();
    g_cancel = false;

    const SourceInfo& src = FindSource(opt.sourceId) ? *FindSource(opt.sourceId) : DefaultSource();

    auto emit = [&](const std::wstring& s) { if (log) log(s); };

    emit(L"■ " + src.name + L" · " + DisplayDate(opt.date) + L"  检索版面 …");
    std::vector<PageInfo> pages;
    std::wstring          err;
    FetchResult fr = FetchPageList(src, opt.date, pages, err);
    if (fr != FetchResult::Ok) {
        if (fr == FetchResult::NotFound || fr == FetchResult::Forbidden)
            err += LatestHint(src, opt.date, fr == FetchResult::Forbidden);
        result.error = err;
        emit(L"✗ " + err);
        return false;
    }

    // 按选择的版号过滤
    if (!opt.pick.empty()) {
        std::vector<PageInfo> filtered;
        for (auto& p : pages) {
            if (std::find(opt.pick.begin(), opt.pick.end(), p.number) != opt.pick.end())
                filtered.push_back(p);
        }
        if (filtered.empty()) {
            result.error = L"所选版面在该日期不存在";
            emit(L"✗ " + result.error);
            return false;
        }
        pages.swap(filtered);
    }

    result.pagesTotal = (int)pages.size();
    std::wstring numbers;
    for (size_t i = 0; i < pages.size(); ++i) {
        if (i) numbers += L"、";
        wchar_t nb[8];
        swprintf(nb, 8, L"%02d", pages[i].number);
        numbers += nb;
    }
    emit(L"  共 " + std::to_wstring(pages.size()) + L" 版：" + numbers);

    if (opt.infoOnly) {
        for (auto& p : pages) {
            wchar_t nb[8];
            swprintf(nb, 8, L"%02d", p.number);
            emit(L"    " + std::wstring(nb) + L"版  " + p.name);
        }
        result.ok = true;
        return true;
    }

    if (!EnsureDir(opt.outDir)) {
        result.error = L"无法创建保存目录：" + opt.outDir;
        emit(L"✗ " + result.error);
        return false;
    }

    const bool wholeIssue = opt.pick.empty();
    std::wstring outPdf = JoinPath(
        opt.outDir, BuildOutputName(src.name, opt.date, wholeIssue ? std::vector<int>() : opt.pick,
                                    true));

    if (opt.merge && wholeIssue && FileExists(outPdf)) {
        result.ok = true;
        result.skipped = true;
        result.outPdf = outPdf;
        result.outFiles.push_back(outPdf);
        result.sizeBytes = FileSizeOf(outPdf);
        emit(L"  已存在，跳过：" + outPdf);
        return true;
    }

    std::wstring tempDir = JoinPath(JoinPath(opt.outDir, L".pages"), IsoDateW(opt.date));
    if (!EnsureDir(tempDir)) {
        result.error = L"无法创建临时目录：" + tempDir;
        emit(L"✗ " + result.error);
        return false;
    }

    std::wstring dates   = IsoDateW(opt.date);  // YYYY-MM-DD
    std::wstring referer = RefererFor(src, opt.date);

    // 解析各版 PDF 直链
    std::vector<PageInfo> ready;
    for (auto& p : pages) {
        if (g_cancel) break;
        std::wstring e;
        if (ResolvePdfUrl(src, opt.date, p, e))
            ready.push_back(p);
        else
            emit(L"  ! " + e);
    }

    // 并发下载
    std::atomic<int>          next{0};
    std::atomic<int>          done{0};
    std::atomic<long long>    gotBytes{0};
    std::mutex                mtx;
    std::vector<bool>         ok(ready.size(), false);
    std::vector<std::wstring> errors(ready.size());

    auto worker = [&]() {
        while (!g_cancel) {
            int i = next.fetch_add(1);
            if (i >= (int)ready.size()) break;
            PageInfo& p = ready[i];
            wchar_t   name[32];
            swprintf(name, 32, L"page_%02d.pdf", p.number);
            std::wstring dest = JoinPath(tempDir, name);
            std::wstring e;
            bool         good = DownloadPdf(p, dest, referer, e);
            long long    sz = good ? FileSizeOf(dest) : 0;
            {
                std::lock_guard<std::mutex> lock(mtx);
                ok[i] = good;
                errors[i] = e;
            }
            int d = done.fetch_add(1) + 1;
            long long sum = gotBytes.fetch_add(sz) + sz;
            if (progress) progress(d, (int)ready.size(), p.name, sum);
            wchar_t nb[8];
            swprintf(nb, 8, L"%02d", p.number);
            if (good) {
                emit(L"  [" + std::to_wstring(d) + L"/" + std::to_wstring(ready.size()) + L"] " +
                     nb + L"版 " + p.name + L" ✓ " + HumanSize(sz));
            } else {
                emit(L"  [" + std::to_wstring(d) + L"/" + std::to_wstring(ready.size()) + L"] " +
                     nb + L"版 下载失败：" + e);
            }
        }
    };

    int threadCount = opt.workers < 1 ? 1 : opt.workers;
    if (threadCount > (int)ready.size()) threadCount = (int)ready.size();
    std::vector<std::thread> pool;
    for (int i = 0; i < threadCount; ++i) pool.emplace_back(worker);
    for (auto& t : pool) t.join();

    if (g_cancel) {
        DeleteDirRecursive(tempDir);
        result.error = L"已取消";
        return false;
    }

    std::vector<std::wstring> files;
    std::vector<int>          okNumbers;
    for (size_t i = 0; i < ready.size(); ++i) {
        if (ok[i]) {
            wchar_t name[32];
            swprintf(name, 32, L"page_%02d.pdf", ready[i].number);
            files.push_back(JoinPath(tempDir, name));
            okNumbers.push_back(ready[i].number);
            result.pagesOk++;
        }
    }
    if (files.empty()) {
        result.error = L"所有版面均下载失败，未生成文件";
        emit(L"✗ " + result.error);
        DeleteDirRecursive(tempDir);
        return false;
    }
    if (result.pagesOk < result.pagesTotal) {
        std::wstring miss;
        for (size_t i = 0; i < ready.size(); ++i) {
            if (!ok[i]) {
                wchar_t nb[16];
                swprintf(nb, 16, L"%02d版 ", ready[i].number);
                miss += nb;
            }
        }
        emit(L"  ⚠ 缺失版面：" + miss + L"（合并文件为不完整版）");
    }

    if (opt.merge) {
        emit(L"  合并 " + std::to_wstring(files.size()) + L" 版 → " +
             BuildOutputName(src.name, opt.date, wholeIssue ? std::vector<int>() : okNumbers, true) +
             L" …");
        std::wstring mergeErr;
        if (!MergePdfs(files, outPdf, src.name + L" " + DisplayDate(opt.date), mergeErr)) {
            result.error = mergeErr;
            emit(L"✗ " + mergeErr);
            DeleteDirRecursive(tempDir);
            return false;
        }
        result.outPdf = outPdf;
        result.outFiles.push_back(outPdf);
        result.sizeBytes = FileSizeOf(outPdf);
    } else {
        // 逐版另存，保留单版文件
        for (size_t i = 0; i < files.size(); ++i) {
            wchar_t name[128];
            swprintf(name, 128, L"%s_%s_%02d版.pdf", src.name.c_str(), dates.c_str(), okNumbers[i]);
            std::wstring dest = JoinPath(opt.outDir, name);
            CopyFileW(files[i].c_str(), dest.c_str(), FALSE);
            result.outFiles.push_back(dest);
            result.sizeBytes += FileSizeOf(dest);
        }
    }

    if (!opt.keepPages) {
        DeleteDirRecursive(tempDir);
        RemoveDirectoryW(JoinPath(opt.outDir, L".pages").c_str());
    }

    result.ok = true;
    emit(L"✓ 已生成 " + std::to_wstring(result.pagesOk) + L" 个版面，共 " +
         HumanSize(result.sizeBytes));
    return true;
}
