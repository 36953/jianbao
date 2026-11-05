// job.h —— 下载与合并任务（界面与控制台共用）
#pragma once

#include "core.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

struct JobOptions {
    std::wstring     sourceId = L"rmrb";  // rmrb / ce / fzrb / dzrb
    Date             date;
    std::wstring     outDir;      // 最终保存目录
    std::vector<int> pick;        // 指定版号；为空表示整份报纸
    bool             merge = true;// false 时逐版分别保存
    bool             infoOnly = false;
    bool             keepPages = false;
    int              workers = 4;
};

struct JobResult {
    bool         ok = false;
    bool         skipped = false;
    std::wstring error;
    std::wstring outPdf;      // 合并模式的成品路径
    std::vector<std::wstring> outFiles;  // 实际生成的每个文件
    int          pagesOk = 0;
    int          pagesTotal = 0;
    long long    sizeBytes = 0;
};

using LogFn      = std::function<void(const std::wstring&)>;
// bytes 为已完成部分的累计字节数，供界面实时显示
using ProgressFn =
    std::function<void(int done, int total, const std::wstring& pageName, long long bytes)>;

extern std::atomic<bool> g_cancel;

bool         RunJob(const JobOptions& opt, const LogFn& log, const ProgressFn& progress,
                    JobResult& result);
std::wstring HumanSize(long long bytes);

// 把版号序列压缩成 "05-08,12" 形式
std::wstring PageRangeLabel(const std::vector<int>& numbers);

// 生成输出文件名（不含目录）
std::wstring BuildOutputName(const std::wstring& paper, const Date& date,
                             const std::vector<int>& pick, bool merge);
