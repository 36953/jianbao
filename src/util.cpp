// util.cpp —— 字符串、日期、文件基础工具
#include "core.h"

#include <winhttp.h>
#include <shlobj.h>

#include <cstdio>
#include <cstring>
#include <cwchar>

// ---------------------------------------------------------------- 字符串
std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n);
    return out;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n, nullptr, nullptr);
    return out;
}

std::wstring TrimW(const std::wstring& s) {
    size_t b = 0, e = s.size();
    auto space = [](wchar_t c) {
        return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0x3000;
    };
    while (b < e && space(s[b])) ++b;
    while (e > b && space(s[e - 1])) --e;
    return s.substr(b, e - b);
}

// ---------------------------------------------------------------- 日期
static long long DaysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const int      era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (long long)era * 146097LL + (long long)doe - 719468LL;
}

static void CivilFromDays(long long z, int& y, int& m, int& d) {
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned  doe = (unsigned)(z - era * 146097);
    const unsigned  yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int       yy  = (int)yoe + (int)era * 400;
    const unsigned  doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned  mp  = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp + (mp < 10 ? 3 : -9));
    y = yy + (m <= 2);
}

Date Today() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    Date d;
    d.y = st.wYear;
    d.m = st.wMonth;
    d.d = st.wDay;
    return d;
}

Date AddDays(const Date& base, int days) {
    long long z = DaysFromCivil(base.y, base.m, base.d) + days;
    Date out;
    CivilFromDays(z, out.y, out.m, out.d);
    return out;
}

int CompareDate(const Date& a, const Date& b) {
    if (a.y != b.y) return a.y < b.y ? -1 : 1;
    if (a.m != b.m) return a.m < b.m ? -1 : 1;
    if (a.d != b.d) return a.d < b.d ? -1 : 1;
    return 0;
}

int DaysBetween(const Date& a, const Date& b) {
    return (int)(DaysFromCivil(b.y, b.m, b.d) - DaysFromCivil(a.y, a.m, a.d));
}

std::string IsoDate(const Date& d) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", d.y, d.m, d.d);
    return buf;
}

std::wstring IsoDateW(const Date& d) {
    return Utf8ToWide(IsoDate(d));
}

std::wstring DisplayDate(const Date& d) {
    wchar_t buf[64];
    swprintf(buf, 64, L"%04d年%02d月%02d日", d.y, d.m, d.d);
    return buf;
}

bool ParseIsoDate(const std::wstring& text, Date& out) {
    int y = 0, m = 0, d = 0;
    if (swscanf(text.c_str(), L"%d-%d-%d", &y, &m, &d) != 3) return false;
    if (y < 1990 || y > 2100 || m < 1 || m > 12 || d < 1 || d > 31) return false;
    out.y = y;
    out.m = m;
    out.d = d;
    return true;
}

// ---------------------------------------------------------------- 文件
std::wstring ExeDir() {
    wchar_t buf[MAX_PATH * 2];
    DWORD   n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    std::wstring path(buf, n);
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring(L".") : path.substr(0, pos);
}

std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    wchar_t last = a[a.size() - 1];
    if (last == L'\\' || last == L'/') return a + b;
    return a + L"\\" + b;
}

bool ReadFileBytes(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0) {
        CloseHandle(h);
        return false;
    }
    out.resize((size_t)size.QuadPart);
    DWORD read = 0, total = 0;
    while (total < out.size()) {
        if (!ReadFile(h, &out[total], (DWORD)(out.size() - total), &read, nullptr) || read == 0) break;
        total += read;
    }
    CloseHandle(h);
    out.resize(total);
    return total == size.QuadPart;
}

bool WriteFileBytes(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0, total = 0;
    const char* p = data.data();
    while (total < data.size()) {
        if (!WriteFile(h, p + total, (DWORD)(data.size() - total), &written, nullptr) || written == 0) break;
        total += written;
    }
    CloseHandle(h);
    return total == data.size();
}

bool EnsureDir(const std::wstring& path) {
    wchar_t full[MAX_PATH * 2];
    GetFullPathNameW(path.c_str(), MAX_PATH * 2, full, nullptr);
    std::wstring abs(full);

    DWORD attr = GetFileAttributesW(abs.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    int r = SHCreateDirectoryExW(nullptr, abs.c_str(), nullptr);
    return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS;
}

bool FileExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES;
}

bool DeleteDirRecursive(const std::wstring& path) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((path + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            std::wstring child = JoinPath(path, name);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                DeleteDirRecursive(child);
            } else {
                DeleteFileW(child.c_str());
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return RemoveDirectoryW(path.c_str()) != 0;
}

// ---------------------------------------------------------------- HTTP
static std::wstring CrackHostPath(const std::wstring& url, std::wstring& host, INTERNET_PORT& port,
                                  bool& secure) {
    URL_COMPONENTS uc;
    ZeroMemory(&uc, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    wchar_t h[256] = {0}, path[4096] = {0};
    uc.lpszHostName     = h;
    uc.dwHostNameLength = 255;
    uc.lpszUrlPath      = path;
    uc.dwUrlPathLength  = 4095;
    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &uc)) return std::wstring();
    port   = uc.nPort;
    secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    host   = h;
    std::wstring full(path);
    if (full.empty()) full = L"/";
    return full;
}

HttpResponse HttpGet(const std::wstring& url, const std::wstring& referer, int timeoutMs) {
    HttpResponse res;

    INTERNET_PORT port   = 0;
    bool          secure = false;
    std::wstring  host;
    std::wstring  path = CrackHostPath(url, host, port, secure);
    if (path.empty() || host.empty()) {
        res.error = L"URL 解析失败：" + url;
        return res;
    }

    HINTERNET session = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0 Safari/537.36",
                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) session = WinHttpOpen(L"JianBao/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        res.error = L"无法初始化网络会话";
        return res;
    }
    WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    HINTERNET connect = WinHttpConnect(session, host.c_str(), port, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        res.error = L"无法连接 " + host;
        return res;
    }

    DWORD flags = secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        res.error = L"无法创建请求";
        return res;
    }

    if (!referer.empty())
        WinHttpAddRequestHeaders(request, (L"Referer: " + referer).c_str(), (DWORD)-1L,
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    WinHttpAddRequestHeaders(request, L"Accept: */*", (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);

    BOOL ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) ok = WinHttpReceiveResponse(request, nullptr);
    if (!ok) {
        DWORD err = GetLastError();
        res.error = L"网络请求失败（错误码 " + std::to_wstring(err) + L"）";
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return res;
    }

    DWORD status = 0, len = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
    res.status = (int)status;

    std::string body;
    DWORD avail = 0;
    do {
        avail = 0;
        if (!WinHttpQueryDataAvailable(request, &avail)) break;
        if (avail == 0) break;
        size_t old = body.size();
        body.resize(old + avail);
        DWORD read = 0;
        if (!WinHttpReadData(request, &body[old], avail, &read)) {
            body.resize(old);
            break;
        }
        body.resize(old + read);
        if (read == 0) break;
    } while (avail > 0);

    res.body.swap(body);
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return res;
}
