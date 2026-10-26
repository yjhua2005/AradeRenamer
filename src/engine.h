#pragma once
#include <string>
#include <vector>
#include <map>
#include <filesystem>

namespace fs = std::filesystem;

// Audio tags read from a file. Only the fields the two 标签 tabs expose; %1..%6 in a
// 标签表达式 map to artist/title/track/album/year/comment in that order (genre is
// 流派, which only 标签写入 shows).
struct AudioTags {
    std::wstring artist, title, album, track, year, comment, genre;
    bool HasAny() const {
        return !(artist.empty() && title.empty() && album.empty() &&
                 track.empty() && year.empty() && comment.empty() && genre.empty());
    }
};

// One file in the list
struct FileItem {
    fs::path fullPath;      // absolute path to the file
    std::wstring oldName;   // original filename (for display + undo reference)
    std::wstring newName;   // computed preview name (no directory)
    std::wstring dedupeKey; // lower-cased absolute path, used to drop duplicate adds
    bool checked = true;    // 打勾列: unchecked rows skip 预览/应用
    bool conflict = false;  // duplicate target detected during preview
    AudioTags tags;         // valid once tagsRead is set
    bool tagsRead = false;
};

// All rename operations. Each tab section can be toggled independently; the
// engine applies them as a fixed pipeline (matching 拖把更名器's combining tabs).
struct RenameConfig {
    // ---- 整体 (Overall) ----
    int ovNameCase = 0;          // 0 none, 1 upper, 2 lower, 3 title (first letter of each word)
    std::wstring ovNameOverride; // "文件名改成" : if non-empty, becomes the new stem
    int ovExtCase = 0;           // 0 none, 1 upper, 2 lower
    std::wstring ovExtOverride;  // "扩展名改成" : if non-empty, becomes the new extension
    std::wstring fileFilter;     // "文件过滤" : display/rename filter (not applied to compute)

    // ---- 替换 (Replace) ----
    bool rpEnable = false;
    std::wstring rpFind, rpReplace;
    bool rpRegex = false;        // explicit regex toggle (or find starts with ':')
    bool rpCaseInsensitive = false;

    // ---- 序号 (Numbering) ----
    bool nmEnable = false;
    std::wstring nmTemplate;     // e.g. "<NAME>_#" or just "#"; '#' -> number
    int nmStart = 1, nmStep = 1;
    int nmWidth = 2;             // 位数: digit count the number is padded to
    bool nmPad = false;          // 不足位补齐: left-pad with '0' up to 位数
    bool nmLetters = false;      // 0 -> 'A'
    bool nmUpper = false;        // 字母大写 (only matters with 字母编号)

    // ---- 添加 (Add) ----
    bool adEnable = false;
    std::wstring adText;
    int adWhere = 0;             // 0 name prefix, 1 name suffix, 2 ext prefix, 3 ext suffix, 4 at position
    int adPos = 1;               // 1-based insertion position (for adWhere==4)

    // ---- 删除 (Delete) ----
    bool dlEnable = false;
    int dlMode = 0;              // 0 text, 1 head N, 2 tail N, 3 range
    std::wstring dlText;
    int dlStart = 1;             // range: 1-based character to start at
    int dlNum = 1;               // range: how many to drop; head/tail: N
    int dlTarget = 0;            // 0 name, 1 extension

    // ---- 扩展更名 (Extension rename) ----
    bool exEnable = false;
    int exMode = 0;              // 0 add, 1 delete, 2 change, 3 case
    std::wstring exText;
    int exCase = 1;              // 1 upper, 2 lower
    std::wstring exCmd;          // 扩展命令: command names glued together, see ApplyExtCommands

    // ---- 标签命名 ----
    bool tgEnable = false;
    std::wstring tgExpr;         // %1艺术家 %2标题 %3音轨 %4专辑 %5年代 %6注释
    bool trackPad2 = false;      // 音轨数字低于两位自动补0 (设置>音乐文件标签)

    // ---- 其它 ----
    bool useMeta = false;        // expand <DATE> etc.
    bool crcEnable = false;      // rename base to CRC32 hex, keep ext
    int  convMode = 0;           // 简繁互转: 0 关, 1 繁体转简体, 2 简体转繁体
    bool autoCorrect = false;    // 自动更正错误: repair an illegal target name
    int  wordSplitMode = 0;      // 文件名单词判断字符: 0 空格, 1 非英文字母和数字

    // ---- 名字来源 (菜单命令, 互斥的一个开关) ----
    // 0 关; 1 以网页文件的标题更名 (只作用于 .htm/.html);
    // 2 以文本文件的第一行内容更名 (只作用于 .txt);
    // 3 调用文本文件命令 (textCmds 里查 原名->新名)。
    int nameSource = 0;
    const std::map<std::wstring, std::wstring>* textCmds = nullptr;

    // ---- 文件名内码转换 (菜单命令) ----
    // 0 关; 1 BIG5->GB; 2 GB->BIG5; 3 日文Shift-JIS转中文; 4 中文转日文Shift-JIS。
    // 修的是"按错误代码页误读"的文件名: 每个字符先取回原代码页字节再按目标代码页解释。
    int encConv = 0;
};

// Compute the new name for item at position `index` in the full list.
std::wstring ComputeNewName(const FileItem& item,
                            const RenameConfig& cfg,
                            int index);

// CRC32 hex (8 uppercase chars) of file content.
std::wstring Crc32Hex(const fs::path& p);

// Read the tags of one audio file with the hand-rolled readers below (no tag library).
// Returns an empty AudioTags for anything it does not recognise. id3v2First picks which
// MP3 block wins when a file carries both (设置 > 音乐文件标签).
AudioTags ReadAudioTags(const fs::path& p, bool id3v2First = false);

// True when the extension is one the tag readers cover, so callers can skip opening
// every .zip and .exe in a folder.
bool IsTaggableExt(const fs::path& p);

// Substitute %1..%6 from `t` into `expr`; %% yields a literal %.
std::wstring ExpandTagExpr(const std::wstring& expr, const AudioTags& t);

// 标签写入。writeMode 同 设置 > 音乐文件标签 > ID3标签写入时: 0 只v1, 1 只v2, 2 同时。
// MP3 (ID3v1 / ID3v2.2-2.4)、FLAC (VORBIS_COMMENT)、OGG/Opus (Vorbis comment,
// 重算分页 CRC)、APE (APEv2)、M4A/AAC (MP4 ilst, 含 stco/co64 偏移修正)、
// WAV (RIFF INFO) 都能写；不认识的容器返回 1。不认识的原有 frame / block 原样保留。
// 返回 0 成功 / 1 不支持 / 2 读写失败。
int WriteAudioTags(const fs::path& p, const AudioTags& t, int writeMode);

// 移除音乐文件标签。removeMode 同 设置 > 音乐文件标签 > 移除ID3标签时:
// 0 只移除ID3v1, 1 只移除ID3v2, 2 同时 (对非 MP3 容器忽略此参数, 全部移除)。
// MP3 剥掉头部 ID3v2 / 尾部 ID3v1；FLAC 丢掉 VORBIS_COMMENT 块；OGG/Opus 把
// 注释包换成空包 (容器规范要求注释头必须存在, 不能整包删)；APE 剥掉尾部 APE 标签；
// M4A 清空 ilst；WAV 删掉 LIST-INFO 块。返回 0 成功 / 1 不支持 / 2 读写失败。
int RemoveAudioTags(const fs::path& p, int removeMode);

// 只读 ID3v1 / 只读 ID3v2 (供 标签 > ID3v1↔ID3v2 互转)。没有对应标签时返回空结构。
AudioTags ReadId3v1Only(const fs::path& p);
AudioTags ReadId3v2Only(const fs::path& p);

// 内码转换 (修"按错误代码页误读"的名字/标签)。
// FixMojibake: s 里每个字符先按 misreadCp 取回字节, 再按 realCp 解释; 解释不了的字符原样留下。
// MakeMojibake: 反向 —— 先把整串编成 realCp 的字节, 再按 misreadCp 误读成字符串。
std::wstring FixMojibake(const std::wstring& s, unsigned int realCp, unsigned int misreadCp = 936);
std::wstring MakeMojibake(const std::wstring& s, unsigned int realCp, unsigned int misreadCp = 936);

// 从 .htm/.html 提取 <title> 作为新名字来源 (自动识别 meta charset / BOM, 解 HTML 实体)。
// 取不到标题时返回空串。
std::wstring HtmlTitleOf(const fs::path& p);
// 文本文件的第一行 (跳过 BOM, 去掉首尾空白); 读不到返回空串。
std::wstring FirstLineOf(const fs::path& p);

// 用 标签写入 的 文件名表达式 从文件名反解标签：%1艺术家 %2标题 %3音轨 %4专辑 %5年代 %6注释。
// 表达式没覆盖到的字段保持 `out` 里原来的值，所以可以先在页面上填专辑再按名字解析艺术家标题。
bool ParseNameToTags(const std::wstring& expr, const std::wstring& nameNoExt, AudioTags& out);
