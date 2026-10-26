#pragma once
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

// 简繁互转。内置表直接吃 OpenCC 的上游原文件（TSCharacters.txt / STCharacters.txt /
// TSPhrases.txt / STPhrases.txt，Apache-2.0），原样 RCDATA 打进 exe，运行时才解析，
// 所以升级字典只需覆盖文件重新编译，exe 单独拷走也照样能用。
// 词组优先于单字：转换时先在词组表里做最长匹配，匹配不到才逐字转。
// Add-ons\gbkfix.txt 是可选的自定义修正表，故意在运行时从磁盘读（不打进 exe）：
// 一行一对 "繁 简"，; 开头是注释，默认按 GBK 解码（带 UTF-8 BOM 时按 UTF-8）。
// 文件不存在就什么都不做；追加表优先于内置表，用户可以逐字覆盖上游的候选。

std::wstring ConvertT2S(const std::wstring& s);   // 繁体 -> 简体
std::wstring ConvertS2T(const std::wstring& s);   // 简体 -> 繁体
int LoadConvFixFile(const fs::path& p);           // 返回读到的条数，0 = 没有可用文件

// 把 RCDATA 读成 UTF-16（上游文件都是 UTF-8，无 BOM）。找不到资源就返回空串。
std::wstring LoadResText(int id);
