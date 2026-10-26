// 多语言支持: 界面字符串全部走 LS(id), id 见 lang_ids.h (由 tools/extract_lang.py 生成)。
//
// 取字符串的回退链:  当前语言 (磁盘 Language\*.ini 或 exe 内嵌副本)
//                 -> 内置简体中文 (始终嵌在 exe 里, 所以单文件也完整可用)
//                 -> 空串 (不该发生: 键表是从代码和 .rc 抽出来的)
//
// 语言包是 INI 风格的扁平键值 (UTF-8, 键名 = 语言包 key, 见 Language\lang_zh-CN.ini);
// 往 exe 旁的 Language\ 目录里丢一个新的 .ini (文件里写 lang.name / lang.code) 就会
// 自动出现在 选项 > 语言 菜单里, 不需要改代码、不需要重新编译。
#pragma once
#include <windows.h>

#include <string>
#include <vector>

#include "lang_ids.h"   // 所有 LS() 用的 ID (由 tools/extract_lang.py 生成)

// 取一条界面文本。返回的是语言表里的指针, 切换语言后旧指针失效 —— 别跨切换保存。
const wchar_t* LS(int id);

struct LangInfo {
    std::wstring code;    // lang.code, 例如 zh-CN / en-US
    std::wstring name;    // lang.name, 菜单上显示的名字 (用各自语言写)
    std::wstring author;  // lang.author, 译者署名 (菜单上显示在名字后面的括号里; 可空)
    std::wstring file;    // 磁盘路径; 空串 = 内嵌在 exe 里
    int          resId;   // 内嵌资源的 RCDATA id; 0 = 磁盘文件
};

// 在建主窗口之前调用: 装好内置中文 + 读注册表里的语言选择 + 扫一遍 Language\。
void LangInit(HINSTANCE hInst);

const std::vector<LangInfo>& LangList();      // 可用语言 (供 选项 > 语言 菜单), 首项是 L"auto"
const std::wstring& LangCurrent();            // 实际生效的 lang.code (auto 已解析成具体语言)
const std::wstring& LangChoice();             // 菜单上勾的那一项 (可能是 L"auto")
bool LangSet(const std::wstring& code);       // 只换语言表, 不刷新界面
void LangSaveChoice(const std::wstring& code);// 记住选择 (HKCU\Software\AradeRenamer\Language)
std::wstring LangDir();                       // <exe 目录>\Language

// 把当前语言的文本刷到窗口上: 标题、所有 Button/Static 子控件、菜单(含弹出菜单)。
// Tab 页名 / ListView 列头 / 工具栏提示 / 下拉条目 由 main.cpp 自己补,
// 因为那些不是按控件 ID 就能对上的。
// titleId 默认主窗口标题; 模态对话框传自己的 (IDS_DLG_SETTINGS / IDS_DLG_CHGDATE ...)。
void ApplyLanguage(HWND hDlg, int titleId = IDS_DLG_MAIN);
