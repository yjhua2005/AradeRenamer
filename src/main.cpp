// Arade文件更名器 (C++ reimplementation) - Win32 API, no MFC/.NET
// UI mirrors the classic tabbed bulk-renamer layout (整体/替换/序号/添加/删除/扩展更名/标签命名/标签写入)
#define _WIN32_IE 0x0600
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <objbase.h>
#include <string>
#include <vector>
#include <algorithm>
#include <ctime>
#include <sys/stat.h>
#include <cwchar>
#include <regex>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <functional>
#include "resource.h"
#include "lang_ids.h"
#include "lang.h"
#include "engine.h"
#include "conv.h"

// Forward declarations (some helpers are called before their definition)
static void RebuildList();   // static 要和下面的定义一致, 否则 MSVC 报 C4211(extern 重定义为 static)
void Recompute(int limit = -1);
static void AutoPreview();
static void UpdateTagPanel();
static void InsertTagToken(int editId, const wchar_t* token);
static void UpdateToolbarState();
static void ApplySettings();
static void Layout();
static fs::path ExeDir();

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "msimg32.lib")

// ----------------------------------------------------------------------------
// Globals
// ----------------------------------------------------------------------------
static HWND   g_hDlg = NULL;
static HWND   g_hList = NULL;
static HWND   g_hStatus = NULL;
static HWND   g_hProgress = NULL;
static HWND   g_hTab = NULL;
static HWND   g_hTip = NULL;
// 工具栏按钮的内嵌图标。按钮 id -> 资源 id, 启动时按当前 dpi 载入 hIcon;
// 载入失败时 OnDrawToolbarButton 退回 DrawGlyph 的几何画法。
struct TbIconEntry { int btnId; int resId; HICON hIcon; };
static TbIconEntry g_tbIcons[] = {
    { IDC_TB_ADD,        IDI_IC_ADD,     NULL },
    { IDC_TB_ADDF,       IDI_IC_ADDF,    NULL },
    { IDC_TB_DEL,        IDI_IC_DEL,     NULL },
    { IDC_TB_CLEAR,      IDI_IC_CLEAR,   NULL },
    { IDC_TB_PREVIEW,    IDI_IC_PREVIEW, NULL },
    { IDC_TB_APPLY,      IDI_IC_APPLY,   NULL },
    { IDC_TB_UNDO,       IDI_IC_UNDO,    NULL },
    { IDC_TB_PIN,        IDI_IC_PIN,     NULL },
    { IDC_TB_TAGDEL,     IDI_IC_TAGDEL,   NULL },
    { IDC_TB_TAGEXTRACT, IDI_IC_TAGEXT,   NULL },
    { IDC_TB_TAGWRITE,   IDI_IC_TAGWRITE, NULL },
    { IDC_TB_T2S,        IDI_IC_T2S,      NULL },
    { IDC_TB_TEXTCMD,    IDI_IC_TEXTCMD,  NULL },
    { IDC_TB_EXPLIST,    IDI_IC_EXPLIST,  NULL },
    { IDC_TB_RECOPEN,    IDI_IC_RECOPEN,  NULL },
    { IDC_TB_RECSAVE,    IDI_IC_RECSAVE,  NULL },
    { IDC_TB_RECCLEAR,   IDI_IC_RECCLEAR, NULL },
    { IDC_TB_CHGDATE,    IDI_IC_CHGDATE,  NULL },
    { IDC_TB_REC_PANEL,  IDI_IC_RECPANEL, NULL },
    { IDC_TB_SPONSOR,    IDI_IC_SPONSOR,  NULL },
};
static const int kTbIconCount = (int)(sizeof(g_tbIcons) / sizeof(g_tbIcons[0]));
static bool   g_pinTop = false;         // 窗口置顶 (always-on-top) state
static std::vector<FileItem> g_items;
static bool g_crcRename = false;   // 以文件的 CRC32 更名 (菜单命令)
static int  g_convMode = 0;        // 简繁互转: 0 关, 1 繁体转简体, 2 简体转繁体 (菜单命令)
// 名字来源 (菜单命令, 互斥): 0 关, 1 以网页文件的标题更名 (.htm/.html),
// 2 以文本文件的第一行内容更名 (.txt), 3 调用文本文件命令 (g_textCmds)。
static int  g_nameSource = 0;
static std::map<std::wstring, std::wstring> g_textCmds;   // 原名 -> 新名
static std::wstring g_textCmdFile;
// 文件名内码转换 (菜单命令): 0 关, 1 BIG5->GB, 2 GB->BIG5,
// 3 日文Shift-JIS转中文, 4 中文转日文Shift-JIS。
static int  g_encConv = 0;

struct UndoEntry { int index; fs::path original; fs::path renamed; std::wstring origName; };
static std::vector<std::vector<UndoEntry>> g_undo;

// 操作记录: 每次成功更名的 原路径 -> 新路径, 会话内累积, 可存成 .xto 再打开。
// 与 g_undo 的区别是它不会被撤销吃掉, 是给用户看/存档的账本。
struct RecEntry { std::wstring from; std::wstring to; };
static std::vector<RecEntry> g_rec;

// ----------------------------------------------------------------------------
// 操作记录面板: 标签条左下方的一条只读列表 (见 操作记录界面.png)
// 它不是标签页, 标签条上也没有它的位置 —— 只有一个工具栏按钮 (IDC_TB_REC_PANEL) 开/关它,
// 而且每次启动都从关闭开始
// ----------------------------------------------------------------------------
static HWND g_hRec = NULL;        // IDC_REC_LIST, 在 WM_INITDIALOG 里创建
static bool g_recPanel = false;   // 面板开关: 仅工具栏按钮, 启动默认 false

static void RefreshRecPanel() {
    if (!g_hRec) return;
    // 列头就是面板顶上那条写着「操作记录」的横条，它是在 WM_INITDIALOG 里插进去的，
    // 切语言时要在这里跟着换，否则英文界面下还挂着中文。
    LVCOLUMNW col = { 0 };
    col.mask = LVCF_TEXT;
    col.pszText = (LPWSTR)LS(IDS_REC_TAB);
    ListView_SetColumn(g_hRec, 0, &col);
    SendMessageW(g_hRec, LVM_DELETEALLITEMS, 0, 0);
    for (size_t i = 0; i < g_rec.size(); ++i) {
        std::wstring from = fs::path(g_rec[i].from).filename().wstring();
        std::wstring to = fs::path(g_rec[i].to).filename().wstring();
        std::wstring line = from + L" -> " + to;
        LVITEMW lv = { 0 };
        lv.mask = LVIF_TEXT;
        lv.iItem = (int)i;
        lv.pszText = (LPWSTR)line.c_str();
        ListView_InsertItem(g_hRec, &lv);
    }
}

static void LayoutListColumns();   // 定义在 Layout() 后面, 这里要先声明
static void ShowRecPanel(bool on); // 定义在 ShowTab() 后面, 见那里的注释

// Tab -> control ids (so we can show/hide per active tab)
static const int TAB_COUNT = 8;
// 存的是语言包 key id 而不是现成文本: 这些是全局数组, 静态初始化期语言表还没装好。
// ListView 9 列表头 (第一列只有打勾框, 表头留空)。
static const int kListHeaderIds[9] = { 0, IDS_COL_NAME, IDS_COL_PREVIEW, IDS_COL_EXT,
                                       IDS_COL_DATE, IDS_COL_PATH, IDS_COL_SIZE,
                                       IDS_COL_KIND, IDS_COL_RESULT };
static const int TAB_NAME_IDS[8] = {
    IDS_TAB_OVERALL, IDS_TAB_REPLACE, IDS_TAB_NUMBER, IDS_TAB_ADD,
    IDS_TAB_DELETE, IDS_TAB_EXT, IDS_TAB_TAGNAME, IDS_TAB_TAGWRITE
};
static const int TAB_CTRL[8][24] = {
    { IDC_OV_GRP_NAME, IDC_OV_GRP_EXT, IDC_OV_LB_NAME, IDC_OV_LB_EXT, IDC_OV_LB_FILTER,
      IDC_OV_NAME_UPPER, IDC_OV_NAME_LOWER, IDC_OV_NAME_TITLE, IDC_OV_NAME_TEXT,
      IDC_OV_EXT_UPPER, IDC_OV_EXT_LOWER, IDC_OV_EXT_TEXT, IDC_OV_FILTER, -1 },
    { IDC_RP_GRP, IDC_RP_LB_FIND, IDC_RP_LB_REPL,
      IDC_RP_ENABLE, IDC_RP_FIND, IDC_RP_REPL, IDC_RP_REGEX, IDC_RP_CASEI, -1 },
    { IDC_NM_GRP, IDC_NM_LB_TPL, IDC_NM_LB_START, IDC_NM_LB_STEP, IDC_NM_LB_PRESET, IDC_NM_LB_WIDTH,
      IDC_NM_ENABLE, IDC_NM_TPL, IDC_NM_PRESET, IDC_NM_START, IDC_NM_STEP, IDC_NM_WIDTH,
      IDC_NM_PAD, IDC_NM_LETTER, IDC_NM_UPPER,
      IDC_NM_START_SPIN, IDC_NM_STEP_SPIN, IDC_NM_WIDTH_SPIN, -1 },
    { IDC_AD_GRP, IDC_AD_LB_TEXT, IDC_AD_LB_POS,
      IDC_AD_ENABLE, IDC_AD_TEXT, IDC_AD_PRE, IDC_AD_SUF, IDC_AD_EXTPRE, IDC_AD_EXTSUF, IDC_AD_AT, IDC_AD_POS,
      IDC_AD_POS_SPIN, -1 },
    { IDC_DL_GRP, IDC_DL_LB_TEXT, IDC_DL_LB_NUM, IDC_DL_LB_START,
      IDC_DL_ENABLE, IDC_DL_TEXT, IDC_DL_RANGE, IDC_DL_HEAD, IDC_DL_TAIL,
      IDC_DL_CONTENT, IDC_DL_START, IDC_DL_NUM, IDC_DL_NAME, IDC_DL_EXT,
      IDC_DL_START_SPIN, IDC_DL_NUM_SPIN, -1 },
    { IDC_EX_GRP, IDC_EX_LB_TEXT, IDC_EX_LB_CMD, IDC_EX_LB_PRESET,
      IDC_EX_ENABLE, IDC_EX_ADD, IDC_EX_DEL, IDC_EX_CHG, IDC_EX_CASE, IDC_EX_TEXT, IDC_EX_UP, IDC_EX_LO,
      IDC_EX_CMD, IDC_EX_PRESET, -1 },
    { IDC_TG_ENABLE, IDC_TG_LB_EXPR, IDC_TG_EXPR, IDC_TG_LB_PRE, IDC_TG_PRESET,
      IDC_TG_B1, IDC_TG_B2, IDC_TG_B3, IDC_TG_B4, IDC_TG_B5, IDC_TG_B6,
      IDC_TG_LB_PREV, IDC_TG_PREVIEW, -1 },
    { IDC_TW_ENABLE, IDC_TW_LB_EXPR, IDC_TW_EXPR, IDC_TW_LB_PRE, IDC_TW_PRESET,
      IDC_TW_CLEAR, IDC_TW_SAVE, IDC_TW_WRITE,
      IDC_TW_LB_ARTIST, IDC_TW_ARTIST, IDC_TW_LB_TITLE, IDC_TW_TITLE,
      IDC_TW_LB_ALBUM, IDC_TW_ALBUM, IDC_TW_LB_TRACK, IDC_TW_TRACK,
      IDC_TW_LB_YEAR, IDC_TW_YEAR, IDC_TW_LB_GENRE, IDC_TW_GENRE,
      IDC_TW_LB_COMMENT, IDC_TW_COMMENT, -1 },
};

// ----------------------------------------------------------------------------
// DPI-aware pixel geometry
// ----------------------------------------------------------------------------
static int g_dpi = 96;
static inline int S(int v) { return MulDiv(v, g_dpi, 96); }

// Layout of one tab page inside the tab control's display area.
// Rows are 30px pitches measured from the display rect's top-left.
struct Row { int y, h; };
static const int PAGE_H = 84;   // tab display area height (2 group rows)
// Client rect of the tab display area (physical px), filled by Layout().
static RECT g_rcPage = {};
static RECT g_rcFrame = {};  // tab page border painted by WM_PAINT (empty until laid out)
// 爱心按钮左边那条竖线的位置 (客户区物理像素), 同样由 WM_PAINT 画: owner-draw 按钮的
// WM_DRAWITEM 拿到的 DC 被裁在按钮自己那一格里, 格子外画不出东西。
static RECT g_rcTbSep = {};

// ----------------------------------------------------------------------------
// 设置 (5 页, 持久化到 HKCU\Software\AradeRenamer)
// ----------------------------------------------------------------------------
// The initialisers below ARE the defaults: 恢复默认 re-applies a fresh AppSettings and
// SettingsLoad leaves a value alone when its registry entry is missing.
struct AppSettings {
    // ---- 一般参数 ----
    bool ignFolder = false;      // 忽略文件夹 (列表里不收目录项)
    bool walkAll = false;        // 遍历文件夹下的所有文件
    bool noFileDate = false;     // 不获取文件的日期
    bool autoPreview = true;     // 文件更名自动预览
    bool sortOnInit = false;     // 文件列表在初始化时排序
    bool fastPreview = false;    // 快速自动预览 (只预览前十项)
    bool autoCorrect = true;     // 自动更正错误 (替换掉 Windows 不允许的文件名字符)
    bool confirmExit = false;    // 退出时确认
    int  wordSplit = 0;          // 文件名单词判断字符: 0 空格, 1 非英文字母和数字
    // ---- 音乐文件标签 ----
    bool id3v2First = false;     // MP3 ID3v2 标签优先 (默认 ID3v1 优先)
    int  removeMode = 2;         // 移除ID3标签时: 0 只v1, 1 只v2, 2 同时
    int  writeMode = 0;          // ID3标签写入时: 0 只v1, 1 只v2, 2 同时
    bool tagPrompt = true;       // 标签写入时提示
    bool keepFileDate = true;    // 保留文件日期
    bool trackPad2 = true;       // 音轨数字低于两位自动补0
    bool keepBlankTag = false;   // 保留空白项的标签
    bool tagBackup = false;      // 写入前先备份原文件 (.bak)，默认不勾选
    // ---- 导出文件列表 ----
    bool expExt = false;         // 同时导出文件扩展名
    bool expPath = false;        // 同时导出文件路径
    bool expNoFolder = true;     // 不包括文件夹
    // ---- 元变量 ----
    bool useMeta = true;         // 使用元变量
    // ---- 界面 ----
    bool enhList = true;         // 文件列表使用增强界面 (整行选中 + 双缓冲)
    bool grid = true;            // 显示表格
    int  colorScheme = 0;        // 文件列表颜色搭配: 0 默认 1 灰色世界 2 郁闷心情 3 绿色草原 4 紫色淡雅
    bool bigIcons = true;        // 工具栏使用大图标 (第二排), 默认勾选
};
static AppSettings g_set;

static const wchar_t* const kRegKey = L"Software\\AradeRenamer";

template<class T> static void RegPut(HKEY k, const wchar_t* name, T v) {
    DWORD d = (DWORD)v;
    RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&d, sizeof(d));
}
template<class T> static void RegFetch(HKEY k, const wchar_t* name, T& v) {
    DWORD d = 0, type = REG_DWORD, cb = sizeof(d);
    if (RegQueryValueExW(k, name, NULL, &type, (BYTE*)&d, &cb) == ERROR_SUCCESS &&
        type == REG_DWORD)
        v = (T)d;
}

static void SettingsLoad() {
    HKEY k = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegKey, 0, KEY_READ, &k) != ERROR_SUCCESS) return;
    RegFetch(k, L"IgnoreFolder", g_set.ignFolder);
    RegFetch(k, L"WalkAll", g_set.walkAll);
    RegFetch(k, L"NoFileDate", g_set.noFileDate);
    RegFetch(k, L"AutoPreview", g_set.autoPreview);
    RegFetch(k, L"SortOnInit", g_set.sortOnInit);
    RegFetch(k, L"FastPreview", g_set.fastPreview);
    RegFetch(k, L"AutoCorrect", g_set.autoCorrect);
    RegFetch(k, L"ConfirmExit", g_set.confirmExit);
    RegFetch(k, L"WordSplit", g_set.wordSplit);
    RegFetch(k, L"Id3v2First", g_set.id3v2First);
    RegFetch(k, L"RemoveMode", g_set.removeMode);
    RegFetch(k, L"WriteMode", g_set.writeMode);
    RegFetch(k, L"TagPrompt", g_set.tagPrompt);
    RegFetch(k, L"KeepFileDate", g_set.keepFileDate);
    RegFetch(k, L"TrackPad2", g_set.trackPad2);
    RegFetch(k, L"KeepBlankTag", g_set.keepBlankTag);
    RegFetch(k, L"TagBackup", g_set.tagBackup);
    RegFetch(k, L"ExportExt", g_set.expExt);
    RegFetch(k, L"ExportPath", g_set.expPath);
    RegFetch(k, L"ExportNoFolder", g_set.expNoFolder);
    RegFetch(k, L"UseMeta", g_set.useMeta);
    RegFetch(k, L"EnhList", g_set.enhList);
    RegFetch(k, L"Grid", g_set.grid);
    RegFetch(k, L"ColorScheme", g_set.colorScheme);
    RegFetch(k, L"BigToolbar", g_set.bigIcons);
    RegCloseKey(k);
}

static void SettingsSave() {
    HKEY k = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL)
        != ERROR_SUCCESS) return;
    RegPut(k, L"IgnoreFolder", g_set.ignFolder);
    RegPut(k, L"WalkAll", g_set.walkAll);
    RegPut(k, L"NoFileDate", g_set.noFileDate);
    RegPut(k, L"AutoPreview", g_set.autoPreview);
    RegPut(k, L"SortOnInit", g_set.sortOnInit);
    RegPut(k, L"FastPreview", g_set.fastPreview);
    RegPut(k, L"AutoCorrect", g_set.autoCorrect);
    RegPut(k, L"ConfirmExit", g_set.confirmExit);
    RegPut(k, L"WordSplit", g_set.wordSplit);
    RegPut(k, L"Id3v2First", g_set.id3v2First);
    RegPut(k, L"RemoveMode", g_set.removeMode);
    RegPut(k, L"WriteMode", g_set.writeMode);
    RegPut(k, L"TagPrompt", g_set.tagPrompt);
    RegPut(k, L"KeepFileDate", g_set.keepFileDate);
    RegPut(k, L"TrackPad2", g_set.trackPad2);
    RegPut(k, L"KeepBlankTag", g_set.keepBlankTag);
    RegPut(k, L"TagBackup", g_set.tagBackup);
    RegPut(k, L"ExportExt", g_set.expExt);
    RegPut(k, L"ExportPath", g_set.expPath);
    RegPut(k, L"ExportNoFolder", g_set.expNoFolder);
    RegPut(k, L"UseMeta", g_set.useMeta);
    RegPut(k, L"EnhList", g_set.enhList);
    RegPut(k, L"Grid", g_set.grid);
    RegPut(k, L"ColorScheme", g_set.colorScheme);
    RegPut(k, L"BigToolbar", g_set.bigIcons);
    RegCloseKey(k);
}

// 元变量 page: the reference dialog's two-column table, verbatim from 指令.txt.
struct MetaVar { const wchar_t* var; const wchar_t* value; };
static const MetaVar kMetaVars[] = {
    { L"<DATE>",  L"YYYY-MM-DD" },
    { L"<Y>",     L"YYYY" },
    { L"<M>",     L"MM" },
    { L"<D>",     L"DD" },
    { L"<TIME>",  L"HH-MM-SS" },
    { L"<H>",     L"HH" },
    { L"<TM>",    L"MM" },
    { L"<S>",     L"SS" },
    { L"<CDATE>", L"YYYY-MM-DD" },
    { L"<CY>",    L"YYYY" },
    { L"<CM>",    L"MM" },
    { L"<CD>",    L"DD" },
    { L"<CTIME>", L"HH-MM-SS" },
    { L"<CH>",    L"HH" },
    { L"<CTM>",   L"MM" },
    { L"<CS>",    L"SS" },
    { L"<SIZE>",  L"%.0n" },
    { L"<SELF>",  L"" },
    { L"<EXT>",   L"" },
    { L"<PARENT>", L"" },
    { L"<.EXT>",  L"" },
    { L"<NAME>",  L"" },
    { L"<R>",     L"n8t2" },
};
static const int kMetaCount = (int)(sizeof(kMetaVars) / sizeof(kMetaVars[0]));

static const int kColorNameIds[] = { IDS_COLOR_DEFAULT, IDS_COLOR_GRAY, IDS_COLOR_BLUE,
                                     IDS_COLOR_GREEN, IDS_COLOR_PURPLE };
static const int kColorCount = 5;

// Text / background colour per scheme, applied through the list's NM_CUSTOMDRAW.
struct ListColors { COLORREF text, back, subtext; };
static ListColors ListSchemeColors(int scheme) {
    switch (scheme) {
    case 1: return { RGB(40, 40, 40),   RGB(232, 232, 232), RGB(70, 70, 70) };
    case 2: return { RGB(235, 235, 245), RGB(90, 95, 110),  RGB(220, 220, 235) };
    case 3: return { RGB(10, 70, 20),    RGB(223, 240, 220), RGB(40, 95, 50) };
    case 4: return { RGB(60, 20, 90),    RGB(238, 230, 245), RGB(95, 55, 130) };
    default: return { RGB(0, 0, 0),      RGB(255, 255, 255), RGB(0, 0, 0) };
    }
}

// ----------------------------------------------------------------------------
// UI helpers
// ----------------------------------------------------------------------------
static std::wstring GetText(int id) {
    HWND h = GetDlgItem(g_hDlg, id);
    if (!h) return L"";
    int n = GetWindowTextLengthW(h);
    std::wstring s; s.resize(n + 1);
    GetWindowTextW(h, &s[0], n + 1);
    s.resize(n);
    return s;
}
static void SetText(int id, const std::wstring& s) {
    SetWindowTextW(GetDlgItem(g_hDlg, id), s.c_str());
}
static bool IsChecked(int id) {
    return IsDlgButtonChecked(g_hDlg, id) == BST_CHECKED;
}
static int ToIntSafe(const std::wstring& s, int def) {
    try { return s.empty() ? def : std::stoi(s); } catch (...) { return def; }
}
static int GetRadioGroup(int firstId, int lastId) {
    for (int id = firstId; id <= lastId; ++id)
        if (IsChecked(id)) return id;
    return 0;
}

static std::wstring FormatSize(uint64_t b) {
    wchar_t buf[64];
    if (b < 1024) { swprintf_s(buf, 64, L"%llu B", b); return buf; }
    double d = (double)b / 1024.0;
    if (d < 1024) { swprintf_s(buf, 64, L"%.1f KB", d); return buf; }
    d /= 1024;
    if (d < 1024) { swprintf_s(buf, 64, L"%.1f MB", d); return buf; }
    d /= 1024;
    swprintf_s(buf, 64, L"%.1f GB", d); return buf;
}

static std::wstring GetFileDate(const fs::path& p) {
    struct _stat64 st;
    if (_wstat64(p.c_str(), &st) != 0) return L"";
    time_t tt = st.st_mtime;
    struct tm tmv; localtime_s(&tmv, &tt);
    wchar_t buf[32]; wcsftime(buf, 32, L"%Y-%m-%d", &tmv);
    return buf;
}

// Simple wildcard match supporting * and ? (case-insensitive)
static bool WildcardMatch(const std::wstring& pat, const std::wstring& name) {
    if (pat == L"*.*" || pat == LS(IDS_FILTER_ALL)) return true;
    std::wstring p = pat, n = name;
    std::transform(p.begin(), p.end(), p.begin(), ::towlower);
    std::transform(n.begin(), n.end(), n.begin(), ::towlower);
    // convert pattern to a regex: escape, turn * -> .*, ? -> .
    std::wstring re;
    for (wchar_t c : p) {
        if (c == L'*') re += L".*";
        else if (c == L'?') re += L".";
        else if (c == L'.' || c == L'(' || c == L')' || c == L'[' || c == L']' ||
                 c == L'\\' || c == L'+' || c == L'^' || c == L'$' || c == L'{' || c == L'}')
            re += L"\\" + std::wstring(1, c);
        else re += c;
    }
    try {
        std::wregex r(re, std::regex_constants::ECMAScript);
        return std::regex_match(n, r);
    } catch (...) { return false; }
}

// ----------------------------------------------------------------------------
// Config reader (combines all enabled tab sections)
// ----------------------------------------------------------------------------
static RenameConfig ReadConfig() {
    RenameConfig cfg;

    // 整体
    int nameRadio = GetRadioGroup(IDC_OV_NAME_UPPER, IDC_OV_NAME_TITLE);
    cfg.ovNameCase = (nameRadio == IDC_OV_NAME_UPPER) ? 1
                   : (nameRadio == IDC_OV_NAME_LOWER) ? 2
                   : (nameRadio == IDC_OV_NAME_TITLE) ? 3 : 0;
    cfg.ovNameOverride = GetText(IDC_OV_NAME_TEXT);
    int extRadio = GetRadioGroup(IDC_OV_EXT_UPPER, IDC_OV_EXT_LOWER);
    cfg.ovExtCase = (extRadio == IDC_OV_EXT_UPPER) ? 1 : (extRadio == IDC_OV_EXT_LOWER) ? 2 : 0;
    cfg.ovExtOverride = GetText(IDC_OV_EXT_TEXT);
    // 文件过滤: CBS_DROPDOWN, so the edit field carries both a picked preset and a
    // hand-typed pattern; reading it covers both cases.
    cfg.fileFilter = GetText(IDC_OV_FILTER);

    // 替换
    cfg.rpEnable = IsChecked(IDC_RP_ENABLE);
    cfg.rpFind = GetText(IDC_RP_FIND);
    cfg.rpReplace = GetText(IDC_RP_REPL);
    cfg.rpRegex = IsChecked(IDC_RP_REGEX);
    cfg.rpCaseInsensitive = IsChecked(IDC_RP_CASEI);

    // 序号
    cfg.nmEnable = IsChecked(IDC_NM_ENABLE);
    cfg.nmTemplate = GetText(IDC_NM_TPL);
    cfg.nmStart = ToIntSafe(GetText(IDC_NM_START), 1);
    cfg.nmStep = ToIntSafe(GetText(IDC_NM_STEP), 1);
    cfg.nmWidth = ToIntSafe(GetText(IDC_NM_WIDTH), 2);
    cfg.nmPad = IsChecked(IDC_NM_PAD);
    cfg.nmLetters = IsChecked(IDC_NM_LETTER);
    cfg.nmUpper = IsChecked(IDC_NM_UPPER);

    // 添加
    cfg.adEnable = IsChecked(IDC_AD_ENABLE);
    cfg.adText = GetText(IDC_AD_TEXT);
    cfg.adWhere = (IsChecked(IDC_AD_PRE)) ? 0 : (IsChecked(IDC_AD_SUF)) ? 1
                : (IsChecked(IDC_AD_EXTPRE)) ? 2 : (IsChecked(IDC_AD_EXTSUF)) ? 3 : 4;
    cfg.adPos = ToIntSafe(GetText(IDC_AD_POS), 1);

    // 删除
    cfg.dlEnable = IsChecked(IDC_DL_ENABLE);
    cfg.dlMode = (IsChecked(IDC_DL_HEAD)) ? 1 : (IsChecked(IDC_DL_TAIL)) ? 2
               : (IsChecked(IDC_DL_RANGE)) ? 3 : 0;
    cfg.dlText = GetText(IDC_DL_CONTENT);
    cfg.dlStart = ToIntSafe(GetText(IDC_DL_START), 1);
    cfg.dlNum = ToIntSafe(GetText(IDC_DL_NUM), 1);
    cfg.dlTarget = IsChecked(IDC_DL_EXT) ? 1 : 0;

    // 扩展更名
    cfg.exEnable = IsChecked(IDC_EX_ENABLE);
    cfg.exMode = (IsChecked(IDC_EX_DEL)) ? 1 : (IsChecked(IDC_EX_CHG)) ? 2
              : (IsChecked(IDC_EX_CASE)) ? 3 : 0;
    cfg.exText = GetText(IDC_EX_TEXT);
    cfg.exCase = IsChecked(IDC_EX_LO) ? 2 : 1;
    cfg.exCmd = GetText(IDC_EX_CMD);

    // 标签命名
    cfg.tgEnable = IsChecked(IDC_TG_ENABLE);
    cfg.tgExpr = GetText(IDC_TG_EXPR);
    cfg.trackPad2 = g_set.trackPad2;
    // 元变量 / CRC32 / 单词判断字符 / 自动更正 come from 设置, not from a tab page.
    cfg.useMeta = g_set.useMeta;
    cfg.crcEnable = g_crcRename;
    cfg.convMode = g_convMode;
    cfg.autoCorrect = g_set.autoCorrect;
    cfg.wordSplitMode = g_set.wordSplit;
    cfg.nameSource = g_nameSource;
    cfg.textCmds = (g_nameSource == 3 && !g_textCmds.empty()) ? &g_textCmds : nullptr;
    cfg.encConv = g_encConv;

    return cfg;
}

// ----------------------------------------------------------------------------
// List management
// ----------------------------------------------------------------------------
// Absolute, case-folded path. NTFS/HFS are case-insensitive by default, so two spellings
// that differ only in case are the same file.
static std::wstring DedupeKeyOf(const fs::path& p) {
    std::error_code ec;
    fs::path a = fs::absolute(p, ec);
    std::wstring k = (ec ? p : a).wstring();
    std::transform(k.begin(), k.end(), k.begin(), ::towlower);
    return k;
}

static std::unordered_set<std::wstring> g_keys;

static void AddOneFile(const fs::path& path) {
    // fs::equivalent() against every existing row opened two handles per comparison and
    // made a 3000-file folder take minutes; a hash of the normalised path is O(1).
    std::wstring key = DedupeKeyOf(path);
    if (g_keys.count(key)) return;
    FileItem fi;
    fi.fullPath = path;
    fi.oldName = path.filename().wstring();
    fi.dedupeKey = key;
    g_items.push_back(fi);
    g_keys.insert(fi.dedupeKey);
}

static void AddPaths(const std::vector<fs::path>& paths) {
    for (auto p : paths) {
        std::error_code ec;
        if (fs::is_directory(p, ec)) {
            if (g_set.walkAll) {
                for (auto it = fs::recursive_directory_iterator(p, ec);
                     it != fs::recursive_directory_iterator(); ++it) {
                    std::error_code e2;
                    if (fs::is_regular_file(it->status())) AddOneFile(it->path());
                }
            } else {
                for (auto it = fs::directory_iterator(p, ec);
                     it != fs::directory_iterator(); ++it) {
                    std::error_code e2;
                    if (fs::is_regular_file(it->status())) AddOneFile(it->path());
                    // 忽略文件夹  unchecked: a sub-directory is a renamable row too.
                    else if (!g_set.ignFolder && fs::is_directory(it->status())) AddOneFile(it->path());
                }
            }
        } else if (fs::is_regular_file(p, ec)) {
            AddOneFile(p);
        }
    }
    if (g_set.sortOnInit) {
        std::stable_sort(g_items.begin(), g_items.end(), [](const FileItem& a, const FileItem& b) {
            return _wcsicmp(a.oldName.c_str(), b.oldName.c_str()) < 0;
        });
    }
    RebuildList();
}

// RebuildList 填充期间为 true：程序化设置打勾状态会给每一行都送出
// LVN_ITEMCHANGED，逐行响应预览就退化成 O(n^2)。
static bool g_noListNotify = false;

static void RebuildList() {
    ListView_DeleteAllItems(g_hList);
    // g_items can shrink outside AddOneFile (移除/清空), so the add-dedupe index is
    // rebuilt from scratch here rather than updated per erase.
    g_keys.clear();
    for (auto& it : g_items) g_keys.insert(it.dedupeKey);
    // Without this, every insert and every subitem write repaints the visible rows, and
    // filling a few thousand items takes longer than the user will wait for the window.
    SendMessageW(g_hList, WM_SETREDRAW, FALSE, 0);
    // Setting the check state below fires LVN_ITEMCHANGED for every row; the handler would
    // answer each one with a full preview pass, so suppress it for the duration of the fill.
    g_noListNotify = true;
    for (size_t i = 0; i < g_items.size(); ++i) {
        LVITEMW lv = { 0 };
        lv.mask = LVIF_PARAM;
        lv.iItem = (int)i; lv.iSubItem = 0;   // 第 0 列只放打勾框，文字在第 1 列
        lv.lParam = (LPARAM)i;
        int idx = ListView_InsertItem(g_hList, &lv);
        ListView_SetItemText(g_hList, idx, 1, (LPWSTR)g_items[i].oldName.c_str());
        ListView_SetCheckState(g_hList, idx, g_items[i].checked);

        std::error_code ec;
        uintmax_t sz = fs::file_size(g_items[i].fullPath, ec);
        if (ec) sz = 0;
        // ListView_SetItemText is a macro that expands to its own { } block, so an
        // argument like FormatSize(...).c_str() would be a full-expression ending at the
        // assignment inside that block: the temporary is already destroyed by the time
        // the macro's SendMessage runs, and the cell shows stale stack bytes. Every
        // string handed to the macro must be a named object that outlives the statement.
        std::wstring ext = g_items[i].fullPath.extension().wstring();
        std::wstring date = g_set.noFileDate ? L"" : GetFileDate(g_items[i].fullPath);
        std::wstring dir = g_items[i].fullPath.parent_path().wstring();
        std::wstring size = FormatSize((uint64_t)sz);
        std::wstring kind = ec ? LS(IDS_KIND_FOLDER) : LS(IDS_KIND_FILE);   // file_size fails on a directory

        ListView_SetItemText(g_hList, idx, 2, (LPWSTR)L"");            // 预览
        ListView_SetItemText(g_hList, idx, 3, (LPWSTR)ext.c_str());
        ListView_SetItemText(g_hList, idx, 4, (LPWSTR)date.c_str());
        ListView_SetItemText(g_hList, idx, 5, (LPWSTR)dir.c_str());
        // A folder has no meaningful size; 大小 would otherwise read 0 B for every one.
        ListView_SetItemText(g_hList, idx, 6, (LPWSTR)(ec ? (LPWSTR)L"" : size.c_str()));
        ListView_SetItemText(g_hList, idx, 7, (LPWSTR)kind.c_str());
        ListView_SetItemText(g_hList, idx, 8, (LPWSTR)L"");            // 结果
    }
    g_noListNotify = false;
    UpdateToolbarState();
    // Recompute() writes the 预览 column for every row, so keep repainting off until it
    // is done and let the control redraw once.
    SendMessageW(g_hList, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(g_hList, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
    AutoPreview();
    UpdateTagPanel();   // the selected row may have just changed
}

// Status text of the owner-drawn (centered) part and the right statistics part.
static std::wstring g_statusCenter, g_statusRight;

// Both text parts are (re)applied here, because SB_SETTEXT for a part index that
// does not exist yet is silently dropped — Layout() calls this right after
// SB_SETPARTS, and Recompute() calls it whenever the counts change.
static void UpdateStatusParts() {
    if (!g_hStatus) return;
    wchar_t buf[160];
    swprintf_s(buf, 160, LS(IDS_STATUS_FILES), (int)g_items.size());
    g_statusCenter = buf;
    SendMessageW(g_hStatus, SB_SETTEXT, 1 | SBT_OWNERDRAW, (LPARAM)0);
    SendMessageW(g_hStatus, SB_SETTEXT, 2, (LPARAM)g_statusRight.c_str());
    InvalidateRect(g_hStatus, NULL, TRUE);
}

static void UpdateStatusRight(int active, int conflicts) {
    wchar_t buf[160];
    if (conflicts > 0)
        swprintf_s(buf, 160, LS(IDS_STATUS_PENDING_CONFLICT), active, conflicts);
    else
        swprintf_s(buf, 160, LS(IDS_STATUS_PENDING), active);
    g_statusRight = buf;
    SendMessageW(g_hStatus, SB_SETTEXT, 2, (LPARAM)g_statusRight.c_str());
}

// ----------------------------------------------------------------------------
// 音频标签
// ----------------------------------------------------------------------------
// The 预设 list both 标签 tabs share: picking an entry writes the matching expression
// into the still-editable expression box (the pick is a filler, not a setting).
struct TagPreset { int nameId; const wchar_t* expr; };
static const TagPreset kTagPresets[] = {
    { IDS_TPRESET_ARTIST_TITLE,       L"%1 - %2" },
    { IDS_TPRESET_TITLE_ARTIST,       L"%2 (%1)" },
    { IDS_TPRESET_TRACK_TITLE,        L"%3.%2" },
    { IDS_TPRESET_TRACK_ARTIST_TITLE, L"%3.%1 - %2" },
    { IDS_TPRESET_TRACK_TITLE_ARTIST, L"%3.%2 (%1)" },
    { IDS_TPRESET_ALL4,               L"%1.%4.%3.%2" },
    { IDS_TPRESET_ARTIST_TITLE_YEAR,  L"%1 - %2  (%5)" },
    { IDS_TPRESET_ALL,                L"%1 - %2 - %3 - %4 - %5 - %6" },
};
static const int kTagPresetCount = (int)(sizeof(kTagPresets) / sizeof(kTagPresets[0]));

// Tags are read once per row and kept in the item: the 预览 column needs them for every
// audio row when 标签命名 is active, and re-opening the file on each keystroke would
// turn that into hundreds of reads.
static void EnsureTags(FileItem& fi) {
    if (fi.tagsRead) return;
    fi.tagsRead = true;
    if (IsTaggableExt(fi.fullPath)) fi.tags = ReadAudioTags(fi.fullPath, g_set.id3v2First);
}

void Recompute(int limit) {
    RenameConfig cfg = ReadConfig();
    std::vector<std::wstring> names(g_items.size());
    int activeCount = 0;
    for (size_t i = 0; i < g_items.size(); ++i) {
        // 快速自动预览 (只预览前十项): the rows past the limit keep showing their old
        // name, so they read as "not renamed" until a full preview or 应用 runs.
        if (limit >= 0 && (int)i >= limit) { names[i] = g_items[i].oldName; continue; }
        // 打勾列: 取消打勾的行等同于不在列表里 —— 不预览、不计入待更名。
        if (!g_items[i].checked) { names[i] = g_items[i].oldName; continue; }
        if (!cfg.fileFilter.empty() && !WildcardMatch(cfg.fileFilter, g_items[i].oldName)) {
            names[i] = g_items[i].oldName; // filtered out -> unchanged
            continue;
        }
        ++activeCount;
        // Only pay for opening the file when 标签命名 can actually use the result.
        if (cfg.tgEnable && !cfg.tgExpr.empty()) EnsureTags(g_items[i]);
        names[i] = ComputeNewName(g_items[i], cfg, (int)i);
    }

    // Conflict detection among the targets. Counting into a hash map keeps this linear;
    // the all-pairs scan it replaces cost O(n^2) and ran on every keystroke.
    std::unordered_map<std::wstring, int> targetCount;
    for (const std::wstring& n : names) ++targetCount[n];

    std::vector<int> counts(names.size(), 1);
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] != g_items[i].oldName) counts[i] = targetCount[names[i]];

    int conflicts = 0;
    for (size_t i = 0; i < g_items.size(); ++i) {
        g_items[i].newName = names[i];
        g_items[i].conflict = (counts[i] > 1);
        ListView_SetItemText(g_hList, (int)i, 2, (LPWSTR)names[i].c_str());
    }
    for (auto c : counts) if (c > 1) ++conflicts;
    conflicts = conflicts / 2 + (conflicts % 2);

    UpdateStatusParts();
    UpdateStatusRight(activeCount, conflicts);
}

// 文件更名自动预览 drives the live pass that runs on every keystroke; 快速自动预览 cuts
// it to the first ten rows. An explicit 预览 (menu/toolbar/Ctrl+F) still runs in full.
static void AutoPreview() {
    if (!g_set.autoPreview) return;
    Recompute(g_set.fastPreview ? 10 : -1);
}

// ----------------------------------------------------------------------------
// Apply / Undo
// ----------------------------------------------------------------------------
static fs::path MakeUnique(const fs::path& p) {
    if (!fs::exists(p)) return p;
    fs::path dir = p.parent_path();
    std::wstring stem = p.stem().wstring();
    std::wstring ext = p.extension().wstring();
    for (int k = 2; k < 10000; ++k) {
        fs::path cand = dir / (stem + L" (" + std::to_wstring(k) + L")" + ext);
        if (!fs::exists(cand)) return cand;
    }
    return p;
}

static void OnApply() {
    if (g_items.empty()) return;
    Recompute();
    RenameConfig cfg = ReadConfig();

    std::vector<UndoEntry> batch;
    int ok = 0, fail = 0;
    int total = (int)g_items.size();
    if (g_hProgress) {
        ShowWindow(g_hProgress, SW_SHOW);
        SendMessageW(g_hProgress, PBM_SETRANGE32, 0, total);
        SendMessageW(g_hProgress, PBM_SETPOS, 0, 0);
    }
    for (size_t i = 0; i < g_items.size(); ++i) {
        FileItem& fi = g_items[i];
        if (g_hProgress) { SendMessageW(g_hProgress, PBM_SETPOS, (int)i, 0); UpdateWindow(g_hProgress); }
        // 取消打勾的行不参与本次更名。
        if (!fi.checked) {
            ListView_SetItemText(g_hList, (int)i, 8, (LPWSTR)LS(IDS_RESULT_UNCHECKED));
            continue;
        }
        if (fi.newName.empty() || fi.newName == fi.oldName) {
            ListView_SetItemText(g_hList, (int)i, 8, (LPWSTR)LS(IDS_RESULT_SKIP));
            continue;
        }
        // respect file filter
        if (!cfg.fileFilter.empty() && !WildcardMatch(cfg.fileFilter, fi.oldName)) {
            ListView_SetItemText(g_hList, (int)i, 8, (LPWSTR)LS(IDS_RESULT_SKIP));
            continue;
        }
        fs::path newPath = fi.fullPath.parent_path() / fi.newName;
        // fs::equivalent() 的抛异常版本在目标名还不存在时(即每一次正常更名)会抛
        // filesystem_error, 整个进程 abort(); 一律改用 error_code 重载。
        std::error_code ecEq;
        bool existsNew = fs::exists(newPath, ecEq);
        if (existsNew && !fs::equivalent(newPath, fi.fullPath, ecEq))
            newPath = MakeUnique(newPath);
        // A case-only rename points at the same file, so fs::equivalent() is true even
        // though the name must change; only treat it as a no-op when the filename string
        // itself is unchanged (MoveFileW does apply case-only changes).
        if (fs::equivalent(newPath, fi.fullPath, ecEq) &&
            newPath.filename().wstring() == fi.fullPath.filename().wstring()) {
            ListView_SetItemText(g_hList, (int)i, 8, (LPWSTR)LS(IDS_RESULT_SKIP));
            continue;
        }
        if (MoveFileW(fi.fullPath.c_str(), newPath.c_str())) {
            UndoEntry e; e.index = (int)i; e.original = fi.fullPath;
            e.renamed = newPath; e.origName = fi.oldName;
            batch.push_back(e);
            RecEntry r; r.from = fi.fullPath.wstring(); r.to = newPath.wstring();
            g_rec.push_back(r);
            fi.fullPath = newPath;
            fi.oldName = fi.newName;
            fi.newName = L"";
            fi.dedupeKey = DedupeKeyOf(newPath);
            ListView_SetItemText(g_hList, (int)i, 1, (LPWSTR)fi.oldName.c_str());
            ListView_SetItemText(g_hList, (int)i, 8, (LPWSTR)LS(IDS_RESULT_OK));
            ++ok;
        } else {
            ListView_SetItemText(g_hList, (int)i, 8, (LPWSTR)LS(IDS_RESULT_FAIL));
            ++fail;
        }
    }
    if (g_hProgress) {
        SendMessageW(g_hProgress, PBM_SETPOS, total, 0);
        UpdateWindow(g_hProgress);
        ShowWindow(g_hProgress, SW_HIDE);
    }
    if (!batch.empty()) g_undo.push_back(batch);
    UpdateToolbarState();
    // Refresh the preview and the 待更名 count: every row just applied is now identical to
    // its target, so the statistics would otherwise still show the pre-rename numbers.
    Recompute();
    RefreshRecPanel();
    wchar_t buf[128];
    swprintf_s(buf, 128, LS(IDS_MSG_RENAME_DONE), ok, fail);
    MessageBoxW(g_hDlg, buf, LS(IDS_APP_TITLE), MB_OK | MB_ICONINFORMATION);
}

static void OnUndo() {
    if (g_undo.empty()) {
        MessageBoxW(g_hDlg, LS(IDS_MSG_UNDO_EMPTY), LS(IDS_APP_TITLE), MB_OK | MB_ICONINFORMATION);
        return;
    }
    auto batch = g_undo.back();
    g_undo.pop_back();
    int ok = 0;
    for (auto& e : batch) {
        if (MoveFileW(e.renamed.c_str(), e.original.c_str())) {
            if ((size_t)e.index < g_items.size()) {
                g_items[e.index].fullPath = e.original;
                g_items[e.index].oldName = e.origName;
                g_items[e.index].newName = L"";
                g_items[e.index].dedupeKey = DedupeKeyOf(e.original);
                ListView_SetItemText(g_hList, e.index, 1, (LPWSTR)e.origName.c_str());
                ListView_SetItemText(g_hList, e.index, 8, (LPWSTR)L"");
            }
            ++ok;
            // 撤销掉的那次更名要从账本里划掉: 从后往前找第一条与之配对的记录。
            for (size_t k = g_rec.size(); k-- > 0;) {
                if (g_rec[k].from == e.original.wstring() && g_rec[k].to == e.renamed.wstring()) {
                    g_rec.erase(g_rec.begin() + k);
                    break;
                }
            }
        }
    }
    Recompute();
    UpdateToolbarState();
    RefreshRecPanel();
    // 撤销后旧路径又空出来了，去重索引要跟着改回来的路径重建，否则重新添加该文件会被当成重复。
    g_keys.clear();
    for (auto& it : g_items) g_keys.insert(it.dedupeKey);
    wchar_t buf[128];
    swprintf_s(buf, 128, LS(IDS_MSG_UNDO_DONE), ok);
    MessageBoxW(g_hDlg, buf, LS(IDS_APP_TITLE), MB_OK | MB_ICONINFORMATION);
}

// ----------------------------------------------------------------------------
// Add files / folder
// ----------------------------------------------------------------------------
static void OnAddFiles() {
    wchar_t buf[65536];
    ZeroMemory(buf, sizeof(buf));
    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hDlg;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 65536;
    ofn.lpstrFilter = LS(IDS_FILTER_ALL_FILES);
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        std::vector<fs::path> paths;
        if (buf[wcslen(buf) + 1] == L'\0') {
            paths.push_back(fs::path(buf));
        } else {
            std::wstring dir = buf;
            const wchar_t* p = buf + wcslen(buf) + 1;
            while (*p) { paths.push_back(fs::path(dir) / p); p += wcslen(p) + 1; }
        }
        AddPaths(paths);
    }
}

static void OnAddFolder() {
    BROWSEINFOW bi = { 0 };
    bi.hwndOwner = g_hDlg;
    bi.lpszTitle = LS(IDS_PICK_FOLDER_TITLE);
    bi.ulFlags = 0x2000 /*BIF_USENEWDIALOGSTYLE*/ | BIF_RETURNONLYFSDIRS;
    PIDLIST_ABSOLUTE pid = SHBrowseForFolderW(&bi);
    if (pid) {
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDListW(pid, path)) {
            std::vector<fs::path> v; v.push_back(fs::path(path));
            AddPaths(v);
        }
        CoTaskMemFree(pid);
    }
}

// ----------------------------------------------------------------------------
// Toolbar (flat owner-draw icon buttons, grouped by separators)
// ----------------------------------------------------------------------------
struct TbButton { int id; bool newGroup; int tipId; bool right; };
// Order and grouping follow 指令.txt's 目标 line. 移除选中项 / 清空列表 are not on the
// toolbar any more; they move to the list's right-click menu in a later round.
// right = true 的按钮不参与从左往右的排布, Layout() 把它钉在工具栏这一行的最右端。
static const TbButton TB_BUTTONS[] = {
    { IDC_TB_ADD,        false, IDS_TIP_ADD },
    { IDC_TB_ADDF,       false, IDS_TIP_ADDF },
    { IDC_TB_PREVIEW,    true,  IDS_TIP_PREVIEW },
    { IDC_TB_UNDO,       false, IDS_TIP_UNDO },
    { IDC_TB_APPLY,      false, IDS_TIP_APPLY },
    { IDC_TB_TAGDEL,     true,  IDS_TIP_TAGDEL },
    { IDC_TB_TAGEXTRACT, false, IDS_TIP_TAGEXTRACT },
    { IDC_TB_TAGWRITE,   false, IDS_TIP_TAGWRITE },
    { IDC_TB_T2S,        false, IDS_TIP_T2S },
    { IDC_TB_TEXTCMD,    true,  IDS_TIP_TEXTCMD },
    { IDC_TB_EXPLIST,    false, IDS_TIP_EXPLIST },
    { IDC_TB_RECOPEN,    true,  IDS_TIP_RECOPEN },
    { IDC_TB_RECSAVE,    false, IDS_TIP_RECSAVE },
    { IDC_TB_RECCLEAR,   false, IDS_TIP_RECCLEAR },
    { IDC_TB_CHGDATE,    true,  IDS_TIP_CHGDATE },
    { IDC_TB_REC_PANEL,  false, IDS_TIP_RECPANEL },
    { IDC_TB_PIN,        false, IDS_TIP_PIN },
    { IDC_TB_SPONSOR,    false, IDS_TIP_SPONSOR, true },
};
static const int TB_COUNT = (int)(sizeof(TB_BUTTONS) / sizeof(TB_BUTTONS[0]));
static int g_hotBtn = 0;   // control id of hovered button, 0 = none

static void UpdateToolbarState() {
    bool has = !g_items.empty();
    for (int i = 0; i < TB_COUNT; ++i) {
        int id = TB_BUTTONS[i].id;
        // Always available: adding, the record panel, pinning, 赞助/联系, and the commands
        // that operate on saved records rather than on the list.
        bool en = (id == IDC_TB_UNDO) ? !g_undo.empty()
              : (id == IDC_TB_ADD || id == IDC_TB_ADDF || id == IDC_TB_PIN ||
                 id == IDC_TB_REC_PANEL || id == IDC_TB_RECSAVE ||
                 id == IDC_TB_RECCLEAR || id == IDC_TB_SPONSOR) ? true : has;
        HWND h = GetDlgItem(g_hDlg, id);
        if (h && (IsWindowEnabled(h) != FALSE) != en) {
            EnableWindow(h, en);
            InvalidateRect(h, NULL, TRUE);
        }
    }
}

static void TbLine(HDC dc, int x1, int y1, int x2, int y2, COLORREF col, int w) {
    HPEN pen = CreatePen(PS_SOLID, w, col);
    HPEN old = (HPEN)SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, NULL);
    LineTo(dc, x2, y2);
    SelectObject(dc, old);
    DeleteObject(pen);
}

// Hand-drawn glyphs on a 16x16 grid (geometric, no external image assets). They are the
// artwork for the buttons that have no embedded .ico, so the grid is mapped through
// MulDiv instead of a whole-pixel unit: at 120 dpi a unit must be able to land on 1.25 px.
static void DrawGlyph(HDC dc, const RECT& rc, int id, COLORREF col) {
    int span = S(g_set.bigIcons ? 22 : 16);
    int x = ((rc.left + rc.right) - span) / 2;
    int y = ((rc.top + rc.bottom) - span) / 2;
    auto FX = [&](int v) { return x + MulDiv(v, span, 16); };
    auto FY = [&](int v) { return y + MulDiv(v, span, 16); };
    auto FW = [&](int w) { int px = MulDiv(w, span, 16); return px < 1 ? 1 : px; };
    int u = FW(1);                       // hairline width

    HPEN savePen = (HPEN)GetCurrentObject(dc, OBJ_PEN);
    HBRUSH saveBr = (HBRUSH)GetCurrentObject(dc, OBJ_BRUSH);
    HPEN pen = CreatePen(PS_SOLID, u, col);
    SelectObject(dc, pen);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    SetBkMode(dc, TRANSPARENT);

    auto rect = [&](int x1, int y1, int x2, int y2) {
        Rectangle(dc, FX(x1), FY(y1), FX(x2), FY(y2));
    };
    auto line = [&](int x1, int y1, int x2, int y2, COLORREF c, int w) {
        TbLine(dc, FX(x1), FY(y1), FX(x2), FY(y2), c, w);
    };
    auto plus = [&](int cx, int cy) {
        // halo keeps the glyph readable where it overlaps the base shape
        COLORREF bg = GetSysColor(COLOR_BTNFACE);
        line(cx - 3, cy, cx + 4, cy, bg, FW(3));
        line(cx, cy - 3, cx, cy + 4, bg, FW(3));
        line(cx - 3, cy, cx + 4, cy, col, FW(2));
        line(cx, cy - 3, cx, cy + 4, col, FW(2));
    };
    auto cross = [&](int cx, int cy, COLORREF c) {
        line(cx - 3, cy - 3, cx + 3, cy + 3, c, FW(2));
        line(cx + 3, cy - 3, cx - 3, cy + 3, c, FW(2));
    };
    auto tag = [&](int cx, int cy, int r) {
        // A label is a rotated square with a punched hole towards its top corner.
        POINT pt[4] = { { FX(cx), FY(cy - r) }, { FX(cx + r), FY(cy) },
                        { FX(cx), FY(cy + r) }, { FX(cx - r), FY(cy) } };
        Polygon(dc, pt, 4);
        Ellipse(dc, FX(cx - 1), FY(cy - r + 2), FX(cx + 1), FY(cy - r + 4));
    };
    // Arrow head pointing right / up / down at the unit point (cx, cy).
    auto headR = [&](int cx, int cy, COLORREF c) {
        line(cx - 3, cy - 3, cx, cy, c, u); line(cx, cy, cx - 3, cy + 3, c, u);
    };
    auto headD = [&](int cx, int cy, COLORREF c) {
        line(cx - 3, cy - 3, cx, cy, c, u); line(cx, cy, cx + 3, cy - 3, c, u);
    };

    switch (id) {
    case IDC_TB_ADD:                       // page + plus
        rect(1, 1, 10, 15);
        plus(12, 11);
        break;
    case IDC_TB_ADDF:                      // folder + plus
        rect(1, 5, 11, 14);
        rect(1, 3, 6, 5);
        plus(13, 10);
        break;
    case IDC_TB_DEL:                       // page + cross
        rect(2, 1, 12, 15);
        cross(7, 8, RGB(200, 60, 60));
        break;
    case IDC_TB_CLEAR:                     // waste bin
        line(3, 4, 13, 4, col, FW(2));
        line(7, 2, 9, 2, col, u);
        rect(4, 5, 12, 15);
        line(6, 7, 6, 13, col, u);
        line(9, 7, 9, 13, col, u);
        break;
    case IDC_TB_PREVIEW:                   // magnifier
        Ellipse(dc, FX(2), FY(2), FX(11), FY(11));
        line(10, 10, 15, 15, col, FW(3));
        break;
    case IDC_TB_APPLY:                     // check mark
        line(2, 9, 6, 13, RGB(30, 140, 60), FW(3));
        line(6, 13, 14, 3, RGB(30, 140, 60), FW(3));
        break;
    case IDC_TB_UNDO: {                    // back arrow
        COLORREF blue = RGB(40, 100, 190);
        HPEN arcPen = CreatePen(PS_SOLID, FW(2), blue);
        SelectObject(dc, arcPen);
        Arc(dc, FX(2), FY(3), FX(14), FY(15), FX(12), FY(12), FX(4), FY(6));
        SelectObject(dc, pen);
        DeleteObject(arcPen);
        POINT head[3] = { { FX(1), FY(5) }, { FX(8), FY(4) }, { FX(4), FY(11) } };
        HPEN triPen = CreatePen(PS_SOLID, u, blue);
        HBRUSH triBr = CreateSolidBrush(blue);
        SelectObject(dc, triPen);
        SelectObject(dc, triBr);
        Polygon(dc, head, 3);
        SelectObject(dc, pen);
        SelectObject(dc, GetStockObject(NULL_BRUSH));
        DeleteObject(triPen);
        DeleteObject(triBr);
        break;
    }
    case IDC_TB_PIN:                       // push pin
        Ellipse(dc, FX(4), FY(2), FX(12), FY(9));
        line(8, 9, 8, 15, col, FW(2));
        line(4, 12, 12, 12, col, u);
        break;
    case IDC_TB_TAGDEL:                    // label + cross
        tag(7, 8, 6);
        cross(12, 12, RGB(200, 60, 60));
        break;
    case IDC_TB_TAGEXTRACT:                // label, arrow out to a name
        tag(5, 8, 4);
        line(9, 8, 13, 8, col, u);
        headR(15, 8, col);
        break;
    case IDC_TB_TAGWRITE:                  // label + pencil
        tag(5, 9, 5);
        line(9, 6, 14, 1, RGB(30, 100, 190), FW(2));
        line(8, 8, 10, 6, RGB(30, 100, 190), FW(2));
        break;
    case IDC_TB_T2S: {                     // dense glyph -> sparse glyph
        rect(1, 4, 6, 12);
        line(2, 7, 5, 7, col, u);
        line(2, 9, 5, 9, col, u);
        line(3, 5, 3, 11, col, u);
        line(5, 5, 5, 11, col, u);
        line(7, 8, 9, 8, col, u);
        headR(10, 8, col);
        rect(11, 5, 15, 11);
        line(12, 8, 14, 8, col, u);
        break;
    }
    case IDC_TB_TEXTCMD:                   // page with a command prompt
        rect(2, 1, 13, 15);
        line(4, 5, 6, 7, col, u);
        line(6, 7, 4, 9, col, u);
        line(8, 10, 11, 10, col, FW(2));
        break;
    case IDC_TB_EXPLIST:                   // list, arrow out
        line(2, 3, 9, 3, col, u);
        line(2, 7, 9, 7, col, u);
        line(2, 11, 9, 11, col, u);
        line(12, 2, 12, 12, col, u);
        headD(12, 14, col);
        break;
    case IDC_TB_RECOPEN:                   // clock (history)
        Ellipse(dc, FX(2), FY(2), FX(14), FY(14));
        line(8, 8, 8, 4, col, u);
        line(8, 8, 12, 10, col, u);
        break;
    case IDC_TB_RECSAVE:                   // floppy
        rect(2, 2, 14, 14);
        rect(5, 2, 11, 6);
        rect(4, 9, 12, 14);
        break;
    case IDC_TB_RECCLEAR:                  // floppy + cross
        rect(2, 2, 12, 13);
        rect(4, 2, 9, 5);
        cross(11, 11, RGB(200, 60, 60));
        break;
    case IDC_TB_CHGDATE:                   // calendar with a pencil
        rect(2, 3, 13, 14);
        line(2, 6, 13, 6, col, FW(2));
        line(5, 1, 5, 4, col, u);
        line(10, 1, 10, 4, col, u);
        line(9, 12, 14, 7, RGB(30, 100, 190), FW(2));
        break;
    case IDC_TB_REC_PANEL:                 // window with a side panel
        rect(1, 2, 15, 14);
        line(6, 2, 6, 14, col, u);
        TbLine(dc, FX(2), FY(4), FX(5), FY(4), col, u);
        TbLine(dc, FX(2), FY(7), FX(5), FY(7), col, u);
        TbLine(dc, FX(2), FY(10), FX(5), FY(10), col, u);
        break;
    case IDC_TB_SPONSOR: {                 // 实心眼形: 两个瓣 + 一个尖
        COLORREF red = RGB(214, 44, 74);
        HBRUSH hb = CreateSolidBrush(red);
        HPEN hp = CreatePen(PS_SOLID, u, red);
        HBRUSH ob = (HBRUSH)SelectObject(dc, hb);
        HPEN op = (HPEN)SelectObject(dc, hp);
        Ellipse(dc, FX(1), FY(2), FX(8), FY(9));
        Ellipse(dc, FX(8), FY(2), FX(15), FY(9));
        POINT pt[3] = { { FX(1), FY(6) }, { FX(15), FY(6) }, { FX(8), FY(15) } };
        Polygon(dc, pt, 3);
        SelectObject(dc, ob); SelectObject(dc, op);
        DeleteObject(hb); DeleteObject(hp);
        break;
    }
    }

    SelectObject(dc, saveBr);
    SelectObject(dc, savePen);
    DeleteObject(pen);
}

// Map a toolbar button id to its embedded icon (NULL when it only has the glyph).
static HICON GetButtonIcon(int id) {
    for (int i = 0; i < kTbIconCount; ++i)
        if (g_tbIcons[i].btnId == id) return g_tbIcons[i].hIcon;
    return NULL;
}

// Disabled buttons show the original artwork faded, not DrawStateW's gray wash:
// at 18px the wash left the thin glyphs (清空/预览) barely visible. The icon is
// composited over a copy of the destination pixels, then blitted back at partial
// opacity, so the fade blends into whatever is behind the button.
static void DrawFadedIcon(HDC dc, HICON hi, int ix, int iy, int sz) {
    HDC mem = CreateCompatibleDC(dc);
    BITMAPINFO bi = { sizeof(BITMAPINFOHEADER) };
    bi.bmiHeader.biWidth = sz;
    bi.bmiHeader.biHeight = -sz;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP dib = mem ? CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0) : NULL;
    if (!dib || !bits) {
        if (dib) DeleteObject(dib);
        if (mem) DeleteDC(mem);
        DrawIconEx(dc, ix, iy, hi, sz, sz, 0, NULL, DI_NORMAL);
        return;
    }
    HBITMAP old = (HBITMAP)SelectObject(mem, dib);
    BitBlt(mem, 0, 0, sz, sz, dc, ix, iy, SRCCOPY);
    DrawIconEx(mem, 0, 0, hi, sz, sz, 0, NULL, DI_NORMAL);
    BYTE* p = (BYTE*)bits;
    for (int i = 0; i < sz * sz; ++i) p[i * 4 + 3] = 255;   // opaque: fade is constant
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 105, 0 };
    AlphaBlend(dc, ix, iy, sz, sz, mem, 0, 0, sz, sz, bf);
    SelectObject(mem, old);
    DeleteObject(dib);
    DeleteDC(mem);
}

static void OnDrawToolbarButton(LPDRAWITEMSTRUCT d) {
    HDC dc = d->hDC;
    RECT rc = d->rcItem;
    int id = (int)d->CtlID;
    bool enabled = !(d->itemState & ODS_DISABLED);
    bool pressed = (d->itemState & ODS_SELECTED) != 0;
    bool hot = (g_hotBtn == id);
    // 置顶 / 操作记录面板 都是"状态开关", 开着的时候按钮保持高亮。
    bool active = (id == IDC_TB_PIN && g_pinTop) ||
                  (id == IDC_TB_REC_PANEL && g_recPanel);
    if (active) pressed = true;

    COLORREF bg = GetSysColor(COLOR_BTNFACE);
    HBRUSH bgBr = CreateSolidBrush(bg);
    FillRect(dc, &rc, bgBr);
    DeleteObject(bgBr);

    if (pressed || hot) {
        RECT hr = rc; InflateRect(&hr, -1, -1);
        COLORREF hotCol = pressed ? RGB(204, 220, 240) : RGB(229, 241, 251);
        HBRUSH hb = CreateSolidBrush(hotCol);
        HPEN hp = CreatePen(PS_SOLID, 1, pressed ? RGB(153, 180, 209) : RGB(173, 209, 238));
        HPEN op = (HPEN)SelectObject(dc, hp);
        HBRUSH ob = (HBRUSH)SelectObject(dc, hb);
        RoundRect(dc, hr.left, hr.top, hr.right, hr.bottom, S(6), S(6));
        SelectObject(dc, ob); SelectObject(dc, op);
        DeleteObject(hb); DeleteObject(hp);
    }

    // group separator to the left of this button
    for (int i = 0; i < TB_COUNT; ++i) {
        if (TB_BUTTONS[i].id == id && TB_BUTTONS[i].newGroup && i > 0) {
            int sx = rc.left - S(4);
            TbLine(dc, sx, rc.top + S(3), sx, rc.bottom - S(3), RGB(160, 160, 160), 1);
            TbLine(dc, sx + 1, rc.top + S(3), sx + 1, rc.bottom - S(3), RGB(250, 250, 250), 1);
            break;
        }
    }

    // Draw the embedded icon; fall back to the geometric glyph if it failed to load.
    HICON hi = GetButtonIcon(id);
    if (hi) {
        int sz = S(g_set.bigIcons ? 26 : 18);
        int ix = ((rc.left + rc.right) - sz) / 2;
        int iy = ((rc.top + rc.bottom) - sz) / 2;
        if (!enabled) DrawFadedIcon(dc, hi, ix, iy, sz);
        else DrawIconEx(dc, ix, iy, hi, sz, sz, 0, NULL, DI_NORMAL);
    } else {
        COLORREF col = enabled ? RGB(60, 60, 60) : RGB(160, 160, 160);
        DrawGlyph(dc, rc, id, col);
    }

    if (d->itemState & ODS_FOCUS) DrawFocusRect(dc, &rc);
}

static LRESULT CALLBACK TbBtnSub(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                UINT_PTR, DWORD_PTR ref) {
    switch (msg) {
    case WM_MOUSEMOVE: {
        int id = (int)ref;
        if (g_hotBtn != id) {
            g_hotBtn = id;
            InvalidateRect(h, NULL, FALSE);
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, h, 0 };
            TrackMouseEvent(&tme);
        }
        break;
    }
    case WM_MOUSELEAVE:
        if (g_hotBtn == (int)ref) { g_hotBtn = 0; InvalidateRect(h, NULL, FALSE); }
        break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

// A plain LISTBOX ignores WM_MOUSEWHEEL, and the system only hands the wheel to the control
// that has focus, so hovering the 预设方案 列表 would do nothing either way. A thread mouse
// hook sees every wheel message this thread receives, wherever its focus is, so the notch
// can be steered to the list actually under the cursor.
static HHOOK g_wheelHook = NULL;

static LRESULT CALLBACK WheelHook(int code, WPARAM wp, LPARAM lp) {
    if (code >= 0 && wp == WM_MOUSEWHEEL && g_hDlg) {
        MSLLHOOKSTRUCT* mh = (MSLLHOOKSTRUCT*)lp;
        HWND hw = WindowFromPoint(mh->pt);
        if (hw && GetAncestor(hw, GA_ROOT) == g_hDlg) {
            wchar_t cls[16] = {};
            GetClassNameW(hw, cls, 16);
            HWND lb = (lstrcmpiW(cls, L"ScrollBar") == 0) ? GetParent(hw) : hw;  // its own bar
            wchar_t lcls[16] = {};
            GetClassNameW(lb, lcls, 16);
            if (lstrcmpiW(lcls, L"LISTBOX") == 0) {
                int lines = 3;
                SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
                if (lines == WHEEL_PAGESCROLL) lines = 5;
                // A positive delta is the wheel rolled towards the user, which shows the
                // earlier rows.
                if (mh->mouseData > 0) lines = -lines;
                // A list box does not act on WM_VSCROLL from outside, so move the first
                // visible row directly and clamp it to what the client height can show.
                RECT rc; GetClientRect(lb, &rc);
                int itemH = (int)SendMessageW(lb, LB_GETITEMHEIGHT, 0, 0);
                int visible = itemH > 0 ? (rc.bottom - rc.top) / itemH : 1;
                if (visible < 1) visible = 1;
                int count = (int)SendMessageW(lb, LB_GETCOUNT, 0, 0);
                int maxTop = count - visible; if (maxTop < 0) maxTop = 0;
                int top = (int)SendMessageW(lb, LB_GETTOPINDEX, 0, 0) + lines;
                if (top < 0) top = 0;
                if (top > maxTop) top = maxTop;
                SendMessageW(lb, LB_SETTOPINDEX, (WPARAM)top, 0);
                return 1;   // consumed: the list box would have dropped it anyway
            }
        }
    }
    return CallNextHookEx(g_wheelHook, code, wp, lp);
}

static void InitToolbar() {
    g_hTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASS, NULL,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        g_hDlg, NULL, GetModuleHandleW(NULL), NULL);
    SendMessageW(g_hTip, TTM_SETMAXTIPWIDTH, 0, S(400));
    for (int i = 0; i < TB_COUNT; ++i) {
        HWND h = GetDlgItem(g_hDlg, TB_BUTTONS[i].id);
        if (!h) continue;
        SetWindowSubclass(h, TbBtnSub, 1, (DWORD_PTR)TB_BUTTONS[i].id);
        TOOLINFOW ti = { sizeof(ti) };
        ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd = g_hDlg;
        ti.uId = (UINT_PTR)h;
        ti.lpszText = (LPWSTR)LS(TB_BUTTONS[i].tipId);
        SendMessageW(g_hTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    }
}

// 切换语言后重设气泡文字 (工具已经注册过了, 用 TTM_UPDATETIPTEXT 原地换)。
static void RefreshToolbarTips() {
    if (!g_hTip) return;
    for (int i = 0; i < TB_COUNT; ++i) {
        HWND h = GetDlgItem(g_hDlg, TB_BUTTONS[i].id);
        if (!h) continue;
        TOOLINFOW ti = { sizeof(ti) };
        ti.hwnd = g_hDlg;
        ti.uId = (UINT_PTR)h;
        ti.lpszText = (LPWSTR)LS(TB_BUTTONS[i].tipId);
        SendMessageW(g_hTip, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
    }
}

// 下拉/列表里的条目: 首项是随语言变的文本, 其余是通配模式或扩展命令关键字(不译)。
// 切换语言要整框重填, 所以单独抽一个函数, WM_INITDIALOG 和切换后都调它。
static void FillCombos(HWND hDlg) {
    // 文件过滤
    HWND hf = GetDlgItem(hDlg, IDC_OV_FILTER);
    const wchar_t* flt[] = { L"", L"*.*", L"*.java", L"*.xml", L"*.mp3", L"*.txt", L"*.ape",
                             L"*.bmp", L"*.gif", L"*.jpg", L"*.zip", L"*.rar", L"*.ogg",
                             L"*.htm", L"*.html" };
    flt[0] = LS(IDS_FILTER_ALL);
    SendMessageW(hf, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < (int)(sizeof(flt) / sizeof(flt[0])); ++i)
        SendMessageW(hf, CB_ADDSTRING, 0, (LPARAM)flt[i]);

    // 序号 预设: 最后一项 [清除列表] 清空 模板 edit (见 IDC_NM_PRESET)
    HWND hPreset = GetDlgItem(hDlg, IDC_NM_PRESET);
    const wchar_t* nmp[] = { L"#.<SELF>", L"#-<SELF>", L"#_<SELF>",
                             L"pic_#", L"file_#", LS(IDS_CLEAR_LIST) };
    SendMessageW(hPreset, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < (int)(sizeof(nmp) / sizeof(nmp[0])); ++i)
        SendMessageW(hPreset, CB_ADDSTRING, 0, (LPARAM)nmp[i]);

    // 扩展更名 预设方案: 这些是要在 扩展命令 框里输入的功能词, 保持中文不译
    HWND hExPreset = GetDlgItem(hDlg, IDC_EX_PRESET);
    const wchar_t* exp[] = { L"汉字转换为拼音", L"汉字转换为拼音且首字母大写",
                             L"添加第一个汉字的拼音声母", L"字符转换为数值",
                             L"数值转换为字符", L"字符顺序颠倒",
                             L"英文字母大小写翻转", L"英文字母一大一小",
                             L"英文字母一小一大" };
    SendMessageW(hExPreset, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < (int)(sizeof(exp) / sizeof(exp[0])); ++i)
        SendMessageW(hExPreset, LB_ADDSTRING, 0, (LPARAM)exp[i]);

    // 标签 预设: 两个标签页共用同一份
    HWND h1 = GetDlgItem(hDlg, IDC_TG_PRESET), h2 = GetDlgItem(hDlg, IDC_TW_PRESET);
    SendMessageW(h1, CB_RESETCONTENT, 0, 0);
    SendMessageW(h2, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < kTagPresetCount; ++i) {
        SendMessageW(h1, CB_ADDSTRING, 0, (LPARAM)LS(kTagPresets[i].nameId));
        SendMessageW(h2, CB_ADDSTRING, 0, (LPARAM)LS(kTagPresets[i].nameId));
    }

    // 流派: CBS_DROPDOWN, 表里的名字只是种子, 不在表里的也能手打
    HWND hg = GetDlgItem(hDlg, IDC_TW_GENRE);
    SendMessageW(hg, CB_RESETCONTENT, 0, 0);
    static const int genreIds[] = { IDS_GENRE_POP, IDS_GENRE_ROCK, IDS_GENRE_FOLK,
                                    IDS_GENRE_ELECTRONIC, IDS_GENRE_CLASSICAL,
                                    IDS_GENRE_HIPHOP, IDS_GENRE_JAZZ, IDS_GENRE_METAL };
    for (int i = 0; i < (int)(sizeof(genreIds) / sizeof(genreIds[0])); ++i)
        SendMessageW(hg, CB_ADDSTRING, 0, (LPARAM)LS(genreIds[i]));
    SendMessageW(hg, CB_ADDSTRING, 0, (LPARAM)L"R&B");
    static const int genreIds2[] = { IDS_GENRE_EASY, IDS_GENRE_SOUNDTRACK,
                                     IDS_GENRE_MANDARIN, IDS_GENRE_CANTONESE,
                                     IDS_GENRE_WESTERN, IDS_GENRE_JPKR, IDS_GENRE_OTHER };
    for (int i = 0; i < (int)(sizeof(genreIds2) / sizeof(genreIds2[0])); ++i)
        SendMessageW(hg, CB_ADDSTRING, 0, (LPARAM)LS(genreIds2[i]));
}

// ----------------------------------------------------------------------------
// Layout: everything is placed in DPI-scaled pixels from the client rect
// ----------------------------------------------------------------------------
static void Place(int id, int x, int y, int w, int h) {
    HWND hw = GetDlgItem(g_hDlg, id);
    if (hw) MoveWindow(hw, S(x), S(y), S(w), S(h), TRUE);
}

// ---- 一排控件的自适应宽度 ----------------------------------------------------------
// 原来每个控件的宽度是写死的, 而那些数字是按中文定的 (见下: 微软雅黑 9pt 一个汉字约
// 13 base px, 单选框再加 20 px), 换到英文 —— 普遍比中文长一半 —— 就会把
// "Filename to upper case" 截成 "Filename to uppe"。所以宽度改成量出来的: 至少 minW,
// 文字更长就按文字走。
struct RowItem {
    int id;      // 控件 ID
    int y;       // 相对 by 的 y, 和原来 Place 的第三参一样
    int minW;    // 最小宽度 = 原设计宽度
    int h;       // 高度
    int fit;     // 0 = 照 minW 原样放 (输入框/下拉框/运行期才填的文字);
                 // 1 = 静态标签, 按文字加宽; 2 = 单选/复选/按钮, 按文字加宽并算上左边的方框
    int gap;     // 与下一个控件的间距 —— 沿用原来写死的那个值, 所以中文下排版分毫不差
};

static int FitW(int id, int minW, int fit) {
    if (fit == 0) return minW;
    HWND hw = GetDlgItem(g_hDlg, id);
    if (!hw) return minW;
    wchar_t buf[256] = { 0 };
    if (!GetWindowTextW(hw, buf, 256) || !buf[0]) return minW;
    HDC dc = GetDC(hw);
    HFONT f = (HFONT)SendMessageW(hw, WM_GETFONT, 0, 0);
    HGDIOBJ old = f ? SelectObject(dc, f) : NULL;
    SIZE sz = { 0 };
    GetTextExtentPoint32W(dc, buf, (int)wcslen(buf), &sz);
    if (old) SelectObject(dc, old);
    ReleaseDC(hw, dc);
    // 量出来的是物理像素, 换回 96dpi 基准 (Place() 会再乘一次 DPI)。
    // fit=2 左边有个 ~16px 的方框和与文字的 4px 间隔, 末尾一律留 6px 免得贴边。
    int need = MulDiv(sz.cx + (fit == 2 ? 20 : 2) + 6, 96, g_dpi);
    return need > minW ? need : minW;
}

// 流式排一行: 每个控件取 FitW 的宽度, 后一个顺次右移 gap。中文下量出来都等于 minW,
// 于是完全还原原来的排版; 换了更长的语言才会撑开。
// 一排放不下 maxRight 时, 优先把「比 minW 多要出来的那部分」按各控件多要的量收回去,
// 保证不越界、也不互相压住。rights 可空, 给的话带回每个控件的右边界 (分组框要用到)。
static void LayoutRow(const RowItem* it, int n, int x0, int maxRight, int* rights = NULL) {
    int want[16], extra[16], total = 0;
    if (n > 16) n = 16;
    for (int i = 0; i < n; ++i) {
        want[i] = FitW(it[i].id, it[i].minW, it[i].fit);
        extra[i] = want[i] - it[i].minW;
        total += want[i] + it[i].gap;
    }
    int avail = maxRight - x0;
    if (total > avail) {
        int over = total - avail, sum = 0;
        for (int i = 0; i < n; ++i) sum += extra[i];
        if (sum > 0) {
            // 按各控件多要的量等比收回: 从头开始扣会把前面几个压回 minW、后面的照旧宽松
            int give = (over < sum) ? over : sum;
            for (int i = 0; i < n; ++i) {
                if (extra[i] <= 0) continue;
                int cut = extra[i] * give / sum;
                want[i] -= cut;
                extra[i] -= cut;
            }
        }
    }
    int x = x0;
    for (int i = 0; i < n; ++i) {
        Place(it[i].id, x, it[i].y, want[i], it[i].h);
        x += want[i] + it[i].gap;
        if (rights) rights[i] = x - it[i].gap;
    }
}

// Mouse-spin arrows for the 序号 number fields: a buddy up-down owning one edit each,
// so the arrows write the integer into the field themselves and hand-typed values stay
// in the same place. The dialog only sees WM_VSCROLL, which is the cue to re-preview.
static void CreateSpin(HWND hDlg, int id, int buddyId, int lo, int hi, int val) {
    HWND hb = GetDlgItem(hDlg, buddyId);
    HWND hs = CreateWindowExW(0, UPDOWN_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
        0, 0, 0, 0, hDlg, (HMENU)(INT_PTR)id,
        (HINSTANCE)GetWindowLongPtrW(hDlg, GWLP_HINSTANCE), NULL);
    if (!hs || !hb) return;
    // Children created after the template land at the BOTTOM of the sibling z-order,
    // where the page's group box covers them and eats the clicks.
    SetWindowPos(hs, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SendMessageW(hs, UDM_SETBUDDY, (WPARAM)hb, 0);
    SendMessageW(hs, UDM_SETRANGE, 0, MAKELPARAM(hi, lo));   // LOWORD = max, HIWORD = min
    SendMessageW(hs, UDM_SETPOS, 0, MAKELPARAM(val, 0));
}

// All coordinates below are 96dpi base pixels; Place()/S() scale them for the
// current DPI. bx/by are the tab page's content origin, W its usable width.
//
// 每一页都是一行一行流式排的 (LayoutRow): 控件至少占 minW, 文字更长就按文字加宽, 后一个
// 顺次右移。minW 和 gap 用的都是原来写死的那些值 —— 原设计是按中文定的 (微软雅黑 9pt
// 一个汉字约 13 base px, 单选/复选框再加 20 px), 中文下 FitW 量出来正好等于 minW, 排版
// 分毫不差; 换到英文 (普遍比中文长一半) 才会撑开, 不会再出现 "Filename to uppe" 这种截断。
static void LayoutTabPage(int bx, int by, int W) {
    // Every group box sits 7 base px inside the tab page frame (see g_rcFrame) on the
    // sides it would otherwise touch, so its dashed edge never merges into the frame's
    // solid line. The full-width ones therefore run bx+7 .. bx+W-7; the 整体 boxes only
    // need the left inset because their right edge stops short of the frame anyway.

    // ---- Tab 0: 整体 (two groups, one row each) ----
    // 文件名改成 / 文件过滤 belong to the page, not to a group, so each box ends just
    // past its last in-group control and those two pairs sit in the clear to its right.
    {
        RowItem r[] = {
            { IDC_OV_NAME_UPPER, by + 20, 116, 15, 2,  6 },
            { IDC_OV_NAME_LOWER, by + 20, 116, 15, 2,  6 },
            { IDC_OV_NAME_TITLE, by + 20, 196, 15, 2, 20 },
            { IDC_OV_LB_NAME,    by + 22,  74, 14, 1,  6 },
            { IDC_OV_NAME_TEXT,  by + 19, 180, 19, 0,  0 },
        };
        int rr[5];
        LayoutRow(r, 5, bx + 18, bx + W - 7, rr);
        // 框只包住前三个单选: rr[2] 是第三个的右边界, 后面那对留在框外的空地上
        Place(IDC_OV_GRP_NAME, bx + 7, by + 4, rr[2] + 8 - (bx + 7), 34);
    }
    {
        RowItem r[] = {
            { IDC_OV_EXT_UPPER, by + 58, 116, 15, 2,  6 },
            { IDC_OV_EXT_LOWER, by + 58, 116, 15, 2, 12 },
            { IDC_OV_LB_EXT,    by + 60,  74, 14, 1,  6 },
            { IDC_OV_EXT_TEXT,  by + 57, 150, 19, 0, 20 },
            { IDC_OV_LB_FILTER, by + 60,  58, 14, 1,  8 },
            { IDC_OV_FILTER,    by + 56, 160, 160, 0,  0 },
        };
        int rr[6];
        LayoutRow(r, 6, bx + 18, bx + W - 7, rr);
        // 这个框包到 扩展名改成 的输入框为止 (rr[3]), 文件过滤那对在框外
        Place(IDC_OV_GRP_EXT, bx + 7, by + 42, rr[3] + 8 - (bx + 7), 34);
    }

    // ---- Tab 1: 替换 ----
    // Row 1: 启用替换 + 查找 + 正则表达式 + 忽略大小写, following the same 12 px gap the
    // first row already uses between controls. Row 2: 替换为, with its edit under 查找's
    // so the two input columns line up; the label is right-aligned to the same edge
    // (LTEXT draws left-aligned, so its box is shifted left instead).
    Place(IDC_RP_GRP, bx + 7, by + 4, W - 14, 72);
    {
        RowItem r[] = {
            { IDC_RP_ENABLE,  by + 20,  80, 15, 2, 12 },
            { IDC_RP_LB_FIND, by + 22,  34, 14, 1,  4 },
            { IDC_RP_FIND,    by + 19, 190, 19, 0, 22 },
            { IDC_RP_REGEX,   by + 20, 200, 15, 2, 20 },
            { IDC_RP_CASEI,   by + 20,  92, 15, 2,  0 },
        };
        LayoutRow(r, 5, bx + 18, bx + W - 7);
    }
    {
        RowItem r[] = {
            { IDC_RP_LB_REPL, by + 52,  52, 14, 1, 4 },
            { IDC_RP_REPL,    by + 49, 190, 19, 0, 0 },
        };
        LayoutRow(r, 2, bx + 92, bx + W - 7);
    }

    // ---- Tab 2: 序号 ----
    // Row 1: 启用序号 + 模板 + 预设 (the picker fills 模板, it is not a setting of its
    // own). Row 2: 起始 / 步长 / 位数, each an edit with a mouse-spin arrow beside it,
    // then 不足位补齐 / 使用字母编号 / 字母大写.
    Place(IDC_NM_GRP, bx + 7, by + 4, W - 14, 72);
    {
        RowItem r[] = {
            { IDC_NM_ENABLE,    by + 20,  80, 15, 2, 12 },
            { IDC_NM_LB_TPL,    by + 22, 250, 14, 1,  0 },
            { IDC_NM_TPL,       by + 19, 230, 19, 0, 20 },
            { IDC_NM_LB_PRESET, by + 22,  34, 14, 1,  6 },
            { IDC_NM_PRESET,    by + 19, 150, 160, 0,  0 },
        };
        LayoutRow(r, 5, bx + 18, bx + W - 7);
    }
    {
        RowItem r[] = {
            { IDC_NM_LB_START,   by + 52, 34, 14, 1,  4 },
            { IDC_NM_START,      by + 49, 38, 19, 0,  0 },
            { IDC_NM_START_SPIN, by + 49, 16, 19, 0, 10 },
            { IDC_NM_LB_STEP,    by + 52, 34, 14, 1,  4 },
            { IDC_NM_STEP,       by + 49, 38, 19, 0,  0 },
            { IDC_NM_STEP_SPIN,  by + 49, 16, 19, 0, 10 },
            { IDC_NM_LB_WIDTH,   by + 52, 34, 14, 1,  4 },
            { IDC_NM_WIDTH,      by + 49, 38, 19, 0,  0 },
            { IDC_NM_WIDTH_SPIN, by + 49, 16, 19, 0, 10 },
            { IDC_NM_PAD,        by + 50, 90, 15, 2,  6 },
            { IDC_NM_LETTER,     by + 50, 150, 15, 2,  6 },
            { IDC_NM_UPPER,      by + 50,  80, 15, 2,  0 },
        };
        LayoutRow(r, 12, bx + 18, bx + W - 7);
    }

    // ---- Tab 3: 添加 ----
    Place(IDC_AD_GRP, bx + 7, by + 4, W - 14, 72);
    {
        RowItem r[] = {
            { IDC_AD_ENABLE,  by + 20,  80, 15, 2, 6 },
            { IDC_AD_LB_TEXT, by + 22,  58, 14, 1, 8 },
            { IDC_AD_TEXT,    by + 19, 230, 19, 0, 0 },
        };
        LayoutRow(r, 3, bx + 18, bx + W - 7);
    }
    {
        RowItem r[] = {
            { IDC_AD_PRE,      by + 50, 80, 15, 2,  6 },
            { IDC_AD_SUF,      by + 50, 80, 15, 2,  6 },
            { IDC_AD_EXTPRE,   by + 50, 80, 15, 2,  6 },
            { IDC_AD_EXTSUF,   by + 50, 80, 15, 2,  6 },
            { IDC_AD_AT,       by + 50, 80, 15, 2, 10 },
            { IDC_AD_LB_POS,   by + 52, 34, 14, 1,  6 },
            { IDC_AD_POS,      by + 49, 38, 19, 0,  0 },
            { IDC_AD_POS_SPIN, by + 49, 16, 19, 0,  0 },
        };
        LayoutRow(r, 8, bx + 18, bx + W - 7);
    }

    // ---- Tab 4: 删除 ----
    // Row 1: 启用删除 + 文本, with the edit in the same column 替换/序号 use for their
    // first input box. Row 2: the four delete modes (one radio group) plus the two numbers
    // they read: 数量 is the N of 删除前/后N个 and the M of the range mode, 起始 (1-based)
    // is where the range mode starts cutting. Both number edits get a mouse spin.
    Place(IDC_DL_GRP, bx + 7, by + 4, W - 14, 72);
    {
        RowItem r[] = {
            { IDC_DL_ENABLE,  by + 20,  80, 15, 2, 12 },
            { IDC_DL_LB_TEXT, by + 22,  34, 14, 1,  4 },
            { IDC_DL_CONTENT, by + 19, 230, 19, 0,  0 },
        };
        LayoutRow(r, 3, bx + 18, bx + W - 7);
    }
    {
        RowItem r[] = {
            { IDC_DL_TEXT,       by + 50, 104, 15, 2,  8 },
            { IDC_DL_RANGE,      by + 50, 140, 15, 2,  8 },
            { IDC_DL_HEAD,       by + 50, 112, 15, 2,  8 },
            { IDC_DL_TAIL,       by + 50, 112, 15, 2, 10 },
            { IDC_DL_LB_START,   by + 52,  34, 14, 1,  4 },
            { IDC_DL_START,      by + 49,  38, 19, 0,  0 },
            { IDC_DL_START_SPIN, by + 49,  16, 19, 0, 10 },
            { IDC_DL_LB_NUM,     by + 52,  34, 14, 1,  4 },
            { IDC_DL_NUM,        by + 49,  38, 19, 0,  0 },
            { IDC_DL_NUM_SPIN,   by + 49,  16, 19, 0, 10 },
            { IDC_DL_NAME,       by + 50, 104, 15, 2,  8 },
            { IDC_DL_EXT,        by + 50, 104, 15, 2,  0 },
        };
        LayoutRow(r, 12, bx + 18, bx + W - 7);
    }

    // ---- Tab 5: 扩展更名 ----
    // Row 1: 启用扩展更名 + the four extension modes, then 预设方案 (that list spans both
    // rows so it shows several commands at once; the rest scrolls).
    // Row 2: the 扩展名 the modes act on, then 扩展命令, whose text the list appends to
    // on double-click.
    Place(IDC_EX_GRP, bx + 7, by + 4, W - 14, 72);
    {
        RowItem r[] = {
            { IDC_EX_ENABLE,    by + 20, 104, 15, 2, 10 },
            { IDC_EX_ADD,       by + 20,  90, 15, 2, 10 },
            { IDC_EX_DEL,       by + 20,  90, 15, 2, 10 },
            { IDC_EX_CHG,       by + 20,  90, 15, 2, 10 },
            { IDC_EX_CASE,      by + 20, 104, 15, 2, 16 },
            { IDC_EX_LB_PRESET, by + 22,  48, 14, 1, 16 },
            { IDC_EX_PRESET,    by + 14, 200, 60, 0,  0 },
        };
        LayoutRow(r, 7, bx + 18, bx + W - 7);
    }
    {
        RowItem r[] = {
            { IDC_EX_LB_TEXT, by + 52,  48, 14, 1,  6 },
            { IDC_EX_TEXT,    by + 49, 168, 19, 0,  8 },
            { IDC_EX_UP,      by + 50,  56, 15, 2,  8 },
            { IDC_EX_LO,      by + 50,  56, 15, 2, 12 },
            { IDC_EX_LB_CMD,  by + 52,  48, 14, 1,  4 },
            { IDC_EX_CMD,     by + 49, 168, 19, 0,  0 },
        };
        LayoutRow(r, 6, bx + 18, bx + W - 7);
    }

    // ---- Tab 6: 标签命名 ----
    // Row 1: 启用标签命名 + 标签表达式 + 预设 (the picker fills the expression box).
    // Row 2: the six %N insert buttons, then 预览 for the selected file's tag values.
    {
        RowItem r[] = {
            { IDC_TG_ENABLE,  by + 20, 110, 15, 2, 10 },
            { IDC_TG_LB_EXPR, by + 22,  62, 14, 1,  4 },
            { IDC_TG_EXPR,    by + 19, 200, 19, 0, 10 },
            { IDC_TG_LB_PRE,  by + 22,  34, 14, 1,  4 },
            { IDC_TG_PRESET,  by + 19, 170, 160, 0,  0 },
        };
        LayoutRow(r, 5, bx + 18, bx + W - 7);
    }
    {
        RowItem r[] = {
            { IDC_TG_B1,      by + 50, 90, 19, 2,  6 },
            { IDC_TG_B2,      by + 50, 90, 19, 2,  6 },
            { IDC_TG_B3,      by + 50, 90, 19, 2,  6 },
            { IDC_TG_B4,      by + 50, 90, 19, 2,  6 },
            { IDC_TG_B5,      by + 50, 90, 19, 2,  6 },
            { IDC_TG_B6,      by + 50, 90, 19, 2, 12 },
            { IDC_TG_LB_PREV, by + 52, 34, 14, 1,  6 },
        };
        int rr[7];
        LayoutRow(r, 7, bx + 18, bx + W - 7, rr);
        // The preview text runs to the right page edge, so it shrinks with the window.
        int pvX = rr[6] + 6, pvW = W - 7 - (pvX - bx);
        if (pvW < 100) pvW = 100;
        Place(IDC_TG_PREVIEW, pvX, by + 52, pvW, 14);
    }

    // ---- Tab 7: 标签写入 ----
    // Three rows instead of the usual two (表达式 + 六个字段), so the row pitch tightens
    // to 24 px; the page is 84 px tall and the last row ends at 77.
    {
        RowItem r[] = {
            { IDC_TW_ENABLE,  by + 10, 110, 15, 2,  8 },
            { IDC_TW_LB_EXPR, by + 13,  74, 14, 1,  4 },
            { IDC_TW_EXPR,    by + 10, 220, 19, 0, 10 },
            { IDC_TW_LB_PRE,  by + 13,  34, 14, 1,  4 },
            { IDC_TW_PRESET,  by + 10, 170, 160, 0, 24 },
            { IDC_TW_CLEAR,   by + 10,  64, 19, 2,  0 },
        };
        LayoutRow(r, 6, bx + 18, bx + W - 7);
    }
    {
        RowItem r[] = {
            { IDC_TW_LB_ARTIST, by + 37,  44, 14, 1,  2 },
            { IDC_TW_ARTIST,    by + 34, 140, 19, 0, 12 },
            { IDC_TW_LB_TITLE,  by + 37,  34, 14, 1,  2 },
            { IDC_TW_TITLE,     by + 34, 140, 19, 0, 12 },
            { IDC_TW_LB_ALBUM,  by + 37,  34, 14, 1,  2 },
            { IDC_TW_ALBUM,     by + 34, 140, 19, 0, 96 },
            { IDC_TW_SAVE,      by + 34,  64, 19, 2,  0 },
        };
        LayoutRow(r, 7, bx + 18, bx + W - 7);
    }
    {
        RowItem r[] = {
            { IDC_TW_LB_TRACK,   by + 61,  34, 14, 1,  4 },
            { IDC_TW_TRACK,      by + 58,  44, 19, 0, 10 },
            { IDC_TW_LB_YEAR,    by + 61,  34, 14, 1,  4 },
            { IDC_TW_YEAR,       by + 58,  52, 19, 0, 10 },
            { IDC_TW_LB_GENRE,   by + 61,  34, 14, 1,  4 },
            { IDC_TW_GENRE,      by + 58, 120, 160, 0, 12 },
            { IDC_TW_LB_COMMENT, by + 61,  34, 14, 1,  2 },
            { IDC_TW_COMMENT,    by + 58, 164, 19, 0, 96 },
            { IDC_TW_WRITE,      by + 58,  64, 19, 2,  0 },
        };
        LayoutRow(r, 9, bx + 18, bx + W - 7);
    }
}

static void Layout() {
    if (!g_hDlg) return;
    RECT rc; GetClientRect(g_hDlg, &rc);
    int cw = MulDiv(rc.right, 96, g_dpi);   // client size in base pixels
    int ch = MulDiv(rc.bottom, 96, g_dpi);
    const int mg = 6, tbTop = 3;
    // 工具栏使用大图标 grows the second row's buttons; the pitch follows the button size so
    // the separators stay centred between neighbours.
    const int tbH = g_set.bigIcons ? 34 : 26;
    const int tbPitch = tbH + 2;

    // toolbar band: a single row of icon buttons, as in the reference layout.
    int x = mg;
    for (int i = 0; i < TB_COUNT; ++i) {
        HWND h = GetDlgItem(g_hDlg, TB_BUTTONS[i].id);
        // 爱心那枚不在队列里: 钉在这一行的最右端, 左边留一条竖线和功能按钮隔开。
        if (TB_BUTTONS[i].right) {
            int sx = cw - mg - tbH;
            if (h) MoveWindow(h, S(sx), S(tbTop), S(tbH), S(tbH), TRUE);
            SetRect(&g_rcTbSep, S(sx) - S(6), S(tbTop + 2), S(sx) - S(4), S(tbTop + tbH - 2));
            continue;
        }
        if (TB_BUTTONS[i].newGroup && i > 0) x += 9;
        if (h) MoveWindow(h, S(x), S(tbTop), S(tbH), S(tbH), TRUE);
        x += tbPitch;
    }
    // 分隔线跟着窗口宽度走, 整条工具栏带重画一遍才不留残线 (WS_CLIPCHILDREN 会跳过按钮本身)。
    RECT tbBand = { 0, 0, rc.right, S(tbTop + tbH + 1) };
    InvalidateRect(g_hDlg, &tbBand, TRUE);

    // Tab strip only — the page controls are siblings drawn on the dialog's own
    // background below it. A full-height tab control would cover the page, and with
    // WS_CLIPCHILDREN on the dialog nothing could then clear the old page's pixels
    // when a tab is switched (the previous page's text stayed on screen as ghosts).
    const int tabStrip = 24;
    const int sbH = 22;
    int tabY = tbTop + tbH + 3;
    // 操作记录面板占掉左边一条。它顶上那条写着「操作记录」的表头必须和标签条在同一行 ——
    // 面板打开时「操作记录」排在最左,
    // 「整体」紧挨着它右边, 两行字同高。所以面板从 tabY 起算, 标签条让位往右挪,
    // 不再通栏。标签页和文件列表同样跟着右移。
    const int recW = 110;
    int panelW = g_recPanel ? recW + 2 : 0;
    if (g_hRec) {
        int py = tabY;
        int ph = ch - py - sbH - 4;
        if (ph < 40) ph = 40;
        MoveWindow(g_hRec, S(mg), S(py), S(recW), S(ph), TRUE);
    }
    MoveWindow(g_hTab, S(mg + panelW), S(tabY), S(cw - 2 * mg - panelW), S(tabStrip), TRUE);
    int bx = mg + panelW;
    int by = tabY + tabStrip - 2;
    int W = cw - 2 * mg - panelW;
    LayoutTabPage(bx, by, W);

    // list view fills the rest
    int listY = tabY + tabStrip + PAGE_H + 2;
    int listH = ch - listY - sbH - 4;
    if (listH < 60) listH = 60;
    int listL = S(mg + panelW), listW = S(cw - 2 * mg - panelW);
    if (g_hList) MoveWindow(g_hList, listL, S(listY), listW, S(listH), TRUE);

    // Client rect of the tab display area, in physical pixels: this is the band that has
    // to be erased between hiding one page and showing the next. It spans the tab page
    // border exactly so no part of that border is ever left unpainted.
    SetRect(&g_rcPage, listL, S(by), listL + listW, S(listY));

    // Tab page border. The tab control is only a strip (see above), so the page's own
    // frame is painted by WM_PAINT: left/right are the list view's columns, the top
    // joins the strip's bottom line, and the bottom stops short of the list view.
    SetRect(&g_rcFrame, listL, S(tabY + tabStrip - 1), listL + listW, S(listY) - S(6));
    RECT band = g_rcFrame;
    InflateRect(&band, 2, 2);
    InvalidateRect(g_hDlg, &band, TRUE);   // a resize must not leave the old border behind

    // status bar
    if (g_hStatus) {
        MoveWindow(g_hStatus, 0, S(ch - sbH), rc.right, S(sbH), TRUE);
        // Absolute part edges: negative offsets are resolved against the bar's own
        // width at call time, which is still 0 on the first layout pass.
        int parts[3] = { S(180), rc.right - S(200), -1 };
        SendMessageW(g_hStatus, SB_SETPARTS, 3, (LPARAM)parts);
        UpdateStatusParts();   // re-apply: the parts only exist from here on
        if (g_hProgress) {
            RECT pr = {}; SendMessageW(g_hStatus, SB_GETRECT, 0, (LPARAM)&pr);
            InflateRect(&pr, -2, -2);
            MoveWindow(g_hProgress, pr.left, pr.top,
                       pr.right - pr.left, pr.bottom - pr.top, TRUE);
        }
    }
}

static void LayoutListColumns() {
    if (!g_hList) return;
    RECT rc; GetWindowRect(g_hList, &rc);
    int w = rc.right - rc.left;
    int fixedW = S(24) + S(240) + S(200) + S(60) + S(80) + S(70) + S(80) + S(70);
    int pathW = w - fixedW - S(24);
    if (pathW < S(120)) pathW = S(120);
    SendMessageW(g_hList, LVM_SETCOLUMNWIDTH, 5, MAKEWPARAM(pathW, 0));
}

// ----------------------------------------------------------------------------
// Tab visibility
// ----------------------------------------------------------------------------
static void ShowTabPage(int tab, bool show) {
    const int* arr = TAB_CTRL[tab];
    for (int k = 0; arr[k] != -1; ++k) {
        HWND h = GetDlgItem(g_hDlg, arr[k]);
        if (h) ShowWindow(h, show ? SW_SHOW : SW_HIDE);
    }
}

static void ShowTab(int tab) {
    // 重建标签条时 (换语言) 会短暂没有选中项, 这时别把所有页都藏了。
    if (tab < 0 || tab >= TAB_COUNT) return;
    // Phase 1: hide the page that is going away, then erase the band while it is empty.
    // Erasing in the same pass as the show does not work: the dialog's background erase
    // excludes the *visible* children, so the incoming page (which sits above the old one
    // in z-order when you move right-to-left) kept whatever pixels the old page left.
    for (int t = 0; t < TAB_COUNT; ++t)
        if (t != tab) ShowTabPage(t, false);
    if (IsRectEmpty(&g_rcPage))
        InvalidateRect(g_hDlg, NULL, TRUE);
    else
        InvalidateRect(g_hDlg, &g_rcPage, TRUE);
    UpdateWindow(g_hDlg);
    // Phase 2: bring in the new page on the cleaned band. No erase here: the band is
    // already clean and erasing again could paint over the controls just shown.
    ShowTabPage(tab, true);
    RedrawWindow(g_hDlg, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    UpdateTagPanel();
}

static void ShowRecPanel(bool on) {
    g_recPanel = on;
    // 开合面板会把整页横移一格, 和切标签是同一类重排, 所以这里必须照搬 ShowTab() 的
    // 「藏 -> 擦 -> 显」两步。只调 RedrawWindow() 是不够的: 对话框带 WS_CLIPCHILDREN,
    // 背景擦除会绕开所有可见子窗口, 而 BS_GROUPBOX 自己又不擦背景, 于是 文件名 /
    // 扩展名 两个框的旧框线擦不掉、新框线只画半截。切一次标签之所以能"修好", 正是
    // 因为 ShowTab() 走的就是下面这套流程。
    int cur = (int)TabCtrl_GetCurSel(g_hTab);
    if (cur < 0 || cur >= TAB_COUNT) cur = 0;
    ShowTabPage(cur, false);            // Phase 1: 先腾空, 好让整条带子能被擦到
    Layout();                           // 面板占掉左边一条, 标签页与文件列表都要跟着挪
    LayoutListColumns();                // 列表窄了一条面板, 末尾的路径列必须重算
    if (g_hRec) ShowWindow(g_hRec, on ? SW_SHOW : SW_HIDE);
    RefreshRecPanel();
    InvalidateRect(g_hDlg, NULL, TRUE);
    UpdateWindow(g_hDlg);
    ShowTabPage(cur, true);             // Phase 2: 在干净的背景上把页面画回来
    RedrawWindow(g_hDlg, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

// Put a %N token where the caret of an expression box is, replacing any selection, and
// leave the caret just after the inserted token.
static void InsertTagToken(int editId, const wchar_t* token) {
    HWND he = GetDlgItem(g_hDlg, editId);
    if (!he) return;
    std::wstring s = GetText(editId);
    DWORD sel = (DWORD)SendMessageW(he, EM_GETSEL, 0, 0);
    size_t a = LOWORD(sel), b = HIWORD(sel);
    if (a > s.size()) a = s.size();
    if (b > s.size()) b = s.size();
    if (a > b) std::swap(a, b);
    std::wstring t = token;
    s.replace(a, b - a, t);
    SetText(editId, s);
    SendMessageW(he, EM_SETSEL, (WPARAM)(a + t.size()), (LPARAM)(a + t.size()));
    SetFocus(he);
}

// Both 标签 tabs follow the selected row: 标签命名 shows the six values it can put into
// a name, 标签写入 loads them into the fields that will be saved.
static void UpdateTagPanel() {
    if (!g_hDlg || !g_hList || !g_hTab) return;
    int tab = (int)TabCtrl_GetCurSel(g_hTab);
    if (tab != 6 && tab != 7) return;
    AudioTags t;
    int sel = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
    if (sel >= 0 && (size_t)sel < g_items.size()) {
        EnsureTags(g_items[sel]);
        t = g_items[sel].tags;
    }
    if (tab == 6) {
        std::wstring s;
        if (sel < 0) s = LS(IDS_TAGPREV_NONE);
        else if (!t.HasAny()) s = LS(IDS_TAGPREV_NOTAG);
        else {
            // 空的字段整段跳过: 否则会拼出 "Artist: Title:xxx" 这种悬空标签。
            // 标签名的译文自带前导空格(英文 " Album:")作分隔, 所以拼完要把开头那一个去掉。
            const int ids[] = { IDS_TAGPREV_ARTIST, IDS_TAGPREV_TITLE, IDS_TAGPREV_TRACK,
                                IDS_TAGPREV_ALBUM, IDS_TAGPREV_YEAR, IDS_TAGPREV_COMMENT };
            const std::wstring* vals[] = { &t.artist, &t.title, &t.track,
                                           &t.album, &t.year, &t.comment };
            for (int i = 0; i < (int)(sizeof(ids) / sizeof(ids[0])); ++i) {
                if (vals[i]->empty()) continue;
                s += LS(ids[i]);
                s += *vals[i];
            }
            if (!s.empty() && s[0] == L' ') s.erase(0, 1);
        }
        SetText(IDC_TG_PREVIEW, s);
        return;
    }
    SetText(IDC_TW_ARTIST, t.artist);
    SetText(IDC_TW_TITLE, t.title);
    SetText(IDC_TW_ALBUM, t.album);
    SetText(IDC_TW_TRACK, t.track);
    SetText(IDC_TW_YEAR, t.year);
    SetText(IDC_TW_COMMENT, t.comment);
    SetWindowTextW(GetDlgItem(g_hDlg, IDC_TW_GENRE), t.genre.c_str());
}

// ----------------------------------------------------------------------------
// 设置对话框 (一般参数 / 音乐文件标签 / 导出文件列表 / 元变量 / 界面)
// ----------------------------------------------------------------------------
static const int SET_TAB_COUNT = 5;
static const int SET_TAB_NAME_IDS[5] = {
    IDS_SET_TAB_GENERAL, IDS_SET_TAB_TAG, IDS_SET_TAB_EXPORT, IDS_SET_TAB_META, IDS_SET_TAB_UI
};
static const int SET_TAB_CTRL[5][12] = {
    { IDC_SET_IGNFOLDER, IDC_SET_WALKALL, IDC_SET_NODATE, IDC_SET_AUTOPREV,
      IDC_SET_SORTINIT, IDC_SET_FASTPREV, IDC_SET_AUTOCORR, IDC_SET_CONFIRMEXIT,
      IDC_SET_LB_WORD, IDC_SET_WORDCHARS, -1 },
    { IDC_SET_ID3V2FIRST, IDC_SET_LB_ID3NOTE, IDC_SET_LB_REMOVEM, IDC_SET_REMOVEMODE,
      IDC_SET_LB_WRITEM, IDC_SET_WRITEMODE, IDC_SET_TAGPROMPT, IDC_SET_KEEPDATE,
      IDC_SET_TRACKPAD, IDC_SET_KEEPBLANK, IDC_SET_TAGBAK, -1 },
    { IDC_SET_EXP_EXT, IDC_SET_EXP_PATH, IDC_SET_EXP_NOFOLDER, -1 },
    { IDC_SET_USEMETA, IDC_SET_METALIST, -1 },
    { IDC_SET_ENHLIST, IDC_SET_GRID, IDC_SET_BIGTB, IDC_SET_LB_COLOR, IDC_SET_COLOR, -1 },
};

static RECT g_rcSetPage = {};   // band erased between pages, see ShowSetTab()

static void ShowSetTabPage(HWND h, int tab, bool show) {
    const int* arr = SET_TAB_CTRL[tab];
    for (int k = 0; arr[k] != -1; ++k) {
        HWND hc = GetDlgItem(h, arr[k]);
        if (hc) ShowWindow(hc, show ? SW_SHOW : SW_HIDE);
    }
}

// Same two-phase erase as the main dialog's ShowTab(): hide the outgoing page, wipe the
// band while nothing is in it, then show the incoming page on the clean band.
static void ShowSetTab(HWND h, int tab) {
    for (int t = 0; t < SET_TAB_COUNT; ++t)
        if (t != tab) ShowSetTabPage(h, t, false);
    InvalidateRect(h, &g_rcSetPage, TRUE);
    UpdateWindow(h);
    ShowSetTabPage(h, tab, true);
    RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

static void SetChk(HWND h, int id, bool on) { CheckDlgButton(h, id, on ? BST_CHECKED : BST_UNCHECKED); }
static bool GetChk(HWND h, int id) { return IsDlgButtonChecked(h, id) == BST_CHECKED; }
// items 是语言包 key id 数组 (不是现成文本): 切换语言时要整框重填。
static void SetCombo(HWND h, int id, const int* items, int count, int sel) {
    HWND c = GetDlgItem(h, id);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < count; ++i) SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)LS(items[i]));
    SendMessageW(c, CB_SETCURSEL, sel, 0);
}
static int ComboSel(HWND h, int id) {
    int s = (int)SendMessageW(GetDlgItem(h, id), CB_GETCURSEL, 0, 0);
    return s < 0 ? 0 : s;
}

static void SettingsToControls(HWND h) {
    SetChk(h, IDC_SET_IGNFOLDER, g_set.ignFolder);
    SetChk(h, IDC_SET_WALKALL, g_set.walkAll);
    SetChk(h, IDC_SET_NODATE, g_set.noFileDate);
    SetChk(h, IDC_SET_AUTOPREV, g_set.autoPreview);
    SetChk(h, IDC_SET_SORTINIT, g_set.sortOnInit);
    SetChk(h, IDC_SET_FASTPREV, g_set.fastPreview);
    SetChk(h, IDC_SET_AUTOCORR, g_set.autoCorrect);
    SetChk(h, IDC_SET_CONFIRMEXIT, g_set.confirmExit);
    static const int wordModes[] = { IDS_WORD_SPACE, IDS_WORD_NONALNUM };
    SetCombo(h, IDC_SET_WORDCHARS, wordModes, 2, g_set.wordSplit);

    SetChk(h, IDC_SET_ID3V2FIRST, g_set.id3v2First);
    static const int removeModes[] = { IDS_REMOVE_V1, IDS_REMOVE_V2, IDS_REMOVE_BOTH };
    static const int writeModes[] = { IDS_WRITE_V1, IDS_WRITE_V2, IDS_WRITE_BOTH };
    SetCombo(h, IDC_SET_REMOVEMODE, removeModes, 3, g_set.removeMode);
    SetCombo(h, IDC_SET_WRITEMODE, writeModes, 3, g_set.writeMode);
    SetChk(h, IDC_SET_TAGPROMPT, g_set.tagPrompt);
    SetChk(h, IDC_SET_KEEPDATE, g_set.keepFileDate);
    SetChk(h, IDC_SET_TRACKPAD, g_set.trackPad2);
    SetChk(h, IDC_SET_KEEPBLANK, g_set.keepBlankTag);
    SetChk(h, IDC_SET_TAGBAK, g_set.tagBackup);

    SetChk(h, IDC_SET_EXP_EXT, g_set.expExt);
    SetChk(h, IDC_SET_EXP_PATH, g_set.expPath);
    SetChk(h, IDC_SET_EXP_NOFOLDER, g_set.expNoFolder);

    SetChk(h, IDC_SET_USEMETA, g_set.useMeta);

    SetChk(h, IDC_SET_ENHLIST, g_set.enhList);
    SetChk(h, IDC_SET_GRID, g_set.grid);
    SetChk(h, IDC_SET_BIGTB, g_set.bigIcons);
    SetCombo(h, IDC_SET_COLOR, kColorNameIds, kColorCount, g_set.colorScheme);
}

static void SettingsFromControls(HWND h) {
    g_set.ignFolder = GetChk(h, IDC_SET_IGNFOLDER);
    g_set.walkAll = GetChk(h, IDC_SET_WALKALL);
    g_set.noFileDate = GetChk(h, IDC_SET_NODATE);
    g_set.autoPreview = GetChk(h, IDC_SET_AUTOPREV);
    g_set.sortOnInit = GetChk(h, IDC_SET_SORTINIT);
    g_set.fastPreview = GetChk(h, IDC_SET_FASTPREV);
    g_set.autoCorrect = GetChk(h, IDC_SET_AUTOCORR);
    g_set.confirmExit = GetChk(h, IDC_SET_CONFIRMEXIT);
    g_set.wordSplit = ComboSel(h, IDC_SET_WORDCHARS);

    g_set.id3v2First = GetChk(h, IDC_SET_ID3V2FIRST);
    g_set.removeMode = ComboSel(h, IDC_SET_REMOVEMODE);
    g_set.writeMode = ComboSel(h, IDC_SET_WRITEMODE);
    g_set.tagPrompt = GetChk(h, IDC_SET_TAGPROMPT);
    g_set.keepFileDate = GetChk(h, IDC_SET_KEEPDATE);
    g_set.trackPad2 = GetChk(h, IDC_SET_TRACKPAD);
    g_set.keepBlankTag = GetChk(h, IDC_SET_KEEPBLANK);
    g_set.tagBackup = GetChk(h, IDC_SET_TAGBAK);

    g_set.expExt = GetChk(h, IDC_SET_EXP_EXT);
    g_set.expPath = GetChk(h, IDC_SET_EXP_PATH);
    g_set.expNoFolder = GetChk(h, IDC_SET_EXP_NOFOLDER);

    g_set.useMeta = GetChk(h, IDC_SET_USEMETA);

    g_set.enhList = GetChk(h, IDC_SET_ENHLIST);
    g_set.grid = GetChk(h, IDC_SET_GRID);
    g_set.bigIcons = GetChk(h, IDC_SET_BIGTB);
    g_set.colorScheme = ComboSel(h, IDC_SET_COLOR);
}

static INT_PTR CALLBACK SettingsProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        ApplyLanguage(h, IDS_DLG_SETTINGS);
        // Same reason the main dialog ignores its template geometry: the DLU grid is
        // derived from the mapped font, so size and place everything in DPI pixels.
        const int CW = 432, CH = 288;
        RECT wr = { 0, 0, S(CW), S(CH) };
        AdjustWindowRectEx(&wr, (DWORD)GetWindowLongPtrW(h, GWL_STYLE), FALSE,
                           (DWORD)GetWindowLongPtrW(h, GWL_EXSTYLE));
        int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
        HWND owner = GetWindow(h, GW_OWNER);
        if (owner) {
            RECT o; GetWindowRect(owner, &o);
            x = o.left + ((o.right - o.left) - (wr.right - wr.left)) / 2;
            y = o.top + ((o.bottom - o.top) - (wr.bottom - wr.top)) / 2;
        } else {
            x = (GetSystemMetrics(SM_CXSCREEN) - (wr.right - wr.left)) / 2;
            y = (GetSystemMetrics(SM_CYSCREEN) - (wr.bottom - wr.top)) / 2;
        }
        SetWindowPos(h, NULL, x, y, wr.right - wr.left, wr.bottom - wr.top, SWP_NOZORDER);

        auto P = [h](int id, int cx, int cy, int cw, int ch) {
            MoveWindow(GetDlgItem(h, id), cx, cy, cw, ch, TRUE);
        };
        // 版面按 6/5 放大: 对话框加宽加高后, 页内控件保持原来的相对排布, 间距、控件
        // 尺寸一起长 20% (字号仍是模板里的 9pt, 不跟着缩)。
        auto PS = [&](int id, int cx, int cy, int cw, int ch) {
            P(id, S(cx * 6 / 5), S(cy * 6 / 5), S(cw * 6 / 5), S(ch * 6 / 5));
        };
        // Tab strip only, exactly like the main dialog: the page controls are siblings
        // of the strip and are shown/hidden per tab.
        P(IDC_SET_TAB, S(10), S(7), S(CW - 20), S(29));
        const int L = 16, R = 180;
        // 一般参数
        PS(IDC_SET_IGNFOLDER,  L, 34,  150, 15);
        PS(IDC_SET_WALKALL,    L, 56,  155, 15);   // 别再压到右列的打勾框
        PS(IDC_SET_NODATE,     L, 78,  150, 15);
        PS(IDC_SET_AUTOPREV,   L, 100, 150, 15);
        PS(IDC_SET_SORTINIT,   R, 34,  170, 15);
        PS(IDC_SET_FASTPREV,   R, 56,  172, 15);
        PS(IDC_SET_AUTOCORR,   R, 78,  150, 15);
        PS(IDC_SET_CONFIRMEXIT,R, 100, 150, 15);
        PS(IDC_SET_LB_WORD,    L, 132, 112, 14);
        PS(IDC_SET_WORDCHARS,  150, 129, 120, 120);
        // 音乐文件标签
        PS(IDC_SET_ID3V2FIRST, L, 34,  160, 15);
        PS(IDC_SET_LB_ID3NOTE, 200, 37, 150, 14);
        PS(IDC_SET_LB_REMOVEM, L, 62,  92,  14);
        PS(IDC_SET_REMOVEMODE, 110, 59, 150, 120);
        PS(IDC_SET_LB_WRITEM,  L, 88,  92,  14);
        PS(IDC_SET_WRITEMODE,  110, 85, 150, 120);
        PS(IDC_SET_TAGPROMPT,  L, 114, 150, 15);
        PS(IDC_SET_KEEPDATE,   200, 114, 140, 15);
        PS(IDC_SET_TRACKPAD,   L, 136, 200, 15);
        PS(IDC_SET_KEEPBLANK,  L, 158, 140, 15);
        PS(IDC_SET_TAGBAK,     160, 158, 192, 15);
        // 导出文件列表
        PS(IDC_SET_EXP_EXT,      L, 34, 180, 15);
        PS(IDC_SET_EXP_PATH,     L, 56, 180, 15);
        PS(IDC_SET_EXP_NOFOLDER, L, 78, 180, 15);
        // 元变量
        PS(IDC_SET_USEMETA,  L, 32, 120, 15);
        PS(IDC_SET_METALIST, L, 54, 328, 124);
        // 界面
        PS(IDC_SET_ENHLIST,  L, 34, 180, 15);
        PS(IDC_SET_GRID,     L, 56, 140, 15);
        PS(IDC_SET_BIGTB,    L, 78, 150, 15);
        PS(IDC_SET_LB_COLOR, L, 106, 112, 14);
        PS(IDC_SET_COLOR,    150, 103, 120, 120);

        P(IDOK,          S(62),  S(252), S(88),  S(29));
        P(IDCANCEL,      S(160), S(252), S(88),  S(29));
        P(IDC_SET_RESTORE, S(250), S(252), S(130), S(29));

        HWND hTab = GetDlgItem(h, IDC_SET_TAB);
        TCITEMW ti = { 0 };
        ti.mask = TCIF_TEXT;
        for (int t = 0; t < SET_TAB_COUNT; ++t) {
            ti.pszText = (LPWSTR)LS(SET_TAB_NAME_IDS[t]);
            TabCtrl_InsertItem(hTab, t, &ti);
        }

        HWND hl = GetDlgItem(h, IDC_SET_METALIST);
        ListView_SetExtendedListViewStyle(hl, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        const wchar_t* heads[] = { LS(IDS_META_COL_VAR), LS(IDS_META_COL_VAL) };
        // 值 column must leave room for the vertical scrollbar or the list shows a
        // horizontal one too.
        int hw[] = { 120, 220 };
        for (int c = 0; c < 2; ++c) {
            LVCOLUMNW col = { 0 };
            col.mask = LVCF_TEXT | LVCF_WIDTH;
            col.pszText = (LPWSTR)heads[c];
            col.cx = S(hw[c]);
            ListView_InsertColumn(hl, c, &col);
        }
        for (int i = 0; i < kMetaCount; ++i) {
            LVITEMW it = { 0 };
            it.mask = LVIF_TEXT;
            it.iItem = i;
            it.pszText = (LPWSTR)kMetaVars[i].var;
            ListView_InsertItem(hl, &it);
            ListView_SetItemText(hl, i, 1, (LPWSTR)kMetaVars[i].value);
        }

        SettingsToControls(h);
        TabCtrl_SetCurSel(hTab, 0);
        // Band the pages live in; it stops above the button row so 确定/取消 are never
        // wiped by a tab change.
        SetRect(&g_rcSetPage, S(10), S(34), S(CW - 10), S(248));
        ShowSetTab(h, 0);
        return TRUE;
    }
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lp;
        if (nm->hwndFrom == GetDlgItem(h, IDC_SET_TAB) && nm->code == TCN_SELCHANGE)
            ShowSetTab(h, TabCtrl_GetCurSel(GetDlgItem(h, IDC_SET_TAB)));
        return FALSE;
    }
    case WM_CTLCOLORSTATIC:
        // "* 默认为ID3v1优先" is a hint, blue as in 设置2.png.
        if (GetDlgCtrlID((HWND)lp) == IDC_SET_LB_ID3NOTE)
            SetTextColor((HDC)wp, RGB(0, 0, 190));
        return FALSE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_SET_RESTORE) {
            g_set = AppSettings();      // the struct's initialisers are the defaults
            SettingsToControls(h);
            return TRUE;
        }
        if (LOWORD(wp) == IDOK) {
            SettingsFromControls(h);
            SettingsSave();
            EndDialog(h, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(h, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// ----------------------------------------------------------------------------
// Commands shared by the menu, the accelerators and the second toolbar row
// ----------------------------------------------------------------------------
// 窗体摆在屏幕中央只做一次: 启动时 (WM_INITDIALOG 末尾) 调用。
// 启动之后窗体不再自动挪动, 用户可以把它拖到任意位置, 拖到哪就停在哪。
// 选项 > 窗口居中显示 复用本函数, 那是用户主动要求挪回屏幕中央。
static void CenterWindow() {
    if (!g_hDlg) return;
    RECT rc; GetWindowRect(g_hDlg, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(MonitorFromWindow(g_hDlg, MONITOR_DEFAULTTONEAREST), &mi);
    int mw = mi.rcWork.right - mi.rcWork.left, mh = mi.rcWork.bottom - mi.rcWork.top;
    SetWindowPos(g_hDlg, NULL, mi.rcWork.left + (mw - w) / 2,
                 mi.rcWork.top + (mh - h) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

// Everything the 界面 / 选项 pages change is re-applied here, both at start-up and after
// the dialog closes with 确定.
static void ApplySettings() {
    if (!g_hDlg) return;
    DWORD ex = LVS_EX_CHECKBOXES;   // 打勾列常驻，不随界面开关重置
    if (g_set.enhList) ex |= LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER;
    if (g_set.grid) ex |= LVS_EX_GRIDLINES;
    ListView_SetExtendedListViewStyle(g_hList, ex);
    ListColors lc = ListSchemeColors(g_set.colorScheme);
    ListView_SetBkColor(g_hList, lc.back);
    ListView_SetTextBkColor(g_hList, lc.back);
    // Row text colour has no message of its own: NM_CUSTOMDRAW applies it per row.

    Layout();
    LayoutListColumns();
    RedrawWindow(g_hDlg, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    InvalidateRect(g_hList, NULL, TRUE);
}

// UTF-8 字节序标记。三个字节都大于 127, 写进 signed char 会触发 MSVC 的
// C4310(强制转换截断常量值) —— 位模式其实没变、写出的文件是对的, 但每次编译都挨 6 次
// 警告很吵, 直接用 unsigned char 表达"这是一串字节"就没这事了。
static const unsigned char kBomUtf8[3] = { 0xEF, 0xBB, 0xBF };

// 导出当前文件列表: one name per line, honouring the 导出文件列表 page. UTF-8 with a BOM
// so 记事本 reads the Chinese back correctly.
static void DoExportList() {
    if (g_items.empty()) return;
    wchar_t file[512] = { 0 };
    wcscpy_s(file, LS(IDS_DEF_FILE_LIST));
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = g_hDlg;
    ofn.lpstrFilter = LS(IDS_FILTER_TXT);
    ofn.lpstrFile = file;
    ofn.nMaxFile = 512;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = L"txt";
    if (!GetSaveFileNameW(&ofn)) return;

    std::wstring text;
    int written = 0;
    for (auto& it : g_items) {
        std::error_code ec;
        bool isDir = fs::is_directory(it.fullPath, ec);
        if (isDir && g_set.expNoFolder) continue;
        std::wstring name = it.fullPath.filename().wstring();
        if (!g_set.expExt) name = it.fullPath.stem().wstring();
        if (g_set.expPath) name = it.fullPath.parent_path().wstring() + L"\\" + name;
        text += name;
        text += L"\r\n";
        ++written;
    }
    int need = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0, NULL, NULL);
    std::string utf8;
    utf8.resize(need);
    if (need > 0)
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &utf8[0], need, NULL, NULL);

    HANDLE hf = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) {
        MessageBoxW(g_hDlg, LS(IDS_ERR_WRITE_FILE), LS(IDS_TITLE_EXPORT_LIST), MB_OK | MB_ICONERROR);
        return;
    }
    DWORD done = 0;
    WriteFile(hf, kBomUtf8, (DWORD)sizeof(kBomUtf8), &done, NULL);
    if (need > 0) WriteFile(hf, utf8.data(), (DWORD)need, &done, NULL);
    CloseHandle(hf);

    wchar_t buf[128];
    swprintf_s(buf, 128, LS(IDS_MSG_LIST_EXPORTED), written, file);
    MessageBoxW(g_hDlg, buf, LS(IDS_TITLE_EXPORT_LIST), MB_OK | MB_ICONINFORMATION);
}

// ----------------------------------------------------------------------------
// 操作记录文件 (.xto): 首行标识, 之后每行 "原完整路径\t新完整路径"。
// UTF-8 带 BOM, 记事本打开也能看懂。打开时把记录里现存的文件装回列表, 并挂上
// 一个撤销批次, 所以 撤销(Ctrl+Z) 能把这批更名整体还原成原名。
// ----------------------------------------------------------------------------
static bool WriteUtf8WithBom(const wchar_t* path, const std::wstring& text, const wchar_t* title) {
    int need = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0, NULL, NULL);
    std::string utf8;
    utf8.resize(need);
    if (need > 0)
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &utf8[0], need, NULL, NULL);
    HANDLE hf = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) {
        MessageBoxW(g_hDlg, LS(IDS_ERR_WRITE_FILE), title, MB_OK | MB_ICONERROR);
        return false;
    }
    DWORD done = 0;
    WriteFile(hf, kBomUtf8, (DWORD)sizeof(kBomUtf8), &done, NULL);
    if (need > 0) WriteFile(hf, utf8.data(), (DWORD)need, &done, NULL);
    CloseHandle(hf);
    return true;
}

static void DoRecSave() {
    if (g_rec.empty()) {
        MessageBoxW(g_hDlg, LS(IDS_MSG_REC_NONE), LS(IDS_TITLE_REC_SAVE), MB_OK | MB_ICONINFORMATION);
        return;
    }
    wchar_t file[512] = { 0 };
    wcscpy_s(file, LS(IDS_DEF_RECORD));
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = g_hDlg;
    ofn.lpstrFilter = LS(IDS_FILTER_XTO);
    ofn.lpstrFile = file;
    ofn.nMaxFile = 512;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = L"xto";
    if (!GetSaveFileNameW(&ofn)) return;

    std::wstring text = L"AradeRenamer-XTO 1\r\n";
    for (auto& r : g_rec) { text += r.from; text += L"\t"; text += r.to; text += L"\r\n"; }
    if (!WriteUtf8WithBom(file, text, LS(IDS_TITLE_REC_SAVE))) return;

    wchar_t buf[512];
    swprintf_s(buf, 512, LS(IDS_MSG_REC_SAVED), (int)g_rec.size(), file);
    MessageBoxW(g_hDlg, buf, LS(IDS_TITLE_REC_SAVE), MB_OK | MB_ICONINFORMATION);
}

static void DoRecOpen() {
    wchar_t file[512] = L"";
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = g_hDlg;
    ofn.lpstrFilter = LS(IDS_FILTER_XTO);
    ofn.lpstrFile = file;
    ofn.nMaxFile = 512;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return;

    HANDLE hf = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) {
        MessageBoxW(g_hDlg, LS(IDS_ERR_OPEN_FILE), LS(IDS_TITLE_REC_OPEN), MB_OK | MB_ICONERROR);
        return;
    }
    std::string raw;
    {
        DWORD sz = GetFileSize(hf, NULL);
        raw.resize((size_t)sz + 1);
        DWORD got = 0;
        ReadFile(hf, &raw[0], sz, &got, NULL);
        raw.resize(got);
    }
    CloseHandle(hf);
    size_t off = 0;
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) off = 3;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, raw.c_str() + off, (int)(raw.size() - off), NULL, 0);
    std::wstring text(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, raw.c_str() + off, (int)(raw.size() - off), &text[0], wlen);

    std::vector<RecEntry> recs;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find(L'\n', pos);
        if (e == std::wstring::npos) e = text.size();
        std::wstring line = text.substr(pos, e - pos);
        pos = e + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.empty() || line[0] == L'#') continue;
        size_t t = line.find(L'\t');
        if (t == std::wstring::npos) continue;   // 标识行/坏行直接跳过
        recs.push_back({ line.substr(0, t), line.substr(t + 1) });
    }
    if (recs.empty()) {
        MessageBoxW(g_hDlg, LS(IDS_MSG_REC_EMPTY_FILE), LS(IDS_TITLE_REC_OPEN),
                    MB_OK | MB_ICONINFORMATION);
        return;
    }

    g_rec = recs;
    g_items.clear();
    g_keys.clear();
    std::vector<UndoEntry> batch;
    int missing = 0;
    for (auto& r : recs) {
        std::error_code ec;
        if (!fs::exists(fs::path(r.to), ec)) { ++missing; continue; }
        int idx = (int)g_items.size();
        AddOneFile(fs::path(r.to));
        if ((int)g_items.size() == idx + 1) {   // 真的入表了(重复项会被忽略)才能撤销
            UndoEntry e;
            e.index = idx;
            e.original = fs::path(r.from);
            e.renamed = fs::path(r.to);
            e.origName = fs::path(r.from).filename().wstring();
            batch.push_back(e);
        }
    }
    RebuildList();
    if (!batch.empty()) g_undo.push_back(batch);
    UpdateToolbarState();
    AutoPreview();
    if (g_recPanel) RefreshRecPanel(); else ShowRecPanel(true);

    if (missing > 0) {
        wchar_t buf[128];
        swprintf_s(buf, 128, LS(IDS_MSG_REC_MISSING), missing);
        MessageBoxW(g_hDlg, buf, LS(IDS_TITLE_REC_OPEN), MB_OK | MB_ICONINFORMATION);
    }
}

// ----------------------------------------------------------------------------
// 文件列表右键菜单: 目标行与批量动作 (见 右键菜单.png)
// ----------------------------------------------------------------------------
// 打勾列就是"这一行要不要被处理"。批量动作先看打勾的行，一行都没打勾时才退回高亮
// 选中的行 —— 刚添加完(默认全部打勾)和手工框选几行两种用法都能得到预期结果。
static std::vector<int> CheckedRows() {
    std::vector<int> v;
    for (size_t i = 0; i < g_items.size(); ++i)
        if (g_items[i].checked) v.push_back((int)i);
    return v;
}

static std::vector<int> SelectedRows(int hitRow) {
    std::vector<int> v;
    for (int i = -1; (i = ListView_GetNextItem(g_hList, i, LVNI_SELECTED)) != -1;)
        if (i >= 0 && (size_t)i < g_items.size()) v.push_back(i);
    if (v.empty() && hitRow >= 0 && (size_t)hitRow < g_items.size()) v.push_back(hitRow);
    return v;
}

static std::vector<int> CtxTargets(int hitRow) {
    std::vector<int> v = CheckedRows();
    return v.empty() ? SelectedRows(hitRow) : v;
}

// 右键菜单条目 + 快捷键提示。菜单资源里那种 \tCtrl+X 的写法对运行时 AppendMenuW
// 建的菜单一样有效: \t 之后的部分由菜单自己右对齐显示。
static std::wstring CtxLabel(int id, const wchar_t* accel) {
    std::wstring s = LS(id);
    if (accel && *accel) { s += L'\t'; s += accel; }
    return s;
}

// ----------------------------------------------------------------------------
// 更改文件日期 (见 更改文件日期对话框.png)
// ----------------------------------------------------------------------------
static bool g_dtUseNow = false;
static SYSTEMTIME g_dtCreate = { 0 }, g_dtAccess = { 0 }, g_dtWrite = { 0 };

static const int DT_GRP[3] = { IDC_DT_GRP_CREATE, IDC_DT_GRP_ACCESS, IDC_DT_GRP_WRITE };
static const int DT_DTP[6] = { IDC_DT_CREATE_D, IDC_DT_CREATE_T, IDC_DT_ACCESS_D,
                               IDC_DT_ACCESS_T, IDC_DT_WRITE_D, IDC_DT_WRITE_T };

// 时间戳在磁盘上是 UTC, 选择器给的是本地时间, 两头都要换算一次。
static bool LocalToUtc(const SYSTEMTIME& st, FILETIME& ft) {
    SYSTEMTIME utc;
    if (!TzSpecificLocalTimeToSystemTime(NULL, &st, &utc)) return false;
    return SystemTimeToFileTime(&utc, &ft) != FALSE;
}

static void ReadFileTimes(const fs::path& p, SYSTEMTIME& c, SYSTEMTIME& a, SYSTEMTIME& w) {
    GetLocalTime(&c); a = c; w = c;
    HANDLE hf = CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (hf == INVALID_HANDLE_VALUE) return;
    FILETIME fc, fa, fw, lf;
    if (GetFileTime(hf, &fc, &fa, &fw)) {
        if (FileTimeToLocalFileTime(&fc, &lf)) FileTimeToSystemTime(&lf, &c);
        if (FileTimeToLocalFileTime(&fa, &lf)) FileTimeToSystemTime(&lf, &a);
        if (FileTimeToLocalFileTime(&fw, &lf)) FileTimeToSystemTime(&lf, &w);
    }
    CloseHandle(hf);
}

static void SetDtp(HWND he, const SYSTEMTIME& st) {
    SendMessageW(he, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&st);
}

static SYSTEMTIME GetDtp(HWND he) {
    SYSTEMTIME st = { 0 };
    if (SendMessageW(he, DTM_GETSYSTEMTIME, 0, (LPARAM)&st) != GDT_VALID) GetLocalTime(&st);
    return st;
}

// 日期和时间是两个独立的选择器, 合成一个 SYSTEMTIME 才能写文件。
static SYSTEMTIME MergeDT(const SYSTEMTIME& d, const SYSTEMTIME& t) {
    SYSTEMTIME r = d;
    r.wHour = t.wHour; r.wMinute = t.wMinute; r.wSecond = t.wSecond; r.wMilliseconds = 0;
    return r;
}

static INT_PTR CALLBACK ChgDateProc(HWND h, UINT msg, WPARAM wp, LPARAM /*lp*/) {
    switch (msg) {
    case WM_INITDIALOG: {
        ApplyLanguage(h, IDS_DLG_CHGDATE);
        const int CW = 390, CH = 300;
        RECT wr = { 0, 0, S(CW), S(CH) };
        AdjustWindowRectEx(&wr, (DWORD)GetWindowLongPtrW(h, GWL_STYLE), FALSE,
                           (DWORD)GetWindowLongPtrW(h, GWL_EXSTYLE));
        int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
        HWND owner = GetWindow(h, GW_OWNER);
        if (owner) {
            RECT o; GetWindowRect(owner, &o);
            x = o.left + ((o.right - o.left) - (wr.right - wr.left)) / 2;
            y = o.top + ((o.bottom - o.top) - (wr.bottom - wr.top)) / 2;
        } else {
            x = (GetSystemMetrics(SM_CXSCREEN) - (wr.right - wr.left)) / 2;
            y = (GetSystemMetrics(SM_CYSCREEN) - (wr.bottom - wr.top)) / 2;
        }
        SetWindowPos(h, NULL, x, y, wr.right - wr.left, wr.bottom - wr.top, SWP_NOZORDER);

        auto P = [h](int id, int cx, int cy, int cw, int ch) {
            MoveWindow(GetDlgItem(h, id), cx, cy, cw, ch, TRUE);
        };
        P(IDC_DT_NOW,     S(16),  S(13), S(184), S(17));
        P(IDC_DT_CUSTOM,  S(16),  S(41), S(184), S(17));
        P(IDOK,           S(290), S(9),  S(84),  S(25));
        P(IDCANCEL,       S(290), S(41), S(84),  S(25));
        for (int i = 0; i < 3; i++) {
            P(DT_GRP[i],         S(16),  S(74 + i * 74), S(358), S(67));
            P(DT_DTP[i * 2],     S(28),  S(100 + i * 74), S(172), S(24));
            P(DT_DTP[i * 2 + 1], S(218), S(100 + i * 74), S(143), S(24));
        }

        // 时间按系统短格式会渲染成 "4:05:06", 24 小时制, 所以钉死格式。
        for (int i = 0; i < 3; i++)
            SendMessageW(GetDlgItem(h, DT_DTP[i * 2 + 1]), DTM_SETFORMAT, 0,
                         (LPARAM)L"HH:mm:ss");

        CheckRadioButton(h, IDC_DT_NOW, IDC_DT_CUSTOM,
                         g_dtUseNow ? IDC_DT_NOW : IDC_DT_CUSTOM);
        SetDtp(GetDlgItem(h, IDC_DT_CREATE_D), g_dtCreate);
        SetDtp(GetDlgItem(h, IDC_DT_CREATE_T), g_dtCreate);
        SetDtp(GetDlgItem(h, IDC_DT_ACCESS_D), g_dtAccess);
        SetDtp(GetDlgItem(h, IDC_DT_ACCESS_T), g_dtAccess);
        SetDtp(GetDlgItem(h, IDC_DT_WRITE_D), g_dtWrite);
        SetDtp(GetDlgItem(h, IDC_DT_WRITE_T), g_dtWrite);
        EnableWindow(GetDlgItem(h, IDC_DT_NOW), TRUE);
        EnableWindow(GetDlgItem(h, IDC_DT_CUSTOM), TRUE);
        for (int i = 0; i < 3; i++) {
            BOOL on = g_dtUseNow ? FALSE : TRUE;
            EnableWindow(GetDlgItem(h, DT_GRP[i]), on);
            EnableWindow(GetDlgItem(h, DT_DTP[i * 2]), on);
            EnableWindow(GetDlgItem(h, DT_DTP[i * 2 + 1]), on);
        }
        return FALSE;   // 让对话框自己把焦点给第一个可聚焦控件
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_DT_NOW || LOWORD(wp) == IDC_DT_CUSTOM) {
            BOOL on = IsDlgButtonChecked(h, IDC_DT_CUSTOM) == BST_CHECKED ? TRUE : FALSE;
            for (int i = 0; i < 3; i++) {
                EnableWindow(GetDlgItem(h, DT_GRP[i]), on);
                EnableWindow(GetDlgItem(h, DT_DTP[i * 2]), on);
                EnableWindow(GetDlgItem(h, DT_DTP[i * 2 + 1]), on);
            }
            return TRUE;
        }
        if (LOWORD(wp) == IDOK) {
            g_dtUseNow = IsDlgButtonChecked(h, IDC_DT_NOW) == BST_CHECKED;
            if (!g_dtUseNow) {
                g_dtCreate = MergeDT(GetDtp(GetDlgItem(h, IDC_DT_CREATE_D)),
                                     GetDtp(GetDlgItem(h, IDC_DT_CREATE_T)));
                g_dtAccess = MergeDT(GetDtp(GetDlgItem(h, IDC_DT_ACCESS_D)),
                                     GetDtp(GetDlgItem(h, IDC_DT_ACCESS_T)));
                g_dtWrite = MergeDT(GetDtp(GetDlgItem(h, IDC_DT_WRITE_D)),
                                    GetDtp(GetDlgItem(h, IDC_DT_WRITE_T)));
            }
            EndDialog(h, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) { EndDialog(h, IDCANCEL); return TRUE; }
        break;
    }
    return FALSE;
}

static void RefreshDateCell(int i) {
    if (i < 0 || (size_t)i >= g_items.size() || !g_hList) return;
    std::wstring s = GetFileDate(g_items[i].fullPath);
    ListView_SetItemText(g_hList, i, 4, (LPWSTR)s.c_str());
}

static void DoChgDate() {
    std::vector<int> rows = CtxTargets(-1);
    if (rows.empty()) return;
    g_dtUseNow = false;
    // 初值取第一个目标文件现在的三个时间: 通常只想把其中一项挪一挪。
    ReadFileTimes(g_items[rows[0]].fullPath, g_dtCreate, g_dtAccess, g_dtWrite);
    if (DialogBoxW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_CHGDATE),
                   g_hDlg, ChgDateProc) != IDOK)
        return;

    if (g_dtUseNow) {
        SYSTEMTIME now;
        GetLocalTime(&now);
        g_dtCreate = g_dtAccess = g_dtWrite = now;
    }
    FILETIME fc, fa, fw;
    if (!LocalToUtc(g_dtCreate, fc) || !LocalToUtc(g_dtAccess, fa) || !LocalToUtc(g_dtWrite, fw)) {
        MessageBoxW(g_hDlg, LS(IDS_MSG_DATE_RANGE), LS(IDS_TITLE_CHGDATE),
                    MB_OK | MB_ICONERROR);
        return;
    }
    int done = 0, fail = 0;
    for (int i : rows) {
        HANDLE hf = CreateFileW(g_items[i].fullPath.c_str(), FILE_WRITE_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (hf == INVALID_HANDLE_VALUE) { ++fail; continue; }
        BOOL ok = SetFileTime(hf, &fc, &fa, &fw);
        CloseHandle(hf);
        if (!ok) { ++fail; continue; }
        ++done;
        RefreshDateCell(i);
    }
    wchar_t buf[128];
    swprintf_s(buf, 128, LS(IDS_MSG_DATE_DONE), done, fail);
    MessageBoxW(g_hDlg, buf, LS(IDS_TITLE_CHGDATE),
                MB_OK | (fail ? MB_ICONWARNING : MB_ICONINFORMATION));
}

// ----------------------------------------------------------------------------
// 赞助与支持（收款码 / 联系方式 / 误报免责声明）
// ----------------------------------------------------------------------------
// 收款码是打进 exe 的 JPEG（RCDATA，由 tools\prep_sponsor.cmd 生成）。解码用系统自带的
// gdiplus.dll：这里只声明用到的几个导出函数的原型，运行期 LoadLibrary + GetProcAddress
// 取地址 —— 不引 SDK 头文件、不额外链 gdiplus.lib，导入表也不多出东西。

namespace Gp {
struct Pic {
    void* img = NULL;
    unsigned w = 0, h = 0;
};

struct StartupInput {
    ULONG version;                        // 必须是 1
    void* debugEventCallback;
    BOOL suppressBackgroundThread;
    BOOL suppressExternalCodeManager;
};
typedef INT  (*StartupP)(ULONG_PTR* token, const StartupInput* input, void* hook);
typedef INT  (*FromStreamP)(IStream* stream, void** image);
typedef INT  (*DisposeP)(void* image);
typedef INT  (*SizeP)(void* image, unsigned* v);
typedef INT  (*FromHDCP)(HDC dc, void** graphics);
typedef INT  (*DeleteGP)(void* graphics);
typedef INT  (*DrawRectP)(void* graphics, void* image, int x, int y, int w, int h);

static void* pFromStream; static void* pDispose;
static void* pWidth;      static void* pHeight;
static void* pCreate;     static void* pDelete;  static void* pDraw;

// 装好导出函数地址（只成功一次即可，重复调用直接返回）。
static bool Ready() {
    static bool tried = false, ok = false;
    if (tried) return ok;
    tried = true;
    HMODULE m = LoadLibraryW(L"gdiplus.dll");
    if (!m) return false;
    StartupP startup = (StartupP)GetProcAddress(m, "GdiplusStartup");
    if (!startup) return false;
    ULONG_PTR token = 0;
    // suppressBackgroundThread 必须为 FALSE：实测（gdiplus 1.1.19041.3996）置 TRUE 时
    // 不同步传 GdiplusStartupOutput 就会返回 InvalidParameter，图就解不出来了。
    StartupInput in = { 1, NULL, FALSE, FALSE };
    if (startup(&token, &in, NULL) != 0) return false;
    // GDI+ 不 Shutdown 也能随进程退出回收；对话框可以反复打开，
    // 在一个实例退出时关掉整个库反而危险，所以这里只 Startup 一次。
    pFromStream = GetProcAddress(m, "GdipCreateBitmapFromStream");
    pDispose    = GetProcAddress(m, "GdipDisposeImage");
    pWidth      = GetProcAddress(m, "GdipGetImageWidth");
    pHeight     = GetProcAddress(m, "GdipGetImageHeight");
    pCreate     = GetProcAddress(m, "GdipCreateFromHDC");
    pDelete     = GetProcAddress(m, "GdipDeleteGraphics");
    pDraw       = GetProcAddress(m, "GdipDrawImageRectI");
    ok = pFromStream && pDispose && pWidth && pHeight && pCreate && pDelete && pDraw;
    return ok;
}

static bool Load(Pic& p, int resId) {
    if (!Ready()) return false;
    HRSRC r = FindResourceW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(resId), RT_RCDATA);
    if (!r) return false;
    DWORD size = SizeofResource(GetModuleHandleW(NULL), r);
    void* data = size ? LoadResource(GetModuleHandleW(NULL), r) : NULL;
    if (!data || !size) return false;
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hg) return false;
    memcpy(GlobalLock(hg), data, size);
    GlobalUnlock(hg);
    IStream* st = NULL;      // discard=TRUE: 流接管 hg，Release 时一起释放
    if (FAILED(CreateStreamOnHGlobal(hg, TRUE, &st)) || !st) { GlobalFree(hg); return false; }
    void* img = NULL;
    if (((FromStreamP)pFromStream)(st, &img) == 0 && img) {
        p.img = img;
        ((SizeP)pWidth)(img, &p.w);
        ((SizeP)pHeight)(img, &p.h);
    }
    st->Release();
    return p.img != NULL;
}

static void Free(Pic& p) {
    if (p.img) ((DisposeP)pDispose)(p.img);
    p.img = NULL; p.w = p.h = 0;
}

// 等比缩放居中画进 rc（收款码是白底 JPEG，先刷白再画）。
static void Draw(HDC dc, RECT rc, const Pic& p) {
    if (!p.img || !p.w || !p.h) return;
    FillRect(dc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
    int bw = rc.right - rc.left, bh = rc.bottom - rc.top;
    int w = bw, h = MulDiv(w, p.h, p.w);
    if (h > bh) { h = bh; w = MulDiv(h, p.w, p.h); }
    rc.left += (bw - w) / 2;
    rc.top += (bh - h) / 2;
    rc.right = rc.left + w;
    rc.bottom = rc.top + h;
    FrameRect(dc, &rc, (HBRUSH)GetStockObject(GRAY_BRUSH));
    void* g = NULL;
    if (((FromHDCP)pCreate)(dc, &g) != 0 || !g) return;
    ((DrawRectP)pDraw)(g, p.img, rc.left, rc.top, w, h);
    ((DeleteGP)pDelete)(g);
}
} // namespace Gp

static const wchar_t* const kSponsorRepo  = L"https://github.com/yjhua2005/AradeRenamer/";
static const wchar_t* const kSponsorQqGrp = L"484508921";
static const wchar_t* const kSponsorEmail = L"mailto:10683223@qq.com";
static const wchar_t* const kSponsorMailAddr = L"10683223@qq.com";

static Gp::Pic g_qr[3];      // 顺序: 微信 / 支付宝 / PayPal

static int QrResId(int i) {
    return i == 0 ? IDR_SPONSOR_WECHAT : (i == 1 ? IDR_SPONSOR_ALIPAY : IDR_SPONSOR_PAYPAL);
}

static void SponsorCopy(HWND h, const wchar_t* text) {
    size_t bytes = (wcslen(text) + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) return;
    memcpy(GlobalLock(mem), text, bytes);
    GlobalUnlock(mem);
    if (!OpenClipboard(h)) { GlobalFree(mem); return; }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);   // 失败才由我们释放
    CloseClipboard();
}

// 收款码占满上半部分，下面五行联系方式、免责声明框、确定按钮从底部倒着排，
// 所以窗口拉高只是码变大，不会把文字挤掉。
// 按钮宽度和免责声明框高度都按当前语言的实际文字量：英文文案比中文长一截，
// 写死尺寸会把 "Copy group ID" 截成 "opy group"。
static int SponsorTextW(HWND h, HWND ctl, int pad) {
    if (!ctl) return pad;
    HDC dc = GetDC(h);
    wchar_t t[128] = { 0 };
    GetWindowTextW(ctl, t, 128);
    HFONT f = (HFONT)SendMessageW(ctl, WM_GETFONT, 0, 0);
    HFONT old = f ? (HFONT)SelectObject(dc, f) : NULL;
    SIZE sz = { 0, 0 };
    GetTextExtentPoint32W(dc, t, (int)wcslen(t), &sz);
    if (old) SelectObject(dc, old);
    ReleaseDC(h, dc);
    return sz.cx + pad;
}

static int SponsorTextH(HWND h, HWND ctl, int wrapW) {
    if (!ctl) return 0;
    HDC dc = GetDC(h);
    wchar_t t[512] = { 0 };
    GetWindowTextW(ctl, t, 512);
    HFONT f = (HFONT)SendMessageW(ctl, WM_GETFONT, 0, 0);
    HFONT old = f ? (HFONT)SelectObject(dc, f) : NULL;
    RECT r = { 0, 0, wrapW, 0 };
    DrawTextW(dc, t, -1, &r, DT_CALCRECT | DT_LEFT | DT_WORDBREAK);
    if (old) SelectObject(dc, old);
    ReleaseDC(h, dc);
    return r.bottom;
}

static void SponsorLayout(HWND h, const RECT& c) {
    const int mg = S(12), gap = S(10), rowH = S(18), rowGap = S(4), btnMin = S(64);
    const int btnH = S(22);
    HWND copyBtn = GetDlgItem(h, IDC_SP_COPY_QQ), okBtn = GetDlgItem(h, IDOK);
    int btnW = SponsorTextW(h, copyBtn, S(20));
    if (btnW < btnMin) btnW = btnMin;
    int okW = SponsorTextW(h, okBtn, S(24));
    if (okW < btnMin) okW = btnMin;
    int txtW = c.right - 2 * mg - S(16);
    int discH = SponsorTextH(h, GetDlgItem(h, IDC_SP_DISC_TEXT), txtW) + S(20);
    if (discH < S(58)) discH = S(58);
    int by = c.bottom - mg - btnH;
    int discY = by - S(6) - discH;
    int rowsY = discY - S(8) - (rowH * 5 + rowGap * 4);
    int qrW = (c.right - 2 * mg - 2 * gap) / 3;
    int qrH = rowsY - S(8) - mg;
    if (qrH < S(60)) qrH = S(60);
    for (int i = 0; i < 3; ++i) {
        HWND q = GetDlgItem(h, IDC_SP_QR_WX + i);
        MoveWindow(q, mg + i * (qrW + gap), mg, qrW, qrH, TRUE);
        // Static 类没有 CS_HREDRAW/CS_VREDRAW，放大时系统只失效新增的那块，
        // 旧像素会留在原处叠成新图，所以自己把整个客户区作废。
        InvalidateRect(q, NULL, TRUE);
    }
    int y = rowsY;
    MoveWindow(GetDlgItem(h, IDC_SP_GITHUB),  mg, y, c.right - 2 * mg, rowH, TRUE); y += rowH + rowGap;
    MoveWindow(GetDlgItem(h, IDC_SP_QQ),      mg, y, c.right - 2 * mg - btnW - S(6), rowH, TRUE);
    MoveWindow(copyBtn, c.right - mg - btnW, y - S(2), btnW, btnH, TRUE);
    y += rowH + rowGap;
    MoveWindow(GetDlgItem(h, IDC_SP_AUTHOR),  mg, y, c.right - 2 * mg, rowH, TRUE); y += rowH + rowGap;
    MoveWindow(GetDlgItem(h, IDC_SP_EMAIL),   mg, y, c.right - 2 * mg, rowH, TRUE); y += rowH + rowGap;
    MoveWindow(GetDlgItem(h, IDC_SP_LIC),     mg, y, c.right - 2 * mg, rowH, TRUE);
    MoveWindow(GetDlgItem(h, IDC_SP_DISCLAIMER), mg, discY, c.right - 2 * mg, discH, TRUE);
    MoveWindow(GetDlgItem(h, IDC_SP_DISC_TEXT), mg + S(8), discY + S(8), txtW, discH - S(14), TRUE);
    MoveWindow(okBtn, c.right - mg - okW, by, okW, btnH, TRUE);
    // 一次性把父窗和所有子控件全部重绘：Static/Button 都没有 CS_*REDRAW，
    // 控件移动后旧位置的像素会留在原地当残影。
    RedrawWindow(h, NULL, NULL,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

// 收款码格子和三行可点链接都要自己画。开了视觉样式后，SS_OWNERDRAW 的 Static 并不会
// 把 WM_DRAWITEM 发到父窗口（实测收不到），所以给这几个控件装子类化过程自己处理
// WM_PAINT / 点击，普通文字行仍交给 Static 默认绘制。
enum { SPK_QR = 1, SPK_REPO, SPK_QQ, SPK_MAIL };

static HWND g_spFlash;      // 正在显示「已复制」的那一行，NULL 表示没有

static void SponsorPaintQr(HWND c, HDC dc, RECT r, int idx) {
    FillRect(dc, &r, (HBRUSH)(COLOR_BTNFACE + 1));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    wchar_t name[64] = { 0 };
    GetWindowTextW(c, name, 64);
    int labH = S(16);
    RECT lr = { r.left, r.bottom - labH, r.right, r.bottom };
    RECT img = r;
    img.bottom -= labH;
    if (g_qr[idx].img) {
        Gp::Draw(dc, img, g_qr[idx]);
    } else {
        // 没跑过 tools\prep_sponsor.cmd 就没图可画：给出提示而不是画空白。
        DrawTextW(dc, LS(IDS_SP_NO_IMG), -1, &img, DT_CENTER | DT_WORDBREAK);
    }
    DrawTextW(dc, name, -1, &lr, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

static void SponsorPaintLink(HWND c, HDC dc, RECT r) {
    wchar_t txt[512] = { 0 };
    if (c == g_spFlash) {
        // 刚点过：整行换成「已复制」，1.8 秒后定时器回来还原。
        wcscpy_s(txt, LS(IDS_SP_COPIED));
    } else {
        GetWindowTextW(c, txt, 512);
    }
    FillRect(dc, &r, (HBRUSH)(COLOR_BTNFACE + 1));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 190));          // 和主界面里提示性标签同一种蓝
    DrawTextW(dc, txt, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

// ----------------------------------------------------------------------------
// 把链接交给系统默认程序（浏览器 / 邮件客户端）
// ----------------------------------------------------------------------------
// 之前就一句 ShellExecuteW(NULL, L"open", url, ...)，在这台机器上有两个坑：
//   1) mailto 的 UrlAssociations 指向 foxmail.Url.mailto，它的 open\command 是
//      "D:\Program Files\Foxmail 7.2\Foxmail.exe" %1 —— Foxmail 早就不在那个目录了。
//      ShellExecute 找不到程序只返回一个失败码，界面上一点反应都没有（表现就是
//      「点了邮箱没动静」）；而且它还会先走一遍 DDE 握手再超时，实测能卡 35 秒。
//   2) 浏览器那条 command 里带 "--" 之类的开关，经由 ShellExecute 的 DDE 转发也不稳。
//
// 所以改成自己照 Windows 的关联规则查注册表拿命令行，把 %1 换成链接后直接
// CreateProcess：不走 DDE 就不会卡，程序不在了我们能立刻知道并换候补。
// 仍然放后台线程跑（万一还有别的坏关联会卡住），失败时发消息回对话框提示用户。
// 另外点的时候先把地址塞进剪贴板并在该行闪一下「已复制」——打不开时用户手里
// 至少还有内容。

#define WM_SP_OPEN_FAILED (WM_APP + 1)      // wparam: 0=网页 1=邮箱

static bool RegGetStr(HKEY root, const wchar_t* sub, const wchar_t* val, std::wstring& out) {
    wchar_t buf[1024];
    DWORD n = sizeof(buf);
    // val 传 NULL 取的是该项的默认值。
    if (RegGetValueW(root, sub, val, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
                     NULL, buf, &n) != ERROR_SUCCESS) return false;
    out.assign(buf);
    return !out.empty();
}

// 协议 (http/https/mailto) -> 默认程序的命令行模板：先看用户选的 UserChoice，再看 HKCR 里
// 协议自己的键。
static bool AssocCommand(const wchar_t* scheme, std::wstring& cmd) {
    std::wstring base = std::wstring(L"Software\\Microsoft\\Windows\\Shell\\Associations\\"
                                     L"UrlAssociations\\") + scheme + L"\\UserChoice";
    std::wstring prog;
    if (RegGetStr(HKEY_CURRENT_USER, base.c_str(), L"Progid", prog)) {
        std::wstring k = prog + L"\\shell\\open\\command";
        if (RegGetStr(HKEY_CLASSES_ROOT, k.c_str(), NULL, cmd)) return true;
    }
    std::wstring k = std::wstring(scheme) + L"\\shell\\open\\command";
    return RegGetStr(HKEY_CLASSES_ROOT, k.c_str(), NULL, cmd);
}

// mailto 关联坏掉时的候补：系统登记的「默认邮件客户端」（SOFTWARE\Clients\Mail 下
// 那一项，这台机器上是 Microsoft Outlook，Outlook 天生就收 mailto: 开头的参数）。
static bool MailClientCommand(std::wstring& cmd) {
    const HKEY roots[2] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    for (HKEY root : roots) {
        std::wstring name;
        if (!RegGetStr(root, L"Software\\Clients\\Mail", NULL, name)) continue;
        std::wstring p = L"Software\\Clients\\Mail\\" + name +
                         L"\\Protocols\\mailto\\shell\\open\\command";
        if (RegGetStr(root, p.c_str(), NULL, cmd)) return true;
        p = L"Software\\Clients\\Mail\\" + name + L"\\shell\\open\\command";
        if (RegGetStr(root, p.c_str(), NULL, cmd)) return true;
    }
    return false;
}

// 把模板里的 %1 换成链接（模板没给引号就自己加），顺便拆出可执行文件全路径。
static void BuildCommandLine(const std::wstring& tpl, const wchar_t* url,
                             std::wstring& exe, std::wstring& cl) {
    const std::wstring quoted = std::wstring(L"\"") + url + L"\"";
    std::wstring out;
    bool sub = false;
    for (size_t i = 0; i < tpl.size();) {
        if (tpl[i] == L'%' && i + 1 < tpl.size() && tpl[i + 1] == L'1') {
            bool already = i > 0 && tpl[i - 1] == L'"' && i + 2 < tpl.size() && tpl[i + 2] == L'"';
            out += already ? url : quoted;
            i += 2;
            sub = true;
            continue;
        }
        out += tpl[i++];
    }
    if (!sub) { out += L' '; out += quoted; }
    cl = out;
    exe.clear();
    if (!cl.empty() && cl[0] == L'"') {              // 带引号的路径：取到下一个引号
        size_t e = cl.find(L'"', 1);
        if (e != std::wstring::npos) exe = cl.substr(1, e - 1);
    } else {                                          // 不带引号：取到第一个空格
        size_t e = cl.find(L' ');
        exe = (e == std::wstring::npos) ? cl : cl.substr(0, e);
    }
}

// 只在 mailto 关联坏掉（指向已卸载的程序）时用。
// 千万不要拿它去开 http/https：Chromium 系浏览器（QQ 浏览器、Chrome、Edge）已经在跑的时候，
// 这样硬起一个新进程会因为实例/profile 冲突直接 DebugBreak 崩掉——退出码 0x80000003
// (STATUS_BREAKPOINT)，界面上表现就是浏览器弹一个错误报告对话框，页面还打不开。
// 交给 ShellExecuteEx 才能走通系统「把 URL 递给已运行实例」的那条路。
static bool LaunchCommand(const std::wstring& tpl, const wchar_t* url) {
    std::wstring exe, cl;
    BuildCommandLine(tpl, url, exe, cl);
    if (exe.empty()) return false;
    // 关联指向的程序已经被卸载（这台机器上的 Foxmail 就是这样）——先查存在性，
    // 不然「程序没了」和 CreateProcess 失败分不清，也没法往下换候补。
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNORMAL;
    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> buf(cl.begin(), cl.end());
    buf.push_back(L'\0');                             // CreateProcessW 的命令行要可写
    if (!CreateProcessW(exe.c_str(), buf.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// 打开链接的正路：交给系统。别自己拼命令行起进程，也别加 SEE_MASK_NOCLOSEPROCESS——
// 那个 flag 会逼着 ShellExecute 走 CreateProcess，绕过「递给已运行实例」的机制，
// 结果是浏览器崩在 0x80000003。SEE_MASK_FLAG_NO_UI 只是让系统别弹它自己的错误框，
// 失败了我们自己提示（地址那时候已经进剪贴板了）。
static bool LaunchShell(const wchar_t* url) {
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = url;
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) && (INT_PTR)sei.hInstApp > 32;
}

// 关联是否指向一个真实存在的程序。坏的关联（程序已卸载）会让 ShellExecute 在内部
// 反复重试、DDE 握手超时，实测能把调用卡住三十几秒。
static bool AssocHealthy(const wchar_t* scheme) {
    std::wstring cmd, exe, cl;
    if (!AssocCommand(scheme, cmd)) return false;
    BuildCommandLine(cmd, L"x", exe, cl);
    return !exe.empty() && GetFileAttributesW(exe.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static bool OpenUrl(const wchar_t* url) {
    std::wstring scheme;
    for (const wchar_t* p = url; *p && *p != L':'; ++p) scheme += *p;
    // mailto 是唯一需要特殊照顾的：默认关联可能指向卸载干净了的客户端，直接交给系统
    // 会卡死。先确认关联是好的，好的就走系统；坏了才自己按 SOFTWARE\Clients\Mail
    // 找候补程序来起。http/https 一律交给系统，浏览器才能正常接到 URL。
    if (scheme == L"mailto") {
        if (AssocHealthy(L"mailto")) return LaunchShell(url);
        std::wstring fb;
        if (MailClientCommand(fb) && LaunchCommand(fb, url)) return true;
    }
    return LaunchShell(url);
}

struct SponsorJob { HWND dlg; int failKind; wchar_t url[256]; };

static DWORD WINAPI SponsorJobThread(LPVOID arg) {
    SponsorJob* job = static_cast<SponsorJob*>(arg);
    // 兜底那条 ShellExecuteEx 要 COM；后台线程默认没初始化，这里补上。
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool ok = OpenUrl(job->url);
    CoUninitialize();
    if (!ok) PostMessageW(job->dlg, WM_SP_OPEN_FAILED, (WPARAM)job->failKind, 0);
    delete job;
    return 0;
}

static void SponsorLaunch(HWND dlg, HWND row, const wchar_t* url, const wchar_t* copyText,
                          int failKind) {
    SponsorCopy(dlg, copyText);
    g_spFlash = row;
    InvalidateRect(row, NULL, TRUE);
    SetTimer(dlg, 2, 1800, NULL);
    SponsorJob* job = new SponsorJob();
    job->dlg = dlg;
    job->failKind = failKind;
    wcscpy_s(job->url, url);
    HANDLE t = CreateThread(NULL, 0, SponsorJobThread, job, 0, NULL);
    if (t) CloseHandle(t); else delete job;
}

static void SponsorOpen(HWND dlg, int kind) {
    if (kind == SPK_REPO) {
        SponsorLaunch(dlg, GetDlgItem(dlg, IDC_SP_GITHUB), kSponsorRepo, kSponsorRepo, 0);
    } else if (kind == SPK_MAIL) {
        SponsorLaunch(dlg, GetDlgItem(dlg, IDC_SP_EMAIL), kSponsorEmail, kSponsorMailAddr, 1);
    } else if (kind == SPK_QQ) {
        SponsorCopy(dlg, kSponsorQqGrp);
        SetDlgItemTextW(dlg, IDC_SP_COPY_QQ, LS(IDS_SP_COPIED));
        SetTimer(dlg, 1, 1500, NULL);          // 定时器回来还原按钮原文字
    }
}

static LRESULT CALLBACK SponsorSubProc(HWND c, UINT msg, WPARAM wp, LPARAM lp,
                                       UINT_PTR, DWORD_PTR kind) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(c, &ps);
        RECT r; GetClientRect(c, &r);
        HFONT f = (HFONT)SendMessageW(c, WM_GETFONT, 0, 0);
        HFONT old = f ? (HFONT)SelectObject(dc, f) : NULL;
        if (kind == SPK_QR) SponsorPaintQr(c, dc, r, GetDlgCtrlID(c) - IDC_SP_QR_WX);
        else SponsorPaintLink(c, dc, r);
        if (old) SelectObject(dc, old);
        EndPaint(c, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;                    // WM_PAINT 里已经刷满，避免闪一下默认背景
    case WM_LBUTTONUP:
        if (kind != SPK_QR) {
            POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
            RECT r; GetClientRect(c, &r);
            if (PtInRect(&r, pt)) SponsorOpen(GetParent(c), (int)kind);
            return 0;
        }
        break;
    case WM_SETCURSOR:
        if (kind != SPK_QR) {
            SetCursor(LoadCursorW(NULL, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_DESTROY:
        RemoveWindowSubclass(c, SponsorSubProc, 0);
        break;
    }
    return DefSubclassProc(c, msg, wp, lp);
}

static void SponsorSubclass(HWND h) {
    const UINT_PTR kinds[] = { SPK_QR, SPK_QR, SPK_QR, SPK_REPO, SPK_QQ, SPK_MAIL };
    const int ids[] = { IDC_SP_QR_WX, IDC_SP_QR_ALI, IDC_SP_QR_PP,
                        IDC_SP_GITHUB, IDC_SP_QQ, IDC_SP_EMAIL };
    for (int i = 0; i < 6; ++i) {
        HWND c = GetDlgItem(h, ids[i]);
        if (c) SetWindowSubclass(c, SponsorSubProc, 0, kinds[i]);
    }
}

static INT_PTR CALLBACK SponsorProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        ApplyLanguage(h, IDS_DLG_SPONSOR);
        const int CW = 460, CH = 380;
        RECT wr = { 0, 0, S(CW), S(CH) };
        AdjustWindowRectEx(&wr, (DWORD)GetWindowLongPtrW(h, GWL_STYLE), FALSE,
                           (DWORD)GetWindowLongPtrW(h, GWL_EXSTYLE));
        int w = wr.right - wr.left, ht = wr.bottom - wr.top;
        int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
        HWND owner = GetWindow(h, GW_OWNER);
        if (owner) {
            RECT o; GetWindowRect(owner, &o);
            x = o.left + ((o.right - o.left) - w) / 2;
            y = o.top + ((o.bottom - o.top) - ht) / 2;
        } else {
            x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
            y = (GetSystemMetrics(SM_CYSCREEN) - ht) / 2;
        }
        SetWindowPos(h, NULL, x, y, w, ht, SWP_NOZORDER);
        for (int i = 0; i < 3; ++i) Gp::Load(g_qr[i], QrResId(i));
        SponsorSubclass(h);
        return FALSE;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = S(380);
        mmi->ptMinTrackSize.y = S(330);
        return FALSE;
    }
    case WM_SIZE: {
        RECT c; GetClientRect(h, &c);
        SponsorLayout(h, c);
        return FALSE;
    }
    case WM_COMMAND: {
        switch (LOWORD(wp)) {
        case IDC_SP_COPY_QQ:
            SponsorOpen(h, SPK_QQ);
            return TRUE;
        case IDOK: case IDCANCEL:
            EndDialog(h, IDOK); return TRUE;
        }
        break;
    }
    case WM_TIMER:
        if (wp == 1) {
            KillTimer(h, 1);
            SetDlgItemTextW(h, IDC_SP_COPY_QQ, LS(IDC_SP_COPY_QQ));
            return TRUE;
        }
        if (wp == 2) {                       // 「已复制」闪一下后还原那一行
            KillTimer(h, 2);
            HWND f = g_spFlash;
            g_spFlash = NULL;
            if (f) InvalidateRect(f, NULL, TRUE);
            return TRUE;
        }
        break;
    case WM_SP_OPEN_FAILED:
        // 后台线程一个程序都没拉起来（比如 mailto 关联指向的 Foxmail 已经不在了）。
        // 地址在点的时候已经复制进剪贴板，这里只把情况说清楚。
        MessageBoxW(h, wp ? LS(IDS_SP_MAIL_NONE)
                          : LS(IDS_SP_OPEN_FAIL),
                    LS(IDS_DLG_SPONSOR), MB_OK | MB_ICONINFORMATION);
        return TRUE;
    case WM_CLOSE:
        EndDialog(h, IDOK);
        return TRUE;
    case WM_DESTROY:
        KillTimer(h, 2);
        g_spFlash = NULL;                    // 后台线程还在跑，但窗口没了就别再引用它的控件
        for (int i = 0; i < 3; ++i) Gp::Free(g_qr[i]);
        return FALSE;
    }
    return FALSE;
}

// ----------------------------------------------------------------------------
// 关于 / 开源协议
// ----------------------------------------------------------------------------
// 许可证全文和字典都走 conv.cpp 的 LoadResText（上游文件按 UTF-8 原样打进 exe）。

static INT_PTR CALLBACK LicenseProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        // 许可证全文很长，所以窗口给足高度并允许拖动改变大小；坐标同样按 DPI 重排。
        const int CW = 460, CH = 340;
        RECT wr = { 0, 0, S(CW), S(CH) };
        AdjustWindowRectEx(&wr, (DWORD)GetWindowLongPtrW(h, GWL_STYLE), FALSE,
                           (DWORD)GetWindowLongPtrW(h, GWL_EXSTYLE));
        int w = wr.right - wr.left, ht = wr.bottom - wr.top;
        HWND owner = GetWindow(h, GW_OWNER);
        int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
        if (owner) {
            RECT o; GetWindowRect(owner, &o);
            x = o.left + ((o.right - o.left) - w) / 2;
            y = o.top + ((o.bottom - o.top) - ht) / 2;
        } else {
            x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
            y = (GetSystemMetrics(SM_CYSCREEN) - ht) / 2;
        }
        SetWindowPos(h, NULL, x, y, w, ht, SWP_NOZORDER);

        std::wstring t;
        t += LS(IDS_LIC_ABOUT_TITLE);
        t += LS(IDS_MIT_H);
        t += LS(IDS_MIT_URL);
        t += LS(IDS_MIT_LIC);
        t += LS(IDS_MIT_NOTE);
        t += LS(IDS_LIC_OPENCC_H);
        t += LS(IDS_LIC_OPENCC_PROJ);
        t += LS(IDS_LIC_OPENCC_URL);
        t += LS(IDS_LIC_OPENCC_LIC);
        t += LS(IDS_LIC_OPENCC_USE);
        t += LS(IDS_LIC_OPENCC_USE2);
        t += LS(IDS_LIC_OPENCC_FULL);
        t += LoadResText(IDR_LIC_OPENCC);
        t += L"\r\n\r\n";
        t += LS(IDS_LIC_PINYIN_H);
        t += LS(IDS_LIC_PINYIN_PROJ);
        t += LS(IDS_LIC_PINYIN_URL);
        t += LS(IDS_LIC_PINYIN_LIC);
        t += LS(IDS_LIC_PINYIN_USE);
        t += LS(IDS_LIC_PINYIN_FULL);
        t += LoadResText(IDR_LIC_PINYIN);
        SetDlgItemTextW(h, IDC_LIC_TEXT, t.c_str());
        SendMessageW(GetDlgItem(h, IDC_LIC_TEXT), EM_SETSEL, 0, 0);
        return FALSE;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = S(320);
        mmi->ptMinTrackSize.y = S(200);
        return FALSE;
    }
    case WM_SIZE: {
        RECT c; GetClientRect(h, &c);
        const int bH = S(20), mg = S(8);
        MoveWindow(GetDlgItem(h, IDC_LIC_TEXT), mg, mg,
                   c.right - 2 * mg, c.bottom - mg - bH - S(6), TRUE);
        MoveWindow(GetDlgItem(h, IDOK), c.right - mg - S(62), c.bottom - mg - bH,
                   S(62), bH, TRUE);
        return FALSE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) { EndDialog(h, IDOK); return TRUE; }
        break;
    case WM_CLOSE:
        EndDialog(h, IDOK);
        return TRUE;
    }
    return FALSE;
}

static INT_PTR CALLBACK AboutProc(HWND h, UINT msg, WPARAM wp, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG: {
        ApplyLanguage(h, IDS_DLG_ABOUT);
        // 按钮宽度跟文字走: 中文「赞助/联系」100px 够用, 英文 "Sponsor / contact" 和
        // "Open source license" 会截断, 所以先量实际文字再摆; 三个按钮排不下时加宽
        // 整个对话框, 标题和说明文字一起拉宽。
        auto wantW = [](HWND b, int minW) {
            wchar_t t[128] = { 0 };
            GetWindowTextW(b, t, 128);
            HDC dc = GetDC(b);
            HFONT of = (HFONT)SelectObject(dc, (HFONT)SendMessageW(b, WM_GETFONT, 0, 0));
            SIZE sz = { 0, 0 };
            GetTextExtentPoint32W(dc, t, (int)wcslen(t), &sz);
            SelectObject(dc, of);
            ReleaseDC(b, dc);
            int need = sz.cx + GetSystemMetrics(SM_CXEDGE) * 4 + S(14);  // 按钮内边距
            return need > minW ? need : minW;
        };
        int spW  = wantW(GetDlgItem(h, IDC_ABOUT_SPONSOR), S(100));
        int licW = wantW(GetDlgItem(h, IDC_ABOUT_LICENSE), S(100));
        int okW  = wantW(GetDlgItem(h, IDOK),              S(74));
        const int CH = 272, gap = S(10), edge = S(18), btnH = S(26);
        int total = spW + gap + licW + gap + okW;
        int cw = S(392);
        if (edge + total + edge > cw) cw = edge + total + edge;
        RECT wr = { 0, 0, cw, S(CH) };
        AdjustWindowRectEx(&wr, (DWORD)GetWindowLongPtrW(h, GWL_STYLE), FALSE,
                           (DWORD)GetWindowLongPtrW(h, GWL_EXSTYLE));
        int w = wr.right - wr.left, ht = wr.bottom - wr.top;
        HWND owner = GetWindow(h, GW_OWNER);
        int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
        if (owner) {
            RECT o; GetWindowRect(owner, &o);
            x = o.left + ((o.right - o.left) - w) / 2;
            y = o.top + ((o.bottom - o.top) - ht) / 2;
        } else {
            x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
            y = (GetSystemMetrics(SM_CYSCREEN) - ht) / 2;
        }
        SetWindowPos(h, NULL, x, y, w, ht, SWP_NOZORDER);
        MoveWindow(GetDlgItem(h, IDC_ABOUT_ICON),  S(18),  S(18),  S(40),  S(40), TRUE);
        MoveWindow(GetDlgItem(h, IDC_ABOUT_TITLE), S(68),  S(24),  cw - S(68) - edge, S(22), TRUE);
        MoveWindow(GetDlgItem(h, IDC_ABOUT_TEXT),  edge,   S(72),  cw - edge * 2, S(144), TRUE);
        int bx = cw - edge - okW;                                       // 右对齐, 往左排
        MoveWindow(GetDlgItem(h, IDOK),            bx, S(232), okW,  btnH, TRUE);
        bx -= gap + licW;
        MoveWindow(GetDlgItem(h, IDC_ABOUT_LICENSE), bx, S(232), licW, btnH, TRUE);
        bx -= gap + spW;
        MoveWindow(GetDlgItem(h, IDC_ABOUT_SPONSOR), bx, S(232), spW,  btnH, TRUE);
        SetDlgItemTextW(h, IDC_ABOUT_TEXT,
            LS(IDS_ABOUT_TEXT));
        return FALSE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_ABOUT_LICENSE) {
            DialogBoxW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_LICENSE), h, LicenseProc);
            return TRUE;
        }
        if (LOWORD(wp) == IDC_ABOUT_SPONSOR) {
            DialogBoxW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_SPONSOR), h, SponsorProc);
            return TRUE;
        }
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) { EndDialog(h, IDOK); return TRUE; }
        break;
    }
    return FALSE;
}

// ----------------------------------------------------------------------------
// 标签写入：保存 = 打勾/选中的行，全写 = 列表里的每一行
// ----------------------------------------------------------------------------
static AudioTags TagFieldsFromControls() {
    AudioTags t;
    t.artist  = GetText(IDC_TW_ARTIST);
    t.title   = GetText(IDC_TW_TITLE);
    t.album   = GetText(IDC_TW_ALBUM);
    t.track   = GetText(IDC_TW_TRACK);
    t.year    = GetText(IDC_TW_YEAR);
    t.genre   = GetText(IDC_TW_GENRE);
    t.comment = GetText(IDC_TW_COMMENT);
    return t;
}

static void SetResultCell(int row, const std::wstring& s) {
    if (!g_hList || row < 0 || (size_t)row >= g_items.size()) return;
    std::wstring txt = s;      // 宏会在整条语句结束后才 SendMessage，必须用有名字的对象
    ListView_SetItemText(g_hList, row, 8, (LPWSTR)txt.c_str());
}

static void DoTagWrite(bool allRows) {
    if (!IsChecked(IDC_TW_ENABLE)) {
        MessageBoxW(g_hDlg, LS(IDS_MSG_TAGWRITE_OFF),
                    LS(IDS_TITLE_TAGWRITE), MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::vector<int> rows;
    if (allRows) {
        for (size_t i = 0; i < g_items.size(); ++i) rows.push_back((int)i);
    } else {
        rows = CtxTargets(-1);
    }
    if (rows.empty()) {
        MessageBoxW(g_hDlg, LS(IDS_MSG_TAGWRITE_NOROWS),
                    LS(IDS_TITLE_TAGWRITE), MB_OK | MB_ICONINFORMATION);
        return;
    }

    AudioTags base = TagFieldsFromControls();
    std::wstring expr = GetText(IDC_TW_EXPR);
    if (expr.empty() && !base.HasAny()) {
        MessageBoxW(g_hDlg, LS(IDS_MSG_TAGWRITE_NOCONTENT),
                    LS(IDS_TITLE_TAGWRITE), MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (g_set.tagPrompt) {
        wchar_t q[128];
        swprintf_s(q, 128, LS(IDS_MSG_TAGWRITE_CONFIRM),
                    (int)rows.size());
        if (MessageBoxW(g_hDlg, q, LS(IDS_TITLE_TAGWRITE), MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
    }

    SendMessageW(g_hList, WM_SETREDRAW, FALSE, 0);
    int done = 0, fail = 0, skip = 0;
    for (int i : rows) {
        std::error_code ec;
        if (fs::is_directory(g_items[i].fullPath, ec)) { SetResultCell(i, LS(IDS_RES_SKIP_DIR)); ++skip; continue; }

        AudioTags t = base;
        std::wstring res = LS(IDS_RES_WRITTEN);
        // 文件名表达式：按每个文件的名字单独解析，页面上填的项作为没解析出来的兜底。
        if (!expr.empty()) {
            std::wstring stem = g_items[i].fullPath.stem().wstring();
            if (!ParseNameToTags(expr, stem, t)) { SetResultCell(i, LS(IDS_RES_NAME_MISMATCH)); ++skip; continue; }
        }
        if (g_set.trackPad2 && t.track.size() == 1 && t.track[0] >= L'0' && t.track[0] <= L'9')
            t.track = L'0' + t.track;          // 音轨补 0 (设置 > 音乐文件标签)
        if (g_set.keepBlankTag) {              // 留空的项沿用文件里原来的标签
            EnsureTags(g_items[i]);
            if (t.artist.empty())  t.artist  = g_items[i].tags.artist;
            if (t.title.empty())   t.title   = g_items[i].tags.title;
            if (t.album.empty())   t.album   = g_items[i].tags.album;
            if (t.track.empty())   t.track   = g_items[i].tags.track;
            if (t.year.empty())    t.year    = g_items[i].tags.year;
            if (t.genre.empty())   t.genre   = g_items[i].tags.genre;
            if (t.comment.empty()) t.comment = g_items[i].tags.comment;
        }
        if (g_set.tagBackup) {
            fs::path bak = g_items[i].fullPath;
            bak += L".bak";
            if (!CopyFileW(g_items[i].fullPath.c_str(), bak.c_str(), FALSE)) {
                SetResultCell(i, LS(IDS_RES_BAK_FAIL)); ++fail; continue;
            }
        }
        // 写入会重写整个文件，时间戳会被系统刷新，所以先记下来再还原。
        SYSTEMTIME c0, a0, w0;
        if (g_set.keepFileDate) ReadFileTimes(g_items[i].fullPath, c0, a0, w0);

        int rc = WriteAudioTags(g_items[i].fullPath, t, g_set.writeMode);
        if (rc == 1) { SetResultCell(i, LS(IDS_RES_UNSUPPORTED)); ++skip; continue; }
        if (rc != 0) { SetResultCell(i, LS(IDS_RES_WRITE_FAIL)); ++fail; continue; }
        if (g_set.keepFileDate) {
            FILETIME fc, fa, fw;
            HANDLE hf = CreateFileW(g_items[i].fullPath.c_str(), FILE_WRITE_ATTRIBUTES,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
            if (hf != INVALID_HANDLE_VALUE) {
                if (LocalToUtc(c0, fc) && LocalToUtc(a0, fa) && LocalToUtc(w0, fw))
                    SetFileTime(hf, &fc, &fa, &fw);
                CloseHandle(hf);
            }
        }
        g_items[i].tagsRead = false;
        EnsureTags(g_items[i]);
        SetResultCell(i, res);
        ++done;
    }
    SendMessageW(g_hList, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(g_hList, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
    AutoPreview();        // 标签命名 用的就是这些标签，写完预览要跟着变
    UpdateTagPanel();   // 选中的那行标签已经变了

    wchar_t buf[128];
    swprintf_s(buf, 128, LS(IDS_MSG_TAGWRITE_DONE), done, skip, fail);
    MessageBoxW(g_hDlg, buf, LS(IDS_TITLE_TAGWRITE),
                MB_OK | (fail ? MB_ICONWARNING : MB_ICONINFORMATION));
}

// ----------------------------------------------------------------------------
// 标签命令公共外壳: 打勾/选中的行 -> (可选)备份 -> op -> 还原文件日期 -> 结果列 -> 汇总框。
// op 返回引擎返回码: 0 成功, 1 跳过(不支持的格式), 2 失败, 3 无需处理。
// 备份 / 保留文件日期 / 消息框都和 标签写入 共用同一套设置, 行为一致。
// ----------------------------------------------------------------------------
static void RunTagRows(const wchar_t* what, const wchar_t* okText,
                       const std::function<int(const fs::path&, int)>& op) {
    std::vector<int> rows = CtxTargets(-1);
    if (rows.empty()) {
        MessageBoxW(g_hDlg, LS(IDS_MSG_TAGOP_NOROWS),
                    what, MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (g_set.tagPrompt) {
        wchar_t q[128];
        swprintf_s(q, 128, LS(IDS_MSG_TAGOP_CONFIRM), (int)rows.size());
        if (MessageBoxW(g_hDlg, q, what, MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
    }

    int done = 0, fail = 0, skip = 0;
    SendMessageW(g_hList, WM_SETREDRAW, FALSE, 0);
    for (int i : rows) {
        std::error_code ec;
        if (fs::is_directory(g_items[i].fullPath, ec)) { SetResultCell(i, LS(IDS_RES_SKIP_DIR)); ++skip; continue; }
        if (g_set.tagBackup) {
            fs::path bak = g_items[i].fullPath;
            bak += L".bak";
            if (!CopyFileW(g_items[i].fullPath.c_str(), bak.c_str(), FALSE)) {
                SetResultCell(i, LS(IDS_RES_BAK_FAIL)); ++fail; continue;
            }
        }
        SYSTEMTIME c0, a0, w0;
        if (g_set.keepFileDate) ReadFileTimes(g_items[i].fullPath, c0, a0, w0);

        int rc = op(g_items[i].fullPath, i);
        if (rc == 3) { SetResultCell(i, LS(IDS_RES_NOTHING)); ++skip; continue; }
        if (rc == 1) { SetResultCell(i, LS(IDS_RES_UNSUPPORTED)); ++skip; continue; }
        if (rc != 0) { SetResultCell(i, LS(IDS_RES_OP_FAIL)); ++fail; continue; }
        if (g_set.keepFileDate) {
            FILETIME fc, fa, fw;
            HANDLE hf = CreateFileW(g_items[i].fullPath.c_str(), FILE_WRITE_ATTRIBUTES,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
            if (hf != INVALID_HANDLE_VALUE) {
                if (LocalToUtc(c0, fc) && LocalToUtc(a0, fa) && LocalToUtc(w0, fw))
                    SetFileTime(hf, &fc, &fa, &fw);
                CloseHandle(hf);
            }
        }
        g_items[i].tagsRead = false;
        EnsureTags(g_items[i]);
        SetResultCell(i, okText);
        ++done;
    }
    SendMessageW(g_hList, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(g_hList, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
    AutoPreview();        // 标签命名 用的就是这些标签，处理完预览要跟着变
    UpdateTagPanel();

    wchar_t buf[128];
    swprintf_s(buf, 128, LS(IDS_MSG_TAGOP_DONE), done, skip, fail);
    MessageBoxW(g_hDlg, buf, what, MB_OK | (fail ? MB_ICONWARNING : MB_ICONINFORMATION));
}

// 标签字段的内码转换: 修"按错误代码页误读"的标签值。
static std::wstring EncConvText(const std::wstring& s, int mode) {
    switch (mode) {
    case 1: return FixMojibake(s, 950, 936);   // BIG5 -> GB
    case 2: return FixMojibake(s, 936, 950);   // GB -> BIG5
    case 3: return FixMojibake(s, 932, 936);   // 日文 Shift-JIS -> 中文
    case 4: return MakeMojibake(s, 932, 936);  // 中文 -> 日文 Shift-JIS
    }
    return s;
}

// 标签繁简互转 / 内码转换: 读出现有标签 -> 逐字段转换 -> 按 标签写入 的同一套设置写回。
// mode: 1 繁体转简体, 2 简体转繁体, 3 BIG5->GB, 4 GB->BIG5, 5 SJIS->中文, 6 中文->SJIS。
static void DoTagConvert(int mode) {
    const wchar_t* what = (mode == 1) ? LS(IDS_TITLE_TAG_T2S) : (mode == 2) ? LS(IDS_TITLE_TAG_S2T) :
        (mode == 3) ? LS(IDS_TITLE_TAG_BIG5GB) : (mode == 4) ? LS(IDS_TITLE_TAG_GBBIG5) :
        (mode == 5) ? LS(IDS_TITLE_TAG_SJISZH) : LS(IDS_TITLE_TAG_ZHSJIS);
    int enc = (mode >= 3) ? mode - 2 : 0;
    RunTagRows(what, LS(IDS_RES_CONVERTED), [mode, enc](const fs::path& p, int i) -> int {
        (void)p;
        EnsureTags(g_items[i]);
        if (!g_items[i].tagsRead || !g_items[i].tags.HasAny()) return 1;
        AudioTags t = g_items[i].tags;
        std::wstring f[7] = { t.artist, t.title, t.album, t.track,
                              t.year, t.genre, t.comment };
        bool any = false;
        for (auto& x : f) {
            std::wstring c = (mode == 1) ? ConvertT2S(x) : (mode == 2) ? ConvertS2T(x)
                                                                   : EncConvText(x, enc);
            if (c != x) { x = c; any = true; }
        }
        if (!any) return 3;                                    // 无需转换
        t.artist = f[0]; t.title = f[1]; t.album = f[2]; t.track = f[3];
        t.year = f[4];   t.genre = f[5];  t.comment = f[6];
        return WriteAudioTags(g_items[i].fullPath, t, g_set.writeMode);
    });
}

// 移除音乐文件标签: 按 设置 > 音乐文件标签 > 移除ID3标签时 的模式剥离。
static void DoTagRemove() {
    RunTagRows(LS(IDS_TITLE_TAG_REMOVE), LS(IDS_RES_REMOVED), [](const fs::path& p, int i) -> int {
        (void)i;
        return RemoveAudioTags(p, g_set.removeMode);
    });
}

// MP3 的 ID3v1↔ID3v2 互转: 读一个块的字段, 按同一个写入模式写进另一个块,
// 另一块保持原样 (只写 v2 时原 v1 尾巴保留, 只写 v1 时原 v2 保留)。
static void DoId3Copy(bool v2ToV1) {
    const wchar_t* what = v2ToV1 ? LS(IDS_TITLE_ID3_V2V1) : LS(IDS_TITLE_ID3_V1V2);
    RunTagRows(what, v2ToV1 ? LS(IDS_RES_WROTE_V1) : LS(IDS_RES_WROTE_V2),
               [v2ToV1](const fs::path& p, int i) -> int {
        (void)i;
        std::wstring ext = p.extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        if (ext != L".mp3") return 1;                          // 只有 MP3 有这两个块
        AudioTags t = v2ToV1 ? ReadId3v2Only(p) : ReadId3v1Only(p);
        if (!t.HasAny()) return 3;                             // 没有源标签
        return WriteAudioTags(p, t, v2ToV1 ? 0 : 1);
    });
}

// 导出音乐文件标签: 每行 "文件名\t艺术家\t标题\t专辑\t音轨\t年代\t流派\t注释",
// UTF-8 带 BOM。这个格式可以直接喂给 利用文本文件写入标签。
static void DoTagExport() {
    std::vector<int> rows = CtxTargets(-1);
    if (rows.empty()) {
        MessageBoxW(g_hDlg, LS(IDS_MSG_TAGEXPORT_NOROWS),
                    LS(IDS_TITLE_TAG_EXPORT), MB_OK | MB_ICONINFORMATION);
        return;
    }
    wchar_t file[512] = { 0 };
    wcscpy_s(file, LS(IDS_DEF_TAG_EXPORT));
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = g_hDlg;
    ofn.lpstrFilter = LS(IDS_FILTER_TXT);
    ofn.lpstrFile = file;
    ofn.nMaxFile = 512;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = L"txt";
    if (!GetSaveFileNameW(&ofn)) return;

    std::wstring text = LS(IDS_TAG_TEXT_HEADER);
    for (int i : rows) {
        EnsureTags(g_items[i]);
        const AudioTags& t = g_items[i].tags;
        const std::wstring* f[7] = { &t.artist, &t.title, &t.album, &t.track,
                                     &t.year, &t.genre, &t.comment };
        text += g_items[i].fullPath.filename().wstring();
        for (auto* v : f) { text += L"\t"; text += *v; }
        text += L"\r\n";
    }
    if (!WriteUtf8WithBom(file, text, LS(IDS_TITLE_TAG_EXPORT))) return;
    wchar_t buf[128];
    swprintf_s(buf, 128, LS(IDS_MSG_TAG_EXPORTED), (int)rows.size(), file);
    MessageBoxW(g_hDlg, buf, LS(IDS_TITLE_TAG_EXPORT), MB_OK | MB_ICONINFORMATION);
}

// 读一个文本文件, 按 BOM / UTF-8 试探 / GBK 猜编码, 返回宽字符内容。
static std::wstring CpToWLocal(UINT cp, const char* s, int n) {
    if (n <= 0) return L"";
    int w = MultiByteToWideChar(cp, 0, s, n, NULL, 0);
    if (w <= 0) return L"";
    std::wstring out((size_t)w, L'\0');
    MultiByteToWideChar(cp, 0, s, n, &out[0], w);
    return out;
}

static std::wstring ReadTextFileAny(const wchar_t* path) {
    HANDLE h = CreateFileW(path, GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return L"";
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (64ll << 20)) {
        CloseHandle(h);
        return L"";
    }
    std::vector<unsigned char> b((size_t)sz.QuadPart);
    DWORD got = 0;
    ReadFile(h, b.data(), (DWORD)b.size(), &got, NULL);
    CloseHandle(h);
    b.resize(got);
    if (b.size() >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF)
        return CpToWLocal(CP_UTF8, (const char*)&b[3], (int)(b.size() - 3));
    auto u16 = [&](bool be) {
        std::wstring out;
        size_t i = 2;
        for (; i + 1 < b.size(); i += 2) {
            uint16_t u = be ? (uint16_t)((b[i] << 8) | b[i + 1])
                            : (uint16_t)(b[i] | (b[i + 1] << 8));
            out.push_back((wchar_t)u);
        }
        return out;
    };
    if (b.size() >= 2 && b[0] == 0xFF && b[1] == 0xFE) return u16(false);
    if (b.size() >= 2 && b[0] == 0xFE && b[1] == 0xFF) return u16(true);
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                            (const char*)b.data(), (int)b.size(), NULL, 0) > 0)
        return CpToWLocal(CP_UTF8, (const char*)b.data(), (int)b.size());
    return CpToWLocal(936, (const char*)b.data(), (int)b.size());   // 按 GBK
}

// 利用文本文件写入标签: 每行 "文件名\t艺术家\t标题\t专辑\t音轨\t年代\t流派\t注释"
// (字段可留空; Tab 分隔; # 或 ; 开头是注释; 首行表头会自动跳过)。
static void DoTagFromText() {
    std::vector<int> rows = CtxTargets(-1);
    if (rows.empty()) {
        MessageBoxW(g_hDlg, LS(IDS_MSG_TAGFROMTEXT_NOROWS),
                    LS(IDS_TITLE_TAG_FROMTEXT), MB_OK | MB_ICONINFORMATION);
        return;
    }
    wchar_t file[512] = L"";
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = g_hDlg;
    ofn.lpstrFilter = LS(IDS_FILTER_TXT);
    ofn.lpstrFile = file;
    ofn.nMaxFile = 512;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrTitle = LS(IDS_PICK_TAG_TITLE);
    if (!GetOpenFileNameW(&ofn)) return;

    std::wstring content = ReadTextFileAny(file);
    std::map<std::wstring, AudioTags> table;
    size_t pos = 0;
    while (pos < content.size()) {
        size_t e = content.find(L"\n", pos);
        std::wstring line = content.substr(pos, e == std::wstring::npos ? std::wstring::npos : e - pos);
        pos = (e == std::wstring::npos) ? content.size() : e + 1;
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();
        size_t b0 = line.find_first_not_of(L" \t");
        if (b0 == std::wstring::npos) continue;
        line.erase(0, b0);
        if (line[0] == L'#' || line[0] == L';') continue;
        std::vector<std::wstring> f;
        size_t p2 = 0;
        while (true) {
            size_t t = line.find(L'\t', p2);
            f.push_back(line.substr(p2, t == std::wstring::npos ? std::wstring::npos : t - p2));
            if (t == std::wstring::npos) break;
            p2 = t + 1;
        }
        if (f.size() < 2 || f[0] == LS(IDS_COL_NAME)) continue;       // 表头行
        AudioTags t;
        const size_t kMax = f.size() < 8 ? f.size() : 8;
        static const int kSlot[7] = { 0, 1, 2, 3, 4, 5, 6 };   // artist..comment
        for (size_t k = 1; k < kMax; ++k) {
            std::wstring v = f[k];
            size_t vb = v.find_first_not_of(L" ");
            if (vb == std::wstring::npos) continue;
            v.erase(0, vb);
            switch (kSlot[k - 1]) {
            case 0: t.artist = v; break;   case 1: t.title = v; break;
            case 2: t.album = v; break;    case 3: t.track = v; break;
            case 4: t.year = v; break;     case 5: t.genre = v; break;
            case 6: t.comment = v; break;
            }
        }
        std::wstring key = f[0];
        std::transform(key.begin(), key.end(), key.begin(), ::towlower);
        table[key] = t;
    }
    if (table.empty()) {
        MessageBoxW(g_hDlg,
            LS(IDS_MSG_TAGFROMTEXT_FORMAT),
            LS(IDS_TITLE_TAG_FROMTEXT), MB_OK | MB_ICONINFORMATION);
        return;
    }
    int matched = 0;
    for (int i : rows) {
        std::wstring key = g_items[i].fullPath.filename().wstring();
        std::transform(key.begin(), key.end(), key.begin(), ::towlower);
        if (table.count(key)) ++matched;
    }
    if (!matched) {
        wchar_t q[160];
        swprintf_s(q, 160, LS(IDS_MSG_TAGFROMTEXT_NOMATCH), (int)table.size());
        MessageBoxW(g_hDlg, q, LS(IDS_TITLE_TAG_FROMTEXT), MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (g_set.tagPrompt) {
        wchar_t q[128];
        swprintf_s(q, 128, LS(IDS_MSG_TAGFROMTEXT_CONFIRM),
                   matched);
        if (MessageBoxW(g_hDlg, q, LS(IDS_TITLE_TAG_FROMTEXT), MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
    }
    RunTagRows(LS(IDS_TITLE_TAG_FROMTEXT), LS(IDS_RES_WRITTEN), [&table](const fs::path& p, int) -> int {
        std::wstring key = p.filename().wstring();
        std::transform(key.begin(), key.end(), key.begin(), ::towlower);
        auto it = table.find(key);
        if (it == table.end()) return 3;                       // 这一行没有对应记录
        return WriteAudioTags(p, it->second, g_set.writeMode);
    });
}

// 调用文本文件命令: 每行 "原文件名=新文件名" (或 Tab 分隔), 匹配到的行按新名预览。
static void DoTextCmd() {
    wchar_t file[512] = L"";
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = g_hDlg;
    ofn.lpstrFilter = LS(IDS_FILTER_TXT);
    ofn.lpstrFile = file;
    ofn.nMaxFile = 512;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrTitle = LS(IDS_PICK_CMD_TITLE);
    if (!GetOpenFileNameW(&ofn)) return;

    std::wstring content = ReadTextFileAny(file);
    std::map<std::wstring, std::wstring> cmds;
    size_t pos = 0;
    while (pos < content.size()) {
        size_t e = content.find(L"\n", pos);
        std::wstring line = content.substr(pos, e == std::wstring::npos ? std::wstring::npos : e - pos);
        pos = (e == std::wstring::npos) ? content.size() : e + 1;
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();
        size_t b0 = line.find_first_not_of(L" \t");
        if (b0 == std::wstring::npos) continue;
        line.erase(0, b0);
        if (line[0] == L'#' || line[0] == L';') continue;
        size_t sep = line.find(L'=');
        if (sep == std::wstring::npos) sep = line.find(L'\t');
        if (sep == std::wstring::npos) continue;
        std::wstring a = line.substr(0, sep), c = line.substr(sep + 1);
        while (!a.empty() && (a.back() == L' ' || a.back() == L'\t')) a.pop_back();
        size_t cb = c.find_first_not_of(L" \t");
        if (cb == std::wstring::npos) continue;
        c.erase(0, cb);
        if (a.empty() || c.empty()) continue;
        cmds[a] = c;
    }
    if (cmds.empty()) {
        MessageBoxW(g_hDlg,
            LS(IDS_MSG_TEXTCMD_FORMAT),
            LS(IDS_TITLE_TEXTCMD), MB_OK | MB_ICONINFORMATION);
        return;
    }
    int matched = 0;
    for (auto& it : g_items) {
        if (cmds.count(it.oldName) ||
            cmds.count(it.fullPath.filename().wstring())) ++matched;
    }
    if (!matched) {
        wchar_t q[160];
        swprintf_s(q, 160, LS(IDS_MSG_TEXTCMD_NOMATCH), (int)cmds.size());
        MessageBoxW(g_hDlg, q, LS(IDS_TITLE_TEXTCMD), MB_OK | MB_ICONINFORMATION);
        return;
    }
    g_textCmds = cmds;
    g_textCmdFile = file;
    g_nameSource = 3;
    Recompute();
    wchar_t q[128];
    swprintf_s(q, 128, LS(IDS_MSG_TEXTCMD_LOADED),
               (int)cmds.size(), matched);
    MessageBoxW(g_hDlg, q, LS(IDS_TITLE_TEXTCMD), MB_OK | MB_ICONINFORMATION);
}

// ----------------------------------------------------------------------------
// 资源管理器右键菜单
// 写 HKEY_CLASSES_ROOT —— 对所有用户生效, 因此**需要管理员权限**。
// 程序平时不需要管理员(manifest 没声明 requireAdministrator), 所以点菜单时先查自己
// 提权了没有: 没提权就用 ShellExecute "runas" 带 /shell-add (或 /shell-remove)
// 重启一份提权副本去干, 弹一次 UAC; 副本在 wWinMain 开头接住参数, 干完就退出。
// 提权后写 HKCR 实际落到 HKLM\Software\Classes。注意 HKCU\Software\Classes 下若残留
// 同名键, 合并视图里 HKCU 优先级更高、会把新写的值盖住(显示成 "Arad" 的旧菜单项
// 就是这么赖着不走的), 所以两边都得铲干净。写/删失败时单独判 ERROR_ACCESS_DENIED,
// 给一句能照做的提示, 否则用户只看到「成功 0 条」不知道发生了什么。
// ----------------------------------------------------------------------------

// 本进程是否已提权。开着 UAC 时, 即使用管理员账号登录, 拿到的也只是标准令牌,
// CheckTokenMembership 会返回 FALSE —— 正好就是"能不能直接写 HKCR"的判定。
static bool IsRunAsAdmin() {
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    PSID admins = NULL;
    if (!AllocateAndInitializeSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admins))
        return false;
    BOOL member = FALSE;
    BOOL ok = CheckTokenMembership(NULL, admins, &member);
    FreeSid(admins);
    return ok && member;
}

// allowElevate=false 是给提权副本用的: 它已经重启过一次, 再弹一次就成死循环了。
static void DoShellMenu(bool install, bool allowElevate = true) {
    wchar_t exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    const wchar_t* title = install ? LS(IDS_TITLE_SHELL_ADD) : LS(IDS_TITLE_SHELL_REMOVE);

    if (!IsRunAsAdmin() && allowElevate) {
        if (MessageBoxW(g_hDlg,
                        LS(IDS_SHELL_NEED_ADMIN),
                        title, MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
            return;
        std::wstring dir(exe);
        size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) dir.erase(slash);
        HINSTANCE hi = ShellExecuteW(NULL, L"runas", exe,
                                     install ? L"/shell-add" : L"/shell-remove",
                                     dir.c_str(), SW_SHOWNORMAL);
        if ((INT_PTR)hi > 32) return;          // 副本会接手并弹结果, 本实例不再吭声
        MessageBoxW(g_hDlg,
                    LS(IDS_SHELL_RUNAS_FAILED),
                    title, MB_OK | MB_ICONERROR);
        return;
    }

    struct Entry { const wchar_t* key; const wchar_t* arg; const wchar_t* label; };
    // HKEY_CLASSES_ROOT 下的路径不带 Software\Classes 前缀。Folder 比 Directory 覆盖面广
    // (文件夹 / 驱动器 / 库都算), 参考实现用的就是 Folder。
    const Entry entries[] = {
        { L"*\\shell\\AradeRenamer",                     L"%1", LS(IDS_FILTER_ALL) },
        { L"Folder\\shell\\AradeRenamer",                L"%V", LS(IDS_KIND_FOLDER) },
        { L"Directory\\Background\\shell\\AradeRenamer", L"%V", LS(IDS_SHELL_CTX_BG) },
    };
    // 历史遗留: 早期写到过这两个位置(现在统一到上面三处), 不清掉会在右键菜单里重复出现。
    static const wchar_t* const kStaleHkcr[] = {
        L"Directory\\shell\\AradeRenamer",
        L"DesktopBackground\\shell\\AradeRenamer",
    };
    // 更麻烦的是用户级(HKCU)残留: 合并视图里 HKCU 盖住 HKLM, 旧值会压掉新写的 ——
    // 那个显示成 "Arad" 的菜单项就是这么来的, 所以装之前必须先铲掉。
    static const wchar_t* const kStaleHkcu[] = {
        L"Software\\Classes\\*\\shell\\AradeRenamer",
        L"Software\\Classes\\Directory\\shell\\AradeRenamer",
        L"Software\\Classes\\Directory\\Background\\shell\\AradeRenamer",
        L"Software\\Classes\\DesktopBackground\\shell\\AradeRenamer",
        L"Software\\Classes\\Folder\\shell\\AradeRenamer",
    };
    const int kN = (int)(sizeof(entries) / sizeof(entries[0]));
    // 清旧键不计入成功计数: 用户不该看到"卸载了 5 条"这种数
    for (const wchar_t* k : kStaleHkcr) RegDeleteTreeW(HKEY_CLASSES_ROOT, k);
    for (const wchar_t* k : kStaleHkcu) RegDeleteTreeW(HKEY_CURRENT_USER, k);

    int ok = 0;
    LONG lastErr = ERROR_SUCCESS;
    for (const Entry& e : entries) {
        if (!install) {
            LONG r = RegDeleteTreeW(HKEY_CLASSES_ROOT, e.key);
            if (r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND) ++ok;
            else lastErr = r;
            continue;
        }
        HKEY hk = NULL;
        LONG r = RegCreateKeyExW(HKEY_CLASSES_ROOT, e.key, 0, NULL, 0,
                                 KEY_SET_VALUE, NULL, &hk, NULL);
        if (r != ERROR_SUCCESS) { lastErr = r; continue; }
        // 注意: 长度必须用 wcslen 算。早先这里写的是 sizeof(LS(...)), 而 LS() 返回的是
        // 指针, sizeof 在 x64 下恒为 8 字节 = 4 个 wchar, 于是菜单上只显示 "Arad"
        // (连结尾的 \0 都没写进去)。这种错编译期一声不响。
        std::wstring verb = LS(IDS_SHELL_VERB);
        RegSetValueExW(hk, NULL, 0, REG_SZ, (const BYTE*)verb.c_str(),
                       (DWORD)((verb.size() + 1) * sizeof(wchar_t)));
        RegSetValueExW(hk, L"Icon", 0, REG_SZ, (const BYTE*)exe,
                       (DWORD)((wcslen(exe) + 1) * sizeof(wchar_t)));
        HKEY hc = NULL;
        if (RegCreateKeyExW(hk, L"command", 0, NULL, 0,
                            KEY_SET_VALUE, NULL, &hc, NULL) == ERROR_SUCCESS) {
            std::wstring cmd = L"\"" + std::wstring(exe) + L"\" \"" + e.arg + L"\"";
            RegSetValueExW(hc, NULL, 0, REG_SZ, (const BYTE*)cmd.c_str(),
                           (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(hc);
        }
        RegCloseKey(hk);
        ++ok;
    }
    if (ok < kN && lastErr == ERROR_ACCESS_DENIED) {
        MessageBoxW(g_hDlg, LS(IDS_ERR_SHELL_DENIED), title, MB_OK | MB_ICONERROR);
        return;
    }
    wchar_t buf[192];
    if (install)
        swprintf_s(buf, 192, LS(IDS_MSG_SHELL_ADDED), ok);
    else
        swprintf_s(buf, 192, LS(IDS_MSG_SHELL_REMOVED), ok);
    MessageBoxW(g_hDlg, buf, title,
                MB_OK | (ok == kN ? MB_ICONINFORMATION : MB_ICONWARNING));
}

// ----------------------------------------------------------------------------
// 发送到 菜单 (SendTo): 往 %APPDATA%\Microsoft\Windows\SendTo\ 放一个指向 exe 的
// 快捷方式。不走注册表 —— Windows 的发送到菜单就是扫描这个文件夹。
// 多选文件发送时, 系统把所有文件一次性放进命令行, 配合单实例全部进同一个窗口。
// ----------------------------------------------------------------------------
static fs::path SendToDir() {
    wchar_t buf[MAX_PATH];
    HRESULT hr = SHGetFolderPathW(NULL, 0x0009 /*CSIDL_SENDTO*/, NULL, 0, buf);
    if (FAILED(hr)) return L"";
    return fs::path(buf);
}

static void DoSendToMenu(bool install) {
    fs::path dir = SendToDir();
    if (dir.empty()) {
        MessageBoxW(g_hDlg, LS(IDS_ERR_SENDTO_DIR), LS(IDS_TITLE_SENDTO), MB_OK | MB_ICONERROR);
        return;
    }
    fs::path lnk = dir / LS(IDS_SENDTO_LNKNAME);
    wchar_t exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (!n || n >= MAX_PATH) return;

    if (!install) {
        std::error_code ec;
        bool had = fs::exists(lnk, ec);
        if (had && !DeleteFileW(lnk.c_str())) {
            MessageBoxW(g_hDlg, LS(IDS_ERR_SENDTO_DEL), LS(IDS_TITLE_SENDTO), MB_OK | MB_ICONERROR);
            return;
        }
        MessageBoxW(g_hDlg, had ? LS(IDS_MSG_SENDTO_REMOVED) : LS(IDS_MSG_SENDTO_NOTHAVE),
                    LS(IDS_TITLE_SENDTO), MB_OK | MB_ICONINFORMATION);
        return;
    }

    // 建快捷方式: 目标就是 exe, 不带参数 —— 选中的文件由系统自动追加在命令行。
    IShellLinkW* link = NULL;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                  IID_IShellLinkW, (LPVOID*)&link);
    if (FAILED(hr) || !link) {
        MessageBoxW(g_hDlg, LS(IDS_ERR_SENDTO_CREATE), LS(IDS_TITLE_SENDTO), MB_OK | MB_ICONERROR);
        return;
    }
    link->SetPath(exe);
    link->SetDescription(LS(IDS_SENDTO_DESC));
    link->SetIconLocation(exe, 0);
    IPersistFile* pf = NULL;
    hr = link->QueryInterface(IID_IPersistFile, (LPVOID*)&pf);
    bool ok = false;
    if (SUCCEEDED(hr) && pf) {
        ok = SUCCEEDED(pf->Save(lnk.c_str(), TRUE));
        pf->Release();
    }
    link->Release();
    if (!ok) {
        MessageBoxW(g_hDlg, LS(IDS_ERR_SENDTO_WRITE), LS(IDS_TITLE_SENDTO), MB_OK | MB_ICONERROR);
        return;
    }
    wchar_t q[256];
    swprintf_s(q, 256, LS(IDS_MSG_SENDTO_ADDED),
               lnk.c_str());
    MessageBoxW(g_hDlg, q, LS(IDS_TITLE_SENDTO), MB_OK | MB_ICONINFORMATION);
}

// 用户手册: 放在 exe 同目录的 用户手册.html, 用默认浏览器打开。
static void OpenManual() {
    // 正式发行时手册和 exe 放一起; 从仓库里直接跑(build 完没拷)时它在 docs\ 下,
    // 两个位置都找一遍再报错。
    fs::path man = ExeDir() / L"用户手册.html";
    std::error_code ec;
    if (!fs::exists(man, ec)) {
        fs::path alt = ExeDir() / L"docs" / L"用户手册.html";
        if (fs::exists(alt, ec)) man = alt;
    }
    if (!fs::exists(man, ec)) {
        MessageBoxW(g_hDlg, LS(IDS_ERR_MANUAL_MISSING),
                    LS(IDS_TITLE_MANUAL), MB_OK | MB_ICONINFORMATION);
        return;
    }
    HINSTANCE r = ShellExecuteW(g_hDlg, L"open", man.c_str(), NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32)
        MessageBoxW(g_hDlg, LS(IDS_ERR_MANUAL_OPEN), LS(IDS_TITLE_MANUAL), MB_OK | MB_ICONERROR);
}

static void UpdateLanguageChecks();

// 菜单勾选状态同步: 开关类命令 (简繁/内码/CRC/名字来源) 在菜单上打勾。
static void UpdateMenuChecks() {
    HMENU menu = GetMenu(g_hDlg);
    if (!menu) return;
    auto chk = [&](int id, bool on) {
        CheckMenuItem(menu, id, on ? MF_CHECKED : MF_UNCHECKED);
    };
    chk(IDM_NAME_T2S, g_convMode == 1);
    chk(IDM_NAME_S2T, g_convMode == 2);
    chk(IDM_NAME_BIG5GB, g_encConv == 1);
    chk(IDM_NAME_GBBIG5, g_encConv == 2);
    chk(IDM_NAME_SJISZH, g_encConv == 3);
    chk(IDM_NAME_ZHSJIS, g_encConv == 4);
    chk(IDM_NAME_CRC32, g_crcRename);
    chk(IDM_NAME_WEBTITLE, g_nameSource == 1);
    chk(IDM_NAME_TEXTLINE1, g_nameSource == 2);
    chk(IDM_TEXT_NAMING, g_nameSource == 3);
    UpdateLanguageChecks();
}

// ----------------------------------------------------------------------------
// 多语言: 选项 > 语言 子菜单是运行时按可用语言重建的
// ----------------------------------------------------------------------------
// 选项 是 rc 里的第 4 个顶层菜单, 语言是它里面唯一的二级弹出菜单。
static HMENU FindLangSubMenu() {
    HMENU opt = GetSubMenu(GetMenu(g_hDlg), 3);
    if (!opt) return NULL;
    for (int i = 0, n = GetMenuItemCount(opt); i < n; ++i) {
        MENUITEMINFOW mi = { sizeof(mi) };
        mi.fMask = MIIM_SUBMENU;
        if (GetMenuItemInfoW(opt, (UINT)i, TRUE, &mi) && mi.hSubMenu) return mi.hSubMenu;
    }
    return NULL;
}

static void UpdateLanguageChecks() {
    HMENU lang = FindLangSubMenu();
    if (!lang) return;
    const std::vector<LangInfo>& list = LangList();
    for (int i = 0; i < (int)list.size(); ++i)
        CheckMenuItem(lang, IDM_LANG_BASE + i,
                      list[i].code == LangChoice() ? MF_CHECKED : MF_UNCHECKED);
}

// 菜单项文字: "English (US)  (yjhua2005)" —— 括号里是语言包里 lang.author 写的译者署名。
// 老语言包没写 lang.author 就只显示名字, 不加空括号。
static std::wstring LangMenuLabel(const LangInfo& li) {
    std::wstring s = li.name;
    if (!li.author.empty()) { s += L"  ("; s += li.author; s += L")"; }
    return s;
}

static void BuildLanguageMenu() {
    HMENU lang = FindLangSubMenu();
    if (!lang) return;
    while (GetMenuItemCount(lang) > 0) RemoveMenu(lang, 0, MF_BYPOSITION);
    const std::vector<LangInfo>& list = LangList();
    for (int i = 0; i < (int)list.size(); ++i)
        // 首项 "自动" 的名字跟着当前语言走, 所以现取而不是用 g_list 里存的名字。
        AppendMenuW(lang, MF_STRING, IDM_LANG_BASE + i,
                    list[i].code == L"auto" ? LS(IDS_LANG_AUTO)
                                            : LangMenuLabel(list[i]).c_str());
    AppendMenuW(lang, MF_SEPARATOR, 0, NULL);
    AppendMenuW(lang, MF_STRING, IDM_LANG_OPEN_DIR, LS(IDS_MNU_LANG_OPEN_DIR));
    UpdateLanguageChecks();
}

static void RefreshListHeaders() {
    if (!g_hList) return;
    for (int c = 0; c < 9; ++c) {
        LVCOLUMNW col = { 0 };
        col.mask = LVCF_TEXT;
        col.pszText = (LPWSTR)(kListHeaderIds[c] ? LS(kListHeaderIds[c]) : L"");
        ListView_SetColumn(g_hList, c, &col);
    }
}

static void RefreshTabNames() {
    if (!g_hTab) return;
    // TabCtrl_SetItem 换了文字不会重算标签宽度: 从中文切到长一截的英文时, 字会被截掉
    // 尾巴 ("Numbering" 只剩 "Numberi")。所以整条删掉重插, 宽度才会按新文字来。
    const int sel = (int)TabCtrl_GetCurSel(g_hTab);
    TabCtrl_DeleteAllItems(g_hTab);
    TCITEMW ti = { 0 };
    ti.mask = TCIF_TEXT;
    for (int t = 0; t < TAB_COUNT; ++t) {
        ti.pszText = (LPWSTR)LS(TAB_NAME_IDS[t]);
        TabCtrl_InsertItem(g_hTab, t, &ti);
    }
    if (sel >= 0) TabCtrl_SetCurSel(g_hTab, sel);
}

// 切完语言把界面整个重刷一遍: lang.cpp 只管得了"按控件/命令 ID 能对上"的那些,
// Tab 页名、列头、下拉条目、工具栏气泡、列表行里的 识别类型/结果 归这里。
static void OnLanguageChanged() {
    int filterSel = (int)SendMessageW(GetDlgItem(g_hDlg, IDC_OV_FILTER), CB_GETCURSEL, 0, 0);
    ApplyLanguage(g_hDlg);
    Layout();          // 控件宽度是按当前语言的文字量出来的, 换了语言必须重排一次
    RefreshTabNames();
    RefreshListHeaders();
    FillCombos(g_hDlg);
    SendMessageW(GetDlgItem(g_hDlg, IDC_OV_FILTER), CB_SETCURSEL,
                 filterSel < 0 ? 0 : filterSel, 0);
    RefreshToolbarTips();
    RebuildList();          // 行里 识别类型 / 结果 两列的文字
    RefreshRecPanel();
    BuildLanguageMenu();      // 语言子菜单是动态建的, ApplyLanguage 覆盖不到它
    UpdateStatusParts();
    UpdateMenuChecks();
    InvalidateRect(g_hDlg, NULL, TRUE);
}

static void DoSwitchLanguage(int index) {
    const std::vector<LangInfo>& list = LangList();
    if (index < 0 || index >= (int)list.size()) return;
    if (!LangSet(list[index].code)) return;
    LangSaveChoice(list[index].code);
    OnLanguageChanged();
}

static void OpenLanguageFolder() {
    std::wstring d = LangDir();
    CreateDirectoryW(d.c_str(), NULL);   // 没有就建一个, 方便往里丢新的 .ini
    ShellExecuteW(g_hDlg, L"open", d.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

// ListView_SetCheckState 会给每一行发一条 LVN_ITEMCHANGED，逐行响应预览在几千行时是
// O(n^2)，所以整段抑制通知、结束后只算一次。
static void SetChecked(const std::vector<int>& rows, bool on) {
    g_noListNotify = true;
    for (int i : rows) {
        g_items[i].checked = on;
        ListView_SetCheckState(g_hList, i, on);
    }
    g_noListNotify = false;
    AutoPreview();
}

static void DeleteRows(std::vector<int> rows) {
    if (rows.empty()) return;
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    for (int k = (int)rows.size() - 1; k >= 0; --k)
        g_items.erase(g_items.begin() + rows[k]);
    RebuildList();   // g_keys 与打勾状态都按 g_items 重建
}

// 读入音乐文件标签: 读出目标行的标签，把第一行设为选中并跳到 标签写入 页回填字段。
static void ReadTagsOf(const std::vector<int>& rows) {
    if (rows.empty()) return;
    for (int i : rows) EnsureTags(g_items[i]);
    int first = rows.front();
    g_noListNotify = true;
    ListView_SetItemState(g_hList, -1, 0, LVIS_SELECTED);
    ListView_SetItemState(g_hList, first, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
    g_noListNotify = false;
    TabCtrl_SetCurSel(g_hTab, 7);
    ShowTab(7);   // ShowTab 末尾的 UpdateTagPanel 负责把标签写进输入框
}

static void PlayRow(int i) {
    if (i < 0 || (size_t)i >= g_items.size()) return;
    std::wstring dir = g_items[i].fullPath.parent_path().wstring();
    std::wstring file = g_items[i].fullPath.wstring();
    HINSTANCE r = ShellExecuteW(g_hDlg, L"open", file.c_str(), NULL, dir.c_str(), SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) {
        wchar_t buf[300];
        swprintf_s(buf, 300, LS(IDS_ERR_PLAY), file.c_str());
        MessageBoxW(g_hDlg, buf, LS(IDS_TITLE_PLAY), MB_OK | MB_ICONERROR);
    }
}

// One entry point for every command id, so a toolbar click, a menu pick and its
// accelerator cannot drift apart.
static void RunCommand(int cmd) {
    switch (cmd) {
    // ---- 列表 / 更名 ----
    case IDC_TB_ADD:        case IDM_FILE_OPEN:     OnAddFiles(); break;
    case IDC_TB_ADDF:       case IDM_FILE_ADDFOLDER: OnAddFolder(); break;
    case IDC_TB_PREVIEW:    case IDM_REN_PREVIEW:    Recompute(); break;
    case IDC_TB_APPLY:      case IDM_REN_APPLY:      OnApply(); break;
    case IDC_TB_UNDO:       case IDM_REN_UNDO:       OnUndo(); break;
    case IDC_TB_PIN:
        g_pinTop = !g_pinTop;
        SetWindowPos(g_hDlg, g_pinTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        InvalidateRect(GetDlgItem(g_hDlg, IDC_TB_PIN), NULL, TRUE);
        break;
    case IDC_TB_EXPLIST:    case IDM_FILE_EXPORT:    DoExportList(); break;
    case IDM_FILE_EXIT:     SendMessageW(g_hDlg, WM_CLOSE, 0, 0); break;

    // ---- 文件列表右键菜单 (打勾 = 这一行参与本次处理) ----
    case IDM_CTX_SELALL: case IDM_CTX_SELNONE: {
        std::vector<int> all(g_items.size());
        for (size_t i = 0; i < g_items.size(); ++i) all[i] = (int)i;
        SetChecked(all, cmd == IDM_CTX_SELALL);
        break;
    }
    case IDM_CTX_CHECK:     SetChecked(SelectedRows(-1), true);   break;
    case IDM_CTX_UNCHECK:   SetChecked(SelectedRows(-1), false);  break;
    case IDM_CTX_DELROWS:   DeleteRows(CtxTargets(-1)); break;
    case IDM_CTX_CLEARLIST: g_items.clear(); RebuildList(); break;
    case IDM_CTX_TAGREAD:   ReadTagsOf(CtxTargets(-1)); break;
    case IDM_CTX_PLAY:      PlayRow(CtxTargets(-1).empty() ? -1 : CtxTargets(-1).front()); break;

    // ---- 标签：读取已完成，跳到对应的标签页；写回是下一轮 ----
    case IDC_TB_TAGEXTRACT: case IDM_TAG_EXTRACT:
        TabCtrl_SetCurSel(g_hTab, 6);
        ShowTab(6);
        break;
    case IDC_TB_TAGWRITE:   case IDM_TAG_WRITEALL:
        TabCtrl_SetCurSel(g_hTab, 7);
        ShowTab(7);
        break;
    case IDC_TB_TAGDEL:     case IDM_TAG_REMOVE:    DoTagRemove(); break;
    case IDM_TAG_EXPORT:    DoTagExport(); break;
    case IDM_TAG_FROMTEXT:  DoTagFromText(); break;
    case IDM_ID3_V1TOV2:    DoId3Copy(false); break;
    case IDM_ID3_V2TOV1:    DoId3Copy(true); break;
    case IDM_TAG_T2S:  DoTagConvert(1);  break;
    case IDM_TAG_S2T:  DoTagConvert(2);  break;
    case IDM_TAG_BIG5GB:    DoTagConvert(3); break;
    case IDM_TAG_GBBIG5:    DoTagConvert(4); break;
    case IDM_TAG_SJISZH:    DoTagConvert(5); break;
    case IDM_TAG_ZHSJIS:    DoTagConvert(6); break;

    // ---- 更名：简繁互转（再点一次同一个命令就取消转换）----
    case IDC_TB_T2S:        case IDM_NAME_T2S:
        g_convMode = (g_convMode == 1) ? 0 : 1;
        Recompute();
        break;
    case IDM_NAME_S2T:
        g_convMode = (g_convMode == 2) ? 0 : 2;
        Recompute();
        break;
    case IDM_NAME_BIG5GB:   case IDM_NAME_GBBIG5:
    case IDM_NAME_SJISZH:   case IDM_NAME_ZHSJIS: {
        int m = (cmd == IDM_NAME_BIG5GB) ? 1 : (cmd == IDM_NAME_GBBIG5) ? 2 :
                (cmd == IDM_NAME_SJISZH) ? 3 : 4;
        g_encConv = (g_encConv == m) ? 0 : m;
        Recompute();
        break;
    }
    case IDM_NAME_WEBTITLE: {
        // 以网页文件的标题更名只对 .htm/.html 生效; 列表里没有网页文件就不开。
        if (g_nameSource == 1) { g_nameSource = 0; Recompute(); break; }
        int n = 0;
        for (const auto& it : g_items) {
            std::wstring e = it.fullPath.extension().wstring();
            std::transform(e.begin(), e.end(), e.begin(), ::towlower);
            if (e == L".htm" || e == L".html") ++n;
        }
        if (!n) {
            MessageBoxW(g_hDlg,
                LS(IDS_MSG_WEBTITLE_NOHTML),
                LS(IDS_TITLE_WEBTITLE), MB_OK | MB_ICONINFORMATION);
            break;
        }
        g_nameSource = 1;
        Recompute();
        break;
    }
    case IDM_TEXT_NAMING:   case IDC_TB_TEXTCMD:    DoTextCmd(); break;
    case IDM_NAME_TEXTLINE1: {
        // 以文本文件的第一行内容更名只对 .txt 生效。
        if (g_nameSource == 2) { g_nameSource = 0; Recompute(); break; }
        int n = 0;
        for (const auto& it : g_items) {
            std::wstring e = it.fullPath.extension().wstring();
            std::transform(e.begin(), e.end(), e.begin(), ::towlower);
            if (e == L".txt") ++n;
        }
        if (!n) {
            MessageBoxW(g_hDlg,
                LS(IDS_MSG_TEXTLINE1_NOTXT),
                LS(IDS_TITLE_TEXTLINE1), MB_OK | MB_ICONINFORMATION);
            break;
        }
        g_nameSource = 2;
        Recompute();
        break;
    }
    case IDM_NAME_CRC32:
        g_crcRename = !g_crcRename;
        Recompute();
        break;

    // ---- 操作记录 ----
    case IDC_TB_RECOPEN:    case IDM_REC_OPEN:       DoRecOpen(); break;
    case IDC_TB_RECSAVE:    case IDM_REC_SAVE:       DoRecSave(); break;
    case IDC_TB_RECCLEAR:   case IDM_REC_CLEAR:
        g_rec.clear();
        RefreshRecPanel();
        break;
    case IDC_TB_REC_PANEL:
        // 标签条上没有它的位置, 所以按钮自己要把"面板开着"这件事显示出来 ——
        // 按下态高亮, 否则用户看不出当前是开是关。
        ShowRecPanel(!g_recPanel);
        InvalidateRect(GetDlgItem(g_hDlg, IDC_TB_REC_PANEL), NULL, TRUE);
        break;

    // ---- 更改文件日期 ----
    case IDC_TB_CHGDATE:                            DoChgDate(); break;

    // ---- 选项 / 帮助 ----
    case IDM_OPT_SETTINGS:
        if (DialogBoxW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_SETTINGS),
                       g_hDlg, SettingsProc) == IDOK) {
            ApplySettings();
            AutoPreview();
        }
        break;
    case IDM_OPT_CENTER:
        // 一次性命令, 不是开关: 把窗体挪回屏幕中央, 之后照样可以随意拖动。
        CenterWindow();
        break;
    case IDM_SHELL_ADD:     DoShellMenu(true);  break;
    case IDM_SHELL_REMOVE:  DoShellMenu(false); break;
    case IDM_SENDTO_ADD:    DoSendToMenu(true);  break;
    case IDM_SENDTO_REMOVE: DoSendToMenu(false); break;
    case IDM_HELP_MANUAL:   OpenManual(); break;
    case IDM_HELP_ABOUT:
        DialogBoxW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_ABOUT), g_hDlg, AboutProc);
        break;
    case IDC_TB_SPONSOR:
        // 工具栏最右边那枚爱心: 和「关于 > 赞助/联系」打开的是同一个对话框。
        DialogBoxW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_SPONSOR), g_hDlg, SponsorProc);
        break;

    // ---- 语言: 子菜单项的命令 ID 是运行时按可用语言分配的, 所以不能写成 case ----
    case IDM_LANG_OPEN_DIR: OpenLanguageFolder(); break;
    default:
        if (cmd >= IDM_LANG_BASE && cmd < IDM_LANG_OPEN_DIR) DoSwitchLanguage(cmd - IDM_LANG_BASE);
        break;
    }
    // 开关类命令 (简繁/内码/CRC/名字来源) 改完状态, 菜单勾选跟着变。
    UpdateMenuChecks();
}

// ----------------------------------------------------------------------------
// Drag & Drop (IDropTarget)
// ----------------------------------------------------------------------------
class CDropTarget : public IDropTarget {
public:
    CDropTarget() : m_ref(1) {}
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) {
        if (riid == IID_IUnknown || riid == IID_IDropTarget) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() { return ++m_ref; }
    IFACEMETHODIMP_(ULONG) Release() { if (--m_ref == 0) delete this; return m_ref; }
    IFACEMETHODIMP DragEnter(IDataObject*, DWORD, POINTL, DWORD* pe) { *pe = DROPEFFECT_COPY; return S_OK; }
    IFACEMETHODIMP DragOver(DWORD, POINTL, DWORD* pe) { *pe = DROPEFFECT_COPY; return S_OK; }
    IFACEMETHODIMP DragLeave() { return S_OK; }
    IFACEMETHODIMP Drop(IDataObject* pdo, DWORD, POINTL, DWORD* pe) {
        FORMATETC fe = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM stg = { 0 };
        if (SUCCEEDED(pdo->GetData(&fe, &stg))) {
            HDROP hdrop = (HDROP)GlobalLock(stg.hGlobal);
            if (hdrop) {
                UINT n = DragQueryFileW(hdrop, 0xFFFFFFFF, nullptr, 0);
                std::vector<fs::path> paths;
                wchar_t tmp[MAX_PATH];
                for (UINT i = 0; i < n; ++i)
                    if (DragQueryFileW(hdrop, i, tmp, MAX_PATH))
                        paths.push_back(fs::path(tmp));
                GlobalUnlock(stg.hGlobal);
                if (!paths.empty()) AddPaths(paths);
            }
            ReleaseStgMedium(&stg);
        }
        *pe = DROPEFFECT_COPY;
        return S_OK;
    }
private:
    ULONG m_ref;
};
static CDropTarget* g_pDrop = nullptr;

// ----------------------------------------------------------------------------
// Dialog procedure
// ----------------------------------------------------------------------------
// Text colour of a row whose target name collides with another row.
static const COLORREF kConflictRgb = RGB(190, 30, 30);

// exe 所在目录：简繁追加表 Add-ons\gbkfix.txt 从这里找，和 exe 一起拷走就能用。
static fs::path ExeDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, buf, MAX_PATH);
    if (!n || n >= MAX_PATH) return L".";
    return fs::path(buf).parent_path();
}

// ----------------------------------------------------------------------------
// 单实例: 资源管理器里选中多个文件右键时, 会为每个文件各调用一次本程序
// (shell 命令是 "app.exe %1", 一次一个文件)。靠命名互斥体认出第一个实例,
// 后续进程把自己的文件经 WM_COPYDATA 转交给它, 于是:
//   - 选 N 个文件 = 全部进同一个窗口的列表 (不是开 N 个窗口)
//   - 程序永远只有一个实例, 再次启动只是把文件并进已有窗口
// ----------------------------------------------------------------------------
static const ULONG_PTR kCopyDataFiles = 1;                     // WM_COPYDATA 的 dwData
static const wchar_t kInstanceProp[]  = L"AradeRenamer.Instance";
static const wchar_t kInstanceMutex[] = L"Local\\AradeRenamerInstanceMutex";

static BOOL CALLBACK FindInstanceWnd(HWND h, LPARAM lp) {
    if (GetPropW(h, kInstanceProp)) {
        *(HWND*)lp = h;
        return FALSE;                                          // 找到了, 停止枚举
    }
    return TRUE;
}

static bool SendFilesToInstance(HWND target, const std::vector<std::wstring>& files) {
    if (files.empty()) return true;
    std::wstring blob;
    for (const auto& f : files) { blob += f; blob.push_back(L'\0'); }
    blob.push_back(L'\0');                                     // 空串收尾
    COPYDATASTRUCT cds = { 0 };
    cds.dwData = kCopyDataFiles;
    cds.cbData = (DWORD)(blob.size() * sizeof(wchar_t));
    cds.lpData = (PVOID)blob.c_str();
    return SendMessageW(target, WM_COPYDATA, (WPARAM)NULL, (LPARAM)&cds) != 0;
}

static void BringInstanceToFront(HWND h) {
    if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
    if (GetForegroundWindow() != h) {
        SetForegroundWindow(h);                                // 非前台进程可能被系统拒绝
        FLASHWINFO fw = { sizeof(fw), h, FLASHW_ALL, 3, 0 };
        FlashWindowEx(&fw);                                    // 抢不到就闪任务栏按钮提醒
    }
}

static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    // 菜单每次弹出前同步勾选状态 (简繁互转/内码转换/CRC32/名字来源/窗口居中)。
    case WM_INITMENUPOPUP:
        UpdateMenuChecks();
        return FALSE;
    // 单实例: 资源管理器选中多个文件时会为每个文件各启动一个进程, 后面的进程把
    // 文件列表经 WM_COPYDATA 转交给第一个实例 (见文件末尾的单实例一节)。
    case WM_COPYDATA: {
        PCOPYDATASTRUCT cds = (PCOPYDATASTRUCT)lParam;
        if (!cds || cds->dwData != kCopyDataFiles || !cds->lpData ||
            cds->cbData < sizeof(wchar_t) || (cds->cbData % sizeof(wchar_t)) != 0)
            return FALSE;
        const wchar_t* p = (const wchar_t*)cds->lpData;
        size_t maxChars = cds->cbData / sizeof(wchar_t);
        std::vector<fs::path> paths;
        size_t i = 0;
        while (i < maxChars) {
            size_t len = wcsnlen(p + i, maxChars - i);
            if (len == 0) break;                            // 空串 = 列表结束
            paths.push_back(fs::path(p + i, p + i + len));
            i += len + 1;
        }
        if (paths.empty()) return FALSE;
        AddPaths(paths);                                    // 和拖放进列表同一条路
        // 这次调用不是用户点的窗口: 把主窗口带到前台, 最小化的话先还原。
        if (IsIconic(hDlg)) ShowWindow(hDlg, SW_RESTORE);
        if (GetForegroundWindow() != hDlg) {
            SetForegroundWindow(hDlg);
            FLASHWINFO fw = { sizeof(fw), hDlg, FLASHW_ALL, 3, 0 };
            FlashWindowEx(&fw);                            // 抢不到前台就闪两下提醒
        }
        return TRUE;
    }
    case WM_INITDIALOG: {
        g_hDlg = hDlg;
        g_dpi = (int)GetDpiForWindow(hDlg);
        LoadConvFixFile(ExeDir() / L"Add-ons" / L"gbkfix.txt");

        // Tab control
        g_hTab = GetDlgItem(hDlg, IDC_TAB);
        // 标签条上只有真正的页面: 操作记录 不在其中, 它是左边那条面板, 由工具栏按钮开关。
        TCITEMW ti = { 0 };
        ti.mask = TCIF_TEXT;
        for (int t = 0; t < TAB_COUNT; ++t) {
            ti.pszText = (LPWSTR)LS(TAB_NAME_IDS[t]);
            TabCtrl_InsertItem(g_hTab, t, &ti);
        }

        // ListView
        g_hList = GetDlgItem(hDlg, IDC_LIST);
        // 界面页的两项开关: 增强界面 = 整行选中 + 双缓冲, 显示表格 = 网格线.
        // 打勾列 (LVS_EX_CHECKBOXES) 与这两个开关无关, 任何配色下都在第一格画复选框。
        DWORD lex = LVS_EX_CHECKBOXES;
        if (g_set.enhList) lex |= LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER;
        if (g_set.grid) lex |= LVS_EX_GRIDLINES;
        ListView_SetExtendedListViewStyle(g_hList, lex);
        // 第一列只放打勾框, 表头留空 (见 右键菜单.png); 文件名从第二列开始。
        int widths[] = { 24, 240, 200, 60, 80, 140, 70, 80, 70 };
        for (int c = 0; c < 9; ++c) {
            LVCOLUMNW col = { 0 };
            col.mask = LVCF_TEXT | LVCF_WIDTH;
            col.pszText = (LPWSTR)(kListHeaderIds[c] ? LS(kListHeaderIds[c]) : L"");
            col.cx = S(widths[c]);
            ListView_InsertColumn(g_hList, c, &col);
        }

        // Status bar: [progress] [N 个文件, centered] [待更名/冲突]
        g_hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hDlg, (HMENU)IDC_STATUS, GetModuleHandleW(NULL), NULL);
        // 操作记录面板: 启动时一律关闭, 只有工具栏按钮能调出 (不做状态记忆)。列宽给得比
        // 面板宽, 靠 WS_HSCROLL 横向滚动看完整的 "原名 -> 新名"。
        // 表头必须留着: 它就是面板顶上那条写着「操作记录」的横条 (见 操作记录界面.png),
        // 加了 LVS_NOCOLUMNHEADER 的话这四个字会被整条藏掉。
        g_hRec = CreateWindowExW(WS_EX_STATICEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | LVS_REPORT | LVS_SHOWSELALWAYS | WS_HSCROLL,
            0, 0, 0, 0, hDlg, (HMENU)IDC_REC_LIST, GetModuleHandleW(NULL), NULL);
        ShowWindow(g_hRec, SW_HIDE);   // 与 g_recPanel 初值保持一致
        ListView_SetExtendedListViewStyle(g_hRec, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        {
            LVCOLUMNW c = { 0 };
            c.mask = LVCF_TEXT | LVCF_WIDTH;
            c.pszText = (LPWSTR)LS(IDS_REC_TAB);
            c.cx = S(320);
            ListView_InsertColumn(g_hRec, 0, &c);
        }
        g_hProgress = CreateWindowExW(WS_EX_STATICEDGE, PROGRESS_CLASSW, L"",
            WS_CHILD, 0, 0, 0, 0, g_hStatus, (HMENU)IDC_PROGRESS,
            GetModuleHandleW(NULL), NULL);
        SendMessageW(g_hProgress, PBM_SETRANGE32, 0, 100);

        FillCombos(hDlg);
        SendMessageW(GetDlgItem(hDlg, IDC_OV_FILTER), CB_SETCURSEL, 0, 0);

        // The spin also writes the field's initial text, so these edits have exactly one
        // default. 起始 tops out at 32767 because an up-down position is 16-bit; typing a
        // larger number in the box still works, the arrows just clamp.
        CreateSpin(hDlg, IDC_NM_START_SPIN, IDC_NM_START, 0, 32767, 1);
        CreateSpin(hDlg, IDC_NM_STEP_SPIN,  IDC_NM_STEP,  1, 9999, 1);
        CreateSpin(hDlg, IDC_NM_WIDTH_SPIN,  IDC_NM_WIDTH, 1, 9, 2);
        CreateSpin(hDlg, IDC_AD_POS_SPIN,   IDC_AD_POS,   1, 32767, 1);
        CreateSpin(hDlg, IDC_DL_NUM_SPIN,   IDC_DL_NUM,   1, 9999, 1);
        CreateSpin(hDlg, IDC_DL_START_SPIN, IDC_DL_START, 1, 32767, 1);

        // Default checkbox / radio state. 启用删除 can start checked because the selected
        // mode (删除指定文本) with an empty 文本 leaves the name untouched.
        CheckDlgButton(hDlg, IDC_RP_ENABLE, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_NM_ENABLE, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_NM_PAD, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_AD_ENABLE, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_AD_PRE, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_DL_ENABLE, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_DL_TEXT, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_DL_NAME, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_EX_ENABLE, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_EX_ADD, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_EX_UP, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_TG_ENABLE, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_TW_ENABLE, BST_CHECKED);

        // Show first tab
        TabCtrl_SetCurSel(g_hTab, 0);
        ShowTab(0);

        InitToolbar();

        g_wheelHook = SetWindowsHookExW(WH_MOUSE, WheelHook, NULL, GetCurrentThreadId());

        // Load the embedded toolbar / app icons (LR_SHARED so we don't leak handles).
        // One size fits both toolbar modes: 大图标 draws at S(26), the default at S(18).
        {
            HINSTANCE hMod = GetModuleHandleW(NULL);
            DWORD flags = LR_DEFAULTCOLOR | LR_SHARED;
            for (int i = 0; i < kTbIconCount; ++i)
                g_tbIcons[i].hIcon = (HICON)LoadImageW(hMod, MAKEINTRESOURCEW(g_tbIcons[i].resId),
                                                       IMAGE_ICON, S(26), S(26), flags);

            HICON hApp = (HICON)LoadImageW(hMod, MAKEINTRESOURCEW(IDI_IC_APP), IMAGE_ICON, S(16), S(16), flags);
            if (hApp) {
                SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hApp);
                SendMessageW(hDlg, WM_SETICON, ICON_BIG,   (LPARAM)hApp);
            }
        }

        // Size the window to a fixed, DPI-scaled client area (template DLU size
        // would vary with the font mapper).
        {
            int cw = S(1240), ch = S(700);
            DWORD st = (DWORD)GetWindowLongPtrW(hDlg, GWL_STYLE);
            DWORD ex = (DWORD)GetWindowLongPtrW(hDlg, GWL_EXSTYLE);
            RECT wr = { 0, 0, cw, ch };
            AdjustWindowRectEx(&wr, st, FALSE, ex);
            wr.bottom += GetSystemMetricsForDpi(SM_CYMENU, g_dpi);
            int wTotal = wr.right - wr.left, hTotal = wr.bottom - wr.top;
            MONITORINFO mi = { sizeof(mi) };
            HMONITOR hm = MonitorFromWindow(hDlg, MONITOR_DEFAULTTONEAREST);
            GetMonitorInfoW(hm, &mi);
            int mw = mi.rcWork.right - mi.rcWork.left, mh = mi.rcWork.bottom - mi.rcWork.top;
            if (wTotal > mw) wTotal = mw;
            if (hTotal > mh) hTotal = mh;
            // 位置留到最后统一摆: 那时语言包已经换完, 菜单栏高度 (也就是总高) 才是最终的。
            SetWindowPos(hDlg, NULL, 0, 0, wTotal, hTotal,
                         SWP_NOMOVE | SWP_NOZORDER);
        }

        Layout();
        LayoutListColumns();
        UpdateToolbarState();
        ApplySettings();
        // 语言: rc 模板里是中文, 这里按当前语言包整体覆盖一遍 (含菜单)。
        BuildLanguageMenu();
        ApplyLanguage(hDlg);
        RefreshTabNames();
        RefreshListHeaders();

        // 启动时窗体显示在屏幕中央 —— 只做这一次。之后窗体不再自动挪动,
        // 用户可以把它拖到任意位置 (拖到哪就停在哪), 重设大小也不会被拉回中央。
        CenterWindow();

        // Drag & drop
        g_pDrop = new CDropTarget();
        RegisterDragDrop(g_hList, g_pDrop);
        return TRUE;
    }
    case WM_PAINT: {
        if (IsRectEmpty(&g_rcFrame) && IsRectEmpty(&g_rcTbSep)) return FALSE;
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hDlg, &ps);
        if (!IsRectEmpty(&g_rcFrame))
            DrawEdge(dc, &g_rcFrame, EDGE_ETCHED, BF_LEFT | BF_RIGHT | BF_BOTTOM);
        if (!IsRectEmpty(&g_rcTbSep)) {
            // 暗线 + 亮线, 和分组分隔线一个样式
            const RECT& r = g_rcTbSep;
            TbLine(dc, r.left, r.top, r.left, r.bottom, RGB(160, 160, 160), 1);
            TbLine(dc, r.left + 1, r.top, r.left + 1, r.bottom, RGB(250, 250, 250), 1);
        }
        EndPaint(hDlg, &ps);
        return TRUE;
    }
    case WM_SIZE: {
        Layout();
        LayoutListColumns();
        return TRUE;
    }
    case WM_MEASUREITEM:
        return FALSE;   // keep template size for owner-draw buttons
    // 分组框 (BS_GROUPBOX) 是拿细线自绘的控件, 而且自己不擦背景: 整页横移之后旧框线
    // 留在原地擦不掉、新框线又只画出一截 —— 看到的就是框线断开、旁边多出一截残线。
    // 给它们一个不透明底色刷 (就是对话框自己的底色 COLOR_3DFACE), 逼它每次重画先把
    // 自己的底擦干净再画框, 残影就没了。
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC: {
        HWND hw = (HWND)lParam;
        DWORD st = (DWORD)GetWindowLongPtrW(hw, GWL_STYLE);
        if ((st & BS_TYPEMASK) == BS_GROUPBOX) {
            SetBkMode((HDC)wParam, OPAQUE);
            SetBkColor((HDC)wParam, GetSysColor(COLOR_3DFACE));
            return (INT_PTR)GetSysColorBrush(COLOR_3DFACE);
        }
        // 标签表达式 / 预设 / 预览 are blue in the reference; returning FALSE lets the
        // dialog supply the background brush as usual.
        int id = (int)GetDlgCtrlID(hw);
        if (id == IDC_TG_LB_EXPR || id == IDC_TG_LB_PRE || id == IDC_TG_LB_PREV ||
            id == IDC_TW_LB_EXPR || id == IDC_TW_LB_PRE)
            SetTextColor((HDC)wParam, RGB(0, 0, 190));
        return FALSE;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT d = (LPDRAWITEMSTRUCT)lParam;
        if (d->CtlID == IDC_STATUS) {
            // part 1 is the SBT_OWNERDRAW part; itemID carries the part index
            if (d->itemID == 1) {
                HDC dc = d->hDC;
                RECT r = d->rcItem;
                FillRect(dc, &r, (HBRUSH)(COLOR_BTNFACE + 1));
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
                DrawTextW(dc, g_statusCenter.c_str(), -1, &r,
                          DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);
            }
            return TRUE;
        }
        if (d->CtlType == ODT_BUTTON) { OnDrawToolbarButton(d); return TRUE; }
        break;
    }
    case WM_GETMINMAXINFO: {
        LPMINMAXINFO mmi = (LPMINMAXINFO)lParam;
        mmi->ptMinTrackSize.x = S(760);
        mmi->ptMinTrackSize.y = S(400);
        return TRUE;
    }
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lParam;
        // A DLGPROC's return value is truncated to BOOL by the dialog manager, so a
        // notification result must be published through DWLP_MSGRESULT. Returning the
        // CDRF_* constant directly leaves the sender reading 0 (CDRF_DODEFAULT), which
        // silently disables the custom draw below.
        auto Reply = [hDlg](LPARAM r) {
            SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, r);
            return TRUE;
        };
        if (nm->hwndFrom == g_hTab && nm->code == TCN_SELCHANGE) {
            ShowTab((int)TabCtrl_GetCurSel(g_hTab));
            return Reply(0);
        }
        if (nm->hwndFrom == g_hList && nm->code == LVN_ITEMCHANGED) {
            // The 标签 tabs mirror the selected row, so every selection change (click,
            // arrow key, programmatic) has to refresh them.
            if (g_noListNotify) return Reply(0);
            NMLISTVIEW* lv = (NMLISTVIEW*)lParam;
            if (lv->uChanged & LVIF_STATE) {
                if (lv->uNewState & LVIS_SELECTED) UpdateTagPanel();
                // 打勾框被点击: state image 1 = 未打勾, 2 = 打勾。写回模型后按 自动预览
                // 开关重算，未打勾的行会从 待更名 计数里消失。
                WORD from = (WORD)((lv->uOldState & LVIS_STATEIMAGEMASK) >> 4);
                WORD to = (WORD)((lv->uNewState & LVIS_STATEIMAGEMASK) >> 4);
                if (to != from && lv->iItem >= 0 && (size_t)lv->iItem < g_items.size()) {
                    g_items[lv->iItem].checked = (to == 2);
                    AutoPreview();
                }
            }
            return Reply(0);
        }
        // 列表里按 Delete: 删掉高亮选中的行。用 LVN_KEYDOWN 而不是加速器表, 是因为
        // 焦点在输入框里时列表根本收不到这个通知, 自然不会误删——换成加速器表的话
        // 还得自己判断焦点在哪, 而且会把输入框里的 Delete(删字符) 抢掉。
        // 一行都没选中就直接放过, 不让 Delete 变成"清空整个列表"(那样误按代价太大)。
        if (nm->hwndFrom == g_hList && nm->code == LVN_KEYDOWN) {
            NMLVKEYDOWN* kd = (NMLVKEYDOWN*)lParam;
            if (kd->wVKey == VK_DELETE) {
                std::vector<int> rows = SelectedRows(-1);
                if (!rows.empty()) DeleteRows(rows);
                return Reply(0);
            }
        }
        if (nm->hwndFrom == g_hList && nm->code == NM_CUSTOMDRAW) {
            NMLVCUSTOMDRAW* cd = (NMLVCUSTOMDRAW*)lParam;
            switch (cd->nmcd.dwDrawStage) {
            case CDDS_PREPAINT: return Reply(CDRF_NOTIFYITEMDRAW);
            case CDDS_ITEMPREPAINT: {
                // The main item (subitem 0) is drawn during this stage, so clrText has to
                // be set here too; setting it only at the subitem stage colours every
                // column but the first.
                size_t i = (size_t)cd->nmcd.lItemlParam;
                bool painted = false;
                if (g_set.colorScheme != 0) {   // 文件列表颜色搭配
                    ListColors lc = ListSchemeColors(g_set.colorScheme);
                    cd->clrText = lc.text;
                    cd->clrTextBk = lc.back;
                    painted = true;
                }
                if (i < g_items.size() && g_items[i].conflict) {
                    cd->clrText = kConflictRgb;
                    painted = true;
                }
                // CDRF_NEWFONT makes the colours above stick; the subitem notification is
                // still needed so columns 2..7 can be coloured separately.
                return Reply(painted ? (CDRF_NEWFONT | CDRF_NOTIFYSUBITEMDRAW)
                                     : CDRF_NOTIFYSUBITEMDRAW);
            }
            case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
                size_t i = (size_t)cd->nmcd.lItemlParam;
                bool painted = false;
                if (g_set.colorScheme != 0) {
                    ListColors lc = ListSchemeColors(g_set.colorScheme);
                    cd->clrText = lc.subtext;
                    cd->clrTextBk = lc.back;
                    painted = true;
                }
                if (i < g_items.size() && g_items[i].conflict &&
                    (cd->iSubItem == 1 || cd->iSubItem == 2)) {
                    cd->clrText = kConflictRgb;
                    painted = true;
                }
                return Reply(painted ? CDRF_NEWFONT : CDRF_DODEFAULT);
            }
            }
            return Reply(CDRF_DODEFAULT);
        }
        return Reply(0);
    }
    case WM_CONTEXTMENU: {
        // 只有落在文件列表上的右键才出这个菜单，其它地方交给默认处理。
        if ((HWND)wParam != g_hList) return FALSE;
        POINT pt = { (SHORT)LOWORD(lParam), (SHORT)HIWORD(lParam) };
        int hit = -1;
        if (pt.x == -1 && pt.y == -1) {
            // 键盘 Menu / Shift+F10: lParam 是 -1，用焦点行当锚点
            hit = ListView_GetNextItem(g_hList, -1, LVNI_FOCUSED);
            if (hit < 0) return TRUE;
            RECT r;
            if (ListView_GetItemRect(g_hList, hit, &r, LVIR_BOUNDS)) {
                pt.x = r.left + S(12);
                pt.y = (r.top + r.bottom) / 2;
                ClientToScreen(g_hList, &pt);
            } else {
                GetCursorPos(&pt);
            }
        } else {
            POINT cp = pt;
            ScreenToClient(g_hList, &cp);
            LVHITTESTINFO ht = { 0 };
            ht.pt = cp;
            hit = ListView_SubItemHitTest(g_hList, &ht);
            if (hit >= 0 && !(ht.flags & LVHT_ONITEM)) hit = -1;
        }
        // 右键点在未选中的行上时，把选中集换成这一行 (资源管理器的习惯)。
        if (hit >= 0 && !(ListView_GetItemState(g_hList, hit, LVIS_SELECTED) & LVIS_SELECTED)) {
            ListView_SetItemState(g_hList, -1, 0, LVIS_SELECTED);
            ListView_SetItemState(g_hList, hit, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
            UpdateTagPanel();
        }
        bool hasTarget = !CtxTargets(hit).empty();
        HMENU m = CreatePopupMenu();
        if (!m) return TRUE;
        AppendMenuW(m, hasTarget ? MF_STRING : MF_GRAYED,
                    IDM_CTX_TAGREAD, LS(IDS_CTX_TAGREAD));
        AppendMenuW(m, hasTarget ? MF_STRING : MF_GRAYED,
                    IDM_CTX_PLAY, LS(IDS_CTX_PLAY));
        AppendMenuW(m, MF_SEPARATOR, 0, 0);
        AppendMenuW(m, MF_STRING, IDM_CTX_CHECK, LS(IDS_CTX_CHECK));
        AppendMenuW(m, MF_STRING, IDM_CTX_UNCHECK, LS(IDS_CTX_UNCHECK));
        AppendMenuW(m, MF_STRING, IDM_CTX_SELALL, CtxLabel(IDS_CTX_SELALL, L"Ctrl+A").c_str());
        AppendMenuW(m, MF_STRING, IDM_CTX_SELNONE, LS(IDS_CTX_SELNONE));
        AppendMenuW(m, MF_SEPARATOR, 0, 0);
        // 「删除选择的列表项」也挂在列表的 Delete 键上, 在菜单里标出来才好发现。
        AppendMenuW(m, hasTarget ? MF_STRING : MF_GRAYED,
                    IDM_CTX_DELROWS, CtxLabel(IDS_CTX_DELROWS, L"Del").c_str());
        AppendMenuW(m, g_items.empty() ? MF_GRAYED : MF_STRING,
                    IDM_CTX_CLEARLIST, LS(IDS_CTX_CLEARLIST));
        int cmd = (int)TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                      pt.x, pt.y, 0, g_hDlg, NULL);
        DestroyMenu(m);
        if (cmd) RunCommand(cmd);
        return TRUE;
    }
    case WM_VSCROLL:
        // Only the 序号 spin arrows reach here (the list view's scroll bars belong to the
        // list view itself). They have already written the new number into their buddy.
        AutoPreview();
        return TRUE;
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        // Edit changes -> live preview
        switch (id) {
        case IDC_OV_NAME_TEXT: case IDC_OV_EXT_TEXT:
        case IDC_RP_FIND: case IDC_RP_REPL:
        case IDC_NM_TPL: case IDC_NM_START: case IDC_NM_STEP: case IDC_NM_WIDTH:
        case IDC_AD_TEXT: case IDC_AD_POS:
        case IDC_DL_CONTENT: case IDC_DL_START: case IDC_DL_NUM:
        case IDC_EX_TEXT: case IDC_EX_CMD:
        case IDC_TG_EXPR:
            if (code == EN_CHANGE) { AutoPreview(); return TRUE; }
            break;
        case IDC_OV_NAME_UPPER: case IDC_OV_NAME_LOWER: case IDC_OV_NAME_TITLE:
        case IDC_OV_EXT_UPPER: case IDC_OV_EXT_LOWER:
        case IDC_RP_ENABLE: case IDC_RP_REGEX: case IDC_RP_CASEI:
        case IDC_NM_ENABLE: case IDC_NM_PAD: case IDC_NM_LETTER: case IDC_NM_UPPER:
        case IDC_AD_ENABLE: case IDC_AD_PRE: case IDC_AD_SUF: case IDC_AD_EXTPRE: case IDC_AD_EXTSUF: case IDC_AD_AT:
        case IDC_DL_ENABLE: case IDC_DL_TEXT: case IDC_DL_RANGE: case IDC_DL_HEAD: case IDC_DL_TAIL: case IDC_DL_NAME: case IDC_DL_EXT:
        case IDC_EX_ENABLE: case IDC_EX_ADD: case IDC_EX_DEL: case IDC_EX_CHG: case IDC_EX_CASE: case IDC_EX_UP: case IDC_EX_LO:
        case IDC_TG_ENABLE:
            if (code == BN_CLICKED) { AutoPreview(); return TRUE; }
            break;
        case IDC_OV_FILTER:
            // SELCHANGE = picked a preset, EDITCHANGE = typed it by hand (or picked one,
            // which rewrites the edit field too).
            if (code == CBN_SELCHANGE || code == CBN_EDITCHANGE) { AutoPreview(); return TRUE; }
            break;
        case IDC_NM_PRESET:
            // A filler for the 模板 edit, not a setting in its own right, so the pick is
            // copied into that box and the box's own EN_CHANGE path does the preview.
            // SetText alone would not recompute (programmatic WM_SETTEXT sends no
            // EN_CHANGE here), hence the explicit call.
            if (code == CBN_SELCHANGE) {
                int sel = (int)SendMessageW((HWND)lParam, CB_GETCURSEL, 0, 0);
                wchar_t buf[64] = {};
                if (sel >= 0) SendMessageW((HWND)lParam, CB_GETLBTEXT, sel, (LPARAM)buf);
                SetText(IDC_NM_TPL, wcscmp(buf, LS(IDS_CLEAR_LIST)) == 0 ? L"" : buf);
                AutoPreview();
                return TRUE;
            }
            break;
        case IDC_EX_PRESET:
            // 扩展命令 is a stack of command names, so a double-click appends the entry
            // rather than replacing what is already there (see ApplyExtCommands).
            if (code == LBN_DBLCLK) {
                int sel = (int)SendMessageW((HWND)lParam, LB_GETCURSEL, 0, 0);
                if (sel < 0) return TRUE;
                wchar_t buf[64] = {};
                SendMessageW((HWND)lParam, LB_GETTEXT, sel, (LPARAM)buf);
                SetText(IDC_EX_CMD, GetText(IDC_EX_CMD) + buf);
                AutoPreview();
                return TRUE;
            }
            break;
        case IDC_TG_PRESET:
        case IDC_TW_PRESET:
            // A filler for the expression box, not a setting of its own: the pick is
            // copied in and the box stays hand-editable.
            if (code == CBN_SELCHANGE) {
                int sel = (int)SendMessageW((HWND)lParam, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < kTagPresetCount)
                    SetText(id == IDC_TG_PRESET ? IDC_TG_EXPR : IDC_TW_EXPR,
                            kTagPresets[sel].expr);
                if (id == IDC_TG_PRESET) AutoPreview();
                return TRUE;
            }
            break;
        case IDC_TG_B1: case IDC_TG_B2: case IDC_TG_B3:
        case IDC_TG_B4: case IDC_TG_B5: case IDC_TG_B6:
            if (code == BN_CLICKED) {
                wchar_t tok[4] = { L'%', (wchar_t)(L'1' + (id - IDC_TG_B1)), 0, 0 };
                InsertTagToken(IDC_TG_EXPR, tok);
                AutoPreview();
                return TRUE;
            }
            break;
        case IDC_TW_CLEAR:
            if (code == BN_CLICKED) {
                const int flds[] = { IDC_TW_ARTIST, IDC_TW_TITLE, IDC_TW_ALBUM,
                                     IDC_TW_TRACK, IDC_TW_YEAR, IDC_TW_COMMENT };
                for (int f : flds) SetText(f, L"");
                SetWindowTextW(GetDlgItem(hDlg, IDC_TW_GENRE), L"");
                return TRUE;
            }
            break;
        case IDC_TW_SAVE:
        case IDC_TW_WRITE:
            // 保存只管打勾(或选中)的行，全写覆盖整个列表。
            if (code == BN_CLICKED) { DoTagWrite(id == IDC_TW_WRITE); return TRUE; }
            break;
        }

        // 工具栏第二排、菜单条目和快捷键都走同一个分派 (RunCommand)，三者不会走偏。
        // code: 0 = 按钮 BN_CLICKED / 菜单选择, 1 = ACCEL (TranslateAcceleratorW 产生)。
        switch (id) {
        case IDC_TB_ADD: case IDC_TB_ADDF: case IDC_TB_PREVIEW: case IDC_TB_APPLY:
        case IDC_TB_UNDO: case IDC_TB_PIN: case IDC_TB_TAGDEL: case IDC_TB_TAGEXTRACT:
        case IDC_TB_TAGWRITE: case IDC_TB_T2S: case IDC_TB_TEXTCMD: case IDC_TB_EXPLIST:
        case IDC_TB_RECOPEN: case IDC_TB_RECSAVE: case IDC_TB_RECCLEAR: case IDC_TB_CHGDATE:
        case IDC_TB_REC_PANEL: case IDC_TB_SPONSOR:
        case IDM_FILE_OPEN: case IDM_FILE_ADDFOLDER: case IDM_FILE_EXIT: case IDM_FILE_EXPORT:
        case IDM_REN_PREVIEW: case IDM_REN_APPLY: case IDM_REN_UNDO:
        case IDM_REC_OPEN: case IDM_REC_SAVE: case IDM_REC_CLEAR: case IDM_TEXT_NAMING:
        case IDM_TAG_EXTRACT: case IDM_NAME_T2S: case IDM_NAME_S2T: case IDM_NAME_BIG5GB:
        case IDM_NAME_GBBIG5: case IDM_NAME_SJISZH: case IDM_NAME_ZHSJIS: case IDM_NAME_CRC32:
        case IDM_NAME_WEBTITLE: case IDM_NAME_TEXTLINE1:
        case IDM_TAG_REMOVE: case IDM_TAG_WRITEALL: case IDM_TAG_EXPORT: case IDM_TAG_FROMTEXT:
        case IDM_ID3_V1TOV2: case IDM_ID3_V2TOV1: case IDM_TAG_BIG5GB: case IDM_TAG_GBBIG5:
        case IDM_TAG_T2S: case IDM_TAG_S2T: case IDM_TAG_SJISZH: case IDM_TAG_ZHSJIS:
        case IDM_OPT_SETTINGS: case IDM_OPT_CENTER: case IDM_SHELL_ADD: case IDM_SHELL_REMOVE:
        case IDM_SENDTO_ADD: case IDM_SENDTO_REMOVE:
        case IDM_CTX_SELALL: case IDM_CTX_SELNONE: case IDM_CTX_CHECK: case IDM_CTX_UNCHECK:
        case IDM_CTX_DELROWS: case IDM_CTX_CLEARLIST: case IDM_CTX_TAGREAD: case IDM_CTX_PLAY:
        case IDM_HELP_MANUAL: case IDM_HELP_ABOUT: case IDM_LANG_OPEN_DIR:
            if (code == 0 || code == 1) RunCommand(id);
            return TRUE;
        default:
            // 选项 > 语言 子菜单的命令 ID 是运行时分配的, 写不成 case 标签。
            if (id >= IDM_LANG_BASE && id < IDM_LANG_OPEN_DIR && (code == 0 || code == 1)) {
                RunCommand(id);
                return TRUE;
            }
            break;
        }
        return TRUE;
    }
    case WM_CLOSE:
        if (g_set.confirmExit &&
            MessageBoxW(hDlg, LS(IDS_MSG_CONFIRM_EXIT), LS(IDS_MSG_CONFIRM_EXIT_T), MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
            return TRUE;
        if (g_pDrop) { RevokeDragDrop(g_hList); g_pDrop->Release(); g_pDrop = nullptr; }
        DestroyWindow(hDlg);
        return TRUE;
    case WM_DESTROY:
        if (g_wheelHook) { UnhookWindowsHookEx(g_wheelHook); g_wheelHook = NULL; }
        PostQuitMessage(0);
        return TRUE;
    }
    return FALSE;
}

// 快捷键 vs 输入框: 焦点在编辑框里时, 别把编辑键抢走
// ----------------------------------------------------------------------------
// 消息循环里 TranslateAcceleratorW 跑在最前面, 加速器命中就不再往下传。
// 于是 Ctrl+A (全选列表行 4205) 和 Ctrl+Z (撤销更名 4013) 会在用户正在输入的时候
// 被截走——在 查找 框里按 Ctrl+A, 结果是列表全选、框里的文字一个都没选中。
// 焦点在输入框里时放行这些组合键, 交给编辑框自己处理。
static bool FocusIsTextInput() {
    HWND f = GetFocus();
    if (!f) return false;
    wchar_t cls[32] = { 0 };
    if (GetClassNameW(f, cls, 32)) {
        if (_wcsicmp(cls, L"Edit") == 0 || _wcsicmp(cls, L"ComboBox") == 0) return true;
    }
    // 组合框的编辑部分是它的子窗口, 焦点可能落在子窗口上
    HWND p = GetParent(f);
    if (p && p != f && GetClassNameW(p, cls, 32) && _wcsicmp(cls, L"ComboBox") == 0)
        return true;
    return false;
}

// 只挡编辑键本身。Ctrl+Shift+<字母> 那批新快捷键不在其中, 在输入框里也照常生效。
static bool AccelBlockedByEdit(const MSG& msg) {
    if (msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN) return false;
    if (!FocusIsTextInput()) return false;
    if (!(GetKeyState(VK_CONTROL) & 0x8000)) return false;
    if (GetKeyState(VK_SHIFT) & 0x8000) return false;       // 带 Shift 的不算编辑键
    switch (msg.wParam) {
    case 'A': case 'C': case 'V': case 'X': case 'Y': case 'Z':
    case VK_INSERT:
        return true;
    }
    return false;
}

// Entry point
// ----------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    OleInitialize(NULL);
    INITCOMMONCONTROLSEX icex = { sizeof(icex),
        ICC_LISTVIEW_CLASSES | ICC_WIN95_CLASSES | ICC_TAB_CLASSES |
        ICC_BAR_CLASSES | ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icex);
    SettingsLoad();
    LangInit(hInst);        // 必须在建窗口之前: WM_INITDIALOG 里就要取界面文本

    // ---- 命令行文件 (可能是多个: 直接带参启动 / 拖到 exe 上 / shell 逐个调用) ----
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> files;
    bool shellAdd = false, shellRemove = false;
    if (argv && argc > 1)
        for (int i = 1; i < argc; ++i) {
            // 提权副本专用开关(见 DoShellMenu): 只装 / 卸右键菜单, 不能当文件路径看。
            if      (!_wcsicmp(argv[i], L"/shell-add"))    shellAdd = true;
            else if (!_wcsicmp(argv[i], L"/shell-remove")) shellRemove = true;
            else files.push_back(argv[i]);
        }
    if (shellAdd || shellRemove) {
        // 提权副本: 干完就退出 —— 不建窗口、也不参与下面的单实例判定(此刻还没建互斥体),
        // 否则会被当成"第二个实例", 把参数转交给前一个(没提权的)实例然后退出, 白跑一趟。
        DoShellMenu(shellAdd, false);
        if (argv) LocalFree(argv);
        OleUninitialize();
        return 0;
    }

    // ---- 单实例判定: 命名互斥体已在 = 有实例在跑 ----
    HANDLE hMutex = CreateMutexW(NULL, FALSE, kInstanceMutex);
    bool primary = (hMutex != NULL) && GetLastError() != ERROR_ALREADY_EXISTS;
    if (!primary) {
        // 资源管理器会连着开好几个进程, 第一个可能还没把窗口建好: 等一下它。
        HWND exist = NULL;
        for (int i = 0; i < 40 && !exist; ++i) {               // 最多 ~2 秒
            EnumWindows(FindInstanceWnd, (LPARAM)&exist);
            if (!exist) Sleep(50);
        }
        if (exist) {
            // 转交文件并激活已有窗口, 自己悄悄退出。
            SendFilesToInstance(exist, files);
            BringInstanceToFront(exist);
            if (hMutex) CloseHandle(hMutex);
            if (argv) LocalFree(argv);
            OleUninitialize();
            return 0;
        }
        // 等了 2 秒还找不到窗口 (那个实例大概正在退出): 自己接管, 别让用户点不开。
        primary = true;
        hMutex = NULL;                                         // 互斥体不归我们, 不强求
    }

    HWND hdlg = CreateDialogParamW(hInst, MAKEINTRESOURCEW(IDR_MAINDLG), NULL, DlgProc, 0);
    if (!hdlg) {
        MessageBoxW(NULL, L"无法创建主窗口。", L"错误", MB_OK | MB_ICONERROR);
        if (hMutex) CloseHandle(hMutex);
        if (argv) LocalFree(argv);
        return 1;
    }
    // 挂上实例标记, 后来的进程靠它找到本窗口 (EnumWindows + GetPropW)。
    SetPropW(hdlg, kInstanceProp, (HANDLE)1);

    // Load the command-line files BEFORE showing the window: a visible-but-empty dialog
    // would queue up clicks that act on an empty list (the load blocks the UI thread).
    if (!files.empty()) {
        std::vector<fs::path> paths;
        for (const auto& f : files) paths.push_back(fs::path(f));
        AddPaths(paths);
    }
    if (argv) LocalFree(argv);

    ShowWindow(hdlg, nCmdShow);
    UpdateWindow(hdlg);
    if (!files.empty()) SetForegroundWindow(hdlg);

    HACCEL hAcc = LoadAcceleratorsW(hInst, MAKEINTRESOURCEW(IDR_MAINACC));

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        // Accelerators first: the dialog manager would otherwise eat Ctrl+<letter> as a
        // character message and no WM_COMMAND would reach RunCommand.
        // AccelBlockedByEdit() 是后加的: 焦点在输入框里时 Ctrl+A / Ctrl+Z 要留给编辑框。
        if (hAcc && !AccelBlockedByEdit(msg) && TranslateAcceleratorW(hdlg, hAcc, &msg))
            continue;
        if (!IsDialogMessageW(hdlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    // 主实例退出: 互斥体随手柄一起还给系统, 下次启动就是新主实例。
    if (hMutex) CloseHandle(hMutex);
    OleUninitialize();
    return (int)msg.wParam;
}
