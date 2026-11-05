// core.h —— 公共声明：日期、字符串、文件、HTTP、站点解析、PDF 合并
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <functional>

// ---------------------------------------------------------------- 版本
// 与 app.rc 中的 FileVersion 保持一致（改版本时两处同改）
extern const wchar_t* const kAppVersion;

// ---------------------------------------------------------------- 字符串
std::wstring Utf8ToWide(const std::string& s);
std::string  WideToUtf8(const std::wstring& s);
std::wstring TrimW(const std::wstring& s);

// ---------------------------------------------------------------- 日期
struct Date {
    int y = 0, m = 0, d = 0;
};

Date        Today();
Date        AddDays(const Date& base, int days);
int         CompareDate(const Date& a, const Date& b);
int         DaysBetween(const Date& a, const Date& b);   // b - a
std::string IsoDate(const Date& d);                       // 2024-10-09
std::wstring IsoDateW(const Date& d);
std::wstring DisplayDate(const Date& d);                  // 2024年10月09日
bool        ParseIsoDate(const std::wstring& text, Date& out);

// ---------------------------------------------------------------- 文件
std::wstring ExeDir();
std::wstring JoinPath(const std::wstring& a, const std::wstring& b);
bool         ReadFileBytes(const std::wstring& path, std::string& out);
bool         WriteFileBytes(const std::wstring& path, const std::string& data);
bool         EnsureDir(const std::wstring& path);
bool         FileExists(const std::wstring& path);
bool         DeleteDirRecursive(const std::wstring& path);

// ---------------------------------------------------------------- HTTP
struct HttpResponse {
    int          status = 0;          // HTTP 状态码；0 表示网络层失败
    std::string  body;
    std::wstring error;
};

HttpResponse HttpGet(const std::wstring& url, const std::wstring& referer, int timeoutMs = 30000);

// ---------------------------------------------------------------- 报纸来源
// 每个来源对应一家提供「按日、按版、可直链下载 PDF」的电子报站点。
// 目前接入的四家均为公开免费、无需登录的数字报平台。
struct SourceInfo {
    std::wstring id;       // rmrb / ce / fzrb / dzrb
    std::wstring name;     // 人民日报
    std::wstring eyebrow;  // 页头西文小标（PEOPLE'S DAILY）
    std::wstring note;     // 归属说明（人民网 · 图文数据库）
    std::wstring home;     // 站点首页
};

const std::vector<SourceInfo>& AllSources();
const SourceInfo*              FindSource(const std::wstring& id);
const SourceInfo&              DefaultSource();

// 下载时携带的 Referer
std::wstring RefererFor(const SourceInfo& src, const Date& date);

struct PageInfo {
    int          number = 0;
    std::wstring name;      // 原始版名，形如“05版：评论”或“评论”
    std::wstring pageUrl;   // 该版页面地址（新旧版式不同）
    std::wstring pdfUrl;
    std::wstring lead;      // 本版头条标题（新版式可解析，旧版式为空）
    long long    bytes = 0; // 下载后回填，供界面展示
};

enum class FetchResult { Ok, NotFound, Forbidden, Network };

// 抓取某日第 01 版并解析出全部版面；失败时 err 给出中文原因
FetchResult FetchPageList(const SourceInfo& src, const Date& date, std::vector<PageInfo>& pages,
                          std::wstring& err);

// 解析某版页面中的 PDF 直链（列表阶段已解析到则直接返回）
bool ResolvePdfUrl(const SourceInfo& src, const Date& date, PageInfo& page, std::wstring& err);

// 抓取某版页面，同时回填 pdfUrl 与头条标题 lead（界面列表用）
bool FetchPageDetail(const SourceInfo& src, const Date& date, PageInfo& page, std::wstring& err);

// 下载单个 PDF 到本地；带重试与 %PDF- 校验
bool DownloadPdf(const PageInfo& page, const std::wstring& destPath,
                 const std::wstring& referer, std::wstring& err);

// 探测某日是否有报纸：返回 HTTP 状态码（0 = 网络错误）
int ProbeDate(const SourceInfo& src, const Date& date);

// 探测当前最新可下载日期
bool FindLatestDate(const SourceInfo& src, Date& out, std::wstring& err,
                    const std::function<void(const std::wstring&)>& log);

// ---------------------------------------------------------------- PDF 合并
// 将多个单页 PDF 按顺序合并为一个 PDF（自研实现，不依赖第三方库）
bool MergePdfs(const std::vector<std::wstring>& files, const std::wstring& outPath,
               const std::wstring& title, std::wstring& err);
