// 语言包: 解析 + 查找 + 往窗口上刷文本。详见 lang.h 顶部的说明。
#include "lang.h"

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <unordered_map>

#include "lang_ids.h"
#include "resource.h"

// ---------------------------------------------------------------------------
// 键名 -> 数值 id (tools/extract_lang.py 生成)
// ---------------------------------------------------------------------------
struct LangKeyEntry { const wchar_t* name; int id; };
static const LangKeyEntry kLangKeys[] = {
#include "lang_keys.inc"
};

static const int kLangKeyCount = (int)(sizeof(kLangKeys) / sizeof(kLangKeys[0]));

// exe 内嵌的语言包。zh-CN 一定在, 且永远作为回退底稿。
static const struct { int res; const wchar_t* code; } kEmbeddedLangs[] = {
    { IDR_LANG_ZH_CN, L"zh-CN" },
    { IDR_LANG_EN_US, L"en-US" },
};

// 顶层弹出菜单按 index 对应的文本键 (顺序由 AradeRenamer.rc 的 IDR_MAINMENU 决定)。
static const int kTopPopupIds[] = { IDS_MNU_FILE, IDS_MNU_RENAME, IDS_MNU_TAG,
                                    IDS_MNU_OPT,  IDS_MNU_HELP };
// 唯一的二级弹出菜单: 选项 > 语言。
static const int kSubPopupId = IDS_MNU_LANGUAGE;

typedef std::unordered_map<int, std::wstring> LangMap;

static LangMap      g_cur;          // 当前语言
static LangMap      g_base;         // 内置简体中文 (回退)
static std::wstring g_code;         // 实际生效的语言
static std::wstring g_choice;       // 用户在菜单上选的 (可能是 L"auto")
static std::vector<LangInfo> g_list;
static HINSTANCE    g_inst = NULL;
static std::wstring g_dir;

static const wchar_t* const kRegKey  = L"Software\\AradeRenamer";
static const wchar_t* const kRegLang = L"Language";

// ---------------------------------------------------------------------------
// 查找
// ---------------------------------------------------------------------------
const wchar_t* LS(int id) {
    LangMap::const_iterator it = g_cur.find(id);
    if (it != g_cur.end()) return it->second.c_str();
    it = g_base.find(id);
    if (it != g_base.end()) return it->second.c_str();
    return L"";
}

// ---------------------------------------------------------------------------
// 解析 (INI 扁平键值, UTF-8 或 UTF-16LE, 可带 BOM)
// ---------------------------------------------------------------------------
static std::wstring DecodeText(const char* p, size_t n) {
    if (n >= 2 && (unsigned char)p[0] == 0xFF && (unsigned char)p[1] == 0xFE)
        return std::wstring((const wchar_t*)(p + 2), (n - 2) / sizeof(wchar_t));
    if (n >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB &&
        (unsigned char)p[2] == 0xBF) { p += 3; n -= 3; }
    if (n == 0) return std::wstring();
    int need = MultiByteToWideChar(CP_UTF8, 0, p, (int)n, NULL, 0);
    if (need <= 0) return std::wstring();
    std::wstring s((size_t)need, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, p, (int)n, &s[0], need);
    return s;
}

static bool IsSpace(wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; }

static std::wstring Trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && IsSpace(s[a])) ++a;
    while (b > a && IsSpace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

// \n \r \t \0 \\ \" —— 和 tools/extract_lang.py 的 esc()/unesc() 同一套约定。
static std::wstring Unescape(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size()) {
            wchar_t c = s[i + 1];
            if (c == L'n')       { out += L'\n'; ++i; continue; }
            if (c == L'r')       { out += L'\r'; ++i; continue; }
            if (c == L't')       { out += L'\t'; ++i; continue; }
            if (c == L'0')       { out += L'\0'; ++i; continue; }
            if (c == L'\\')      { out += L'\\'; ++i; continue; }
            if (c == L'"')       { out += L'"';  ++i; continue; }
        }
        out += s[i];
    }
    return out;
}

// out 里按 id 存译文; info 收下 lang.name / lang.code。解析不出 lang.code 就算失败。
static bool ParseLang(const std::wstring& text, LangMap& out, LangInfo& info) {
    static std::unordered_map<std::wstring, int> nameToId;
    if (nameToId.empty())
        for (int i = 0; i < kLangKeyCount; ++i) nameToId[kLangKeys[i].name] = kLangKeys[i].id;

    out.clear();
    info.name.clear();
    info.code.clear();
    size_t i = 0;
    while (i < text.size()) {
        size_t e = text.find(L'\n', i);
        if (e == std::wstring::npos) e = text.size();
        std::wstring line = text.substr(i, e - i);
        i = e + 1;
        std::wstring t = Trim(line);
        if (t.empty() || t[0] == L';' || t[0] == L'#') continue;
        size_t eq = t.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = Trim(t.substr(0, eq));
        std::wstring val = Unescape(Trim(t.substr(eq + 1)));
    if (key == L"lang.name")     { info.name = val; continue; }
    if (key == L"lang.code")     { info.code = val; continue; }
    if (key == L"lang.author")   { info.author = val; continue; }
        std::unordered_map<std::wstring, int>::const_iterator k = nameToId.find(key);
        if (k != nameToId.end()) out[k->second] = val;
        // 不认识的键直接丢掉: 旧版语言包在新 exe 上也能用 (缺的键回退中文)。
    }
    return !info.code.empty();
}

// ---------------------------------------------------------------------------
// 载入: 磁盘文件 / exe 内嵌 RCDATA
// ---------------------------------------------------------------------------
static bool LoadFile(const std::wstring& path, LangMap& out, LangInfo& info) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 8 * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    std::string buf((size_t)sz.QuadPart, '\0');
    DWORD got = 0;
    BOOL ok = ReadFile(h, &buf[0], (DWORD)buf.size(), &got, NULL);
    CloseHandle(h);
    if (!ok || got == 0) return false;
    info.file = path;
    return ParseLang(DecodeText(buf.data(), got), out, info);
}

static bool LoadEmbedded(int resId, LangMap& out, LangInfo& info) {
    HRSRC hr = FindResourceW(g_inst, MAKEINTRESOURCEW(resId), RT_RCDATA);
    if (!hr) return false;
    HGLOBAL hg = LoadResource(g_inst, hr);
    if (!hg) return false;
    const char* p = (const char*)LockResource(hg);
    DWORD n = SizeofResource(g_inst, hr);
    if (!p || n == 0) return false;
    info.file.clear();
    info.resId = resId;
    return ParseLang(DecodeText(p, n), out, info);
}

// ---------------------------------------------------------------------------
// 目录 / 注册表
// ---------------------------------------------------------------------------
std::wstring LangDir() { return g_dir; }

static void InitDir() {
    wchar_t buf[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring exe(buf);
    size_t s = exe.find_last_of(L"\\/");
    g_dir = (s == std::wstring::npos ? L"." : exe.substr(0, s)) + L"\\Language";
}

static std::wstring RegGetString() {
    std::wstring v;
    HKEY k = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegKey, 0, KEY_READ, &k) != ERROR_SUCCESS)
        return v;
    wchar_t buf[128];
    DWORD cb = sizeof(buf), type = 0;
    if (RegQueryValueExW(k, kRegLang, NULL, &type, (BYTE*)buf, &cb) == ERROR_SUCCESS &&
        type == REG_SZ && cb >= sizeof(wchar_t))
        v.assign(buf, cb / sizeof(wchar_t) - (buf[cb / sizeof(wchar_t) - 1] == 0 ? 1 : 0));
    RegCloseKey(k);
    return v;
}

// ---------------------------------------------------------------------------
// 初始化 / 切换
// ---------------------------------------------------------------------------
static void ScanLanguages() {
    g_list.clear();
    for (int i = 0; i < (int)(sizeof(kEmbeddedLangs) / sizeof(kEmbeddedLangs[0])); ++i) {
        LangInfo li;
        li.code = kEmbeddedLangs[i].code;
        li.resId = kEmbeddedLangs[i].res;
        LangMap dump;
        if (LoadEmbedded(kEmbeddedLangs[i].res, dump, li)) {
            li.resId = kEmbeddedLangs[i].res;
            g_list.push_back(li);
        }
    }
    // 扫磁盘上的 Language\*.ini。目录不存在 / 目录是空的就直接跳过 —— 注意这里不能提前
    // return: 单文件发行时 exe 旁边根本没有 Language 目录, 那样会把下面的"自动"也一起弄丢。
    std::wstring pat = g_dir + L"\\*.ini";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring path = g_dir + L"\\" + fd.cFileName;
            LangInfo li;
            li.file = path;
            LangMap dump;
            if (!LoadFile(path, dump, li) || li.code.empty() || li.name.empty()) continue;
            bool replaced = false;
            for (size_t i = 0; i < g_list.size(); ++i)
                if (g_list[i].code == li.code) { g_list[i] = li; replaced = true; break; }
            if (!replaced) g_list.push_back(li);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    // 固定第一项: 跟随系统界面语言。名字在菜单里用 LS(IDS_LANG_AUTO) 现取, 所以会跟着
    // 当前语言变 (中文界面显示"自动", 英文界面显示"Auto")。
    LangInfo autoLi;
    autoLi.code = L"auto";
    g_list.insert(g_list.begin(), autoLi);
}

// 按 Windows 的界面语言挑一个最接近的可用语言: 先全等 (zh-CN), 再比主语言 (zh)。
// 挑不到就回退简体中文 —— 内置中文永远在, 不会落空。
static std::wstring ResolveSystemLang() {
    LANGID id = GetUserDefaultUILanguage();
    wchar_t buf[LOCALE_NAME_MAX_LENGTH] = { 0 };
    std::wstring full, prim;
    if (id && LCIDToLocaleName(MAKELCID(id, SORT_DEFAULT), buf, LOCALE_NAME_MAX_LENGTH, 0)
            && buf[0]) {
        full = buf;                                   // "zh-CN" / "en-US" / "zh-Hans-CN"
        size_t dash = full.find(L'-');
        prim = (dash == std::wstring::npos) ? full : full.substr(0, dash);
    }
    for (size_t i = 0; i < g_list.size() && !full.empty(); ++i)
        if (g_list[i].code != L"auto" && _wcsicmp(g_list[i].code.c_str(), full.c_str()) == 0)
            return g_list[i].code;
    for (size_t i = 0; i < g_list.size() && !prim.empty(); ++i) {
        if (g_list[i].code == L"auto") continue;
        std::wstring p = g_list[i].code.substr(0, g_list[i].code.find(L'-'));
        if (_wcsicmp(p.c_str(), prim.c_str()) == 0) return g_list[i].code;
    }
    return L"zh-CN";
}

void LangInit(HINSTANCE hInst) {
    g_inst = hInst ? hInst : GetModuleHandleW(NULL);
    InitDir();

    // 内置简体中文永远先装好, 后面不管选什么语言都有底稿可退。
    LangInfo zh;
    zh.code = L"zh-CN";
    LoadEmbedded(IDR_LANG_ZH_CN, g_base, zh);
    g_code = L"zh-CN";

    ScanLanguages();

    std::wstring want = RegGetString();
    if (want.empty()) want = L"zh-CN";   // 全新安装: 简体中文 (想跟随系统就选"自动")
    LangSet(want);
}

const std::vector<LangInfo>& LangList()   { return g_list; }
const std::wstring& LangCurrent()         { return g_code; }      // 实际生效的
const std::wstring& LangChoice()          { return g_choice; }    // 菜单上勾的那个

bool LangSet(const std::wstring& choice) {
    std::wstring code = (choice == L"auto") ? ResolveSystemLang() : choice;
    for (size_t i = 0; i < g_list.size(); ++i) {
        if (g_list[i].code != code) continue;
        LangMap m;
        LangInfo info;
        info.code = code;
        bool ok = g_list[i].file.empty() ? LoadEmbedded(g_list[i].resId, m, info)
                                         : LoadFile(g_list[i].file, m, info);
        if (!ok) return false;
        if (!info.name.empty())   g_list[i].name   = info.name;
        if (!info.author.empty()) g_list[i].author = info.author;
        g_cur.swap(m);
        g_code = code;
        g_choice = choice;
        return true;
    }
    return false;
}

void LangSaveChoice(const std::wstring& code) {
    HKEY k = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL)
        != ERROR_SUCCESS) return;
    RegSetValueExW(k, kRegLang, 0, REG_SZ, (const BYTE*)code.c_str(),
                   (DWORD)((code.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
}

// ---------------------------------------------------------------------------
// 刷到窗口上
// ---------------------------------------------------------------------------
static void SetMenuText(HMENU m, int pos, const wchar_t* t) {
    MENUITEMINFOW mi = { sizeof(mi) };
    mi.fMask = MIIM_STRING;
    mi.dwTypeData = (LPWSTR)t;
    SetMenuItemInfoW(m, (UINT)pos, TRUE, &mi);
}

static bool HasText(int id) {
    return g_cur.count(id) != 0 || g_base.count(id) != 0;
}

static void ApplyMenu(HMENU m, int depth) {
    int n = GetMenuItemCount(m);
    for (int i = 0; i < n; ++i) {
        MENUITEMINFOW mi = { sizeof(mi) };
        mi.fMask = MIIM_SUBMENU | MIIM_ID | MIIM_FTYPE;
        if (!GetMenuItemInfoW(m, (UINT)i, TRUE, &mi)) continue;
        if (mi.fType & MFT_SEPARATOR) continue;
        if (mi.hSubMenu) {
            int key = (depth == 0 && i < (int)(sizeof(kTopPopupIds) / sizeof(kTopPopupIds[0])))
                          ? kTopPopupIds[i] : kSubPopupId;
            if (HasText(key)) SetMenuText(m, i, LS(key));
            ApplyMenu(mi.hSubMenu, depth + 1);
        } else if (mi.wID > 0 && HasText(mi.wID)) {
            SetMenuText(m, i, LS(mi.wID));
        }
    }
}

struct ApplyCtx { HWND dlg; };

static BOOL CALLBACK ApplyChild(HWND h, LPARAM) {
    int id = GetDlgCtrlID(h);
    if (id == 0) return TRUE;
    wchar_t cls[64] = { 0 };
    if (!GetClassNameW(h, cls, 64)) return TRUE;
    // 只动 Button / Static: edit / combo / list / header 里的文字是数据, 不是标签。
    if (_wcsicmp(cls, L"Button") != 0 && _wcsicmp(cls, L"Static") != 0) return TRUE;
    if (!HasText(id)) return TRUE;
    SetWindowTextW(h, LS(id));
    return TRUE;
}

void ApplyLanguage(HWND hDlg, int titleId) {
    if (!hDlg) return;
    HMENU m = GetMenu(hDlg);
    if (m) {
        ApplyMenu(m, 0);
        DrawMenuBar(hDlg);
    }
    if (HasText(titleId)) SetWindowTextW(hDlg, LS(titleId));
    EnumChildWindows(hDlg, ApplyChild, 0);
    InvalidateRect(hDlg, NULL, TRUE);
}
