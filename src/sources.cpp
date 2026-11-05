// sources.cpp —— 各报纸电子版站点的版面解析与 PDF 直链定位
//
// 已接入四家公开、免登录、按日提供 PDF 的数字报：
//   人民日报  paper.people.com.cn/rmrb
//             新版 /pc/layout/YYYYMM/DD/node_NN.html        PDF /pc/attachement/YYYYMM/DD/<guid>.pdf
//             旧版 /html/YYYY-MM/DD/nbs.D110000renmrb_NN.htm PDF /images/YYYY-MM/DD/NN/rmrbYYYYMMDDNN.pdf
//   经济日报  paper.ce.cn/pc/layout/YYYYMM/DD/node_NN.html  PDF /pc/attachment/YYYYMM/DD/<guid>.pdf
//   法治日报  epaper.legaldaily.com.cn/fzrb/content/YYYYMMDD/PageNNTB.htm
//             PDF /fzrb/PDF/YYYYMMDD/NN.pdf（可推导）
//   大众日报  paper.dzwww.com/dzrb/content/YYYYMMDD/PageNNNU.htm
//             PDF /dzrb/PDF/YYYYMMDD/NN.pdf（可推导）
#include "core.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>

// ---------------------------------------------------------------- 来源表
const std::vector<SourceInfo>& AllSources() {
    static const std::vector<SourceInfo> table = {
        {L"rmrb", L"人民日报", L"PEOPLE'S DAILY", L"人民网 · 图文数据库",
         L"https://paper.people.com.cn/rmrb/"},
        {L"ce", L"经济日报", L"ECONOMIC DAILY", L"中国经济网 · 数字报", L"http://paper.ce.cn/"},
        {L"fzrb", L"法治日报", L"LEGAL DAILY", L"法治网 · 数字报",
         L"http://epaper.legaldaily.com.cn/fzrb/"},
        {L"dzrb", L"大众日报", L"DAZHONG DAILY", L"大众网 · 数字报",
         L"http://paper.dzwww.com/dzrb/"},
    };
    return table;
}

const SourceInfo* FindSource(const std::wstring& id) {
    for (auto& s : AllSources())
        if (s.id == id) return &s;
    return nullptr;
}

const SourceInfo& DefaultSource() { return AllSources().front(); }

// ---------------------------------------------------------------- 小工具
static std::wstring FmtYmd8(const Date& d) {
    wchar_t b[16];
    swprintf(b, 16, L"%04d%02d%02d", d.y, d.m, d.d);
    return b;
}

static std::wstring FmtYm(const Date& d) {
    wchar_t b[16];
    swprintf(b, 16, L"%04d%02d", d.y, d.m);
    return b;
}

static bool EndsWithCI(const std::string& s, const char* suf) {
    size_t n = strlen(suf);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i)
        if (tolower((unsigned char)s[s.size() - n + i]) != tolower((unsigned char)suf[i]))
            return false;
    return true;
}

// 不区分大小写的查找（页面里标签大小写混用很常见）
static size_t FindCI(const std::string& hay, const char* needle, size_t from) {
    size_t n = strlen(needle);
    if (n == 0 || hay.size() < n || from > hay.size() - n) return std::string::npos;
    for (size_t i = from; i + n <= hay.size(); ++i) {
        size_t k = 0;
        while (k < n && tolower((unsigned char)hay[i + k]) == tolower((unsigned char)needle[k])) ++k;
        if (k == n) return i;
    }
    return std::string::npos;
}

// 去掉标签并归一化空白，用于取版名与标题
static std::wstring NormText(const std::string& html) {
    std::string text;
    text.reserve(html.size());
    bool inTag = false;
    for (char c : html) {
        if (c == '<') { inTag = true; continue; }
        if (c == '>') { inTag = false; continue; }
        if (!inTag) text.push_back(c);
    }
    std::wstring w = Utf8ToWide(text);
    std::wstring out;
    bool         space = false;
    for (wchar_t c : w) {
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0x3000 || c == 0x00A0) {
            space = true;
        } else {
            if (space && !out.empty()) out.push_back(L' ');
            space = false;
            out.push_back(c);
        }
    }
    return TrimW(out);
}

// 「第05版：评论」「05版:评论」「第 05版:评论」→「评论」；无冒号时原样返回
static std::wstring CleanSection(const std::wstring& raw) {
    std::wstring t = TrimW(raw);
    size_t       p = t.rfind(L'版');
    if (p != std::wstring::npos) {
        size_t c = t.find(L'：', p);
        if (c == std::wstring::npos) c = t.find(L':', p);
        if (c != std::wstring::npos) {
            std::wstring rest = TrimW(t.substr(c + 1));
            if (!rest.empty()) return rest;
        }
    }
    return t;
}

// 从 pos 处起找第一个 > 与其后的 </a>，返回其间文本
static std::wstring AnchorTextAt(const std::string& html, size_t pos) {
    size_t gt = html.find('>', pos);
    if (gt == std::string::npos) return L"";
    size_t close = FindCI(html, "</a>", gt + 1);
    if (close == std::string::npos) return L"";
    return NormText(html.substr(gt + 1, close - gt - 1));
}

// 从 pos 处起向后 span 字节内，取形如 value="....pdf" 的直链
static std::string HiddenPdfAfter(const std::string& html, size_t pos, size_t span) {
    size_t from  = pos;
    size_t limit = (pos + span > html.size()) ? html.size() : pos + span;
    while (true) {
        size_t v = FindCI(html, "value=", from);
        if (v == std::string::npos || v > limit) return "";
        size_t q = v + 6;
        if (q >= html.size()) return "";
        char quote = html[q];
        if (quote != '"' && quote != '\'') { from = q; continue; }
        size_t end = html.find(quote, q + 1);
        if (end == std::string::npos) return "";
        std::string val = html.substr(q + 1, end - q - 1);
        if (EndsWithCI(val, ".pdf")) return val;
        from = end + 1;
    }
}

// 按 RFC 3986 规则解析相对链接（正确处理 ../ 的层级回退）
static std::wstring ResolveUrl(const std::wstring& baseUrl, const std::wstring& href) {
    if (href.empty()) return href;
    if (href.rfind(L"http://", 0) == 0 || href.rfind(L"https://", 0) == 0) return href;

    size_t schemeEnd = baseUrl.find(L"://");
    if (schemeEnd == std::wstring::npos) return href;
    size_t       hostEnd = baseUrl.find(L'/', schemeEnd + 3);
    std::wstring schemeHost =
        (hostEnd == std::wstring::npos) ? baseUrl : baseUrl.substr(0, hostEnd);
    std::wstring basePath = (hostEnd == std::wstring::npos) ? L"/" : baseUrl.substr(hostEnd);

    if (href[0] == L'/') return schemeHost + href;

    size_t       slash = basePath.find_last_of(L'/');
    std::wstring dir = (slash == std::wstring::npos) ? L"/" : basePath.substr(0, slash + 1);

    std::wstring rel = href;
    while (rel.rfind(L"../", 0) == 0) {
        rel = rel.substr(3);
        if (dir.size() > 1) {
            size_t s = dir.find_last_of(L'/', dir.size() - 2);
            dir = (s == std::wstring::npos) ? L"/" : dir.substr(0, s + 1);
        }
    }
    while (rel.rfind(L"./", 0) == 0) rel = rel.substr(2);
    return schemeHost + dir + rel;
}

// ---------------------------------------------------------------- 头条解析
// marker 指向文章列表容器（news-list / articlelist / newslist），取其内首个链接文本
static std::wstring ParseLead(const std::string& body, const char* marker) {
    size_t bodyPos = FindCI(body, "<body", 0);
    size_t from    = (bodyPos == std::string::npos) ? 0 : bodyPos;

    size_t listPos = FindCI(body, marker, from);
    if (listPos == std::string::npos) return L"";

    // 条目可能是 <li>（人民日报/大众日报/经济日报），也可能是 <div>（法治日报）
    size_t liPos = FindCI(body, "<li", listPos);
    size_t start = (liPos != std::string::npos && liPos - listPos <= 4000) ? liPos : listPos;
    if (start == std::string::npos || start - listPos > 4000) return L"";

    size_t aOpen = FindCI(body, "<a", start);
    if (aOpen == std::string::npos || aOpen - listPos > 4000) return L"";
    size_t aClose = FindCI(body, "</a>", aOpen);
    if (aClose == std::string::npos) return L"";
    size_t gt = body.find('>', aOpen);
    if (gt == std::string::npos || gt > aClose) return L"";

    std::wstring title = NormText(body.substr(gt + 1, aClose - gt - 1));
    if (title.find(L"责编") != std::wstring::npos) return L"";
    if (title.size() > 60) title = title.substr(0, 60);
    return title;
}

static const char* LeadMarkerFor(const std::wstring& id) {
    if (id == L"ce") return "articlelist";
    if (id == L"fzrb" || id == L"dzrb") return "newslist";
    return "news-list";
}

// ---------------------------------------------------------------- 人民日报
static const wchar_t* kRmrb = L"https://paper.people.com.cn/rmrb";

enum class Layout { New, Old };

static std::wstring RmrbCatalog(const Date& d, Layout layout) {
    wchar_t buf[512];
    if (layout == Layout::New)
        swprintf(buf, 512, L"%s/pc/layout/%04d%02d/%02d/node_01.html", kRmrb, d.y, d.m, d.d);
    else
        swprintf(buf, 512, L"%s/html/%04d-%02d/%02d/nbs.D110000renmrb_01.htm", kRmrb, d.y, d.m, d.d);
    return buf;
}

static std::wstring RmrbPage(const Date& d, Layout layout, int page) {
    wchar_t buf[512];
    if (layout == Layout::New)
        swprintf(buf, 512, L"%s/pc/layout/%04d%02d/%02d/node_%02d.html", kRmrb, d.y, d.m, d.d, page);
    else
        swprintf(buf, 512, L"%s/html/%04d-%02d/%02d/nbs.D110000renmrb_%02d.htm", kRmrb, d.y, d.m, d.d,
                 page);
    return buf;
}

// 解析版面导航：兼容 nbs.D110000renmrb_NN.htm 与 node_NN.html 两种写法
static void ParseRmrbNav(const std::string& html, Layout layout, std::vector<PageInfo>& out) {
    const std::string key  = (layout == Layout::New) ? "node_" : "nbs.D110000renmrb_";
    const std::string tail = (layout == Layout::New) ? ".html" : ".htm";

    std::map<int, std::wstring> found;
    size_t                      pos = 0;
    while ((pos = html.find(key, pos)) != std::string::npos) {
        size_t p     = pos + key.size();
        size_t start = p;
        while (p < html.size() && isdigit((unsigned char)html[p])) ++p;
        if (p == start || p - start > 2) { pos = p; continue; }
        int num = atoi(html.substr(start, p - start).c_str());
        if (html.compare(p, tail.size(), tail) != 0) { pos = p; continue; }
        std::wstring name = AnchorTextAt(html, p + tail.size());
        if (num >= 1 && num <= 99) {
            auto it = found.find(num);
            if (it == found.end() || (it->second.empty() && !name.empty())) found[num] = name;
        }
        pos = p + tail.size();
    }
    out.clear();
    for (auto& kv : found) {
        PageInfo info;
        info.number = kv.first;
        info.name   = kv.second.empty() ? (std::to_wstring(kv.first) + L"版") : CleanSection(kv.second);
        out.push_back(info);
    }
    std::sort(out.begin(), out.end(),
              [](const PageInfo& a, const PageInfo& b) { return a.number < b.number; });
}

static bool TryRmrbLayout(const Date& date, Layout layout, std::vector<PageInfo>& pages,
                          FetchResult& result, std::wstring& err) {
    HttpResponse r = HttpGet(RmrbCatalog(date, layout), RmrbCatalog(date, Layout::Old));
    if (r.status == 0) {
        err    = r.error.empty() ? L"网络请求失败" : r.error;
        result = FetchResult::Network;
        return false;
    }
    if (r.status != 200) {
        result = (r.status == 403)   ? FetchResult::Forbidden
                 : (r.status == 404) ? FetchResult::NotFound
                                     : FetchResult::Network;
        if (result == FetchResult::Network)
            err = L"站点返回异常状态：" + std::to_wstring(r.status);
        return false;
    }
    ParseRmrbNav(r.body, layout, pages);
    if (pages.empty()) {
        err    = L"未能解析出版面导航，站点结构可能已变更";
        result = FetchResult::Network;
        return false;
    }
    for (auto& p : pages) p.pageUrl = RmrbPage(date, layout, p.number);
    result = FetchResult::Ok;
    return true;
}

static FetchResult FetchRmrbList(const Date& date, std::vector<PageInfo>& pages, std::wstring& err) {
    FetchResult  r1 = FetchResult::NotFound;
    std::wstring e1;
    if (TryRmrbLayout(date, Layout::New, pages, r1, e1)) return FetchResult::Ok;

    FetchResult  r2 = FetchResult::NotFound;
    std::wstring e2;
    if (TryRmrbLayout(date, Layout::Old, pages, r2, e2)) return FetchResult::Ok;

    if (r1 == FetchResult::Network || r2 == FetchResult::Network) {
        err = (!e1.empty() ? e1 : e2);
        return FetchResult::Network;
    }
    if (r1 == FetchResult::Forbidden || r2 == FetchResult::Forbidden) {
        err = L"该日期早于站点公开存档范围（403）";
        return FetchResult::Forbidden;
    }
    err = L"该日期没有报纸（404）";
    return FetchResult::NotFound;
}

// ---------------------------------------------------------------- 经济日报
static std::wstring CeCatalog(const Date& d) {
    wchar_t buf[512];
    swprintf(buf, 512, L"http://paper.ce.cn/pc/layout/%s/%02d/node_01.html", FmtYm(d).c_str(), d.d);
    return buf;
}

static std::wstring CePage(const Date& d, int page) {
    wchar_t buf[512];
    swprintf(buf, 512, L"http://paper.ce.cn/pc/layout/%s/%02d/node_%02d.html", FmtYm(d).c_str(), d.d,
             page);
    return buf;
}

// 导航形如：
//   <li class="posRelative"><a href="node_01.html">第01版：要闻</a>
//   <input type="hidden" value="../../../attachment/202611/05/<guid>.pdf"></li>
static void ParseCeNav(const std::string& html, const std::wstring& catalogUrl,
                       std::vector<PageInfo>& out) {
    std::map<int, PageInfo> found;
    size_t                  pos = 0;
    while ((pos = html.find("node_", pos)) != std::string::npos) {
        size_t p     = pos + 5;
        size_t start = p;
        while (p < html.size() && isdigit((unsigned char)html[p])) ++p;
        if (p == start || p - start > 2) { pos = p; continue; }
        if (html.compare(p, 5, ".html") != 0) { pos = p; continue; }
        int          num  = atoi(html.substr(start, p - start).c_str());
        std::wstring name = AnchorTextAt(html, p + 5);
        std::string  pdf  = HiddenPdfAfter(html, p + 5, 600);

        if (num >= 1 && num <= 99) {
            PageInfo info;
            info.number = num;
            info.name   = CleanSection(name);
            if (info.name.empty()) info.name = std::to_wstring(num) + L"版";
            if (!pdf.empty()) info.pdfUrl = ResolveUrl(catalogUrl, Utf8ToWide(pdf));
            auto it = found.find(num);
            if (it == found.end() || it->second.pdfUrl.empty()) found[num] = info;
        }
        pos = p + 5;
    }
    out.clear();
    for (auto& kv : found) out.push_back(kv.second);
    std::sort(out.begin(), out.end(),
              [](const PageInfo& a, const PageInfo& b) { return a.number < b.number; });
}

// ---------------------------------------------------------------- 法治日报
static std::wstring FzrbCatalog(const Date& d) {
    wchar_t buf[512];
    swprintf(buf, 512, L"http://epaper.legaldaily.com.cn/fzrb/content/%s/Page01TB.htm",
             FmtYmd8(d).c_str());
    return buf;
}

static std::wstring FzrbPage(const Date& d, int page) {
    wchar_t buf[512];
    swprintf(buf, 512, L"http://epaper.legaldaily.com.cn/fzrb/content/%s/Page%02dTB.htm",
             FmtYmd8(d).c_str(), page);
    return buf;
}

static std::wstring FzrbPdf(const Date& d, int page) {
    wchar_t buf[512];
    swprintf(buf, 512, L"http://epaper.legaldaily.com.cn/fzrb/PDF/%s/%02d.pdf", FmtYmd8(d).c_str(),
             page);
    return buf;
}

// 版面块形如：
//   <div class="banlist-li"><a href='../../PDF/20261105/01.pdf' class="pdf">…</a>
//   <A class="18" href="Page01TB.htm">第 01版:要闻</A></div>
static void ParseFzrbNav(const std::string& html, const Date& date, std::vector<PageInfo>& out) {
    std::map<int, PageInfo> found;
    size_t                  pos = 0;
    while ((pos = html.find("class=\"banlist-li\"", pos)) != std::string::npos) {
        size_t      next = html.find("class=\"banlist-li\"", pos + 10);
        std::string blk  = html.substr(pos, (next == std::string::npos ? html.size() : next) - pos);

        size_t      pp   = blk.find("Page");
        int         num  = 0;
        std::wstring name;
        while (pp != std::string::npos) {
            size_t q     = pp + 4;
            size_t start = q;
            while (q < blk.size() && isdigit((unsigned char)blk[q])) ++q;
            if (q > start && q - start <= 2) {
                // 版号后可能带 1~3 个字母后缀（TB / GN / MT …），再接 .htm
                size_t t = q;
                while (t < blk.size() && isalpha((unsigned char)blk[t]) && t - q < 3) ++t;
                if (FindCI(blk, ".htm", t) == t) {
                    num            = atoi(blk.substr(start, q - start).c_str());
                    std::wstring x = AnchorTextAt(blk, q);
                    if (!x.empty()) name = CleanSection(x);
                    break;
                }
            }
            pp = blk.find("Page", pp + 4);
        }
        if (num >= 1 && num <= 99) {
            PageInfo info;
            info.number  = num;
            info.name    = name.empty() ? (std::to_wstring(num) + L"版") : name;
            info.pageUrl = FzrbPage(date, num);
            info.pdfUrl  = FzrbPdf(date, num);
            found[num]   = info;
        }
        pos += 10;
    }
    out.clear();
    for (auto& kv : found) out.push_back(kv.second);
    std::sort(out.begin(), out.end(),
              [](const PageInfo& a, const PageInfo& b) { return a.number < b.number; });
}

// ---------------------------------------------------------------- 大众日报
static std::wstring DzrbCatalog(const Date& d) {
    wchar_t buf[512];
    swprintf(buf, 512, L"http://paper.dzwww.com/dzrb/content/%s/Page01NU.htm", FmtYmd8(d).c_str());
    return buf;
}

static std::wstring DzrbPage(const Date& d, int page) {
    wchar_t buf[512];
    swprintf(buf, 512, L"http://paper.dzwww.com/dzrb/content/%s/Page%02dNU.htm", FmtYmd8(d).c_str(),
             page);
    return buf;
}

static std::wstring DzrbPdf(const Date& d, int page) {
    wchar_t buf[512];
    swprintf(buf, 512, L"http://paper.dzwww.com/dzrb/PDF/%s/%02d.pdf", FmtYmd8(d).c_str(), page);
    return buf;
}

// 导航形如：[<SPAN class=banci-A01><A href="Page02NU.htm">02</A></SPAN>]:<A …>要闻</A>
// 页面上另有「收藏本页」「打印本页」等功能链接，同样指向版面页，需排除
static bool IsUiWord(const std::wstring& t) {
    static const wchar_t* words[] = {L"收藏本页", L"打印本页", L"关闭窗口", L"上一版", L"下一版",
                                     L"返回首页", L"返回",     L"首页",     L"收藏",   L"打印"};
    for (auto* w : words)
        if (t == w) return true;
    return false;
}

static void ParseDzrbNav(const std::string& html, const Date& date, std::vector<PageInfo>& out) {
    std::map<int, PageInfo> found;
    size_t                  pos = 0;
    while ((pos = html.find("Page", pos)) != std::string::npos) {
        size_t q     = pos + 4;
        size_t start = q;
        while (q < html.size() && isdigit((unsigned char)html[q])) ++q;
        if (q == start || q - start > 2 || FindCI(html, "NU.htm", q) != q) { pos = q; continue; }
        int          num  = atoi(html.substr(start, q - start).c_str());
        std::wstring text = AnchorTextAt(html, q);
        // 只含数字的链接（版号本身）与功能链接都不是版名
        bool numeric = !text.empty() &&
                       text.find_first_not_of(L"0123456789 ") == std::wstring::npos;
        if (num >= 1 && num <= 99) {
            if (found.find(num) == found.end()) {
                PageInfo info;
                info.number  = num;
                info.pageUrl = DzrbPage(date, num);
                info.pdfUrl  = DzrbPdf(date, num);
                found[num]   = info;
            }
            if (!numeric && !text.empty() && !IsUiWord(text) && found[num].name.empty())
                found[num].name = CleanSection(text);
        }
        pos = q;
    }
    out.clear();
    for (auto& kv : found) {
        PageInfo p = kv.second;
        if (p.name.empty()) p.name = std::to_wstring(p.number) + L"版";
        out.push_back(p);
    }
    std::sort(out.begin(), out.end(),
              [](const PageInfo& a, const PageInfo& b) { return a.number < b.number; });
}

// ---------------------------------------------------------------- 统一入口
static FetchResult FetchSimpleList(const SourceInfo& src, const Date& date,
                                   std::vector<PageInfo>& pages, std::wstring& err) {
    std::wstring url = (src.id == L"ce")     ? CeCatalog(date)
                       : (src.id == L"fzrb") ? FzrbCatalog(date)
                                             : DzrbCatalog(date);
    HttpResponse r = HttpGet(url, src.home);
    if (r.status == 0) {
        err = r.error.empty() ? L"网络请求失败" : r.error;
        return FetchResult::Network;
    }
    if (r.status == 403) {
        err = L"该日期早于站点公开存档范围（403）";
        return FetchResult::Forbidden;
    }
    if (r.status == 404) {
        err = L"该日期没有报纸（404）";
        return FetchResult::NotFound;
    }
    if (r.status != 200) {
        err = L"站点返回异常状态：" + std::to_wstring(r.status);
        return FetchResult::Network;
    }

    if (src.id == L"ce") ParseCeNav(r.body, url, pages);
    else if (src.id == L"fzrb") ParseFzrbNav(r.body, date, pages);
    else ParseDzrbNav(r.body, date, pages);

    if (pages.empty()) {
        err = L"未能解析出版面导航，站点结构可能已变更";
        return FetchResult::Network;
    }
    return FetchResult::Ok;
}

FetchResult FetchPageList(const SourceInfo& src, const Date& date, std::vector<PageInfo>& pages,
                          std::wstring& err) {
    pages.clear();
    if (src.id == L"rmrb") return FetchRmrbList(date, pages, err);
    return FetchSimpleList(src, date, pages, err);
}

bool FetchPageDetail(const SourceInfo& src, const Date& date, PageInfo& page, std::wstring& err) {
    if (page.pageUrl.empty()) {
        if (src.id == L"rmrb") page.pageUrl = RmrbPage(date, Layout::New, page.number);
        else if (src.id == L"ce") page.pageUrl = CePage(date, page.number);
        else if (src.id == L"fzrb") page.pageUrl = FzrbPage(date, page.number);
        else page.pageUrl = DzrbPage(date, page.number);
    }
    HttpResponse r = HttpGet(page.pageUrl, src.home);
    if (r.status != 200) {
        err = L"第 " + std::to_wstring(page.number) + L" 版页面获取失败";
        return false;
    }

    if (page.lead.empty()) page.lead = ParseLead(r.body, LeadMarkerFor(src.id));

    if (!page.pdfUrl.empty()) return true;

    // 页面内 <a href="...pdf"> 直链
    size_t pos = 0;
    while ((pos = FindCI(r.body, "href=", pos)) != std::string::npos) {
        size_t q = pos + 5;
        if (q >= r.body.size()) break;
        char quote = r.body[q];
        if (quote != '"' && quote != '\'') { pos = q; continue; }
        size_t end = r.body.find(quote, q + 1);
        if (end == std::string::npos) break;
        std::string href = r.body.substr(q + 1, end - q - 1);
        if (EndsWithCI(href, ".pdf")) {
            page.pdfUrl = ResolveUrl(page.pageUrl, Utf8ToWide(href));
            return true;
        }
        pos = end;
    }

    if (src.id == L"fzrb") { page.pdfUrl = FzrbPdf(date, page.number); return true; }
    if (src.id == L"dzrb") { page.pdfUrl = DzrbPdf(date, page.number); return true; }

    // 人民日报旧版可按命名规则兜底
    wchar_t buf[512];
    swprintf(buf, 512, L"%s/images/%04d-%02d/%02d/%02d/rmrb%04d%02d%02d%02d.pdf", kRmrb, date.y,
             date.m, date.d, page.number, date.y, date.m, date.d, page.number);
    page.pdfUrl = buf;
    return true;
}

bool ResolvePdfUrl(const SourceInfo& src, const Date& date, PageInfo& page, std::wstring& err) {
    if (!page.pdfUrl.empty()) return true;
    return FetchPageDetail(src, date, page, err);
}

std::wstring RefererFor(const SourceInfo& src, const Date& date) {
    if (src.id == L"rmrb") return RmrbCatalog(date, Layout::New);
    if (src.id == L"ce") return CeCatalog(date);
    if (src.id == L"fzrb") return FzrbCatalog(date);
    return DzrbCatalog(date);
}

bool DownloadPdf(const PageInfo& page, const std::wstring& destPath,
                 const std::wstring& referer, std::wstring& err) {
    for (int attempt = 1; attempt <= 3; ++attempt) {
        HttpResponse r = HttpGet(page.pdfUrl, referer);
        if (r.status == 200 && r.body.size() >= 8192 && r.body.compare(0, 5, "%PDF-") == 0) {
            if (!WriteFileBytes(destPath, r.body)) {
                err = L"写入文件失败";
                return false;
            }
            return true;
        }
        if (r.status == 200 && r.body.compare(0, 5, "%PDF-") != 0) {
            err = L"返回内容不是有效 PDF";
        } else if (r.status != 0 && r.status != 200) {
            err = L"HTTP " + std::to_wstring(r.status);
        } else {
            err = r.error.empty() ? L"下载中断" : r.error;
        }
        if (attempt < 3) Sleep(800 * attempt);
    }
    return false;
}

int ProbeDate(const SourceInfo& src, const Date& date) {
    bool         forbidden = false;
    std::wstring catalog   = (src.id == L"rmrb")     ? RmrbCatalog(date, Layout::New)
                             : (src.id == L"ce")     ? CeCatalog(date)
                             : (src.id == L"fzrb")   ? FzrbCatalog(date)
                                                     : DzrbCatalog(date);

    for (int i = 0; i < 2; ++i) {
        std::wstring url = (i == 0) ? catalog
                                    : (src.id == L"rmrb" ? RmrbCatalog(date, Layout::Old) : catalog);
        HttpResponse r = HttpGet(url, src.home, 15000);
        if (r.status == 200) return 200;
        if (r.status == 0) return 0;
        if (r.status == 403) forbidden = true;
        if (src.id != L"rmrb") break;
    }
    return forbidden ? 403 : 404;
}

bool FindLatestDate(const SourceInfo& src, Date& out, std::wstring& err,
                    const std::function<void(const std::wstring&)>& log) {
    Date today = Today();

    int status = ProbeDate(src, today);
    if (status == 0) {
        err = L"无法连接" + src.name + L"站点，请检查网络";
        return false;
    }
    if (status == 200) {
        out = today;
        return true;
    }

    int  gap = 1, failedGap = 0;
    Date candidate;
    bool found = false;
    while (gap <= 1500) {
        candidate = AddDays(today, -gap);
        int st    = ProbeDate(src, candidate);
        if (st == 0) {
            err = L"无法连接" + src.name + L"站点，请检查网络";
            return false;
        }
        if (st == 200) { found = true; break; }
        failedGap = gap;
        gap *= 2;
    }
    if (!found) {
        err = L"近 4 年内未找到可下载的" + src.name + L"，站点结构可能已变更";
        return false;
    }

    Date low  = candidate;
    Date high = AddDays(today, -failedGap);
    while (DaysBetween(low, high) > 1) {
        Date probe = AddDays(low, DaysBetween(low, high) / 2);
        int  st    = ProbeDate(src, probe);
        if (st == 0) {
            err = L"无法连接" + src.name + L"站点，请检查网络";
            return false;
        }
        if (st == 200) low = probe;
        else high = probe;
    }
    if (log) log(L"已确认当前最新可下载日期：" + IsoDateW(low));
    out = low;
    return true;
}
