// pdfmerge.cpp —— 轻量 PDF 合并器
//
// 各报的版面 PDF 均为经典结构（明文对象 + xref 表，无对象流、无加密），因此可以
// 线性扫描对象、整体重编号后重建页面树：
//   1) 预扫描出全部“裸整数对象”（"N 0 obj 长度 endobj"），用于解析间接 /Length；
//   2) 扫描每个源文件的全部对象：先按 /Length 精确定位流数据，失败才回退搜 endstream；
//   3) 为每个对象加固定偏移量重编号，并改写字典中的间接引用（流数据区原样照抄）；
//   4) 校验引用完整性——任何一个被引用的对象缺失就立即中止，不产出半损文件；
//   5) 写出 Catalog / Pages / Info，重建 xref 表与 trailer。
#include "core.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

const size_t NPOS = (size_t)-1;

struct Obj {
    int         num = 0;
    std::string raw;                  // "obj" 之后到 "endobj" 之前的原始字节
    size_t      streamStart = NPOS;   // raw 中 "stream" 关键字位置，其后整段原样照抄
    bool        isPage = false;
};

bool IsDigit(char c) { return c >= '0' && c <= '9'; }
bool IsSpace(char c) {
    return c == ' ' || c == '\r' || c == '\n' || c == '\t' || c == '\f' || c == 0;
}
bool IsDelim(char c) {
    return IsSpace(c) || c == '/' || c == '<' || c == '>' || c == '[' || c == ']' ||
           c == '(' || c == ')' || c == '{' || c == '}' || c == '%';
}

// 从 pos 向前读一个整数（跳过空白），返回起点，失败返回 NPOS
size_t ReadIntBackward(const std::string& s, size_t pos, long& value) {
    size_t p = pos;
    while (p > 0 && IsSpace(s[p - 1])) --p;
    size_t end = p;
    while (p > 0 && IsDigit(s[p - 1])) --p;
    if (p == end) return NPOS;
    value = atol(s.substr(p, end - p).c_str());
    return p;
}

// 查找独立关键字 kw（前后均为分隔符）。对 "stream" 跳过 "endstream" 中的同名片段。
size_t FindKeyword(const std::string& s, const char* kw, size_t from) {
    const size_t klen = strlen(kw);
    const bool   isStream = (strcmp(kw, "stream") == 0);
    size_t       p = from;
    while ((p = s.find(kw, p)) != NPOS) {
        bool ok = true;
        if (p > 0 && !IsDelim(s[p - 1])) ok = false;
        if (ok && isStream && p >= 3 && s.compare(p - 3, 3, "end") == 0) ok = false;
        if (ok) {
            size_t q = p + klen;
            ok = (q >= s.size()) || IsDelim(s[q]);
        }
        if (ok) return p;
        p += klen;
    }
    return NPOS;
}

// 判断位置 pos 是否落在 PDF 字面字符串 "( ... )" 内。
// 有些报纸的 PDF 在 Info 字典里塞了自定义字段，如 /cfeng (<< /Contents 12 0 R /Type /Page >>)，
// 若按裸字节扫描会把这类字符串内容误当成页对象，导致合并结果多出空白页。
bool InsideLiteralString(const std::string& s, size_t pos) {
    int depth = 0;
    for (size_t i = 0; i < pos && i < s.size(); ++i) {
        char c = s[i];
        if (c == '\\') { ++i; continue; }
        if (c == '(') ++depth;
        else if (c == ')' && depth > 0) --depth;
    }
    return depth > 0;
}

// 判断位置 pos 处 "N 0 obj" 的 N 是否位于行首（用于过滤流数据里的伪对象头）
bool AtLineStart(const std::string& s, size_t numPos) {
    return numPos == 0 || s[numPos - 1] == '\n' || s[numPos - 1] == '\r';
}

// 预扫描：收集裸整数对象的值（形如 "49 0 obj\n7827\nendobj"）。
// 各报大量使用间接长度 "/Length 49 0 R"，必须先把这些值查出来，
// 否则会把对象号当成数据长度，导致跳错位置、对象被"吞掉"、图片丢失甚至整页空白。
void CollectIntegerObjects(const std::string& buf, std::map<int, long long>& vals) {
    size_t pos = 0;
    while ((pos = buf.find("obj", pos)) != NPOS) {
        size_t p = pos;
        pos += 3;
        if (p > 0 && !IsSpace(buf[p - 1])) continue;
        long gen = 0, num = 0;
        size_t pgen = ReadIntBackward(buf, p, gen);
        if (pgen == NPOS) continue;
        size_t pnum = ReadIntBackward(buf, pgen, num);
        if (pnum == NPOS || !AtLineStart(buf, pnum)) continue;

        size_t q = p + 3;
        while (q < buf.size() && IsSpace(buf[q])) ++q;
        if (q >= buf.size() || !IsDigit(buf[q])) continue;
        size_t e = q;
        while (e < buf.size() && IsDigit(buf[e])) ++e;
        long long v = atoll(buf.substr(q, e - q).c_str());
        size_t r = e;
        while (r < buf.size() && IsSpace(buf[r])) ++r;
        if (buf.compare(r, 6, "endobj") == 0) vals[(int)num] = v;
    }
}

// 扫描全部对象头编号，用于校验“没有对象被漏掉”
void CollectObjectHeaders(const std::string& buf, std::set<int>& nums) {
    size_t pos = 0;
    while ((pos = buf.find("obj", pos)) != NPOS) {
        size_t p = pos;
        pos += 3;
        if (p > 0 && !IsSpace(buf[p - 1])) continue;
        long gen = 0, num = 0;
        size_t pgen = ReadIntBackward(buf, p, gen);
        if (pgen == NPOS) continue;
        size_t pnum = ReadIntBackward(buf, pgen, num);
        if (pnum == NPOS || !AtLineStart(buf, pnum)) continue;
        nums.insert((int)num);
    }
}

// 解析某对象的 /Length：成功则给出数据终点（不含）
bool ResolveStreamEnd(const std::string& buf, size_t bodyStart, size_t streamKw,
                      const std::map<int, long long>& ints, size_t dataStart, size_t& dataEnd) {
    size_t lp = buf.rfind("/Length", streamKw);
    if (lp == NPOS || lp < bodyStart) return false;
    size_t q = lp + 7;
    while (q < streamKw && IsSpace(buf[q])) ++q;
    if (q >= streamKw || !IsDigit(buf[q])) return false;
    size_t e = q;
    while (e < streamKw && IsDigit(buf[e])) ++e;
    long long v = atoll(buf.substr(q, e - q).c_str());

    // 可能是间接引用：/Length 49 0 R
    size_t k = e;
    while (k < streamKw && IsSpace(buf[k])) ++k;
    if (k < streamKw && IsDigit(buf[k])) {
        size_t g = k;
        while (g < streamKw && IsDigit(buf[g])) ++g;
        size_t m = g;
        while (m < streamKw && IsSpace(buf[m])) ++m;
        if (m < streamKw && buf[m] == 'R') {
            auto it = ints.find((int)v);
            if (it == ints.end()) return false;   // 长度对象未被解析出来
            v = it->second;
        }
    }
    if (v < 0) return false;

    size_t cand = dataStart + (size_t)v;
    if (cand > buf.size()) return false;
    // 校验：长度之后应紧跟 endstream（允许最多 2 个换行）
    size_t w = cand;
    int    skipped = 0;
    while (w < buf.size() && skipped < 3 &&
           (buf[w] == '\r' || buf[w] == '\n' || buf[w] == ' ')) { ++w; ++skipped; }
    if (buf.compare(w, 9, "endstream") != 0) return false;
    dataEnd = cand;
    return true;
}

// 回退：找不到可信的 /Length 时，找“后面紧跟 endobj 的 endstream”来界定数据区。
// 注意有些生成器写成 "endstreamendobj"（无分隔符），因此不能要求 endobj 前有空白。
bool FindEndStreamAndObj(const std::string& buf, size_t from, size_t& es, size_t& eo) {
    size_t p = from;
    while ((p = buf.find("endstream", p)) != NPOS) {
        size_t q = p + 9;
        while (q < buf.size() &&
               (buf[q] == '\r' || buf[q] == '\n' || buf[q] == ' ' || buf[q] == '\t')) ++q;
        if (buf.compare(q, 6, "endobj") == 0) { es = p; eo = q; return true; }
        p += 9;
    }
    return false;
}

// 在 pos 处（允许少量空白）匹配 "endstream" 与紧随其后的 "endobj"
bool MatchEndStreamAt(const std::string& buf, size_t pos, size_t& eo) {
    size_t q = pos;
    while (q < buf.size() &&
           (buf[q] == '\r' || buf[q] == '\n' || buf[q] == ' ' || buf[q] == '\t')) ++q;
    if (q + 9 > buf.size() || buf.compare(q, 9, "endstream") != 0) return false;
    q += 9;
    while (q < buf.size() &&
           (buf[q] == '\r' || buf[q] == '\n' || buf[q] == ' ' || buf[q] == '\t')) ++q;
    if (q + 6 > buf.size() || buf.compare(q, 6, "endobj") != 0) return false;
    eo = q;
    return true;
}

bool ParseObjects(const std::string& buf, std::vector<Obj>& out) {
    std::map<int, long long> ints;
    CollectIntegerObjects(buf, ints);

    size_t pos = 0;
    while (true) {
        size_t objPos = buf.find("obj", pos);
        if (objPos == NPOS) break;
        pos = objPos + 3;

        // 校验 "obj" 是独立关键字
        if (objPos > 0 && !IsSpace(buf[objPos - 1])) continue;

        long gen = 0, num = 0;
        size_t pgen = ReadIntBackward(buf, objPos, gen);
        if (pgen == NPOS) continue;
        size_t pnum = ReadIntBackward(buf, pgen, num);
        if (pnum == NPOS) continue;

        size_t bodyStart = objPos + 3;
        size_t endobj = NPOS, ds = NPOS;

        size_t eo0 = FindKeyword(buf, "endobj", bodyStart);
        size_t st0 = FindKeyword(buf, "stream", bodyStart);

        if (st0 != NPOS && (eo0 == NPOS || st0 < eo0)) {
            size_t dataStart = st0 + 6;
            if (dataStart < buf.size() && buf[dataStart] == '\r') ++dataStart;
            if (dataStart < buf.size() && buf[dataStart] == '\n') ++dataStart;
            ds = st0;                       // 字典区到此为止，之后整段原样照抄

            size_t cand = NPOS;
            if (ResolveStreamEnd(buf, bodyStart, st0, ints, dataStart, cand) &&
                MatchEndStreamAt(buf, cand, endobj)) {
                // 长度可信且校验通过
            } else {
                size_t es = NPOS, eo = NPOS;
                if (!FindEndStreamAndObj(buf, dataStart, es, eo)) continue;
                endobj = eo;
            }
        } else {
            endobj = eo0;
        }

        if (endobj == NPOS || endobj <= bodyStart) continue;

        Obj o;
        o.num = (int)num;
        o.raw = buf.substr(bodyStart, endobj - bodyStart);
        if (ds != NPOS && ds >= bodyStart && ds < endobj) o.streamStart = ds - bodyStart;

        // 判定是否为页对象：/Type /Page 且不是 /Pages
        size_t t = o.raw.find("/Type");
        size_t dictEnd = o.streamStart == NPOS ? o.raw.size() : o.streamStart;
        while (t != NPOS && t < dictEnd) {
            if (InsideLiteralString(o.raw, t)) {
                t = o.raw.find("/Type", t + 5);
                continue;
            }
            size_t q = t + 5;
            while (q < dictEnd && IsSpace(o.raw[q])) ++q;
            if (o.raw.compare(q, 5, "/Page") == 0) {
                size_t after = q + 5;
                if (after >= dictEnd || o.raw[after] != 's') o.isPage = true;
                break;
            }
            t = o.raw.find("/Type", t + 5);
        }

        out.push_back(std::move(o));
        pos = endobj + 6;
    }
    return !out.empty();
}

// 遍历字典区中的间接引用，回调 (对象号, 代号)
template <typename Fn>
void ForEachRef(const std::string& raw, size_t dictEnd, Fn fn) {
    size_t i = 0;
    while (i < dictEnd) {
        char c = raw[i];
        if (c == '(') {                       // 字面字符串：整段跳过
            size_t j = i + 1;
            int    depth = 1;
            while (j < dictEnd && depth > 0) {
                if (raw[j] == '\\') { j += 2; continue; }
                if (raw[j] == '(') ++depth;
                else if (raw[j] == ')') --depth;
                ++j;
            }
            i = j;
            continue;
        }
        if (c == '<' && i + 1 < dictEnd && raw[i + 1] == '<') {   // 字典起始，逐字节处理
            i += 2;
            continue;
        }
        if (c == '<') {                       // 十六进制串：整段跳过
            size_t j = raw.find('>', i + 1);
            i = (j == std::string::npos || j >= dictEnd) ? dictEnd : j + 1;
            continue;
        }
        if (c == '%') {                       // 注释
            size_t j = raw.find('\n', i);
            i = (j == std::string::npos || j >= dictEnd) ? dictEnd : j + 1;
            continue;
        }
        if (!IsDigit((unsigned char)c)) { ++i; continue; }

        size_t s = i;
        while (i < dictEnd && IsDigit((unsigned char)raw[i])) ++i;
        size_t j = i;
        while (j < dictEnd && IsSpace(raw[j])) ++j;
        if (j > i && j < dictEnd && IsDigit((unsigned char)raw[j])) {
            size_t k = j;
            while (k < dictEnd && IsDigit((unsigned char)raw[k])) ++k;
            size_t l = k;
            while (l < dictEnd && IsSpace(raw[l])) ++l;
            if (l > k && l < dictEnd && raw[l] == 'R' &&
                (l + 1 >= dictEnd || IsDelim(raw[l + 1]))) {
                fn(atol(raw.substr(s, i - s).c_str()),
                   atol(raw.substr(j, k - j).c_str()));
                continue;
            }
        }
        // 非引用：原样保留（指针已跳过第一个数字串）
    }
}

// 改写字典中的间接引用 "n g R"，流数据区原样照抄。
// newDictLen 回填改写后的字典区长度（对象号变长会使流数据起点后移）。
std::string RenumberRefs(const std::string& raw, size_t dictEnd, int offset,
                         size_t& newDictLen) {
    std::string out;
    out.reserve(raw.size() + 64);
    size_t i = 0;
    while (i < dictEnd) {
        char c = raw[i];
        if (c == '(') {
            size_t j     = i + 1;
            int    depth = 1;
            while (j < dictEnd && depth > 0) {
                if (raw[j] == '\\') { j += 2; continue; }
                if (raw[j] == '(') ++depth;
                else if (raw[j] == ')') --depth;
                ++j;
            }
            out.append(raw, i, j - i);
            i = j;
            continue;
        }
        if (c == '<' && i + 1 < dictEnd && raw[i + 1] == '<') {   // 字典起始：照抄后继续
            out += "<<";
            i += 2;
            continue;
        }
        if (c == '<') {                       // 十六进制串：整段原样照抄
            size_t j = raw.find('>', i + 1);
            size_t end = (j == std::string::npos || j >= dictEnd) ? dictEnd : j + 1;
            out.append(raw, i, end - i);
            i = end;
            continue;
        }
        if (c == '%') {
            size_t j = raw.find('\n', i);
            size_t end = (j == std::string::npos || j >= dictEnd) ? dictEnd : j + 1;
            out.append(raw, i, end - i);
            i = end;
            continue;
        }
        if (!IsDigit((unsigned char)c)) {
            out.push_back(c);
            ++i;
            continue;
        }
        size_t s = i;
        while (i < dictEnd && IsDigit((unsigned char)raw[i])) ++i;
        size_t j = i;
        while (j < dictEnd && IsSpace(raw[j])) ++j;
        if (j > i && j < dictEnd && IsDigit((unsigned char)raw[j])) {
            size_t k = j;
            while (k < dictEnd && IsDigit((unsigned char)raw[k])) ++k;
            size_t l = k;
            while (l < dictEnd && IsSpace(raw[l])) ++l;
            if (l > k && l < dictEnd && raw[l] == 'R' &&
                (l + 1 >= dictEnd || IsDelim(raw[l + 1]))) {
                long v = atol(raw.substr(s, i - s).c_str());
                out += std::to_string(v + offset);
                continue;
            }
        }
        out.append(raw, s, i - s);
    }
    newDictLen = out.size();
    if (dictEnd < raw.size()) out.append(raw, dictEnd, raw.size() - dictEnd);
    return out;
}

// 把页对象的 /Parent 改指向新的 Pages 节点（obj 2）
std::string PatchParent(const std::string& raw, size_t dictEnd) {
    size_t p = raw.find("/Parent", 0);
    if (p != NPOS && p >= dictEnd) p = NPOS;
    if (p == NPOS) {
        size_t open = raw.find("<<");
        if (open == NPOS || open >= dictEnd) return raw;
        std::string out = raw;
        out.insert(open + 2, " /Parent 2 0 R");
        return out;
    }
    size_t q = p + 7;
    while (q < dictEnd && IsSpace(raw[q])) ++q;
    for (int token = 0; token < 2; ++token) {   // 跳过 "num gen R"
        while (q < dictEnd && IsDigit((unsigned char)raw[q])) ++q;
        while (q < dictEnd && IsSpace(raw[q])) ++q;
    }
    if (q < dictEnd && raw[q] == 'R') ++q;
    std::string out = raw.substr(0, p);
    out += "/Parent 2 0 R";
    out += raw.substr(q);
    return out;
}

std::string PdfHexString(const std::wstring& text) {
    std::string hex = "<FEFF";
    char        buf[8];
    for (wchar_t ch : text) {
        unsigned int code = (unsigned int)ch;
        if (code > 0xFFFF) code = '?';
        snprintf(buf, sizeof(buf), "%04X", code);
        hex += buf;
    }
    hex += ">";
    return hex;
}

// 校验：每个对象字典里引用的对象，都必须在本次解析结果中真实存在。
// 缺一个就说明解析漏掉了对象（过去会导致图片丢失、整页空白），必须中止而不是产出半损文件。
std::vector<int> FindDanglingRefs(const std::vector<Obj>& objs, const std::set<int>& headers) {
    std::set<int> have;
    for (const auto& o : objs) have.insert(o.num);

    std::vector<int> missing;
    std::set<int>    seen;
    for (const auto& o : objs) {
        size_t dictEnd = o.streamStart == NPOS ? o.raw.size() : o.streamStart;
        ForEachRef(o.raw, dictEnd, [&](long n, long gen) {
            if (gen != 0) return;
            int x = (int)n;
            if (x == 0 || have.count(x) || seen.count(x)) return;
            if (!headers.count(x)) return;    // 源文件本身就没有该对象，原样保留
            seen.insert(x);
            missing.push_back(x);
        });
    }
    return missing;
}

}  // namespace

bool MergePdfs(const std::vector<std::wstring>& files, const std::wstring& outPath,
               const std::wstring& title, std::wstring& err) {
    struct CopyBlock {
        int         newNum;
        std::string body;
    };

    std::vector<CopyBlock> blocks;
    std::vector<int>       pageIds;
    int                    nextId = 4;  // 1=Catalog, 2=Pages, 3=Info

    for (const auto& file : files) {
        std::string buf;
        if (!ReadFileBytes(file, buf)) {
            err = L"读取分版 PDF 失败：" + file;
            return false;
        }
        if (buf.compare(0, 5, "%PDF-") != 0) {
            err = L"文件不是 PDF：" + file;
            return false;
        }
        if (buf.find("/Encrypt") != std::string::npos) {
            err = L"该版 PDF 已加密，无法合并：" + file;
            return false;
        }
        std::vector<Obj> objs;
        if (!ParseObjects(buf, objs)) {
            err = L"解析 PDF 对象失败：" + file;
            return false;
        }

        std::set<int> headers;
        CollectObjectHeaders(buf, headers);
        std::vector<int> missing = FindDanglingRefs(objs, headers);
        if (!missing.empty()) {
            size_t slash = file.find_last_of(L"\\/");
            std::wstring name = slash == std::wstring::npos ? file : file.substr(slash + 1);
            std::wstring msg = L"PDF 结构异常，已停止合并以避免生成残缺文件：" + name +
                               L" 中缺少对象 ";
            for (size_t i = 0; i < missing.size() && i < 4; ++i) {
                if (i) msg += L"、";
                msg += std::to_wstring(missing[i]);
            }
            msg += L"。请稍后重试，若反复出现请反馈该版面。";
            err = msg;
            return false;
        }

        int minId = objs[0].num, maxId = objs[0].num;
        for (auto& o : objs) {
            if (o.num < minId) minId = o.num;
            if (o.num > maxId) maxId = o.num;
        }
        int offset = nextId - minId;

        std::vector<std::pair<int, int>> filePages;  // (原号, 新号)
        for (auto& o : objs) {
            size_t      dictEnd = o.streamStart == NPOS ? o.raw.size() : o.streamStart;
            size_t      newDictLen = 0;
            std::string body    = RenumberRefs(o.raw, dictEnd, offset, newDictLen);
            int         newNum  = o.num + offset;
            if (o.isPage) {
                // 重编号可能改变对象号的位数，因此用改写后的字典区长度作为边界
                body = PatchParent(body, newDictLen);
                filePages.push_back({o.num, newNum});
            }
            blocks.push_back({newNum, std::move(body)});
        }
        std::sort(filePages.begin(), filePages.end(),
                  [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
                      return a.first < b.first;
                  });
        for (auto& p : filePages) pageIds.push_back(p.second);

        nextId += (maxId - minId + 1);
        if (nextId > 800000) {
            err = L"PDF 对象数量过多";
            return false;
        }
    }

    if (pageIds.empty()) {
        err = L"源文件中未找到页面对象";
        return false;
    }

    std::string out;
    out.reserve(16 * 1024 * 1024);
    out += "%PDF-1.7\n";
    out += "%\xE2\xE3\xCF\xD3\n";

    const int totalObjs = nextId;  // 编号 0..nextId-1
    std::vector<size_t> offsets(totalObjs, 0);

    auto writeObj = [&](int num, const std::string& body) {
        offsets[num] = out.size();
        out += std::to_string(num);
        out += " 0 obj\n";
        out += body;
        out += "\nendobj\n";
    };

    writeObj(1, "<< /Type /Catalog /Pages 2 0 R >>");

    std::string kids = "<< /Type /Pages /Count " + std::to_string(pageIds.size()) + " /Kids [";
    for (size_t i = 0; i < pageIds.size(); ++i) {
        if (i) kids += " ";
        kids += std::to_string(pageIds[i]) + " 0 R";
    }
    kids += "] >>";
    writeObj(2, kids);

    std::string info = "<< /Title " + PdfHexString(title) +
                       " /Producer " + PdfHexString(L"见报 JianBao (C++ 原生合并)") + " >>";
    writeObj(3, info);

    for (auto& b : blocks) writeObj(b.newNum, b.body);

    size_t xrefPos = out.size();
    out += "xref\n0 " + std::to_string(totalObjs) + "\n";
    out += "0000000000 65535 f\r\n";
    char line[64];
    for (int i = 1; i < totalObjs; ++i) {
        if (offsets[i] == 0) {
            snprintf(line, sizeof(line), "0000000000 65535 f\r\n");
        } else {
            snprintf(line, sizeof(line), "%010llu 00000 n\r\n",
                     (unsigned long long)offsets[i]);
        }
        out += line;
    }
    out += "trailer\n<< /Size " + std::to_string(totalObjs) +
           " /Root 1 0 R /Info 3 0 R >>\nstartxref\n" +
           std::to_string(xrefPos) + "\n%%EOF\n";

    if (!WriteFileBytes(outPath, out)) {
        err = L"写入合并文件失败（请检查磁盘空间与权限）";
        return false;
    }
    return true;
}
