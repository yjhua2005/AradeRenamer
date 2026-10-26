// 文件名简繁互转：内置表 (OpenCC 上游原文件, RCDATA) + 可选的 Add-ons\gbkfix.txt 追加表。
// 词组表优先于单字表：先在词组里做最长匹配，匹配不到才逐字转。
#include <windows.h>
#include <cstdio>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include "conv.h"
#include "resource.h"

struct ConvPair { unsigned int from, to; };

static void EnsureDict();

// 追加表按文件里的出现顺序存放，查找时从后往前，所以后写的行覆盖先写的行。
static std::vector<ConvPair> g_fixT2S;
static std::vector<ConvPair> g_fixS2T;
// 内置表：从资源里的上游字典现解析，按 from 升序，所以能二分。
static std::vector<ConvPair> g_dictT2S;
static std::vector<ConvPair> g_dictS2T;
// 词组表：整个词作为键，直接哈希查找；g_phMaxLen 记最长键的长度，
// 转换时从那么长往下试 (最长匹配)，所以长的词不会被拆成单字错转。
static std::unordered_map<std::wstring, std::wstring> g_phT2S;
static std::unordered_map<std::wstring, std::wstring> g_phS2T;
static size_t g_phMaxLen = 0;

static unsigned int LookupFix(const std::vector<ConvPair>& v, unsigned int cp) {
    for (auto it = v.rbegin(); it != v.rend(); ++it)
        if (it->from == cp) return it->to;
    return 0;
}

static unsigned int LookupTable(const ConvPair* tbl, size_t n, unsigned int cp) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t m = lo + (hi - lo) / 2;
        if (tbl[m].from < cp) lo = m + 1; else hi = m;
    }
    return (lo < n && tbl[lo].from == cp) ? tbl[lo].to : 0;
}

static unsigned int ConvertChar(unsigned int cp, bool toSimplified) {
    unsigned int r = LookupFix(toSimplified ? g_fixT2S : g_fixS2T, cp);
    if (r) return r;
    EnsureDict();
    const std::vector<ConvPair>& d = toSimplified ? g_dictT2S : g_dictS2T;
    return LookupTable(d.data(), d.size(), cp);
}

static std::wstring Convert(const std::wstring& s, bool toSimplified) {
    EnsureDict();
    const std::unordered_map<std::wstring, std::wstring>& ph =
        toSimplified ? g_phT2S : g_phS2T;
    // 词组表可能还没解析出来 (资源缺失): 没有就直接走单字，不特殊处理。
    size_t maxLen = ph.empty() ? 0 : g_phMaxLen;
    if (maxLen > 32) maxLen = 32;          // 文件名里出现 32 字以上的词没有意义，截断省时间

    std::wstring out;
    out.reserve(s.size());
    std::wstring key;                            // 复用的查找缓冲, 别每个长度都分配
    size_t i = 0;
    while (i < s.size()) {
        // ---- 先试词组 (从最长键往下试, 命中就整词替换) ----
        // 词组键全是中文 (数据文件里以 ASCII 开头的行只有注释), 首字符是 ASCII
        // 时不可能命中, 整轮扫描跳过。
        bool matched = false;
        if (s[i] >= 0x80 && maxLen >= 2) {
            size_t remain = s.size() - i;
            size_t L = remain < maxLen ? remain : maxLen;
            for (; L >= 2; --L) {
                key.assign(s, i, L);
                auto it = ph.find(key);
                if (it != ph.end()) {
                    out += it->second;
                    i += L;
                    matched = true;
                    break;
                }
            }
        }
        if (matched) continue;

        // ---- 词组没命中: 逐字转 (代理对按两个 wchar_t 一起算一个码位) ----
        wchar_t c = s[i];
        unsigned int cp = (unsigned int)c;
        size_t adv = 1;
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() &&
            s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            cp = 0x10000u + (((unsigned int)c - 0xD800u) << 10) +
                 ((unsigned int)s[i + 1] - 0xDC00u);
            adv = 2;
        }
        unsigned int t = ConvertChar(cp, toSimplified);
        if (!t) {
            out.append(s, i, adv);
        } else if (t <= 0xFFFF) {
            out.push_back((wchar_t)t);
        } else {
            unsigned int u = t - 0x10000u;
            out.push_back((wchar_t)(0xD800u + (u >> 10)));
            out.push_back((wchar_t)(0xDC00u + (u & 0x3FFu)));
        }
        i += adv;
    }
    return out;
}

std::wstring ConvertT2S(const std::wstring& s) { return Convert(s, true); }
std::wstring ConvertS2T(const std::wstring& s) { return Convert(s, false); }

static bool ReadAllBytes(const fs::path& p, std::string& out) {
    FILE* f = NULL;
    if (_wfopen_s(&f, p.c_str(), L"rb") != 0 || !f) return false;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

// 一行一对 "繁 简"，中间用空格/制表符/逗号分开；; 和 # 开头的是注释。
static unsigned int FirstCodePoint(const std::wstring& s) {
    wchar_t c = s[0];
    if (c >= 0xD800 && c <= 0xDBFF && s.size() > 1 && s[1] >= 0xDC00 && s[1] <= 0xDFFF)
        return 0x10000u + (((unsigned int)c - 0xD800u) << 10) + ((unsigned int)s[1] - 0xDC00u);
    return (unsigned int)c;
}

static bool ParsePair(const std::wstring& line, unsigned int& trad, unsigned int& simp) {
    size_t i = 0;
    while (i < line.size() && (line[i] == L' ' || line[i] == L'\t' || line[i] == 0x3000)) i++;
    if (i >= line.size() || line[i] == L';' || line[i] == L'#') return false;
    std::wstring fields[2];
    for (int f = 0; f < 2; f++) {
        while (i < line.size() && (line[i] == L' ' || line[i] == L'\t' || line[i] == 0x3000 ||
                                   line[i] == L',' || line[i] == 0xFF0C)) i++;
        size_t start = i;
        while (i < line.size() && line[i] != L' ' && line[i] != L'\t' && line[i] != 0x3000 &&
               line[i] != L',' && line[i] != 0xFF0C) i++;
        fields[f] = line.substr(start, i - start);
        if (fields[f].empty()) return false;
    }
    trad = FirstCodePoint(fields[0]);
    simp = FirstCodePoint(fields[1]);
    return trad != simp;
}

int LoadConvFixFile(const fs::path& p) {
    std::error_code ec;
    if (!fs::exists(p, ec)) return 0;
    std::string raw;
    if (!ReadAllBytes(p, raw)) return 0;

    UINT cp = CP_UTF8;   // 原始 gbkfix.txt 是 GBK 的
    int skip = 0;
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB &&
        (unsigned char)raw[2] == 0xBF) skip = 3;
    else if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.data(), (int)raw.size(),
                                 NULL, 0) == 0) cp = 936;   // 解不成 UTF-8 才当 GBK
    int wn = MultiByteToWideChar(cp, 0, raw.data() + skip, (int)(raw.size() - skip), NULL, 0);
    if (wn <= 0) return 0;
    std::wstring text(wn, L'\0');
    MultiByteToWideChar(cp, 0, raw.data() + skip, (int)(raw.size() - skip), &text[0], wn);

    int count = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find(L'\n', pos);
        if (eol == std::wstring::npos) eol = text.size();
        std::wstring line = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        unsigned int trad = 0, simp = 0;
        if (!ParsePair(line, trad, simp)) continue;
        g_fixT2S.push_back({ trad, simp });
        g_fixS2T.push_back({ simp, trad });
        count++;
    }
    return count;
}

// ---------------------------------------------------------------------------
// 内置表：直接解析打进 exe 的上游字典原文件
// ---------------------------------------------------------------------------
std::wstring LoadResText(int id) {
    HMODULE mod = GetModuleHandleW(NULL);
    HRSRC rs = FindResourceW(mod, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!rs) return std::wstring();
    HGLOBAL hg = LoadResource(mod, rs);
    if (!hg) return std::wstring();
    int cb = (int)SizeofResource(mod, rs);
    const char* p = (const char*)LockResource(hg);
    if (!p || cb <= 0) return std::wstring();
    if (cb >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB &&
        (unsigned char)p[2] == 0xBF) { p += 3; cb -= 3; }
    std::wstring w((size_t)MultiByteToWideChar(CP_UTF8, 0, p, cb, NULL, 0), L'\0');
    if (!w.empty()) MultiByteToWideChar(CP_UTF8, 0, p, cb, &w[0], (int)w.size());
    return w;
}

// 一行 "键<TAB>值"，# 和 ; 开头是注释；键和值都只取第一个字符（码位），
// 所以 "㐹\t㑶 㐹" 这种多候选行取第一个候选，和以前生成的静态表口径一致。
static void ParseDictLines(const std::wstring& text, std::vector<ConvPair>& out) {
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find(L'\n', pos);
        if (eol == std::wstring::npos) eol = text.size();
        unsigned int a = 0, b = 0;
        if (ParsePair(text.substr(pos, eol - pos), a, b)) out.push_back({ a, b });
        pos = eol + 1;
    }
}

static void SortDedup(std::vector<ConvPair>& v) {
    std::sort(v.begin(), v.end(),
              [](const ConvPair& x, const ConvPair& y) { return x.from < y.from; });
    size_t w = 0;
    for (size_t i = 0; i < v.size(); i++)
        if (w == 0 || v[i].from != v[w - 1].from) v[w++] = v[i];
    v.resize(w);
}

// 第一次转换时才解析：几千行字典读出来不到 10 ms，但没必要在启动路径上花时间。
static void EnsureDict() {
    static bool done = false;
    if (done) return;
    done = true;                 // 资源缺失也别每字重试一次
    ParseDictLines(LoadResText(IDR_DICT_T2S), g_dictT2S);
    ParseDictLines(LoadResText(IDR_DICT_S2T), g_dictS2T);
    SortDedup(g_dictT2S);
    SortDedup(g_dictS2T);

    // 词组表：一行 "键<TAB>值"，# 开头是注释。值有多个候选时取第一个
    // (OpenCC 的口径：第一个是默认转换)。键/值按整个宽字符串存，不拆码位。
    auto parsePhrases = [](const std::wstring& text,
                           std::unordered_map<std::wstring, std::wstring>& out,
                           size_t& maxLen) {
        size_t pos = 0;
        while (pos < text.size()) {
            size_t eol = text.find(L'\n', pos);
            if (eol == std::wstring::npos) eol = text.size();
            std::wstring line = text.substr(pos, eol - pos);
            pos = eol + 1;
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            size_t h = line.find_first_not_of(L" \t");
            if (h == std::wstring::npos || line[h] == L'#') continue;
            size_t tab = line.find(L'\t', h);
            if (tab == std::wstring::npos) continue;
            std::wstring key = line.substr(h, tab - h);
            // 值: 跳过 tab 后的空白, 取到下一个空白为止 (第一个候选)
            size_t v0 = line.find_first_not_of(L" \t", tab + 1);
            if (v0 == std::wstring::npos) continue;
            size_t v1 = line.find_first_of(L" \t", v0);
            std::wstring val = line.substr(v0, v1 == std::wstring::npos ? std::wstring::npos
                                                                        : v1 - v0);
            if (key.empty() || val.empty() || key == val) continue;
            // 键/值尾部可能还有空白, 收掉
            while (!key.empty() && (key.back() == L' ' || key.back() == L'\t')) key.pop_back();
            while (!val.empty() && (val.back() == L' ' || val.back() == L'\t')) val.pop_back();
            if (key.size() > maxLen) maxLen = key.size();
            out[key] = val;
        }
    };
    parsePhrases(LoadResText(IDR_DICT_T2S_PHRASES), g_phT2S, g_phMaxLen);
    parsePhrases(LoadResText(IDR_DICT_S2T_PHRASES), g_phS2T, g_phMaxLen);
}
