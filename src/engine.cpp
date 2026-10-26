#include "engine.h"
#include "conv.h"
#include "resource.h"
#include <algorithm>
#include <cwctype>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <regex>
#include <random>
#include <sstream>
#include <fstream>
#include <windows.h>

// ----------------------------------------------------------------------------
// Small string helpers
// ----------------------------------------------------------------------------
static std::wstring ReplaceAll(std::wstring s, const std::wstring& from,
                               const std::wstring& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::wstring::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

static std::wstring ReplaceAllCI(std::wstring s, const std::wstring& from,
                                 const std::wstring& to) {
    if (from.empty()) return s;
    std::wstring low = s, lowFrom = from;
    std::transform(low.begin(), low.end(), low.begin(), ::towlower);
    std::transform(lowFrom.begin(), lowFrom.end(), lowFrom.begin(), ::towlower);
    size_t pos = 0;
    while ((pos = low.find(lowFrom, pos)) != std::wstring::npos) {
        s.replace(pos, from.size(), to);
        low.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

static std::wstring RegexReplace(const std::wstring& src,
                                 const std::wstring& pat,
                                 const std::wstring& repl,
                                 bool icase) {
    try {
        std::regex_constants::syntax_option_type opt = std::regex_constants::ECMAScript;
        if (icase) opt |= std::regex_constants::icase;
        std::wregex re(pat, opt);
        return std::regex_replace(src, re, repl);
    } catch (...) {
        return src; // invalid pattern -> leave unchanged
    }
}

// Convert a non-negative integer to base-26 letters (0 -> "A")
static std::wstring ToLetters(long long n, bool upper) {
    wchar_t base = upper ? L'A' : L'a';
    if (n < 0) n = -n;
    std::wstring out;
    if (n == 0) return std::wstring(1, base);
    while (n > 0) {
        int rem = (int)(n % 26);
        out.push_back((wchar_t)(base + rem));
        n /= 26;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

// ----------------------------------------------------------------------------
// Case conversion on a single string
// wordSplit: 0 = a word starts after a space, 1 = after anything that is not an
// English letter or a digit (设置 > 一般参数 > 文件名单词判断字符).
// ----------------------------------------------------------------------------
static std::wstring ApplyCase(std::wstring s, int mode, int wordSplit = 0) {
    switch (mode) {
    case 1: // upper
        for (auto& c : s) c = (wchar_t)std::towupper(c);
        break;
    case 2: // lower
        for (auto& c : s) c = (wchar_t)std::towlower(c);
        break;
    case 3: // title: first letter of each word upper, rest lower
        {
            bool wordStart = true;
            for (auto& c : s) {
                bool boundary = (wordSplit == 1)
                    ? !((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') ||
                        (c >= L'0' && c <= L'9'))
                    : std::iswspace(c) != 0;
                if (boundary) {
                    wordStart = true;
                } else if (wordStart) {
                    c = (wchar_t)std::towupper(c);
                    wordStart = false;
                } else {
                    c = (wchar_t)std::towlower(c);
                }
            }
        }
        break;
    default:
        break;
    }
    return s;
}

// ----------------------------------------------------------------------------
// 扩展命令 (the 预设方案 list on the 扩展更名 tab)
// The 扩展命令 box is a stack of command names, because double-clicking a preset appends
// instead of replacing. So the text is consumed greedily: at each position the first known
// command name wins, separators and unrecognised text are stepped over one character at a
// time. The three 汉字→拼音 presets have no entry here on purpose - they need a Han/pinyin
// table that does not exist yet, and the UI refuses them before this code is reached.
// ----------------------------------------------------------------------------
static const wchar_t* kCnDigits[10] = {
    L"零", L"一", L"二", L"三", L"四", L"五", L"六", L"七", L"八", L"九"
};

static int CnDigitVal(wchar_t c) {
    for (int i = 0; i < 10; ++i)
        if (c == kCnDigits[i][0]) return i;
    return -1;
}

static int CnUnitVal(wchar_t c) {
    if (c == L'十') return 10;
    if (c == L'百') return 100;
    if (c == L'千') return 1000;
    return 0;
}

// 0..9999 spelled the way it is read: 12 -> 十二 (no leading 一), 105 -> 一百零五.
static std::wstring NumToCn(int v) {
    if (v < 0) v = 0;
    if (v < 10) return kCnDigits[v];
    const int place[4] = { 1000, 100, 10, 1 };
    const wchar_t* placeName[3] = { L"千", L"百", L"十" };
    std::wstring out;
    bool zeroPending = false, started = false;
    for (int i = 0; i < 4; ++i) {
        int d = (v / place[i]) % 10;
        if (d == 0) { if (started) zeroPending = true; continue; }
        if (zeroPending) out += kCnDigits[0];
        zeroPending = false;
        if (!(i == 2 && d == 1 && out.empty())) out += kCnDigits[d];
        if (i < 3) out += placeName[i];
        started = true;
    }
    return out;
}

// One run of numeral characters, spelled as a number ("一百零五" -> 105, "十" -> 10).
static int CnRunToNum(const std::wstring& s, size_t begin, size_t end) {
    int total = 0, cur = 0;
    for (size_t i = begin; i < end; ++i) {
        int d = CnDigitVal(s[i]);
        if (d >= 0) { cur = d; continue; }
        int u = CnUnitVal(s[i]);
        if (u == 0) continue;
        total += (cur == 0 ? 1 : cur) * u;
        cur = 0;
    }
    return total + cur;
}

static std::wstring CnNumToArabic(const std::wstring& s) {
    std::wstring out;
    size_t i = 0;
    while (i < s.size()) {
        if (CnDigitVal(s[i]) < 0 && CnUnitVal(s[i]) == 0) { out += s[i++]; continue; }
        size_t j = i;
        bool hasUnit = false;
        while (j < s.size() && (CnDigitVal(s[j]) >= 0 || CnUnitVal(s[j]) > 0)) {
            if (CnUnitVal(s[j]) > 0) hasUnit = true;
            ++j;
        }
        if (!hasUnit) {
            // No 十/百/千 means the run reads as digits ("一二三" is 123, not 3).
            for (size_t k = i; k < j; ++k) out += std::to_wstring(CnDigitVal(s[k]));
        } else {
            out += std::to_wstring(CnRunToNum(s, i, j));
        }
        i = j;
    }
    return out;
}

static std::wstring ArabicToCnNum(const std::wstring& s) {
    std::wstring out;
    size_t i = 0;
    while (i < s.size()) {
        if (!(s[i] >= L'0' && s[i] <= L'9')) { out += s[i++]; continue; }
        size_t j = i;
        while (j < s.size() && s[j] >= L'0' && s[j] <= L'9') ++j;
        // A run with a leading zero is a code rather than a number ("007"), and anything
        // longer than four digits has no 千-based spelling; both go digit by digit.
        int n = 0;
        bool spell = (j - i <= 4) && !(j - i > 1 && s[i] == L'0');
        if (spell) { try { n = std::stoi(s.substr(i, j - i)); } catch (...) { spell = false; } }
        if (spell) out += NumToCn(n);
        else for (size_t k = i; k < j; ++k) out += kCnDigits[s[k] - L'0'];
        i = j;
    }
    return out;
}

static std::wstring SwapLetterCase(std::wstring s) {
    for (auto& c : s) {
        if (c >= L'a' && c <= L'z') c = (wchar_t)(c - (L'a' - L'A'));
        else if (c >= L'A' && c <= L'Z') c = (wchar_t)(c + (L'a' - L'A'));
    }
    return s;
}

// 一大一小 starts upper case, 一小一大 starts lower. Only ASCII letters advance the
// alternation, so "a1b2" still becomes "A1B2".
static std::wstring AlternateLetterCase(std::wstring s, bool bigFirst) {
    bool big = bigFirst;
    for (auto& c : s) {
        bool lower = (c >= L'a' && c <= L'z'), upper = (c >= L'A' && c <= L'Z');
        if (!lower && !upper) continue;
        if (big && lower) c = (wchar_t)(c - (L'a' - L'A'));
        if (!big && upper) c = (wchar_t)(c + (L'a' - L'A'));
        big = !big;
    }
    return s;
}

// ----------------------------------------------------------------------------
// 汉字 -> 拼音
//   内置表直接吃 pinyin-data 的上游原文件 (pinyin.txt, MIT)：原样 RCDATA 打进 exe，
//   第一次用到才解析，所以升级拼音表只要覆盖文件重新编译。
//   上游的读音带声调，这里用 StripTone 去掉；多音字取第一个读音（上游按常用度排序）；
//   ü 写成 v，因为文件名里习惯用 v。声母是从读音推出来的，所以表里不用第二列。
//   表里没有的字（比如 BMP 之外的生僻字）原样保留。
// ----------------------------------------------------------------------------
static std::vector<std::pair<unsigned int, std::wstring>> g_pinyin;   // 按码位升序

// 返回去掉声调后的字母；0 表示这个字符要丢掉（组合用调号、非字母）。
static wchar_t StripTone(wchar_t c) {
    if (c < 0x80) return (c >= L'a' && c <= L'z') ? c : 0;
    switch (c) {
    case 0x0101: case 0x00E1: case 0x01CE: case 0x00E0: return L'a';   // ā á ǎ à
    case 0x0113: case 0x00E9: case 0x011B: case 0x00E8: case 0x00EA:   // ē é ě è ê
    case 0x1EBF: case 0x1EC1: return L'e';                             // ế ề
    case 0x012B: case 0x00ED: case 0x01D0: case 0x00EC: return L'i';   // ī í ǐ ì
    case 0x014D: case 0x00F3: case 0x01D2: case 0x00F2: return L'o';   // ō ó  ò
    case 0x016B: case 0x00FA: case 0x01D4: case 0x00F9: return L'u';   // ū ú ǔ ù
    case 0x01D6: case 0x01D8: case 0x01DA: case 0x01DC: case 0x00FC:   // ǖ ǘ ǚ ǜ ü
        return L'v';
    case 0x0144: case 0x0148: case 0x01F9: return L'n';                // ń ň ǹ
    case 0x1E3F: return L'm';                                          // ḿ
    default: return 0;                                                 // 含 ̄ ̌ ̀ 组合调号
    }
}

static void EnsurePinyin() {
    static bool done = false;
    if (done) return;
    done = true;                       // 资源缺失也别每个字都重试一遍
    std::wstring text = LoadResText(IDR_PINYIN);
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find(L'\n', pos);
        if (eol == std::wstring::npos) eol = text.size();
        std::wstring line = text.substr(pos, eol - pos);
        pos = eol + 1;
        // 一行形如 "U+3400: qiū  # 㐀"；# 开头的是上游注释。
        if (line.size() < 8 || line[0] != L'U' || line[1] != L'+') continue;
        size_t colon = line.find(L':', 2);
        if (colon == std::wstring::npos || colon > 9) continue;
        unsigned int cp = 0;
        bool bad = false;
        for (size_t k = 2; k < colon; k++) {
            wchar_t d = line[k];
            unsigned int v;
            if (d >= L'0' && d <= L'9') v = d - L'0';
            else if (d >= L'A' && d <= L'F') v = d - L'A' + 10;
            else if (d >= L'a' && d <= L'f') v = d - L'a' + 10;
            else { bad = true; break; }
            cp = cp * 16 + v;
        }
        if (bad || cp == 0 || cp > 0xFFFF) continue;   // 只进 BMP，和 UTF-16 逐字查找一致
        size_t sharp = line.find(L'#', colon);
        if (sharp == std::wstring::npos) sharp = line.size();
        std::wstring field = line.substr(colon + 1, sharp - colon - 1);
        size_t comma = field.find(L',');               // 多音字取第一个读音
        if (comma != std::wstring::npos) field.resize(comma);
        std::wstring py;
        for (wchar_t c : field) {
            wchar_t p = StripTone(c);
            if (p) py.push_back(p);
        }
        if (!py.empty()) g_pinyin.push_back(std::make_pair(cp, py));
    }
    std::sort(g_pinyin.begin(), g_pinyin.end(),
              [](const std::pair<unsigned int, std::wstring>& x,
                 const std::pair<unsigned int, std::wstring>& y) {
                  return x.first < y.first;
              });
    size_t w = 0;
    for (size_t i = 0; i < g_pinyin.size(); i++)
        if (w == 0 || g_pinyin[i].first != g_pinyin[w - 1].first) g_pinyin[w++] = g_pinyin[i];
    g_pinyin.resize(w);
}

static const std::wstring* PinyinOf(wchar_t ch) {
    EnsurePinyin();
    unsigned int cp = (unsigned int)(unsigned short)ch;
    size_t lo = 0, hi = g_pinyin.size();
    while (lo < hi) {
        size_t m = lo + (hi - lo) / 2;
        if (g_pinyin[m].first < cp) lo = m + 1; else hi = m;
    }
    if (lo < g_pinyin.size() && g_pinyin[lo].first == cp) return &g_pinyin[lo].second;
    return nullptr;
}

// zh/ch/sh are single initials, so they have to be taken before their first letter.
// A zero-initial syllable (安/an, 一/yi) contributes its opening letter.
static std::wstring ShengmuOf(const std::wstring& py) {
    if (py.size() > 1 && (py[0] == L'z' || py[0] == L'c' || py[0] == L's') && py[1] == L'h')
        return py.substr(0, 2);
    return py.substr(0, 1);
}

static std::wstring HanToPinyin(const std::wstring& s, bool capEach) {
    std::wstring out;
    for (wchar_t c : s) {
        const std::wstring* py = PinyinOf(c);
        if (!py) { out += c; continue; }
        if (capEach) { out += (wchar_t)std::towupper((*py)[0]); out += py->substr(1); }
        else out += *py;
    }
    return out;
}

static std::wstring PrependShengmu(const std::wstring& s) {
    for (wchar_t c : s) {
        const std::wstring* py = PinyinOf(c);
        if (py) return ShengmuOf(*py) + s;
    }
    return s;
}

static const wchar_t* const kExtCmds[] = {
    // 汉字转换为拼音 is a prefix of 汉字转换为拼音且首字母大写, so the longer name must
    // be tried first by the scanner below.
    L"英文字母大小写翻转", L"英文字母一大一小", L"英文字母一小一大",
    L"字符顺序颠倒", L"字符转换为数值", L"数值转换为字符",
    L"汉字转换为拼音且首字母大写", L"汉字转换为拼音", L"添加第一个汉字的拼音声母",
};

static std::wstring ApplyExtCommands(const std::wstring& name, const std::wstring& cmds) {
    std::wstring s = name;
    size_t i = 0;
    while (i < cmds.size()) {
        wchar_t c = cmds[i];
        if (c == L' ' || c == L'\r' || c == L'\n' || c == L'\t' ||
            c == L';' || c == L'；' || c == L',' || c == L'，') { ++i; continue; }
        int kind = -1;
        for (int k = 0; k < (int)(sizeof(kExtCmds) / sizeof(kExtCmds[0])); ++k) {
            size_t n = wcslen(kExtCmds[k]);
            if (cmds.compare(i, n, kExtCmds[k]) == 0) { kind = k; i += n; break; }
        }
        switch (kind) {
        case 0: s = SwapLetterCase(s); break;
        case 1: s = AlternateLetterCase(s, true); break;
        case 2: s = AlternateLetterCase(s, false); break;
        case 3: std::reverse(s.begin(), s.end()); break;
        case 4: s = CnNumToArabic(s); break;
        case 5: s = ArabicToCnNum(s); break;
        case 6: s = HanToPinyin(s, true); break;
        case 7: s = HanToPinyin(s, false); break;
        case 8: s = PrependShengmu(s); break;
        default: ++i; break;
        }
    }
    return s;
}

// ----------------------------------------------------------------------------
// Meta variable expansion
//   self = current in-progress full name (for <SELF>); defaults to original stem
// ----------------------------------------------------------------------------
static std::wstring RandomHex(size_t len) {
    static std::mt19937 rng((unsigned)std::chrono::steady_clock::now().time_since_epoch().count());
    static const wchar_t* hex = L"0123456789ABCDEF";
    std::wstring out;
    std::uniform_int_distribution<int> d(0, 15);
    for (size_t i = 0; i < len; ++i) out.push_back(hex[d(rng)]);
    return out;
}

// 元变量 (设置 > 元变量). The token set is exactly the table that page shows, so what the
// user reads there is what the engine understands. C-style tokens come from the file's
// creation time, the plain ones from its modification time.
static void ExpandMeta(std::wstring& s, const fs::path& p,
                       const std::wstring& origStem,
                       const std::wstring& origExtNoDot,
                       bool enable, int index,
                       const std::wstring& self = L"") {
    if (!enable) return;
    const std::wstring selfVal = self.empty() ? origStem : self;

    std::wstring dateStr, timeStr, year, month, day, hour, min, sec;
    std::wstring cdateStr, ctimeStr, cyear, cmonth, cday, chour, cmin, csec;
    std::wstring sizeStr;
    struct _stat64 st;
    if (_wstat64(p.c_str(), &st) == 0) {
        wchar_t buf[32];
        struct tm tmv;
        time_t tt = st.st_mtime;
        localtime_s(&tmv, &tt);
        wcsftime(buf, 32, L"%Y-%m-%d", &tmv); dateStr = buf;
        wcsftime(buf, 32, L"%H-%M-%S", &tmv); timeStr = buf;
        wcsftime(buf, 32, L"%Y", &tmv); year = buf;
        wcsftime(buf, 32, L"%m", &tmv); month = buf;
        wcsftime(buf, 32, L"%d", &tmv); day = buf;
        wcsftime(buf, 32, L"%H", &tmv); hour = buf;
        wcsftime(buf, 32, L"%M", &tmv); min = buf;
        wcsftime(buf, 32, L"%S", &tmv); sec = buf;
        // On Windows st_ctime is the creation time, not an inode-change time.
        tt = st.st_ctime;
        localtime_s(&tmv, &tt);
        wcsftime(buf, 32, L"%Y-%m-%d", &tmv); cdateStr = buf;
        wcsftime(buf, 32, L"%H-%M-%S", &tmv); ctimeStr = buf;
        wcsftime(buf, 32, L"%Y", &tmv); cyear = buf;
        wcsftime(buf, 32, L"%m", &tmv); cmonth = buf;
        wcsftime(buf, 32, L"%d", &tmv); cday = buf;
        wcsftime(buf, 32, L"%H", &tmv); chour = buf;
        wcsftime(buf, 32, L"%M", &tmv); cmin = buf;
        wcsftime(buf, 32, L"%S", &tmv); csec = buf;
        swprintf(buf, 32, L"%llu", (unsigned long long)st.st_size); sizeStr = buf;
    }

    std::wstring parent = p.parent_path().filename().wstring();

    auto rep = [&](const wchar_t* tok, const std::wstring& val) {
        s = ReplaceAll(s, tok, val);
    };
    rep(L"<DATE>", dateStr);
    rep(L"<Y>", year);
    rep(L"<M>", month);
    rep(L"<D>", day);
    rep(L"<TIME>", timeStr);
    rep(L"<H>", hour);
    rep(L"<TM>", min);
    rep(L"<S>", sec);
    rep(L"<CDATE>", cdateStr);
    rep(L"<CY>", cyear);
    rep(L"<CM>", cmonth);
    rep(L"<CD>", cday);
    rep(L"<CTIME>", ctimeStr);
    rep(L"<CH>", chour);
    rep(L"<CTM>", cmin);
    rep(L"<CS>", csec);
    rep(L"<SIZE>", sizeStr);
    rep(L"<NAME>", origStem);
    rep(L"<EXT>", origExtNoDot);
    rep(L"<.EXT>", origExtNoDot.empty() ? L"" : (L"." + origExtNoDot));
    rep(L"<PARENT>", parent);
    rep(L"<SELF>", selfVal);
    rep(L"<R>", RandomHex(8));
    rep(L"<N>", std::to_wstring(index + 1)); // 1-based position
}

// ----------------------------------------------------------------------------
// CRC32
// ----------------------------------------------------------------------------
static DWORD g_crcTable[256];
static bool g_crcInit = false;
static void InitCrc() {
    for (DWORD i = 0; i < 256; ++i) {
        DWORD c = i;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
        g_crcTable[i] = c;
    }
    g_crcInit = true;
}

std::wstring Crc32Hex(const fs::path& p) {
    if (!g_crcInit) InitCrc();
    DWORD crc = 0xFFFFFFFF;
    try {
        std::ifstream f(p, std::ios::binary);
        if (f) {
            char buf[65536];
            while (f.read(buf, sizeof(buf)) || f.gcount() > 0) {
                std::streamsize n = f.gcount();
                for (std::streamsize i = 0; i < n; ++i) {
                    crc = g_crcTable[(crc ^ (unsigned char)buf[i]) & 0xFF] ^ (crc >> 8);
                }
                if (f.eof()) break;
            }
        }
    } catch (...) {}
    crc ^= 0xFFFFFFFF;
    wchar_t buf[16];
    swprintf_s(buf, 16, L"%08X", crc);
    return buf;
}

// ----------------------------------------------------------------------------
// Split a "stem.ext" full name into (stem, ext-with-dot)
// Files beginning with '.' (e.g. ".gitignore") are treated as having no ext.
// ----------------------------------------------------------------------------
static void SplitName(const std::wstring& cur, std::wstring& stem, std::wstring& ext) {
    size_t dot = cur.rfind(L'.');
    if (dot == std::wstring::npos || dot == 0) {
        stem = cur;
        ext = L"";
    } else {
        stem = cur.substr(0, dot);
        ext = cur.substr(dot); // includes the leading dot
    }
}

// ----------------------------------------------------------------------------
// 音频标签读取 (hand-rolled: no tag library is linked)
// ----------------------------------------------------------------------------
// Containers covered: MP3 (ID3v2.2/2.3/2.4 + ID3v1), FLAC and OGG/Opus (Vorbis
// comment), APE (APEv2), M4A/AAC (MP4 udta/meta/ilst) and WAV (RIFF INFO). Each reader
// checks its own magic bytes, so the dispatcher can call them in any order and the two
// tail formats (APEv2, ID3v1) only fill in fields the main block left empty.
//
// A tag block is fetched with its own seek + read instead of mapping the file, so a
// 300 MB .ape costs the same few kilobytes as a 4 MB .mp3.

// Positional reader for one file. Every read is clamped to the file size and to `cap`.
struct TagFile {
    HANDLE h = INVALID_HANDLE_VALUE;
    unsigned long long size = 0;
    explicit TagFile(const fs::path& p) {
        h = CreateFileW(p.c_str(), GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return;
        LARGE_INTEGER li;
        if (GetFileSizeEx(h, &li) && li.QuadPart > 0) size = (unsigned long long)li.QuadPart;
    }
    ~TagFile() { Close(); }
    // 替换文件前必须放手：读句柄还在的时候 MoveFileEx 会 ACCESS_DENIED。
    void Close() { if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); h = INVALID_HANDLE_VALUE; } }
    TagFile(const TagFile&) = delete;
    TagFile& operator=(const TagFile&) = delete;
    bool Ok() const { return h != INVALID_HANDLE_VALUE && size >= 16; }
    bool At(unsigned long long off, std::vector<unsigned char>& out,
            unsigned long long want, unsigned long long cap = 0) const {
        out.clear();
        if (!Ok() || off >= size) return false;
        if (want > size - off) want = size - off;
        if (cap && want > cap) want = cap;
        if (want > kMaxRead) want = kMaxRead;
        if (want == 0) return false;
        out.resize((size_t)want);
        LARGE_INTEGER li; li.QuadPart = (LONGLONG)off;
        if (!SetFilePointerEx(h, li, NULL, FILE_BEGIN)) { out.clear(); return false; }
        DWORD got = 0;
        if (!ReadFile(h, out.data(), (DWORD)want, &got, NULL) || got == 0) {
            out.clear();
            return false;
        }
        out.resize(got);
        return true;
    }
    static const unsigned long long kMaxRead = 64ull << 20;
};

static uint32_t Be32(const unsigned char* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint32_t Le32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t Be16(const unsigned char* p) { return (uint16_t)((p[0] << 8) | p[1]); }
// ID3v2 sizes are 7 bits per byte.
static uint32_t Sync32(const unsigned char* p) {
    return ((uint32_t)(p[0] & 0x7F) << 21) | ((uint32_t)(p[1] & 0x7F) << 14) |
           ((uint32_t)(p[2] & 0x7F) << 7) | (p[3] & 0x7F);
}

static std::wstring CpToW(UINT cp, const char* s, int n) {
    if (n <= 0) return L"";
    int w = MultiByteToWideChar(cp, 0, s, n, NULL, 0);
    if (w <= 0) return L"";
    std::wstring out((size_t)w, L'\0');
    MultiByteToWideChar(cp, 0, s, n, &out[0], w);
    return out;
}

static std::wstring Utf16ToW(const unsigned char* p, size_t n, bool be) {
    std::wstring out;
    for (size_t i = 0; i + 1 < n; i += 2) {
        uint16_t u = be ? Be16(p + i) : (uint16_t)(p[i] | (p[i + 1] << 8));
        if (!u) break;
        out.push_back((wchar_t)u);
    }
    return out;
}

// Drop everything after the first terminator and both ends of padding. Tag fields are
// fixed-width in the legacy formats, so trailing spaces and NULs are the norm.
static void CleanText(std::wstring& s) {
    size_t z = s.find(L'\0');
    if (z != std::wstring::npos) s.erase(z);
    size_t e = s.size();
    while (e > 0 && (s[e - 1] == L' ' || s[e - 1] == L'\r' || s[e - 1] == L'\n' || s[e - 1] == L'\t'))
        --e;
    s.erase(e);
    size_t b = 0;
    while (b < s.size() && (s[b] == L' ' || s[b] == L'\t')) ++b;
    if (b) s.erase(0, b);
}

// "(13)Pop" carries a numeric ID3v1 genre index in front of the real name. Only that
// wrapper is removed; a bare "(13)" is left alone rather than mapped through a table
// this file does not carry.
static std::wstring UnwrapGenre(std::wstring v) {
    if (v.size() > 2 && v[0] == L'(') {
        size_t close = v.find(L')');
        if (close != std::wstring::npos && close > 1 && close <= 4) {
            bool digits = true;
            for (size_t i = 1; i < close; ++i)
                if (v[i] < L'0' || v[i] > L'9') digits = false;
            if (digits) v.erase(0, close + 1);
        }
    }
    return v;
}

// Route one key/value pair into the field it names. The key arrives in whatever
// spelling the container uses (ID3 frame ID, Vorbis/MP4/APE name) and is upper-cased.
static void SetTagField(AudioTags& t, const std::string& key, std::wstring val) {
    CleanText(val);
    if (val.empty()) return;
    std::string k = key;
    for (auto& c : k) c = (char)toupper((unsigned char)c);
    std::wstring* dst = nullptr;
    if (k == "ARTIST" || k == "TPE1" || k == "TP1" || k == "\xA9" "ART") dst = &t.artist;
    else if (k == "TITLE" || k == "TIT2" || k == "TT2" || k == "\xA9" "NAM") dst = &t.title;
    else if (k == "ALBUM" || k == "TALB" || k == "TAL" || k == "\xA9" "ALB") dst = &t.album;
    else if (k == "TRACKNUMBER" || k == "TRACK" || k == "TRCK" || k == "TRK" || k == "TRKN") dst = &t.track;
    else if (k == "YEAR" || k == "DATE" || k == "TYER" || k == "TDRC" || k == "TYE" || k == "\xA9" "DAY") dst = &t.year;
    else if (k == "COMMENT" || k == "COMM" || k == "COM" || k == "\xA9" "CMT") dst = &t.comment;
    else if (k == "GENRE" || k == "TCON" || k == "TCO" || k == "\xA9" "GEN") dst = &t.genre;
    else return;
    if (dst == &t.genre) val = UnwrapGenre(val);
    // DATE/TDRC/ICRD often carry a full ISO date; the year is what 年代 wants.
    if (dst == &t.year && val.size() > 4) {
        bool fourDigits = val[0] >= L'0' && val[0] <= L'9' && val[1] >= L'0' && val[1] <= L'9' &&
                          val[2] >= L'0' && val[2] <= L'9' && val[3] >= L'0' && val[3] <= L'9';
        if (fourDigits) val.erase(4);
    }
    if (dst->empty()) *dst = val;
}

// Copy only the fields `d` does not have yet.
static void MergeTags(AudioTags& d, const AudioTags& s) {
    if (d.artist.empty()) d.artist = s.artist;
    if (d.title.empty()) d.title = s.title;
    if (d.album.empty()) d.album = s.album;
    if (d.track.empty()) d.track = s.track;
    if (d.year.empty()) d.year = s.year;
    if (d.comment.empty()) d.comment = s.comment;
    if (d.genre.empty()) d.genre = s.genre;
}

static std::wstring DecodeId3Text(const unsigned char* p, size_t n, int enc) {
    if (enc == 1) {                                   // UTF-16 with byte-order mark
        if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) return Utf16ToW(p + 2, n - 2, false);
        if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) return Utf16ToW(p + 2, n - 2, true);
        return Utf16ToW(p, n, false);
    }
    if (enc == 2) return Utf16ToW(p, n, true);        // UTF-16BE, no mark
    if (enc == 3) return CpToW(CP_UTF8, (const char*)p, (int)n);
    return CpToW(1252, (const char*)p, (int)n);       // 0: ISO-8859-1
}

// ID3v2 "unsynchronisation": every 0x00 that was inserted after a 0xFF is removed.
static std::vector<unsigned char> Deunsynch(const std::vector<unsigned char>& b) {
    std::vector<unsigned char> out;
    out.reserve(b.size());
    for (size_t i = 0; i < b.size(); ++i) {
        out.push_back(b[i]);
        if (b[i] == 0xFF && i + 1 < b.size() && b[i + 1] == 0x00) ++i;
    }
    return out;
}

static void ReadId3v2(const TagFile& f, AudioTags& t) {
    std::vector<unsigned char> h;
    if (!f.At(0, h, 10, 0) || h.size() < 10 || memcmp(h.data(), "ID3", 3)) return;
    int ver = h[3];
    if (ver < 2 || ver > 4) return;
    uint32_t sz = Sync32(&h[6]);
    std::vector<unsigned char> b;
    if (!f.At(10, b, sz, 16u << 20) || b.empty()) return;
    if (h[5] & 0x80) b = Deunsynch(b);

    const int idLen = (ver == 2) ? 3 : 4;
    const int hdrLen = idLen + ((ver == 2) ? 3 : 6);
    size_t pos = 0;
    while (pos + (size_t)hdrLen <= b.size()) {
        if (b[pos] == 0) break;                       // start of the padding area
        std::string id((const char*)&b[pos], idLen);
        uint32_t fsz;
        uint16_t flags = 0;
        if (ver == 2) {
            fsz = ((uint32_t)b[pos + 3] << 16) | ((uint32_t)b[pos + 4] << 8) | b[pos + 5];
        } else {
            fsz = (ver == 4) ? Sync32(&b[pos + 4]) : Be32(&b[pos + 4]);
            flags = (uint16_t)((b[pos + 8] << 8) | b[pos + 9]);
        }
        pos += hdrLen;
        if (fsz == 0 || fsz > b.size() - pos) break;
        std::vector<unsigned char> fr(b.begin() + pos, b.begin() + pos + fsz);
        pos += fsz;
        if (ver == 4 && (flags & 0x02)) fr = Deunsynch(fr);
        // v2.2 的注释 frame 叫 COM，不在这个判断里就会被整帧丢掉。
        bool isComm = (id == "COMM" || id == "COM");
        if (id[0] != 'T' && !isComm) continue;
        if (fr.empty()) continue;
        int enc = fr[0];
        const unsigned char* vp = fr.data() + 1;
        size_t vn = fr.size() - 1;
        if (isComm) {
            // encoding, 3-byte language, short description (NUL-terminated), real text.
            // v2.2 的 COMM 没有短描述这一项，跳过去会把注释的头几个字吃掉。
            if (vn < 3) continue;
            vp += 3; vn -= 3;
            if (ver != 2) {
                size_t sep = 0;
                if (enc == 1 || enc == 2) {
                    while (sep + 1 < vn && (vp[sep] | vp[sep + 1])) sep += 2;
                    sep += 2;
                } else {
                    while (sep < vn && vp[sep]) ++sep;
                    ++sep;
                }
                if (sep > vn) continue;
                vp += sep; vn -= sep;
            }
        }
        SetTagField(t, id, DecodeId3Text(vp, vn, enc));
    }
}

static void ReadId3v1(const TagFile& f, AudioTags& t) {
    if (f.size < 128) return;
    std::vector<unsigned char> b;
    if (!f.At(f.size - 128, b, 128, 0) || b.size() < 128) return;
    if (memcmp(b.data(), "TAG", 3)) return;
    auto field = [&](size_t off, size_t n) {
        std::wstring s = CpToW(CP_ACP, (const char*)&b[off], (int)n);
        CleanText(s);
        return s;
    };
    t.title = field(3, 30);
    t.artist = field(33, 30);
    t.album = field(63, 30);
    t.year = field(93, 4);
    // v1.1 borrows the last two comment bytes for the track number.
    if (b[125] == 0 && b[126] != 0) {
        wchar_t buf[16];
        swprintf_s(buf, 16, L"%d", b[126]);
        t.track = buf;
        t.comment = field(97, 28);
    } else {
        t.comment = field(97, 30);
    }
    // The genre byte indexes a table this file does not carry, so it is left unread
    // rather than shown as a wrong name.
}

// Vorbis comment block: vendor string then "KEY=value" pairs, all UTF-8 little-endian.
// Shared by FLAC and OGG/Opus.
static void ParseVorbis(const unsigned char* p, size_t n, AudioTags& t) {
    if (n < 4) return;
    uint32_t vl = Le32(p);
    p += 4; n -= 4;
    if (vl > n) return;
    p += vl; n -= vl;
    if (n < 4) return;
    uint32_t cnt = Le32(p);
    p += 4; n -= 4;
    if (cnt > 512) cnt = 512;
    for (uint32_t i = 0; i < cnt && n >= 4; ++i) {
        uint32_t l = Le32(p);
        p += 4; n -= 4;
        if (l > n) break;
        std::string kv((const char*)p, l);
        p += l; n -= l;
        size_t eq = kv.find('=');
        if (eq == std::string::npos) continue;
        SetTagField(t, kv.substr(0, eq),
                    CpToW(CP_UTF8, kv.c_str() + eq + 1, (int)(kv.size() - eq - 1)));
    }
}

static void ReadFlac(const TagFile& f, AudioTags& t) {
    std::vector<unsigned char> h;
    if (!f.At(0, h, 4, 0) || h.size() < 4 || memcmp(h.data(), "fLaC", 4)) return;
    unsigned long long off = 4;
    for (int i = 0; i < 128; ++i) {
        std::vector<unsigned char> bh;
        if (!f.At(off, bh, 4, 0) || bh.size() < 4) return;
        bool last = (bh[0] & 0x80) != 0;
        uint32_t type = bh[0] & 0x7F;
        uint32_t len = ((uint32_t)bh[1] << 16) | ((uint32_t)bh[2] << 8) | bh[3];
        off += 4;
        if (type == 4) {                              // VORBIS_COMMENT
            std::vector<unsigned char> v;
            if (f.At(off, v, len, 8u << 20)) ParseVorbis(v.data(), v.size(), t);
        }
        if (last) return;
        off += len;
    }
}

static void ReadOgg(const TagFile& f, AudioTags& t) {
    std::vector<unsigned char> b;
    if (!f.At(0, b, 65536, 0) || b.size() < 8 || memcmp(b.data(), "OggS", 4)) return;
    // The comment packet is the second packet of the audio stream, so it is always near
    // the front; scanning the raw first pages finds it without reassembling segments.
    // Vorbis 注释包魔数是 7 字节的 "\x03vorbis" (0x03=注释头), Opus 是 8 字节的 "OpusTags"。
    for (size_t j = 0; j + 9 < b.size(); ++j) {
        if (!memcmp(&b[j], "\x03" "vorbis", 7)) { ParseVorbis(&b[j + 7], b.size() - j - 7, t); return; }
        if (!memcmp(&b[j], "OpusTags", 8)) { ParseVorbis(&b[j + 8], b.size() - j - 8, t); return; }
    }
}

static void ReadApev2(const TagFile& f, AudioTags& t) {
    std::vector<unsigned char> ft;
    if (!f.At(f.size - 32, ft, 32, 0) || ft.size() < 32) return;
    if (memcmp(ft.data(), "APETAGEX", 8)) return;
    // APEv2 footer: magic(8) version(4) tagSize(4) itemCount(4) flags(4) reserved(8)。
    // tagSize = items + footer, 不含 header; flags 位31 = 带 header, 位30 = 本条是 header。
    uint32_t tagSize = Le32(&ft[12]);
    uint32_t items = Le32(&ft[16]);
    uint32_t flags = Le32(&ft[20]);
    bool hasHeader = (flags & 0x80000000u) != 0;
    if (items > 512) items = 512;
    auto parseItems = [&](unsigned long long start, uint32_t want) -> bool {
        std::vector<unsigned char> b;
        if (!f.At(start, b, want, 8u << 20) || b.size() != want) return false;
        size_t p = 0;
        uint32_t seen = 0;
        AudioTags tmp;
        for (uint32_t i = 0; i < items && p + 8 <= b.size(); ++i) {
            uint32_t vsz = Le32(&b[p]);
            uint32_t vflags = Le32(&b[p + 4]);
            size_t keyStart = p + 8;
            size_t keyEnd = keyStart;
            while (keyEnd < b.size() && b[keyEnd]) ++keyEnd;
            if (keyEnd >= b.size()) return false;
            std::string key((const char*)&b[keyStart], keyEnd - keyStart);
            size_t v0 = keyEnd + 1;
            if (vsz > b.size() - v0) return false;
            // bits 1-2 of the item flags: 0 text, 1 binary (cover art), 2 external
            if (((vflags >> 1) & 3) == 0) {
                std::wstring val = CpToW(CP_UTF8, (const char*)&b[v0], (int)vsz);
                SetTagField(tmp, key, val);
            }
            p = v0 + vsz;
            ++seen;
        }
        if (seen != items) return false;
        MergeTags(t, tmp);
        return true;
    };
    // items 起点: 规范约定 tagSize = items + footer (不含 header), items 紧挨 footer 之前,
    // 所以起点 = f.size - tagSize; 少数写入器把 header 也算进 tagSize, 起点 = f.size + 32 - tagSize。
    unsigned long long lenA = (unsigned long long)tagSize - 32;          // 规范: items + footer
    unsigned long long startA = f.size - (unsigned long long)tagSize;
    if (tagSize > 32 && lenA <= (8u << 20) && startA + lenA <= f.size && parseItems(startA, (uint32_t)lenA))
        return;
    // 少数写入器把 header 也算进 tagSize, 再按那个约定试一次。
    if (hasHeader && tagSize > 64) {
        unsigned long long lenB = (unsigned long long)tagSize - 64;      // header + items + footer
        unsigned long long startB = f.size + 32 - (unsigned long long)tagSize;
        if (startB + lenB <= f.size) parseItems(startB, (uint32_t)lenB);
    }
}

// ---- MP4 (M4A / AAC in an MP4 container) ----

static void Mp4Items(const std::vector<unsigned char>& d, size_t off, size_t end, AudioTags& t) {
    while (off + 8 <= end) {
        uint32_t sz = Be32(&d[off]);
        std::string name((const char*)&d[off + 4], 4);
        if (sz < 8 || off + sz > end) return;
        size_t c0 = off + 8, c1 = off + sz;
        if (c0 + 16 <= c1 && Be32(&d[c0]) >= 16 && !memcmp(&d[c0 + 4], "data", 4)) {
            uint32_t dsize = Be32(&d[c0]);
            uint32_t flags = Be32(&d[c0 + 8]) & 0x00FFFFFF;
            size_t v0 = c0 + 16;
            size_t v1 = c0 + dsize;
            if (v1 > c1) v1 = c1;                   // windows.h owns a min() macro
            std::wstring val;
            if (name == "trkn") {
                if (v1 - v0 >= 4 && Be16(&d[v0 + 2])) {
                    wchar_t buf[16];
                    swprintf_s(buf, 16, L"%u", (unsigned)Be16(&d[v0 + 2]));
                    val = buf;
                }
            } else if (flags != 13 && flags != 14 && flags != 21) {   // not cover art
                val = CpToW(CP_UTF8, (const char*)&d[v0], (int)(v1 - v0));
            }
            SetTagField(t, name, val);
        }
        off = c1;
    }
}

// `meta` carries a 4-byte version/flags field before its children; every other box
// here holds children directly.
static void Mp4Scan(const std::vector<unsigned char>& d, size_t off, size_t end, int depth, AudioTags& t) {
    if (depth > 6) return;
    while (off + 8 <= end) {
        uint64_t sz = Be32(&d[off]);
        std::string type((const char*)&d[off + 4], 4);
        size_t hdr = 8;
        if (sz == 1) {
            if (off + 16 > end) return;
            sz = ((uint64_t)Be32(&d[off + 8]) << 32) | Be32(&d[off + 12]);
            hdr = 16;
        } else if (sz == 0) {
            sz = end - off;
        }
        if (sz < hdr || off + sz > end) return;
        size_t c0 = off + hdr, c1 = off + (size_t)sz;
        if (type == "ilst") { Mp4Items(d, c0, c1, t); return; }
        if (type == "meta") Mp4Scan(d, c0 + 4 < c1 ? c0 + 4 : c1, c1, depth + 1, t);
        else if (type == "moov" || type == "udta" || type == "trak")
            Mp4Scan(d, c0, c1, depth + 1, t);
        off = c1;
    }
}

static void ReadMp4(const TagFile& f, AudioTags& t) {
    unsigned long long off = 0;
    for (int i = 0; i < 16 && off + 8 <= f.size; ++i) {
        std::vector<unsigned char> h;
        if (!f.At(off, h, 8, 0) || h.size() < 8) return;
        uint64_t sz = Be32(h.data());
        std::string type((const char*)&h[4], 4);
        unsigned long long hdr = 8;
        if (sz == 1) {
            std::vector<unsigned char> e;
            if (!f.At(off + 8, e, 8, 0) || e.size() < 8) return;
            sz = ((uint64_t)Be32(e.data()) << 32) | Be32(&e[4]);
            hdr = 16;
        } else if (sz == 0) {
            sz = f.size - off;
        }
        if (sz < hdr) return;
        if (type == "moov") {
            std::vector<unsigned char> d;
            if (f.At(off + hdr, d, sz - hdr, TagFile::kMaxRead) && !d.empty())
                Mp4Scan(d, 0, d.size(), 0, t);
            return;
        }
        // Anything else that is not a known top-level box means this is not MP4.
        if (type != "ftyp" && type != "free" && type != "mdat" && type != "wide" &&
            type != "skip" && type != "pnot" && type != "uuid")
            return;
        off += sz;
    }
}

// ---- WAV (RIFF INFO) ----

static void ParseRiffInfo(const std::vector<unsigned char>& d, size_t off, size_t end, AudioTags& t) {
    static const char* kMap[][2] = {
        { "INAM", "TITLE" }, { "IART", "ARTIST" }, { "IPRD", "ALBUM" },
        { "ITRK", "TRACKNUMBER" }, { "ICRD", "YEAR" }, { "ICMT", "COMMENT" },
        { "IGNR", "GENRE" },
    };
    while (off + 8 <= end) {
        std::string id((const char*)&d[off], 4);
        uint32_t sz = Le32(&d[off + 4]);
        size_t v0 = off + 8;
        if (v0 + sz > end) return;
        for (auto& m : kMap) {
            if (id == m[0]) {
                SetTagField(t, m[1], CpToW(CP_ACP, (const char*)&d[v0], (int)sz));
                break;
            }
        }
        off = v0 + sz + (sz & 1);
    }
}

static void ReadWav(const TagFile& f, AudioTags& t) {
    std::vector<unsigned char> h;
    if (!f.At(0, h, 12, 0) || h.size() < 12) return;
    if (memcmp(h.data(), "RIFF", 4) || memcmp(&h[8], "WAVE", 4)) return;
    uint32_t riffLen = Be32(&h[4]);
    unsigned long long end = 12ull + riffLen + (riffLen & 1);
    if (end > f.size) end = f.size;
    unsigned long long off = 12;
    for (int i = 0; i < 64 && off + 8 <= end; ++i) {
        std::vector<unsigned char> ch;
        if (!f.At(off, ch, 8, 0) || ch.size() < 8) return;
        std::string id((const char*)ch.data(), 4);
        uint32_t sz = Le32(&ch[4]);
        unsigned long long body = off + 8;
        off = body + sz + (sz & 1);
        if (id == "LIST") {
            std::vector<unsigned char> lb;
            if (f.At(body, lb, sz, 1u << 20) && lb.size() >= 4 && !memcmp(lb.data(), "INFO", 4))
                ParseRiffInfo(lb, 4, lb.size(), t);
        }
    }
}

bool IsTaggableExt(const fs::path& p) {
    static const wchar_t* kExts[] = {
        L".mp3", L".flac", L".ape", L".ogg", L".oga", L".opus", L".spx",
        L".m4a", L".m4b", L".m4p", L".mp4", L".aac", L".wav", L".wv", L".mpc"
    };
    std::wstring e = p.extension().wstring();
    std::transform(e.begin(), e.end(), e.begin(), ::towlower);
    for (auto s : kExts)
        if (e == s) return true;
    return false;
}

AudioTags ReadAudioTags(const fs::path& p, bool id3v2First) {
    AudioTags out;
    TagFile f(p);
    if (!f.Ok()) return out;
    std::wstring ext = p.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    bool isMp3 = (ext == L".mp3");
    // 设置 > 音乐文件标签 > MP3 ID3v2 标签优先. MergeTags only fills what is still empty,
    // so whichever block is read first wins: with the switch off (the default) ID3v1 is
    // read up front and ID3v2 can only supply the fields v1 left blank.
    if (isMp3 && !id3v2First) ReadId3v1(f, out);
    std::vector<unsigned char> h;
    AudioTags main;
    if (f.At(0, h, 16, 0) && h.size() >= 16) {
        if (!memcmp(h.data(), "fLaC", 4))      ReadFlac(f, main);
        else if (!memcmp(h.data(), "OggS", 4)) ReadOgg(f, main);
        else if (!memcmp(h.data(), "RIFF", 4)) ReadWav(f, main);
        else if (!memcmp(h.data(), "ID3", 3))  ReadId3v2(f, main);
        else                                   ReadMp4(f, main);
    }
    MergeTags(out, main);
    // APEv2 and ID3v1 both live at the end of the file and any of the containers above
    // may carry them, so they only fill what the main block left empty.
    AudioTags ape;
    ReadApev2(f, ape);
    MergeTags(out, ape);
    if (id3v2First && !out.HasAny()) {
        AudioTags v1;
        ReadId3v1(f, v1);
        MergeTags(out, v1);
    }
    return out;
}

// ----------------------------------------------------------------------------
// 标签写入 (标签写入 tab)
// ----------------------------------------------------------------------------
// 只重写能安全还原结构的两种容器：MP3 的 ID3v1 / ID3v2 和 FLAC 的 VORBIS_COMMENT 块。
// 读得懂但写不好的 (OGG/Opus 要重算分页 CRC、M4A 要改 stco 偏移) 一律回 1 = 不支持，
// 半写坏用户文件比不写更糟。不认识的原 frame / block 原样保留。

static std::vector<unsigned char> WToCp(UINT cp, const std::wstring& s) {
    std::vector<unsigned char> b;
    if (s.empty()) return b;
    int n = WideCharToMultiByte(cp, 0, s.c_str(), (int)s.size(), NULL, 0, NULL, NULL);
    if (n <= 0) return b;
    b.resize((size_t)n);
    WideCharToMultiByte(cp, 0, s.c_str(), (int)s.size(), (char*)b.data(), n, NULL, NULL);
    return b;
}

static bool IsPlainAscii(const std::wstring& s) {
    for (wchar_t c : s) if (c < 0x20 || c > 0x7E) return false;
    return true;
}

// UTF-16LE 带字节序标记，末尾再补一个 UTF-16 NUL，和 DecodeId3Text 的读法对称。
static std::vector<unsigned char> WToUtf16Le(const std::wstring& s) {
    std::vector<unsigned char> b = { 0xFF, 0xFE };
    for (wchar_t c : s) {
        b.push_back((unsigned char)(c & 0xFF));
        b.push_back((unsigned char)((c >> 8) & 0xFF));
    }
    b.push_back(0); b.push_back(0);
    return b;
}

static void PutBe(std::vector<unsigned char>& b, uint32_t v, int n) {
    for (int i = n - 1; i >= 0; --i) b.push_back((unsigned char)((v >> (8 * i)) & 0xFF));
}
static void PutLe(std::vector<unsigned char>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back((unsigned char)((v >> (8 * i)) & 0xFF));
}
static void PutSyncSafe(std::vector<unsigned char>& b, uint32_t v) {
    b.push_back((unsigned char)((v >> 21) & 0x7F));
    b.push_back((unsigned char)((v >> 14) & 0x7F));
    b.push_back((unsigned char)((v >> 7) & 0x7F));
    b.push_back((unsigned char)(v & 0x7F));
}

// 一个文本 frame 的正文：编码字节 + 文本。纯 ASCII 走 ISO-8859-1，否则 UTF-16，
// 这样中文标题在老播放器里也不会变问号。
static std::vector<unsigned char> TextBody(const std::wstring& val) {
    std::vector<unsigned char> body;
    if (IsPlainAscii(val)) {
        body.push_back(0);
        std::vector<unsigned char> t = WToCp(1252, val);
        body.insert(body.end(), t.begin(), t.end());
        body.push_back(0);
    } else {
        body.push_back(1);
        std::vector<unsigned char> t = WToUtf16Le(val);
        body.insert(body.end(), t.begin(), t.end());
    }
    return body;
}

// ver: 2 / 3 / 4 —— 沿用文件原有标签版本，id3 和 id4 是同一含义在两种版本下的写法。
static void AddFrame(std::vector<unsigned char>& tag, int ver, const char* id4,
                     const char* id3, const std::vector<unsigned char>& body) {
    if (body.empty()) return;
    if (ver == 2) {
        tag.insert(tag.end(), id3, id3 + 3);
        PutBe(tag, (uint32_t)body.size(), 3);
    } else {
        tag.insert(tag.end(), id4, id4 + 4);
        if (ver == 4) PutSyncSafe(tag, (uint32_t)body.size()); else PutBe(tag, (uint32_t)body.size(), 4);
        PutBe(tag, 0, 2);
    }
    tag.insert(tag.end(), body.begin(), body.end());
}

static void AddText(std::vector<unsigned char>& tag, int ver, const char* id4,
                    const char* id3, const std::wstring& val) {
    if (val.empty()) return;
    AddFrame(tag, ver, id4, id3, TextBody(val));
}

static void AddComment(std::vector<unsigned char>& tag, int ver, const std::wstring& val) {
    if (val.empty()) return;
    std::vector<unsigned char> body = TextBody(val);
    int enc = body[0];
    body.insert(body.begin() + 1, { 'e', 'n', 'g' });
    if (ver != 2) {                       // v2.2 的 COMM 没有短描述这一项
        if (enc == 1) body.insert(body.begin() + 4, { 0, 0 });
        else body.insert(body.begin() + 4, { 0 });
    }
    AddFrame(tag, ver, "COMM", "COM", body);
}

// 把已有标签拆成整块 frame，好把我们要写的那几个换掉、其余原样留下。
struct RawFrame { std::string id; std::vector<unsigned char> bytes; };

static void SplitId3v2(const std::vector<unsigned char>& b, int ver, std::vector<RawFrame>& out) {
    const int idLen = (ver == 2) ? 3 : 4;
    const int hdrLen = idLen + ((ver == 2) ? 3 : 6);
    size_t pos = 0;
    while (pos + (size_t)hdrLen <= b.size()) {
        if (b[pos] == 0) break;                       // 进了填充区
        std::string id((const char*)&b[pos], idLen);
        uint32_t fsz;
        if (ver == 2) fsz = ((uint32_t)b[pos + 3] << 16) | ((uint32_t)b[pos + 4] << 8) | b[pos + 5];
        else if (ver == 4) fsz = Sync32(&b[pos + 4]);
        else fsz = Be32(&b[pos + 4]);
        if (fsz == 0 || fsz > b.size() - pos - hdrLen) break;
        size_t total = (size_t)hdrLen + fsz;
        out.push_back({ id, std::vector<unsigned char>(b.begin() + pos, b.begin() + pos + total) });
        pos += total;
    }
}

static bool IsOurFrame(const std::string& id) {
    static const char* kIds[] = { "TIT2", "TPE1", "TALB", "TRCK", "TYER", "TDRC", "TCON",
                                  "COMM", "TT2", "TP1", "TAL", "TRK", "TYE", "TCO", "COM" };
    for (auto s : kIds) if (id == s) return true;
    return false;
}

static std::vector<unsigned char> BuildId3v2(int ver, const AudioTags& t,
                                             const std::vector<RawFrame>& keep) {
    std::vector<unsigned char> body;
    for (const auto& fr : keep)
        // 扩展头不是真 frame，新标签的 flags 全为 0，留着它会和头部对不上。
        if (!IsOurFrame(fr.id) && fr.id.compare(0, 3, "EXT") != 0)
            body.insert(body.end(), fr.bytes.begin(), fr.bytes.end());
    AddText(body, ver, "TIT2", "TT2", t.title);
    AddText(body, ver, "TPE1", "TP1", t.artist);
    AddText(body, ver, "TALB", "TAL", t.album);
    AddText(body, ver, "TRCK", "TRK", t.track);
    AddText(body, ver, "TYER", "TYE", t.year);
    AddText(body, ver, "TCON", "TCO", t.genre);
    AddComment(body, ver, t.comment);

    std::vector<unsigned char> tag = { 'I', 'D', '3', (unsigned char)ver, 0, 0 };
    PutSyncSafe(tag, (uint32_t)body.size());
    tag.insert(tag.end(), body.begin(), body.end());
    return tag;
}

// ID3v1 只有 128 字节，写不下中文，按系统 ANSI 尽力而为（这台机器就是 GBK）。
static void PadField(std::vector<unsigned char>& b, size_t off, size_t n, const std::wstring& s,
                     UINT cp) {
    std::vector<unsigned char> t = WToCp(cp, s);
    if (t.size() > n) t.resize(n);
    for (size_t i = 0; i < n; ++i) b[off + i] = i < t.size() ? t[i] : ' ';
}

static std::vector<unsigned char> BuildId3v1(const AudioTags& t) {
    std::vector<unsigned char> b(128, ' ');
    b[0] = 'T'; b[1] = 'A'; b[2] = 'G';
    UINT cp = CP_ACP;
    PadField(b, 3, 30, t.title, cp);
    PadField(b, 33, 30, t.artist, cp);
    PadField(b, 63, 30, t.album, cp);
    PadField(b, 93, 4, t.year, cp);
    size_t cn = 30;
    int track = 0;
    if (!t.track.empty()) {                 // v1.1：注释最后两字节挪给音轨号
        wchar_t* end = NULL;
        long v = wcstol(t.track.c_str(), &end, 10);
        if (end && end != t.track.c_str() && v > 0 && v <= 255) { track = (int)v; cn = 28; }
    }
    PadField(b, 97, cn, t.comment, cp);
    for (size_t i = 97 + cn; i < 125; ++i) b[i] = ' ';
    b[125] = 0;
    b[126] = (unsigned char)track;
    b[127] = 0xFF;                          // 流派名表不在这里，留给 ID3v2 的 TCON
    return b;
}

static bool HasId3v1At(const std::vector<unsigned char>& tail) {
    return tail.size() >= 128 && !memcmp(tail.data(), "TAG", 3);
}

// 只写 v1 时不必动整个文件：原地覆盖尾部 128 字节，没有标签就追加一条。
static int WriteId3v1Only(const fs::path& p, const AudioTags& t) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 2;
    int rc = 2;
    LARGE_INTEGER sz;
    std::vector<unsigned char> tag = BuildId3v1(t);
    if (GetFileSizeEx(h, &sz) && sz.QuadPart >= 128) {
        std::vector<unsigned char> tail(128);
        LARGE_INTEGER at; at.QuadPart = sz.QuadPart - 128;
        DWORD got = 0;
        bool ok = SetFilePointerEx(h, at, NULL, FILE_BEGIN) &&
                  ReadFile(h, tail.data(), 128, &got, NULL) && got == 128;
        if (ok) {
            at.QuadPart = HasId3v1At(tail) ? sz.QuadPart - 128 : sz.QuadPart;
            SetFilePointerEx(h, at, NULL, FILE_BEGIN);
            DWORD written = 0;
            ok = WriteFile(h, tag.data(), 128, &written, NULL) && written == 128;
        }
        if (ok) rc = 0;
    }
    CloseHandle(h);
    return rc;
}

// 重写文件头：临时文件里放新标签 + 原音频数据，成功后替换原文件。
static bool RewriteWithId3v2(const fs::path& p, const std::vector<unsigned char>& newTag,
                             unsigned long long bodyOff, unsigned long long bodyEnd) {
    std::error_code ec;
    fs::path tmp = p;
    tmp += L".srn-tmp";
    HANDLE src = CreateFileW(p.c_str(), GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (src == INVALID_HANDLE_VALUE) { return false; }
    HANDLE dst = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    if (dst == INVALID_HANDLE_VALUE) { CloseHandle(src); return false; }
    bool ok = true;
    DWORD done = 0;
    if (!newTag.empty())
        ok = WriteFile(dst, newTag.data(), (DWORD)newTag.size(), &done, NULL) &&
             done == (DWORD)newTag.size();
    if (ok) {
        LARGE_INTEGER at; at.QuadPart = (LONGLONG)bodyOff;
        ok = SetFilePointerEx(src, at, NULL, FILE_BEGIN) != FALSE;
        std::vector<unsigned char> buf(64 * 1024);
        while (ok && bodyOff < bodyEnd) {
            DWORD want = (DWORD)std::min<unsigned long long>(buf.size(), bodyEnd - bodyOff);
            DWORD got = 0;
            ok = ReadFile(src, buf.data(), want, &got, NULL) && got > 0;
            if (!ok) break;
            ok = WriteFile(dst, buf.data(), got, &done, NULL) && done == got;
            bodyOff += got;
        }
        if (ok && bodyOff < bodyEnd) ok = false;
    }
    CloseHandle(dst);
    CloseHandle(src);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }
    if (!MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

static int WriteMp3Tags(const fs::path& p, const AudioTags& t, int writeMode) {
    TagFile f(p);
    if (!f.Ok()) return 2;
    if (writeMode == 0) return WriteId3v1Only(p, t);

    std::vector<unsigned char> h;
    if (!f.At(0, h, 10, 0)) return 2;
    bool hasV2 = h.size() >= 10 && !memcmp(h.data(), "ID3", 3);
    int ver = hasV2 ? h[3] : 3;
    if (ver < 2 || ver > 4) ver = 3;
    unsigned long long v2End = 0;
    std::vector<RawFrame> keep;
    if (hasV2) {
        uint32_t sz = Sync32(&h[6]);
        std::vector<unsigned char> b;
        if (!f.At(10, b, sz, 16u << 20)) return 2;
        SplitId3v2(b, ver, keep);
        v2End = 10ull + b.size();
    }
    // 同时写入时 v1 由我们重写；只写 v2 时把原来的 v1 尾巴原样留在 body 里。
    bool writeV1 = (writeMode == 2);
    unsigned long long bodyEnd = f.size;
    std::vector<unsigned char> v1;
    if (writeV1 && f.size >= 128) {
        std::vector<unsigned char> tail;
        if (f.At(f.size - 128, tail, 128, 0) && HasId3v1At(tail)) bodyEnd = f.size - 128;
    }
    std::vector<unsigned char> tag = BuildId3v2(ver, t, keep);
    f.Close();                       // 替换文件前必须放手
    if (!RewriteWithId3v2(p, tag, v2End, bodyEnd)) return 2;
    if (writeV1) {
        v1 = BuildId3v1(t);
        HANDLE hw = CreateFileW(p.c_str(), GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hw == INVALID_HANDLE_VALUE) return 2;
        LARGE_INTEGER at; at.QuadPart = 0;
        bool ok = SetFilePointerEx(hw, at, NULL, FILE_END) != FALSE;
        DWORD written = 0;
        if (ok) ok = WriteFile(hw, v1.data(), (DWORD)v1.size(), &written, NULL) &&
                          written == (DWORD)v1.size();
        CloseHandle(hw);
        if (!ok) return 2;
    }
    return 0;
}

// FLAC：把 VORBIS_COMMENT 块换成新的，其余块（STREAMINFO 必须排第一）原样复制。
static void VorbisAdd(std::vector<unsigned char>& b, const char* key, const std::wstring& val) {
    if (val.empty()) return;
    std::string s = key;
    s += "=";
    std::vector<unsigned char> v = WToCp(CP_UTF8, val);
    s.append((const char*)v.data(), v.size());
    PutLe(b, (uint32_t)s.size());
    b.insert(b.end(), s.begin(), s.end());
}

// FLAC 块头：1 字节 last|type + 3 字节大端长度，共 4 字节，必须原地填。
static std::vector<unsigned char> FlacBlockHdr(unsigned char type, bool last, uint32_t len) {
    std::vector<unsigned char> h = { (unsigned char)(type | (last ? 0x80 : 0)),
                                     (unsigned char)((len >> 16) & 0xFF),
                                     (unsigned char)((len >> 8) & 0xFF),
                                     (unsigned char)(len & 0xFF) };
    return h;
}

static int WriteFlacTags(const fs::path& p, const AudioTags& t) {
    TagFile f(p);
    if (!f.Ok()) return 2;
    std::vector<unsigned char> h;
    if (!f.At(0, h, 4, 0) || h.size() < 4 || memcmp(h.data(), "fLaC", 4)) return 1;

    struct Blk { unsigned long long off; uint32_t type; uint32_t len; };
    std::vector<Blk> blocks;
    std::vector<unsigned char> vendor;
    unsigned long long off = 4, audioOff = 0;
    bool lastBlock = false;
    for (int i = 0; i < 128 && !lastBlock; ++i) {
        std::vector<unsigned char> bh;
        if (!f.At(off, bh, 4, 0) || bh.size() < 4) return 2;
        lastBlock = (bh[0] & 0x80) != 0;
        uint32_t type = bh[0] & 0x7F;
        uint32_t len = ((uint32_t)bh[1] << 16) | ((uint32_t)bh[2] << 8) | bh[3];
        off += 4;
        blocks.push_back({ off, type, len });
        if (type == 4) {
            std::vector<unsigned char> v;
            if (f.At(off, v, len, 8u << 20) && v.size() >= 4) {
                uint32_t vl = Le32(v.data());
                if (vl + 4 <= v.size())
                    vendor.assign(v.begin() + 4, v.begin() + 4 + vl);   // 保留原 vendor
            }
        }
        off += len;
    }
    if (!lastBlock) return 2;                     // 块表读不完，不敢动这个文件
    audioOff = off;

    std::vector<unsigned char> vc;
    PutLe(vc, (uint32_t)vendor.size());
    vc.insert(vc.end(), vendor.begin(), vendor.end());
    struct Kv { const char* key; const std::wstring* val; };
    const Kv kv[] = {
        { "ARTIST", &t.artist },   { "TITLE", &t.title },
        { "ALBUM", &t.album },     { "TRACKNUMBER", &t.track },
        { "DATE", &t.year },       { "GENRE", &t.genre },
        { "COMMENT", &t.comment },
    };
    std::vector<unsigned char> list;
    uint32_t cnt = 0;
    for (const Kv& e : kv) {
        if (e.val->empty()) continue;
        VorbisAdd(list, e.key, *e.val);
        cnt++;
    }
    PutLe(vc, cnt);
    vc.insert(vc.end(), list.begin(), list.end());

    std::error_code ec;
    fs::path tmp = p;
    tmp += L".srn-tmp";
    HANDLE dst = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    if (dst == INVALID_HANDLE_VALUE) return 2;
    auto put = [&](const void* d, size_t n) {
        DWORD done = 0;
        return n == 0 || (WriteFile(dst, d, (DWORD)n, &done, NULL) && done == (DWORD)n);
    };
    std::vector<Blk> keep;
    for (const Blk& b : blocks) if (b.type != 4) keep.push_back(b);   // 旧注释块丢掉
    // 新注释块插在 STREAMINFO 之后（它必须排第一）；没有 STREAMINFO 就追加在块表末尾。
    size_t insertPos = keep.size();
    for (size_t i = 0; i < keep.size(); ++i)
        if (keep[i].type == 0) { insertPos = i + 1; break; }

    bool ok = put("fLaC", 4);
    if (ok && keep.empty()) {
        std::vector<unsigned char> hdr = FlacBlockHdr(4, true, (uint32_t)vc.size());
        ok = put(hdr.data(), 4) && put(vc.data(), vc.size());
    }
    for (size_t i = 0; ok && i < keep.size(); ++i) {
        const Blk& b = keep[i];
        std::vector<unsigned char> body;
        if (b.len) {
            if (!f.At(b.off, body, b.len, 16u << 20) || body.size() != b.len) { ok = false; break; }
        }
        bool willInsert = (i + 1 == insertPos);
        std::vector<unsigned char> hdr =
            FlacBlockHdr((unsigned char)b.type, i + 1 == keep.size() && !willInsert, b.len);
        ok = put(hdr.data(), 4) && put(body.data(), body.size());
        if (ok && willInsert) {
            std::vector<unsigned char> ch =
                FlacBlockHdr(4, i + 1 == keep.size(), (uint32_t)vc.size());
            ok = put(ch.data(), 4) && put(vc.data(), vc.size());
        }
    }
    // 音频数据原样搬到临时文件
    if (ok) {
        std::vector<unsigned char> buf(64 * 1024);
        unsigned long long pos = audioOff;
        while (ok && pos < f.size) {
            std::vector<unsigned char> got;
            if (!f.At(pos, got, buf.size(), 0) || got.empty()) { ok = false; break; }
            ok = put(got.data(), got.size());
            pos += got.size();
        }
    }
    CloseHandle(dst);
    f.Close();                       // 替换文件前必须放手
    if (!ok) { DeleteFileW(tmp.c_str()); return 2; }
    if (!MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        return 2;
    }
    return 0;
}

// ----------------------------------------------------------------------------
// 通用文件重写助手 (临时文件 + 原子替换, 失败不留半成品)
// ----------------------------------------------------------------------------
static bool ReadWholeFile(const fs::path& p, std::vector<unsigned char>& out, unsigned long long cap) {
    out.clear();
    TagFile f(p);
    if (!f.Ok() || f.size > cap) return false;
    out.resize((size_t)f.size);
    if (out.empty()) return true;
    LARGE_INTEGER at; at.QuadPart = 0;
    if (!SetFilePointerEx(f.h, at, NULL, FILE_BEGIN)) return false;
    size_t done = 0;
    while (done < out.size()) {
        DWORD got = 0;
        DWORD want = (DWORD)std::min<size_t>(out.size() - done, 1u << 20);
        if (!ReadFile(f.h, out.data() + done, want, &got, NULL) || got == 0) return false;
        done += got;
    }
    return true;
}

static void PutBeAt(std::vector<unsigned char>& b, size_t off, uint32_t v, int n) {
    for (int i = 0; i < n; ++i)
        if (off + (size_t)i < b.size())
            b[off + i] = (unsigned char)((v >> (8 * (n - 1 - i))) & 0xFF);
}

// 追加一段字节。注意不要写 insert(v, "LIT", "LIT"+n): 两个字面量若不是同一个
// 对象, 迭代器距离就是垃圾值 (MSVC 默认不池化字符串), 直接崩。
static void AppendBytes(std::vector<unsigned char>& b, const void* s, size_t n) {
    if (!s || !n) return;
    const unsigned char* p = (const unsigned char*)s;
    b.insert(b.end(), p, p + n);
}

// 一段"新字节"或一段"原文件区间", 按顺序拼成新文件。
struct RewriteChunk {
    bool range = false;
    std::vector<unsigned char> bytes;
    unsigned long long off = 0, len = 0;
};

static bool RewriteFileChunks(const fs::path& p, const std::vector<RewriteChunk>& chunks) {
    std::error_code ec;
    fs::path tmp = p;
    tmp += L".srn-tmp";
    HANDLE src = CreateFileW(p.c_str(), GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (src == INVALID_HANDLE_VALUE) return false;
    HANDLE dst = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    if (dst == INVALID_HANDLE_VALUE) { CloseHandle(src); return false; }
    auto put = [&](const void* d, size_t n) {
        DWORD done = 0;
        return n == 0 || (WriteFile(dst, d, (DWORD)n, &done, NULL) && done == (DWORD)n);
    };
    bool ok = true;
    std::vector<unsigned char> buf(64 * 1024);
    for (const auto& c : chunks) {
        if (!ok) break;
        if (!c.range) {
            ok = c.bytes.empty() || put(c.bytes.data(), c.bytes.size());
            continue;
        }
        LARGE_INTEGER at; at.QuadPart = (LONGLONG)c.off;
        ok = SetFilePointerEx(src, at, NULL, FILE_BEGIN) != FALSE;
        unsigned long long left = c.len;
        while (ok && left) {
            DWORD want = (DWORD)std::min<unsigned long long>(buf.size(), left);
            DWORD got = 0;
            ok = ReadFile(src, buf.data(), want, &got, NULL) && got > 0;
            if (!ok) break;
            ok = put(buf.data(), got);
            left -= got;
        }
    }
    CloseHandle(dst);
    CloseHandle(src);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }
    if (!MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

static bool ReplaceFileContent(const fs::path& p, const std::vector<unsigned char>& data) {
    RewriteChunk c;
    c.range = false;
    c.bytes = data;
    return RewriteFileChunks(p, { c });
}

// ----------------------------------------------------------------------------
// OGG / Opus (Vorbis comment) 写入
// ----------------------------------------------------------------------------
// 注释包是音频流的第 2 个包, 规范要求它从页边界开始 (Opus 还要求它独占页面)。
// 换掉它之后: 后续页的颗粒位置按包计、不随字节数变化, 但页序号要重排、
// 每页 CRC32 要重算 (Ogg 用 CRC-32/MPEG-2: 多项式 0x04C11DB7, 不反射, 初值 0)。
// ID 头页原样不动, 保证 Vorbis/Opus 对头页的独占要求。多路复用/链式流不碰。
struct OggPage {
    unsigned long long off = 0;   // 页头在文件里的偏移
    uint32_t body = 0;            // 载荷字节数 (分片表之和)
    uint8_t  type = 0;            // 页头 flags
    uint64_t granule = 0;
    uint32_t serial = 0, seq = 0, crc = 0;
    uint8_t  nseg = 0;
};

static uint32_t g_oggCrcTab[256];
static bool      g_oggCrcReady = false;
static void OggCrcInit() {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t r = i << 24;
        for (int j = 0; j < 8; ++j)
            r = (r & 0x80000000u) ? ((r << 1) ^ 0x04C11DB7u) : (r << 1);
        g_oggCrcTab[i] = r;
    }
    g_oggCrcReady = true;
}
static uint32_t OggCrc(const unsigned char* d, size_t n) {
    if (!g_oggCrcReady) OggCrcInit();
    uint32_t c = 0;
    for (size_t i = 0; i < n; ++i)
        c = (c << 8) ^ g_oggCrcTab[(c >> 24) ^ d[i]];
    return c;
}

static bool OggParsePage(const std::vector<unsigned char>& b, size_t off, OggPage& pg) {
    if (off + 27 > b.size()) return false;
    if (memcmp(&b[off], "OggS", 4)) return false;
    if (b[off + 4] != 0) return false;                 // 版本必须是 0
    const unsigned char* p = &b[off];
    pg.off = off;
    pg.type = p[5];
    pg.granule = 0;
    for (int i = 0; i < 8; ++i) pg.granule = (pg.granule << 8) | p[6 + i];
    pg.serial = Be32(p + 14);
    pg.seq = Be32(p + 18);
    pg.crc = Be32(p + 22);
    pg.nseg = p[26];
    uint32_t body = 0;
    for (int i = 0; i < pg.nseg; ++i) body += p[27 + i];
    if ((size_t)body > b.size() - off - 27 - pg.nseg) return false;
    pg.body = body;
    return true;
}

// 把一组包写成一页或几页 (每页分片数上限 255)。每包带一个颗粒位置, 页的颗粒
// 位置取该页最后一个完成包的颗粒位置; lastFlags 加在最后一页 (EOS=0x04)。
static void OggEmit(std::vector<unsigned char>& out, uint32_t serial, uint32_t& seq,
                    const std::vector<std::vector<unsigned char>>& pkts,
                    const std::vector<uint64_t>& gran, uint8_t lastFlags) {
    std::vector<unsigned char> segs, body;
    uint64_t pageGran = 0;
    uint64_t lastGran = 0;
    bool pageCont = false;          // 本页是否接着上一页的半包
    auto flush = [&](bool isLast) {
        if (segs.empty()) { pageCont = false; return; }
        size_t base = out.size();
        std::vector<unsigned char> hdr(27 + segs.size(), 0);
        memcpy(&hdr[0], "OggS", 4);
        hdr[5] = (uint8_t)((pageCont ? 0x01 : 0) | (isLast ? lastFlags : 0));
        for (int i = 0; i < 8; ++i) hdr[6 + i] = (unsigned char)((pageGran >> (8 * (7 - i))) & 0xFF);
        PutBeAt(hdr, 14, serial, 4);
        PutBeAt(hdr, 18, seq++, 4);
        PutBeAt(hdr, 22, 0, 4);
        hdr[26] = (unsigned char)segs.size();
        memcpy(&hdr[27], segs.data(), segs.size());
        out.insert(out.end(), hdr.begin(), hdr.end());
        out.insert(out.end(), body.begin(), body.end());
        uint32_t crc = OggCrc(&out[base], out.size() - base);
        PutBeAt(out, base + 22, crc, 4);
        segs.clear(); body.clear();
        lastGran = pageGran;
        pageCont = false;
    };
    for (size_t i = 0; i < pkts.size(); ++i) {
        const std::vector<unsigned char>& pk = pkts[i];
        if (pk.empty()) {
            if (segs.size() == 255) { flush(false); pageCont = true; }
            segs.push_back(0);
            pageGran = gran[i];
            continue;
        }
        size_t done = 0;
        while (true) {
            if (segs.size() == 255) { flush(false); pageCont = true; }
            size_t room = 255 - segs.size();
            size_t take = pk.size() - done;
            if (take > room) take = room;
            bool last = (done + take >= pk.size() && take < 255);
            segs.push_back((unsigned char)take);
            body.insert(body.end(), pk.begin() + done, pk.begin() + done + take);
            done += take;
            if (last) { pageGran = gran[i]; break; }
        }
    }
    flush(true);
}

// 空 EOS 页 (最后一页只有结束标志、没有完成任何包时补一个)。
static void OggEmitBare(std::vector<unsigned char>& out, uint32_t serial, uint32_t& seq,
                        uint64_t granule, uint8_t flags) {
    size_t base = out.size();
    std::vector<unsigned char> hdr(27, 0);
    memcpy(&hdr[0], "OggS", 4);
    hdr[5] = flags;
    for (int i = 0; i < 8; ++i) hdr[6 + i] = (unsigned char)((granule >> (8 * (7 - i))) & 0xFF);
    PutBeAt(hdr, 14, serial, 4);
    PutBeAt(hdr, 18, seq++, 4);
    PutBeAt(hdr, 22, 0, 4);
    out.insert(out.end(), hdr.begin(), hdr.end());
    uint32_t crc = OggCrc(&out[base], out.size() - base);
    PutBeAt(out, base + 22, crc, 4);
}

// vendor 沿用文件里原来的字符串, 没有就用程序名。
static std::vector<unsigned char> BuildVorbisComment(const std::vector<unsigned char>& vendor,
                                                     const AudioTags& t, bool opus) {
    std::vector<unsigned char> b;
    if (opus) AppendBytes(b, "OpusTags", 8);
    else     AppendBytes(b, "\x03vorbis", 7);
    PutLe(b, (uint32_t)vendor.size());
    b.insert(b.end(), vendor.begin(), vendor.end());
    struct Kv { const char* key; const std::wstring* val; };
    const Kv kv[] = {
        { "ARTIST", &t.artist },   { "TITLE", &t.title },
        { "ALBUM", &t.album },     { "TRACKNUMBER", &t.track },
        { "DATE", &t.year },       { "GENRE", &t.genre },
        { "COMMENT", &t.comment },
    };
    std::vector<unsigned char> list;
    uint32_t cnt = 0;
    for (const Kv& e : kv) {
        if (e.val->empty()) continue;
        std::string s = e.key;
        s += "=";
        std::vector<unsigned char> v = WToCp(CP_UTF8, *e.val);
        s.append((const char*)v.data(), v.size());
        PutLe(list, (uint32_t)s.size());
        list.insert(list.end(), s.begin(), s.end());
        ++cnt;
    }
    PutLe(b, cnt);
    b.insert(b.end(), list.begin(), list.end());
    if (!opus) b.push_back(1);     // Vorbis 的 framing bit; Opus 没有
    return b;
}

static int WriteOggTags(const fs::path& p, const AudioTags& t) {
    std::vector<unsigned char> b;
    if (!ReadWholeFile(p, b, 512ull << 20)) return 2;      // 大于 512MB 不碰
    if (b.size() < 64 || memcmp(b.data(), "OggS", 4)) return 1;

    std::vector<OggPage> pages;
    size_t off = 0;
    while (off < b.size()) {
        OggPage pg;
        if (!OggParsePage(b, off, pg)) return 2;           // 页结构坏了, 不敢写
        pages.push_back(pg);
        off = pg.off + 27 + pg.nseg + pg.body;
    }
    if (pages.empty()) return 1;

    // 目标流: 第一个 ID 头是 Vorbis / Opus 的页
    int tgt = -1;
    bool opus = false;
    for (size_t i = 0; i < pages.size(); ++i) {
        const unsigned char* pl = &b[pages[i].off + 27 + pages[i].nseg];
        if (pages[i].body >= 7 && !memcmp(pl, "\x01vorbis", 7)) { tgt = (int)i; break; }
        if (pages[i].body >= 8 && !memcmp(pl, "OpusHead", 8)) { tgt = (int)i; opus = true; break; }
    }
    if (tgt < 0) return 1;                                 // 不是 Vorbis/Opus (Theora/Speex 等)

    // 目标流的连续页段: 遇到别的 serial 就停 (多路复用/链式流不碰)
    size_t runEnd = (size_t)tgt;
    while (runEnd < pages.size() && pages[runEnd].serial == pages[tgt].serial) ++runEnd;

    // 分包到注释包结束 (packet[1]); 注释包必须从第 2 页的第一个分片开始
    std::vector<unsigned char> cur, commentBytes;
    int pktIdx = 0;
    int commentPage = -1;
    uint64_t commentGran = 0;
    std::vector<std::vector<unsigned char>> spillPkts;     // 与注释包同页完成的后续包
    for (size_t j = (size_t)tgt; j < runEnd; ++j) {
        const OggPage& pg = pages[j];
        const unsigned char* st = &b[pg.off + 27];
        const unsigned char* pl = st + pg.nseg;
        size_t used = 0;
        for (int s = 0; s < pg.nseg; ++s) {
            unsigned L = st[s];
            if (pktIdx == 1 && commentPage < 0 && (s != 0 || (pg.type & 1))) return 2;
            cur.insert(cur.end(), pl + used, pl + used + L);
            used += L;
            if (L < 255) {
                if (pktIdx == 1) {
                    commentBytes = cur;
                    commentGran = pg.granule;
                    commentPage = (int)(j - tgt);
                } else if (commentPage >= 0) {
                    spillPkts.push_back(cur);
                }
                ++pktIdx;
                cur.clear();
            }
        }
        if (commentPage >= 0) break;
    }
    if (commentPage != 1) return 2;                        // ID 头必须独占首页, 注释包从第 2 页开始

    // 旧注释包的 vendor 字符串 (Vorbis 魔数 7 字节, Opus 8 字节)
    std::vector<unsigned char> vendor;
    if (!commentBytes.empty()) {
        size_t magic = opus ? 8 : 7;
        if (commentBytes.size() > magic + 4) {
            uint32_t vl = Le32(&commentBytes[magic]);
            if ((size_t)magic + 4 + vl <= commentBytes.size())
                vendor.assign(commentBytes.begin() + magic + 4,
                              commentBytes.begin() + magic + 4 + vl);
        }
    }

    uint32_t serial = pages[tgt].serial;
    uint32_t seq = 1;                                      // ID 头页是 0, 新注释页从 1 排
    std::vector<unsigned char> out;
    out.insert(out.end(), b.begin(), b.begin() + pages[tgt + 1].off);   // 前缀 (含 ID 头页)

    std::vector<unsigned char> newComment = BuildVorbisComment(vendor, t, opus);
    OggEmit(out, serial, seq, { newComment }, { commentGran }, 0);      // 新注释页独占

    if (!spillPkts.empty()) {                              // 旧注释页同页的后续包原样搬回
        std::vector<uint64_t> g(spillPkts.size(), commentGran);
        OggEmit(out, serial, seq, spillPkts, g, 0);
    }

    // 其余页: 按原来的包完成边界重排, 颗粒位置跟着包走。
    // cur 里可能还剩半个包 (注释页上 packet[3] 开头的那几个分片), 先交给 carry。
    std::vector<std::vector<unsigned char>> group;
    std::vector<uint64_t> groupGran;
    std::vector<unsigned char> carry = cur;
    cur.clear();
    for (size_t j = tgt + (size_t)commentPage + 1; j < runEnd; ++j) {
        const OggPage& pg = pages[j];
        const unsigned char* st = &b[pg.off + 27];
        const unsigned char* pl = st + pg.nseg;
        size_t used = 0;
        for (int s = 0; s < pg.nseg; ++s) {
            unsigned L = st[s];
            carry.insert(carry.end(), pl + used, pl + used + L);
            used += L;
            if (L < 255) {
                group.push_back(carry);
                groupGran.push_back(pg.granule);
                carry.clear();
            }
        }
        bool isLastRun = (j + 1 == runEnd);
        if (!group.empty()) {
            uint8_t flags = (isLastRun && (pg.type & 0x04)) ? 0x04 : 0;
            OggEmit(out, serial, seq, group, groupGran, flags);
            group.clear(); groupGran.clear();
        } else if (isLastRun && (pg.type & 0x04)) {
            OggEmitBare(out, serial, seq, pg.granule, 0x04);
        }
        if (!carry.empty() && isLastRun) return 2;         // 文件在半个包处结束 (截断)
    }
    if (!carry.empty()) return 2;

    // 目标流之后的页 (其它 serial) 原样复制
    const OggPage& lastRun = pages[runEnd - 1];
    out.insert(out.end(), b.begin() + lastRun.off + 27 + lastRun.nseg + lastRun.body, b.end());

    if (!ReplaceFileContent(p, out)) return 2;
    return 0;
}

// ----------------------------------------------------------------------------
// APE (APEv2) 写入
// ----------------------------------------------------------------------------
static bool ApeTagStart(const fs::path& p, unsigned long long& tagStart) {
    tagStart = 0;
    TagFile f(p);
    if (!f.Ok() || f.size < 32) return false;
    std::vector<unsigned char> foot;
    if (!f.At(f.size - 32, foot, 32, 0) || foot.size() != 32) return false;
    if (memcmp(foot.data(), "APETAGEX", 8)) return false;
    uint32_t tagSize = Le32(&foot[12]);
    uint32_t flags = Le32(&foot[20]);
    if (tagSize < 32 || tagSize > f.size) return false;
    bool hasHdr = (flags & 0x80000000u) != 0;
    // 整块标签区 (header+items+footer) 的起点: 规范约定 tagSize = items + footer,
    // 不含 header, 所以起点 = f.size - tagSize - (hasHdr ? 32 : 0);
    // 少数写入器把 header 也算进 tagSize, 那时起点 = f.size - tagSize。
    unsigned long long a = f.size - tagSize - (hasHdr ? 32 : 0);
    unsigned long long b2 = f.size - tagSize;
    auto hdrOk = [&](unsigned long long s) {
        if (s + 32 > f.size) return false;
        std::vector<unsigned char> hd;
        return f.At(s, hd, 32, 0) && hd.size() == 32 && !memcmp(hd.data(), "APETAGEX", 8);
    };
    if (hasHdr) {
        if (hdrOk(a)) { tagStart = a; return true; }
        if (hdrOk(b2)) { tagStart = b2; return true; }
        return false;                                            // 对不上, 不敢动
    }
    tagStart = a;
    return true;
}

static int WriteApeTags(const fs::path& p, const AudioTags& t) {
    unsigned long long tagStart = 0;
    bool have = ApeTagStart(p, tagStart);
    TagFile f(p);
    if (!f.Ok()) return 2;
    if (!have) tagStart = f.size;

    std::vector<unsigned char> items;
    struct Kv { const char* key; const std::wstring* val; };
    const Kv kv[] = {
        { "Artist", &t.artist }, { "Title", &t.title }, { "Album", &t.album },
        { "Track", &t.track },   { "Year", &t.year },   { "Genre", &t.genre },
        { "Comment", &t.comment },
    };
    uint32_t count = 0;
    for (const Kv& e : kv) {
        if (e.val->empty()) continue;
        std::vector<unsigned char> v = WToCp(CP_UTF8, *e.val);
        PutLe(items, (uint32_t)v.size());                       // size 只含 value, 不含 key
        PutLe(items, 0);                                        // flags: 0 = UTF-8 文本
        items.insert(items.end(), e.key, e.key + strlen(e.key));
        items.push_back('\0');
        items.insert(items.end(), v.begin(), v.end());
        ++count;
    }
    std::vector<unsigned char> tag;
    auto apeHead = [&](bool isHeader) {
        AppendBytes(tag, "APETAGEX", 8);
        PutLe(tag, 2000);
        PutLe(tag, (uint32_t)items.size() + 32);                // items + footer, 不含 header
        PutLe(tag, count);
        PutLe(tag, isHeader ? (0x80000000u | 0x40000000u) : 0x80000000u);
        for (int i = 0; i < 8; ++i) tag.push_back(0);           // reserved
    };
    apeHead(true);
    tag.insert(tag.end(), items.begin(), items.end());
    apeHead(false);

    RewriteChunk keep, add;
    keep.range = true; keep.off = 0; keep.len = tagStart;
    add.range = false; add.bytes = tag;
    f.Close();                                        // 替换文件前必须放手, 否则 MoveFileEx 被拒
    if (!RewriteFileChunks(p, { keep, add })) return 2;
    return 0;
}

// ----------------------------------------------------------------------------
// M4A / AAC (MP4 ilst) 写入
// ----------------------------------------------------------------------------
static bool Mp4Find(const std::vector<unsigned char>& d, size_t off, size_t end,
                    const char* type, size_t& bStart, size_t& bEnd) {
    while (off + 8 <= end) {
        uint64_t sz = Be32(&d[off]);
        size_t hdr = 8;
        if (sz == 1) {
            if (off + 16 > end) return false;
            sz = ((uint64_t)Be32(&d[off + 8]) << 32) | Be32(&d[off + 12]);
            hdr = 16;
        } else if (sz == 0) {
            sz = end - off;
        }
        if (sz < hdr || off + sz > end) return false;
        if (!memcmp(&d[off + 4], type, 4)) { bStart = off; bEnd = off + (size_t)sz; return true; }
        off += (size_t)sz;
    }
    return false;
}

// moov 大小变了, 里面的 chunk 偏移要跟着平移。只平移位于 moov 之后的偏移
// (moov 缩小为负、扩大为正); 位于 moov 之前的块不受影响。
static bool Mp4FixOffsets(std::vector<unsigned char>& d, size_t off, size_t end,
                          long long delta, unsigned long long minOff, int depth) {
    if (depth > 8) return true;
    while (off + 8 <= end) {
        uint64_t sz = Be32(&d[off]);
        size_t hdr = 8;
        if (sz == 1) {
            if (off + 16 > end) return false;
            sz = ((uint64_t)Be32(&d[off + 8]) << 32) | Be32(&d[off + 12]);
            hdr = 16;
        } else if (sz == 0) {
            sz = end - off;
        }
        if (sz < hdr || off + sz > end) return false;
        std::string type((const char*)&d[off + 4], 4);
        size_t c0 = off + hdr, c1 = off + (size_t)sz;
        if ((type == "stco" || type == "co64") && c1 - c0 >= 8) {
            uint32_t n = Be32(&d[c0 + 4]);
            size_t p = c0 + 8;
            int w = (type == "stco") ? 4 : 8;
            for (uint32_t i = 0; i < n; ++i, p += w) {
                if (p + (size_t)w > c1) break;
                uint64_t v = 0;
                for (int k = 0; k < w; ++k) v = (v << 8) | d[p + k];
                if (v < minOff) continue;                       // moov 之前的块不动
                long long nv = (long long)v + delta;
                if (nv < 0) return false;
                if (w == 4 && nv > 0xFFFFFFFFLL) return false;  // 32 位装不下
                for (int k = 0; k < w; ++k)
                    d[p + k] = (unsigned char)(((uint64_t)nv >> (8 * (w - 1 - k))) & 0xFF);
            }
        } else if (type == "moov" || type == "trak" || type == "mdia" ||
                   type == "minf" || type == "stbl") {
            if (!Mp4FixOffsets(d, c0, c1, delta, minOff, depth + 1)) return false;
        }
        off = c1;
    }
    return true;
}

static void Mp4DataBox(std::vector<unsigned char>& item, uint32_t typeFlag,
                       const unsigned char* data, size_t n) {
    PutBe(item, (uint32_t)(16 + n), 4);
    AppendBytes(item, "data", 4);
    PutBe(item, typeFlag & 0xFFFFFF, 4);                        // version 0 + 类型(1=UTF-8 文本)
    PutBe(item, 0, 4);                                          // locale
    item.insert(item.end(), data, data + n);
}

// keepOld: 写入时保留封面等不认识的项; 移除标签时 (clearAll) 全部丢掉。
static std::vector<unsigned char> Mp4BuildIlst(const AudioTags& t, bool keepOld,
                                               const std::vector<unsigned char>& oldBody) {
    std::vector<unsigned char> out;
    if (keepOld) {
        size_t off = 0;
        while (off + 8 <= oldBody.size()) {
            uint64_t sz = Be32(&oldBody[off]);
            if (sz < 8 || off + sz > oldBody.size()) break;
            std::string key((const char*)&oldBody[off + 4], 4);
            static const char* kOurs[] = { "\xA9" "nam", "\xA9" "ART", "\xA9" "alb", "trkn",
                                           "\xA9" "day", "\xA9" "gen", "\xA9" "cmt", "cmt" };
            bool mine = false;
            for (auto k : kOurs) if (key == k) mine = true;
            if (!mine) out.insert(out.end(), oldBody.begin() + off,
                                  oldBody.begin() + off + (size_t)sz);
            off += (size_t)sz;
        }
    }
    auto text = [&](const char* key, const std::wstring& val) {
        if (val.empty()) return;
        std::vector<unsigned char> v = WToCp(CP_UTF8, val);
        std::vector<unsigned char> item;
        Mp4DataBox(item, 1, v.data(), v.size());
        PutBe(out, (uint32_t)(8 + item.size()), 4);
        out.insert(out.end(), key, key + 4);
        out.insert(out.end(), item.begin(), item.end());
    };
    text("\xA9" "nam", t.title);
    text("\xA9" "ART", t.artist);
    text("\xA9" "alb", t.album);
    text("\xA9" "day", t.year);
    text("\xA9" "gen", t.genre);
    text("\xA9" "cmt", t.comment);
    if (!t.track.empty()) {
        int trk = 0;
        for (wchar_t c : t.track) {
            if (c < L'0' || c > L'9') break;
            trk = trk * 10 + (int)(c - L'0');
            if (trk > 0xFFFF) { trk = 0xFFFF; break; }
        }
        unsigned char payload[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        payload[2] = (unsigned char)((trk >> 8) & 0xFF);
        payload[3] = (unsigned char)(trk & 0xFF);
        std::vector<unsigned char> item;
        Mp4DataBox(item, 0, payload, 8);                        // trkn 是二进制
        PutBe(out, (uint32_t)(8 + item.size()), 4);
        AppendBytes(out, "trkn", 4);
        out.insert(out.end(), item.begin(), item.end());
    }
    return out;
}

static int WriteMp4TagsImpl(const fs::path& p, const AudioTags& t, bool clearAll) {
    std::vector<unsigned char> b;
    if (!ReadWholeFile(p, b, 1ull << 30)) return 2;
    if (b.size() < 16) return 2;

    size_t moovOff = 0, moovEnd = 0;
    {
        size_t off = 0;
        bool found = false;
        while (off + 8 <= b.size()) {
            uint64_t sz = Be32(&b[off]);
            if (sz == 1) return 2;                              // 64-bit moov 不碰
            if (sz == 0) sz = b.size() - off;
            if (sz < 8 || off + sz > b.size()) return 2;
            std::string type((const char*)&b[off + 4], 4);
            if (type == "moov") { moovOff = off; moovEnd = off + (size_t)sz; found = true; break; }
            if (type != "ftyp" && type != "free" && type != "mdat" && type != "wide" &&
                type != "skip" && type != "pnot" && type != "uuid" && type != "moof" &&
                type != "sidx" && type != "mfra")
                return 1;                                        // 不是 MP4
            off += (size_t)sz;
        }
        if (!found) return 1;
    }
    size_t udtaOff = 0, udtaEnd = 0;
    bool hasUdta = Mp4Find(b, moovOff + 8, moovEnd, "udta", udtaOff, udtaEnd);
    size_t metaOff = 0, metaEnd = 0;
    bool hasMeta = hasUdta && Mp4Find(b, udtaOff + 8, udtaEnd, "meta", metaOff, metaEnd) &&
                   metaEnd >= metaOff + 12;
    size_t ilstOff = 0, ilstEnd = 0;
    bool hasIlst = hasMeta && Mp4Find(b, metaOff + 12, metaEnd, "ilst", ilstOff, ilstEnd);

    std::vector<unsigned char> oldIlstBody;
    if (hasIlst) oldIlstBody.assign(b.begin() + ilstOff + 8, b.begin() + ilstEnd);
    std::vector<unsigned char> newIlstBody = Mp4BuildIlst(t, !clearAll, oldIlstBody);
    std::vector<unsigned char> newIlst;
    PutBe(newIlst, (uint32_t)(8 + newIlstBody.size()), 4);
    AppendBytes(newIlst, "ilst", 4);
    newIlst.insert(newIlst.end(), newIlstBody.begin(), newIlstBody.end());

    std::vector<unsigned char> newMoovBody;
    size_t off = moovOff + 8;
    while (off + 8 <= moovEnd) {
        uint64_t sz = Be32(&b[off]);
        if (sz == 1) return 2;
        if (sz == 0) sz = moovEnd - off;
        if (sz < 8 || off + sz > moovEnd) return 2;
        std::string type((const char*)&b[off + 4], 4);
        size_t c1 = off + (size_t)sz;
        if (type == "udta") {
            std::vector<unsigned char> newUdtaBody;
            size_t o2 = off + 8;
            while (o2 + 8 <= c1) {
                uint64_t s2 = Be32(&b[o2]);
                if (s2 == 1) return 2;
                if (s2 == 0) s2 = c1 - o2;
                if (s2 < 8 || o2 + s2 > c1) return 2;
                std::string t2((const char*)&b[o2 + 4], 4);
                if (t2 == "meta") {
                    // meta = 4 字节版本/标志 + 子 box; ilst 换成新的, 其余原样
                    std::vector<unsigned char> newMetaBody(b.begin() + o2 + 8, b.begin() + o2 + 12);
                    size_t o3 = o2 + 12;
                    size_t metaC1 = o2 + (size_t)s2;
                    while (o3 + 8 <= metaC1) {
                        uint64_t s3 = Be32(&b[o3]);
                        if (s3 == 1) return 2;
                        if (s3 == 0) s3 = metaC1 - o3;
                        if (s3 < 8 || o3 + s3 > metaC1) return 2;
                        std::string t3((const char*)&b[o3 + 4], 4);
                        if (t3 != "ilst")
                            newMetaBody.insert(newMetaBody.end(), b.begin() + o3,
                                               b.begin() + o3 + (size_t)s3);
                        o3 += (size_t)s3;
                    }
                    newMetaBody.insert(newMetaBody.end(), newIlst.begin(), newIlst.end());
                    std::vector<unsigned char> newMeta;
                    PutBe(newMeta, (uint32_t)(8 + newMetaBody.size()), 4);
                    AppendBytes(newMeta, "meta", 4);
                    newMeta.insert(newMeta.end(), newMetaBody.begin(), newMetaBody.end());
                    newUdtaBody.insert(newUdtaBody.end(), newMeta.begin(), newMeta.end());
                } else {
                    newUdtaBody.insert(newUdtaBody.end(), b.begin() + o2,
                                       b.begin() + o2 + (size_t)s2);
                }
                o2 += (size_t)s2;
            }
            std::vector<unsigned char> newUdta;
            PutBe(newUdta, (uint32_t)(8 + newUdtaBody.size()), 4);
            AppendBytes(newUdta, "udta", 4);
            newUdta.insert(newUdta.end(), newUdtaBody.begin(), newUdtaBody.end());
            newMoovBody.insert(newMoovBody.end(), newUdta.begin(), newUdta.end());
        } else {
            newMoovBody.insert(newMoovBody.end(), b.begin() + off, b.begin() + c1);
        }
        off = c1;
    }
    if (!hasUdta) {
        // 没有 udta: 新建 udta>meta>ilst 追加到 moov 末尾 (mvhd 仍在最前)
        std::vector<unsigned char> metaBody(4, 0);              // 版本/标志
        metaBody.insert(metaBody.end(), newIlst.begin(), newIlst.end());
        std::vector<unsigned char> meta;
        PutBe(meta, (uint32_t)(8 + metaBody.size()), 4);
        AppendBytes(meta, "meta", 4);
        meta.insert(meta.end(), metaBody.begin(), metaBody.end());
        std::vector<unsigned char> udta;
        PutBe(udta, (uint32_t)(8 + meta.size()), 4);
        AppendBytes(udta, "udta", 4);
        udta.insert(udta.end(), meta.begin(), meta.end());
        newMoovBody.insert(newMoovBody.end(), udta.begin(), udta.end());
    }
    std::vector<unsigned char> newMoov;
    PutBe(newMoov, (uint32_t)(8 + newMoovBody.size()), 4);
    AppendBytes(newMoov, "moov", 4);
    newMoov.insert(newMoov.end(), newMoovBody.begin(), newMoovBody.end());

    long long delta = (long long)newMoov.size() - (long long)(moovEnd - moovOff);
    if (delta != 0 && !Mp4FixOffsets(newMoov, 8, newMoov.size(), delta, moovEnd, 0))
        return 2;

    RewriteChunk c0, c1, c2;
    c0.range = true; c0.off = 0; c0.len = moovOff;
    c1.range = false; c1.bytes = newMoov;
    c2.range = true; c2.off = moovEnd; c2.len = b.size() - moovEnd;
    if (!RewriteFileChunks(p, { c0, c1, c2 })) return 2;
    return 0;
}

// ----------------------------------------------------------------------------
// WAV (RIFF INFO) 写入
// ----------------------------------------------------------------------------
static int WavRewrite(const fs::path& p, const AudioTags* t) {
    TagFile f(p);
    if (!f.Ok()) return 2;
    std::vector<unsigned char> h;
    if (!f.At(0, h, 12, 0) || h.size() < 12 ||
        memcmp(h.data(), "RIFF", 4) || memcmp(&h[8], "WAVE", 4)) return 1;
    struct Chunk { std::string id; unsigned long long off; uint32_t size; };
    std::vector<Chunk> chunks;
    unsigned long long riffLen = Le32(&h[4]);
    unsigned long long end = 12ull + riffLen + (riffLen & 1);
    if (end > f.size || end < 12) end = f.size;
    unsigned long long off = 12;
    while (off + 8 <= end) {
        std::vector<unsigned char> ch;
        if (!f.At(off, ch, 8, 0) || ch.size() < 8) break;
        Chunk c;
        c.id.assign((const char*)ch.data(), 4);
        c.size = Le32(&ch[4]);
        c.off = off + 8;
        if (c.off + c.size > end) break;                        // 块越界, 后面的不动
        chunks.push_back(c);
        off = c.off + c.size + (c.size & 1);
    }
    int infoIdx = -1;
    for (size_t i = 0; i < chunks.size(); ++i)
        if (chunks[i].id == "LIST") {
            std::vector<unsigned char> lb;
            if (f.At(chunks[i].off, lb, 4, 0) && lb.size() == 4 && !memcmp(lb.data(), "INFO", 4)) {
                infoIdx = (int)i;
                break;
            }
        }
    static const char* kOurs[] = { "INAM", "IART", "IPRD", "ITRK", "ICRD", "IGNR", "ICMT" };

    // 新的 LIST-INFO 载荷 ("INFO" + 子块); t 为空表示移除 (不写新 INFO)
    std::vector<unsigned char> newInfo;
    if (t) {
        struct Sub { const char* id; const std::wstring* val; };
        const Sub subs[] = {
            { "INAM", &t->title }, { "IART", &t->artist }, { "IPRD", &t->album },
            { "ITRK", &t->track }, { "ICRD", &t->year },   { "IGNR", &t->genre },
            { "ICMT", &t->comment },
        };
        // 先保留不认识的旧子块, 再写我们的 7 个键 (空值 = 删掉该子块)
        std::vector<std::pair<std::string, std::vector<unsigned char>>> keep;
        if (infoIdx >= 0) {
            unsigned long long so = chunks[infoIdx].off + 4;
            unsigned long long se = chunks[infoIdx].off + chunks[infoIdx].size;
            while (so + 8 <= se) {
                std::vector<unsigned char> sh;
                if (!f.At(so, sh, 8, 0) || sh.size() < 8) break;
                std::string id((const char*)sh.data(), 4);
                uint32_t sz = Le32(&sh[4]);
                if (so + 8 + sz > se) break;
                bool mine = false;
                for (auto o : kOurs) if (id == o) mine = true;
                if (!mine) {
                    std::vector<unsigned char> data;
                    if (sz && (!f.At(so + 8, data, sz, 1u << 20) || data.size() != sz)) break;
                    keep.push_back({ id, data });
                }
                so += 8 + sz + (sz & 1);
            }
        }
        AppendBytes(newInfo, "INFO", 4);
        for (auto& k : keep) {
            newInfo.insert(newInfo.end(), k.first.begin(), k.first.end());
            PutLe(newInfo, (uint32_t)k.second.size());
            newInfo.insert(newInfo.end(), k.second.begin(), k.second.end());
            if (k.second.size() & 1) newInfo.push_back(0);
        }
        for (const Sub& s : subs) {
            if (s.val->empty()) continue;
            std::vector<unsigned char> v = WToCp(CP_ACP, *s.val);
            newInfo.insert(newInfo.end(), s.id, s.id + 4);
            PutLe(newInfo, (uint32_t)v.size());
            newInfo.insert(newInfo.end(), v.begin(), v.end());
            if (v.size() & 1) newInfo.push_back(0);
        }
    }
    if (!t && infoIdx < 0) return 0;                            // 本来就没有 INFO
    if (t && infoIdx < 0 && newInfo.size() <= 4) return 0;      // 没有可写的内容

    std::vector<unsigned char> body;                            // "WAVE" 之后的块
    auto putChunk = [&](const std::string& id, const std::vector<unsigned char>& data) {
        body.insert(body.end(), id.begin(), id.end());
        PutLe(body, (uint32_t)data.size());
        body.insert(body.end(), data.begin(), data.end());
        if (data.size() & 1) body.push_back(0);
    };
    for (size_t i = 0; i < chunks.size(); ++i) {
        if ((int)i == infoIdx) {
            if (t) putChunk("LIST", newInfo);                    // 旧 INFO 在这儿换成新的
            continue;
        }
        std::vector<unsigned char> data;
        if (chunks[i].size) {
            if (!f.At(chunks[i].off, data, chunks[i].size, 256u << 20) ||
                data.size() != chunks[i].size) return 2;
        }
        if (infoIdx < 0 && t && chunks[i].id == "data") putChunk("LIST", newInfo);  // 插在 data 前
        putChunk(chunks[i].id, data);
    }
    if (infoIdx < 0 && t && (chunks.empty() || chunks.back().id != "data"))
        putChunk("LIST", newInfo);                              // 没有 data 块就追加在末尾

    std::vector<unsigned char> file2;
    AppendBytes(file2, "RIFF", 4);
    PutLe(file2, (uint32_t)(4 + body.size()));
    AppendBytes(file2, "WAVE", 4);
    file2.insert(file2.end(), body.begin(), body.end());
    f.Close();                                        // 替换文件前必须放手, 否则 MoveFileEx 被拒
    if (!ReplaceFileContent(p, file2)) return 2;
    return 0;
}

// ----------------------------------------------------------------------------
// 分派 + 移除 + ID3 单读
// ----------------------------------------------------------------------------
int WriteAudioTags(const fs::path& p, const AudioTags& t, int writeMode) {
    std::error_code ec;
    if (!fs::exists(p, ec)) return 2;
    std::vector<unsigned char> h;
    {
        TagFile f(p);
        if (!f.Ok()) return 2;
        if (!f.At(0, h, 12, 0)) return 2;
    }
    if (h.size() >= 4 && !memcmp(h.data(), "fLaC", 4)) return WriteFlacTags(p, t);
    if (h.size() >= 12 && !memcmp(h.data(), "RIFF", 4) && !memcmp(&h[8], "WAVE", 4))
        return WavRewrite(p, &t);
    if (h.size() >= 4 && !memcmp(h.data(), "OggS", 4)) return WriteOggTags(p, t);
    if (h.size() >= 8 && !memcmp(&h[4], "ftyp", 4)) return WriteMp4TagsImpl(p, t, false);
    std::wstring ext = p.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    bool isMp3 = (ext == L".mp3") || (h.size() >= 3 && !memcmp(h.data(), "ID3", 3));
    if (!isMp3 && h.size() >= 4 && (!memcmp(h.data(), "MAC ", 4) || !memcmp(h.data(), "wvpk", 4)))
        return WriteApeTags(p, t);                              // Monkey's Audio / WavPack
    if (isMp3) return WriteMp3Tags(p, t, writeMode);
    unsigned long long ts = 0;
    if (ApeTagStart(p, ts)) return WriteApeTags(p, t);          // 尾部带 APE 标签的其它容器
    return 1;
}

AudioTags ReadId3v1Only(const fs::path& p) {
    AudioTags t;
    TagFile f(p);
    if (f.Ok()) ReadId3v1(f, t);
    return t;
}

AudioTags ReadId3v2Only(const fs::path& p) {
    AudioTags t;
    TagFile f(p);
    if (f.Ok()) ReadId3v2(f, t);
    return t;
}

static int RemoveFlacTags(const fs::path& p) {
    TagFile f(p);
    if (!f.Ok()) return 2;
    std::vector<unsigned char> h;
    if (!f.At(0, h, 4, 0) || h.size() < 4 || memcmp(h.data(), "fLaC", 4)) return 1;
    struct Blk { unsigned long long off; uint32_t type; uint32_t len; };
    std::vector<Blk> blocks;
    unsigned long long off = 4;
    bool last = false;
    for (int i = 0; i < 128 && !last; ++i) {
        std::vector<unsigned char> bh;
        if (!f.At(off, bh, 4, 0) || bh.size() < 4) return 2;
        last = (bh[0] & 0x80) != 0;
        uint32_t type = bh[0] & 0x7F;
        uint32_t len = ((uint32_t)bh[1] << 16) | ((uint32_t)bh[2] << 8) | bh[3];
        off += 4;
        blocks.push_back({ off, type, len });
        off += len;
    }
    if (!last) return 2;
    bool has = false;
    for (auto& b : blocks) if (b.type == 4) has = true;
    if (!has) return 0;                                         // 没有注释块
    unsigned long long audioOff = off;
    std::vector<unsigned char> out;
    AppendBytes(out, "fLaC", 4);
    std::vector<Blk> keep;
    for (auto& b : blocks) if (b.type != 4) keep.push_back(b);
    for (size_t i = 0; i < keep.size(); ++i) {
        std::vector<unsigned char> body;
        if (keep[i].len) {
            if (!f.At(keep[i].off, body, keep[i].len, 16u << 20) || body.size() != keep[i].len) return 2;
        }
        std::vector<unsigned char> hdr = FlacBlockHdr((unsigned char)keep[i].type,
                                                      i + 1 == keep.size(), keep[i].len);
        out.insert(out.end(), hdr.begin(), hdr.end());
        out.insert(out.end(), body.begin(), body.end());
    }
    RewriteChunk c0, c1;
    c0.range = false; c0.bytes = out;
    c1.range = true; c1.off = audioOff; c1.len = f.size - audioOff;
    f.Close();                                        // 替换文件前必须放手, 否则 MoveFileEx 被拒
    if (!RewriteFileChunks(p, { c0, c1 })) return 2;
    return 0;
}

static int RemoveMp3Tags(const fs::path& p, int removeMode) {
    TagFile f(p);
    if (!f.Ok()) return 2;
    std::vector<unsigned char> h;
    if (!f.At(0, h, 10, 0) || h.size() < 10) return 2;
    bool isMp3 = !memcmp(h.data(), "ID3", 3);
    std::wstring ext = p.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    if (!isMp3 && ext != L".mp3") return 1;
    unsigned long long v2End = 0;
    if (isMp3 && h[3] >= 2 && h[3] <= 4) {
        uint32_t sz = Sync32(&h[6]);
        v2End = 10ull + sz;
        if (h[3] == 4 && (h[5] & 0x10)) v2End += 10;            // v2.4 尾部 footer
        if (v2End > f.size) v2End = 0;                          // 大小不合法, 不动
    }
    unsigned long long v1Start = f.size;
    if (f.size >= 128) {
        std::vector<unsigned char> tail;
        if (f.At(f.size - 128, tail, 128, 0) && tail.size() == 128 && !memcmp(tail.data(), "TAG", 3))
            v1Start = f.size - 128;
    }
    bool stripV2 = (removeMode != 0) && v2End > 0;
    bool stripV1 = (removeMode != 1) && v1Start < f.size;
    if (!stripV2 && !stripV1) return 0;                         // 没有可移除的
    RewriteChunk c;
    c.range = true;
    c.off = stripV2 ? v2End : 0;
    c.len = (stripV1 ? v1Start : f.size) - c.off;
    f.Close();                                        // 替换文件前必须放手, 否则 MoveFileEx 被拒
    if (!RewriteFileChunks(p, { c })) return 2;
    return 0;
}

static int RemoveApeTags(const fs::path& p) {
    unsigned long long tagStart = 0;
    if (!ApeTagStart(p, tagStart)) return 0;                    // 没有 APE 标签
    RewriteChunk c;
    c.range = true; c.off = 0; c.len = tagStart;
    if (!RewriteFileChunks(p, { c })) return 2;
    return 0;
}

int RemoveAudioTags(const fs::path& p, int removeMode) {
    std::error_code ec;
    if (!fs::exists(p, ec)) return 2;
    // 探测容器类型; TagFile 必须在分发前放手, 否则底层重写时的 MoveFileEx 会被拒。
    enum { K_NONE, K_FLAC, K_WAV, K_OGG, K_MP4, K_MP3, K_APE, K_APE_END } kind = K_NONE;
    {
        TagFile f(p);
        if (!f.Ok()) return 2;
        std::vector<unsigned char> h;
        if (!f.At(0, h, 12, 0) || h.size() < 4) return 2;
        if (h.size() >= 4 && !memcmp(h.data(), "fLaC", 4)) kind = K_FLAC;
        else if (h.size() >= 12 && !memcmp(h.data(), "RIFF", 4) && !memcmp(&h[8], "WAVE", 4)) kind = K_WAV;
        else if (h.size() >= 4 && !memcmp(h.data(), "OggS", 4)) kind = K_OGG;
        else if (h.size() >= 8 && !memcmp(&h[4], "ftyp", 4)) kind = K_MP4;
        else {
            std::wstring ext = p.extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
            if (ext == L".mp3" || (h.size() >= 3 && !memcmp(h.data(), "ID3", 3))) kind = K_MP3;
            else if (h.size() >= 4 && (!memcmp(h.data(), "MAC ", 4) || !memcmp(h.data(), "wvpk", 4))) kind = K_APE;
            else {
                unsigned long long ts = 0;
                if (ApeTagStart(p, ts)) kind = K_APE_END;
            }
        }
    }
    switch (kind) {
    case K_FLAC: return RemoveFlacTags(p);
    case K_WAV:  return WavRewrite(p, nullptr);
    case K_OGG: {
        AudioTags empty;                                        // 注释包换成空包 (规范要求它存在)
        return WriteOggTags(p, empty);
    }
    case K_MP4: {
        AudioTags empty;
        return WriteMp4TagsImpl(p, empty, true);
    }
    case K_MP3:  return RemoveMp3Tags(p, removeMode);
    case K_APE:
    case K_APE_END: return RemoveApeTags(p);
    default: return 1;
    }
}

// ----------------------------------------------------------------------------
// 内码转换 (BIG5<->GB / Shift-JIS<->中文)
// ----------------------------------------------------------------------------
// 修的是"按错误代码页误读"的字符串: 每个字符先按 misreadCp 取回它当时的字节,
// 再按 realCp 解释; 解释不了 (那个字节序列在 realCp 里不存在) 就原样留下。
std::wstring FixMojibake(const std::wstring& s, unsigned int realCp, unsigned int misreadCp) {
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s) {
        std::vector<unsigned char> b = WToCp(misreadCp, std::wstring(1, c));
        bool done = false;
        if (b.size() == 1 || b.size() == 2) {
            wchar_t w = 0;
            int n = MultiByteToWideChar(realCp, MB_ERR_INVALID_CHARS,
                                        (const char*)b.data(), (int)b.size(), &w, 1);
            if (n == 1) { out.push_back(w); done = true; }
        }
        if (!done) out.push_back(c);
    }
    return out;
}

// 反向: 先把整串编成 realCp 的字节, 再按 misreadCp 误读成字符串
// (中文 -> 日文 Shift-JIS 用: 让 GBK 系统把文件名显示成那段日文)。
std::wstring MakeMojibake(const std::wstring& s, unsigned int realCp, unsigned int misreadCp) {
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s) {
        std::vector<unsigned char> b = WToCp(realCp, std::wstring(1, c));
        if (b.size() == 2) {
            wchar_t w = 0;
            int n = MultiByteToWideChar(misreadCp, MB_ERR_INVALID_CHARS,
                                        (const char*)b.data(), 2, &w, 1);
            if (n == 1) { out.push_back(w); continue; }
        }
        out.push_back(c);       // 单字节 (ASCII) 或目标代码页读不了的字节: 原样
    }
    return out;
}

// ----------------------------------------------------------------------------
// 名字来源: 网页标题 / 文本第一行
// ----------------------------------------------------------------------------
static void DecodeHtmlEntities(std::wstring& s) {
    static const wchar_t* kEnt[][2] = {
        { L"&amp;", L"&" }, { L"&lt;", L"<" }, { L"&gt;", L">" },
        { L"&quot;", L"\"" }, { L"&apos;", L"'" }, { L"&nbsp;", L" " },
    };
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ) {
        if (s[i] == L'&') {
            bool hit = false;
            for (auto& e : kEnt) {
                size_t n = wcslen(e[0]);
                if (!_wcsnicmp(s.c_str() + i, e[0], n) && i + n <= s.size()) {
                    out += e[1];
                    i += n;
                    hit = true;
                    break;
                }
            }
            if (hit) continue;
            size_t semi = s.find(L';', i + 1);
            if (semi != std::wstring::npos && semi - i <= 10) {
                std::wstring num = s.substr(i + 1, semi - i - 1);
                if (!num.empty() && (num[0] == L'#')) {
                    num.erase(0, 1);
                    bool hex = !num.empty() && (num[0] == L'x' || num[0] == L'X');
                    if (hex) num.erase(0, 1);
                    bool digits = !num.empty();
                    for (wchar_t c : num)
                        if (!iswxdigit(c) && !(c >= L'0' && c <= L'9')) digits = false;
                    if (digits) {
                        long v = wcstol(num.c_str(), NULL, hex ? 16 : 10);
                        if (v > 0 && v < 0x110000) { out.push_back((wchar_t)v); i = semi + 1; continue; }
                    }
                }
            }
        }
        out.push_back(s[i]);
        ++i;
    }
    s.swap(out);
}

// Windows 没有 UTF-16 的代码页常量, BOM 走手工解码 ( CpToW 不支持)。
static std::wstring Utf16FullToW(const unsigned char* p, size_t n, bool be) {
    std::wstring out;
    for (size_t i = 0; i + 1 < n; i += 2) {
        uint16_t u = be ? (uint16_t)((p[i] << 8) | p[i + 1])
                        : (uint16_t)(p[i] | (p[i + 1] << 8));
        out.push_back((wchar_t)u);
    }
    return out;
}

// <title> 标签里的内容: 去掉内嵌标签, 解实体, 折叠空白。
static std::wstring CleanHtmlTitle(std::wstring s) {
    std::wstring noTags;
    for (size_t i = 0; i < s.size(); ) {
        if (s[i] == L'<') {
            size_t e = s.find(L'>', i);
            if (e == std::wstring::npos) break;
            i = e + 1;
        } else {
            noTags.push_back(s[i]);
            ++i;
        }
    }
    DecodeHtmlEntities(noTags);
    std::wstring out;
    bool sp = false;
    for (wchar_t c : noTags) {
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0xA0) { sp = true; continue; }
        if (sp && !out.empty()) out.push_back(L' ');
        sp = false;
        out.push_back(c);
    }
    return out;
}

std::wstring HtmlTitleOf(const fs::path& p) {
    TagFile f(p);
    if (!f.Ok()) return L"";
    unsigned long long want = f.size < (1u << 20) ? f.size : (1u << 20);
    std::vector<unsigned char> b;
    if (!f.At(0, b, want, 0) || b.empty()) return L"";

    // 编码: BOM > meta charset > UTF-8 试探 > GBK
    unsigned int cp = 936;
    size_t bodyStart = 0;
    bool utf16 = false, utf16be = false;
    if (b.size() >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) { cp = CP_UTF8; bodyStart = 3; }
    else if (b.size() >= 2 && b[0] == 0xFF && b[1] == 0xFE) { utf16 = true; bodyStart = 2; }
    else if (b.size() >= 2 && b[0] == 0xFE && b[1] == 0xFF) { utf16 = true; utf16be = true; bodyStart = 2; }
    else {
        // 在前 4KB 里找 charset
        std::string head((const char*)b.data(), (size_t)std::min<unsigned long long>(b.size(), 4096));
        std::string low = head;
        for (auto& c : low) c = (char)tolower((unsigned char)c);
        size_t cs = low.find("charset");
        if (cs != std::string::npos) {
            size_t q1 = low.find_first_of("=\""), q2 = low.find_first_not_of("= \t\"'", cs + 7);
            (void)q1;
            if (q2 != std::string::npos) {
                size_t e = low.find_first_of("\"' ;>", q2);
                std::string name = low.substr(q2, e == std::string::npos ? std::string::npos : e - q2);
                if (name.find("utf-8") != std::string::npos || name.find("utf8") != std::string::npos) cp = CP_UTF8;
                else if (name.find("big5") != std::string::npos) cp = 950;
                else if (name.find("shift") != std::string::npos || name.find("sjis") != std::string::npos ||
                         name.find("x-euc") != std::string::npos) cp = 932;
                else if (name.find("gb") != std::string::npos) cp = 936;
                else if (name.find("1252") != std::string::npos || name.find("8859") != std::string::npos ||
                         name.find("ascii") != std::string::npos) cp = 1252;
            }
        } else if (b.size() > bodyStart) {
            // 没有 meta: 整段是合法 UTF-8 就按 UTF-8, 否则按 GBK
            int w = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                        (const char*)&b[bodyStart], (int)(b.size() - bodyStart), NULL, 0);
            cp = (w > 0) ? CP_UTF8 : 936;
        }
    }
    // 找 <title> ... </title>
    std::wstring text = utf16 ? Utf16FullToW(&b[bodyStart], b.size() - bodyStart, utf16be)
                              : CpToW(cp, (const char*)&b[bodyStart], (int)(b.size() - bodyStart));
    std::wstring low = text;
    for (auto& c : low) c = (wchar_t)towlower(c);
    size_t t0 = low.find(L"<title");
    if (t0 == std::wstring::npos) return L"";
    size_t g0 = low.find(L'>', t0);
    if (g0 == std::wstring::npos) return L"";
    size_t t1 = low.find(L"</title", g0);
    if (t1 == std::wstring::npos) t1 = text.size();
    return CleanHtmlTitle(text.substr(g0 + 1, t1 - g0 - 1));
}

std::wstring FirstLineOf(const fs::path& p) {
    TagFile f(p);
    if (!f.Ok()) return L"";
    std::vector<unsigned char> b;
    if (!f.At(0, b, 65536, 0) || b.empty()) return L"";
    size_t i = 0;
    unsigned int cp = CP_UTF8;
    bool utf16 = false, utf16be = false;
    if (b.size() >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) { i = 3; }
    else if (b.size() >= 2 && b[0] == 0xFF && b[1] == 0xFE) { utf16 = true; i = 2; }
    else if (b.size() >= 2 && b[0] == 0xFE && b[1] == 0xFF) { utf16 = true; utf16be = true; i = 2; }
    else {
        int w = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                    (const char*)b.data(), (int)b.size(), NULL, 0);
        if (w <= 0) cp = 936;                                   // 不是 UTF-8 就按 GBK
    }
    size_t j = i;
    if (utf16) {
        while (j + 1 < b.size()) {
            uint16_t u = utf16be ? (uint16_t)((b[j] << 8) | b[j + 1])
                                 : (uint16_t)(b[j] | (b[j + 1] << 8));
            if (u == L'\n' || u == L'\r') break;
            j += 2;
        }
    } else {
        while (j < b.size() && b[j] != '\r' && b[j] != '\n') ++j;
    }
    std::wstring s = utf16 ? Utf16FullToW(&b[i], j - i, utf16be)
                           : CpToW(cp, (const char*)&b[i], (int)(j - i));
    CleanText(s);
    return s;
}

bool ParseNameToTags(const std::wstring& expr, const std::wstring& nameNoExt, AudioTags& out) {
    if (expr.empty()) return false;
    std::wstring re = L"^";
    std::vector<int> order;
    for (size_t i = 0; i < expr.size(); ++i) {
        if (expr[i] == L'%' && i + 1 < expr.size()) {
            wchar_t n = expr[i + 1];
            if (n >= L'1' && n <= L'6') { re += L"(.+?)"; order.push_back(n - L'1'); ++i; continue; }
            if (n == L'%') { re += L"%"; ++i; continue; }
        }
        wchar_t c = expr[i];
        if (wcschr(L"()[]{}+*?^$.|\\", c)) re += L'\\';
        re += c;
    }
    re += L"$";
    std::wregex rx;
    try { rx.assign(re, std::regex_constants::ECMAScript); }
    catch (...) { return false; }
    std::wsmatch m;
    try {
        if (!std::regex_search(nameNoExt, m, rx)) return false;
    } catch (...) { return false; }
    for (size_t k = 0; k < order.size() && k + 1 < m.size(); ++k) {
        std::wstring v = m[k + 1].str();
        CleanText(v);
        if (v.empty()) continue;
        switch (order[k]) {
        case 0: out.artist = v; break;
        case 1: out.title = v; break;
        case 2: out.track = v; break;
        case 3: out.album = v; break;
        case 4: out.year = v; break;
        case 5: out.comment = v; break;
        }
    }
    return true;
}

// Filename-hostile characters in a tag value become spaces: a title with '/' would
// silently move the renamed file into a subfolder (or fail the rename).
static std::wstring SanitizeForName(std::wstring s) {
    for (auto& c : s)
        if (c < 32 || wcschr(L"\\/:*?\"<>|", c)) c = L' ';
    CleanText(s);
    return s;
}

std::wstring ExpandTagExpr(const std::wstring& expr, const AudioTags& t) {
    std::wstring out;
    for (size_t i = 0; i < expr.size(); ++i) {
        if (expr[i] == L'%' && i + 1 < expr.size()) {
            const std::wstring* v = nullptr;
            switch (expr[i + 1]) {
            case L'1': v = &t.artist; break;
            case L'2': v = &t.title; break;
            case L'3': v = &t.track; break;
            case L'4': v = &t.album; break;
            case L'5': v = &t.year; break;
            case L'6': v = &t.comment; break;
            case L'%': out += L'%'; ++i; continue;
            }
            if (v) { out += SanitizeForName(*v); ++i; continue; }
        }
        out += expr[i];
    }
    return out;
}

// ----------------------------------------------------------------------------
// Core: compute new name (pipeline)
// ----------------------------------------------------------------------------
// 自动更正错误: repair a name Windows would refuse. Illegal characters become '-',
// a trailing dot or space (which the shell hides but MoveFileW rejects) is dropped, and
// an empty or all-dot result falls back to the name the file already has.
static std::wstring CorrectIllegalName(const std::wstring& in, const std::wstring& fallback) {
    std::wstring s;
    for (wchar_t c : in)
        s.push_back(c < 32 || wcschr(L"\\/:*?\"<>|", c) ? L'-' : c);
    while (!s.empty() && (s.back() == L'.' || s.back() == L' ')) s.pop_back();
    size_t first = s.find_first_not_of(L". ");
    if (first == std::wstring::npos) return fallback;
    if (first > 0) s.erase(0, first);
    return s;
}

std::wstring ComputeNewName(const FileItem& item, const RenameConfig& cfg, int index) {
    fs::path p = item.fullPath;
    std::wstring origStem = p.stem().wstring();
    std::wstring origExt = p.extension().wstring();
    std::wstring origExtNoDot = origExt.empty() ? L"" : origExt.substr(1);

    std::wstring cur = p.filename().wstring();

    // ---- -1) 名字来源 (菜单命令: 网页标题 / 文本第一行 / 文本文件命令) ----
    // 网页标题只作用于 .htm/.html, 文本第一行只作用于 .txt; 取不到内容
    // (无标题/空文件) 时保持原名, 不会把文件抹成空名。
    if (cfg.nameSource >= 1 && cfg.nameSource <= 3) {
        std::wstring stem, ext;
        SplitName(cur, stem, ext);
        std::wstring src;
        bool full = false;                       // true = src 是完整新文件名
        if (cfg.nameSource == 1) {
            std::wstring e = p.extension().wstring();
            std::transform(e.begin(), e.end(), e.begin(), ::towlower);
            if (e == L".htm" || e == L".html") src = HtmlTitleOf(p);
        } else if (cfg.nameSource == 2) {
            std::wstring e = p.extension().wstring();
            std::transform(e.begin(), e.end(), e.begin(), ::towlower);
            if (e == L".txt") src = FirstLineOf(p);
        } else if (cfg.nameSource == 3 && cfg.textCmds) {
            std::wstring fn = p.filename().wstring();
            auto it = cfg.textCmds->find(fn);
            if (it != cfg.textCmds->end()) {
                src = it->second;
                full = true;
            } else {
                // 文件名大小写不敏感的回查 (Windows 文件系统本来就不区分大小写)
                for (const auto& kv : *cfg.textCmds) {
                    if (_wcsicmp(kv.first.c_str(), fn.c_str()) == 0) {
                        src = kv.second;
                        full = true;
                        break;
                    }
                }
            }
        }
        if (!src.empty()) {
            std::wstring clean = SanitizeForName(src);
            if (!clean.empty()) {
                if (full) {
                    // 命令给的是完整新文件名; 没带扩展名就保留原扩展名
                    cur = (clean.find(L'.') == std::wstring::npos && !ext.empty())
                              ? (clean + ext) : clean;
                } else {
                    cur = clean + ext;
                }
            }
        }
    }

    // ---- 0) 标签命名: the tag expression replaces the stem, then the other tabs edit
    // that result. A file with no tags (or an expression that expands to nothing) keeps
    // its own name, so a mixed folder is not flattened into separator-only filenames.
    if (cfg.tgEnable && !cfg.tgExpr.empty() && item.tags.HasAny()) {
        AudioTags t = item.tags;
        // 音轨数字低于两位自动补0 (设置 > 音乐文件标签): "3" reads as "03" in a list.
        if (cfg.trackPad2 && t.track.size() == 1 && t.track[0] >= L'1' && t.track[0] <= L'9')
            t.track = L"0" + t.track;
        std::wstring s = SanitizeForName(ExpandTagExpr(cfg.tgExpr, t));
        if (!s.empty()) {
            std::wstring stem, ext;
            SplitName(cur, stem, ext);
            cur = s + ext;
        }
    }

    // ---- 1) 整体: name case / override ----
    {
        std::wstring stem, ext;
        SplitName(cur, stem, ext);
        if (!cfg.ovNameOverride.empty()) {
            std::wstring o = cfg.ovNameOverride;
            ExpandMeta(o, p, origStem, origExtNoDot, cfg.useMeta, index);
            stem = o;
        } else if (cfg.ovNameCase != 0) {
            stem = ApplyCase(stem, cfg.ovNameCase, cfg.wordSplitMode);
        }
        cur = stem + ext;
    }

    // ---- 2) 替换 ----
    if (cfg.rpEnable && !cfg.rpFind.empty()) {
        std::wstring find = cfg.rpFind;
        bool useRegex = cfg.rpRegex || find[0] == L':';
        if (useRegex && find[0] == L':') find = find.substr(1);
        std::wstring repl = cfg.rpReplace;
        ExpandMeta(repl, p, origStem, origExtNoDot, cfg.useMeta, index);
        if (useRegex)
            cur = RegexReplace(cur, find, repl, cfg.rpCaseInsensitive);
        else if (cfg.rpCaseInsensitive)
            cur = ReplaceAllCI(cur, find, repl);
        else
            cur = ReplaceAll(cur, find, repl);
    }

    // ---- 3) 删除 ----
    if (cfg.dlEnable) {
        std::wstring stem, ext;
        SplitName(cur, stem, ext);
        std::wstring& target = (cfg.dlTarget == 0) ? stem : ext;
        if (cfg.dlTarget == 1 && !ext.empty()) target = ext.substr(1); // drop dot for ext
        if (cfg.dlMode == 0) {
            target = ReplaceAll(target, cfg.dlText, L"");
        } else if (cfg.dlMode == 1) { // delete first N
            int n = cfg.dlNum;
            if (n >= (int)target.size()) target = L"";
            else target = target.substr(n);
        } else if (cfg.dlMode == 2) { // delete last N
            int n = cfg.dlNum;
            if (n >= (int)target.size()) target = L"";
            else target = target.substr(0, target.size() - n);
        } else if (cfg.dlMode == 3) { // delete 数量 chars from the 起始-th (1-based)
            int from = cfg.dlStart - 1;
            int n = cfg.dlNum;
            if (from < 0) from = 0;
            if (from < (int)target.size() && n > 0) {
                if (from + n > (int)target.size()) n = (int)target.size() - from;
                target.erase(from, n);
            }
        }
        if (cfg.dlTarget == 1) ext = target.empty() ? L"" : (L"." + target);
        cur = stem + ext;
    }

    // ---- 4) 添加 ----
    if (cfg.adEnable && !cfg.adText.empty()) {
        std::wstring stem, ext;
        SplitName(cur, stem, ext);
        std::wstring add = cfg.adText;
        ExpandMeta(add, p, origStem, origExtNoDot, cfg.useMeta, index);
        std::wstring extNoDot = ext.empty() ? L"" : ext.substr(1);
        switch (cfg.adWhere) {
        case 0: stem = add + stem; break;
        case 1: stem = stem + add; break;
        case 2: extNoDot = add + extNoDot; break;
        case 3: extNoDot = extNoDot + add; break;
        case 4: { // insert at position
            int pos = cfg.adPos - 1;
            if (pos < 0) pos = 0;
            if (pos > (int)stem.size()) pos = (int)stem.size();
            stem = stem.substr(0, pos) + add + stem.substr(pos);
            break;
        }
        }
        ext = extNoDot.empty() ? L"" : (L"." + extNoDot);
        cur = stem + ext;
    }

    // ---- 5) 序号 ----
    if (cfg.nmEnable && !cfg.nmTemplate.empty()) {
        long long n = (long long)cfg.nmStart + (long long)index * cfg.nmStep;
        if (n < 0) n = 0;
        std::wstring numStr;
        if (cfg.nmLetters) {
            numStr = ToLetters(n, cfg.nmUpper);
        } else {
            // 位数 only pads the numeric form; letters have no width concept here.
            numStr = std::to_wstring(n);
            if (cfg.nmPad && cfg.nmWidth > (int)numStr.size())
                numStr = std::wstring(cfg.nmWidth - numStr.size(), L'0') + numStr;
        }
        std::wstring tpl = cfg.nmTemplate;
        ExpandMeta(tpl, p, origStem, origExtNoDot, cfg.useMeta, index, cur); // <SELF>=cur
        if (tpl.find(L'#') != std::wstring::npos) {
            size_t pos;
            while ((pos = tpl.find(L'#')) != std::wstring::npos)
                tpl.replace(pos, 1, numStr);
        }
        cur = tpl;
        // Intuitive for "加序号": if the result has no extension, keep the
        // original one so we never accidentally strip ".jpg"/".txt" etc.
        if (cur.find(L'.') == std::wstring::npos && !origExtNoDot.empty())
            cur += (L"." + origExtNoDot);
    }

    // ---- 6) 扩展更名 ----
    if (cfg.exEnable) {
        std::wstring stem, ext;
        SplitName(cur, stem, ext);
        // 扩展命令 rewrites the name body only; the extension is what the modes below own.
        if (!cfg.exCmd.empty()) stem = ApplyExtCommands(stem, cfg.exCmd);
        std::wstring extNoDot = ext.empty() ? L"" : ext.substr(1);
        switch (cfg.exMode) {
        case 0: // add extension
            if (!cfg.exText.empty() && extNoDot.empty())
                extNoDot = cfg.exText;
            break;
        case 1: // delete extension
            extNoDot = L"";
            break;
        case 2: // change extension
            extNoDot = cfg.exText;
            break;
        case 3: // extension case
            extNoDot = ApplyCase(extNoDot, cfg.exCase);
            break;
        }
        if (!extNoDot.empty() && extNoDot[0] == L'.') extNoDot = extNoDot.substr(1);
        ext = extNoDot.empty() ? L"" : (L"." + extNoDot);
        cur = stem + ext;
    }

    // ---- 7) 整体: extension case / override (final pass) ----
    if (cfg.ovExtCase != 0 || !cfg.ovExtOverride.empty()) {
        std::wstring stem, ext;
        SplitName(cur, stem, ext);
        std::wstring extNoDot = ext.empty() ? L"" : ext.substr(1);
        if (!cfg.ovExtOverride.empty()) {
            extNoDot = cfg.ovExtOverride;
            if (!extNoDot.empty() && extNoDot[0] == L'.') extNoDot = extNoDot.substr(1);
        } else if (cfg.ovExtCase != 0) {
            extNoDot = ApplyCase(extNoDot, cfg.ovExtCase);
        }
        ext = extNoDot.empty() ? L"" : (L"." + extNoDot);
        cur = stem + ext;
    }

    // ---- 8) CRC32 ----
    if (cfg.crcEnable) {
        std::wstring stem, ext;
        SplitName(cur, stem, ext);
        cur = Crc32Hex(p) + ext;
    }

    // ---- 8.5) 简繁互转 ----
    // 整个文件名一起转，扩展名里的中文也跟着转。
    if (cfg.convMode == 1) cur = ConvertT2S(cur);
    else if (cfg.convMode == 2) cur = ConvertS2T(cur);

    // ---- 8.6) 文件名内码转换 (BIG5<->GB / Shift-JIS<->中文) ----
    // 修"按错误代码页误读"的名字: BIG5->GB 就是把每个字符按 GBK 取回字节再按
    // Big5 解释; 中文->Shift-JIS 反过来, 让 GBK 系统把名字显示成那段日文。
    if (cfg.encConv == 1)      cur = FixMojibake(cur, 950, 936);
    else if (cfg.encConv == 2) cur = FixMojibake(cur, 936, 950);
    else if (cfg.encConv == 3) cur = FixMojibake(cur, 932, 936);
    else if (cfg.encConv == 4) cur = MakeMojibake(cur, 932, 936);

    // ---- 9) 自动更正错误 (设置 > 一般参数) ----
    // Off, an illegal name is left as typed so the row fails at 应用 and shows 失败.
    // On, the characters Windows refuses in a file name are replaced and a name that
    // would be illegal as-is (empty, all dots) falls back to the original.
    if (cfg.autoCorrect) cur = CorrectIllegalName(cur, item.oldName);

    return cur;
}
