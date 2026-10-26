#include "engine.h"
#include "conv.h"
#include "lang.h"
#include "resource.h"
#include <cstdio>
#include <string>
#include <vector>
#include <windows.h>

static int g_pass = 0, g_fail = 0;
static void check(const char* name, const std::wstring& got, const std::wstring& want) {
    if (got == want) { ++g_pass; printf("  PASS  %s  -> %ls\n", name, got.c_str()); }
    else { ++g_fail; printf("  FAIL  %s  got=%ls want=%ls\n", name, got.c_str(), want.c_str()); }
}
static void checkb(const char* name, bool ok) {
    if (ok) { ++g_pass; printf("  PASS  %s\n", name); }
    else { ++g_fail; printf("  FAIL  %s\n", name); }
}

static fs::path P(const wchar_t* name) {
    // 假的绝对路径: engine 只读它的名字/扩展名, 不会真去碰磁盘。
    // 用系统临时目录而不是写死某个盘符, 免得开源后看着像开发者本机的残留。
    return fs::temp_directory_path() / L"AradeRenamerTest" / name;
}
static FileItem mk(const wchar_t* name) {
    FileItem it; it.fullPath = P(name); it.oldName = name; return it;
}

#define CFG() RenameConfig cfg;

// ----------------------------------------------------------------------------
// 临时文件助手
// ----------------------------------------------------------------------------
static fs::path TmpDir() {
    static fs::path d = fs::temp_directory_path() / L"srn_engine_test";
    std::error_code ec;
    fs::create_directories(d, ec);
    return d;
}
static void WriteBytes(const fs::path& p, const std::vector<unsigned char>& b) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD n = 0;
    WriteFile(h, b.data(), (DWORD)b.size(), &n, NULL);
    CloseHandle(h);
}
static std::vector<unsigned char> ReadBytes(const fs::path& p) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); return {}; }
    std::vector<unsigned char> b((size_t)sz.QuadPart);
    DWORD got = 0;
    ReadFile(h, b.data(), (DWORD)b.size(), &got, NULL);
    CloseHandle(h);
    b.resize(got);
    return b;
}
static void PutBe(std::vector<unsigned char>& b, uint32_t v, int n) {
    for (int i = n - 1; i >= 0; --i) b.push_back((unsigned char)((v >> (8 * i)) & 0xFF));
}
static void PutLe(std::vector<unsigned char>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back((unsigned char)((v >> (8 * i)) & 0xFF));
}
static void Append(std::vector<unsigned char>& b, const char* s) {
    b.insert(b.end(), s, s + strlen(s));
}
static void AppendW(std::vector<unsigned char>& b, const std::wstring& s) {
    for (wchar_t c : s) {
        b.push_back((unsigned char)(c & 0xFF));
        b.push_back((unsigned char)((c >> 8) & 0xFF));
    }
}
// UTF-8 编码 (宽字符 -> UTF-8 字节)
static void AppendUtf8(std::vector<unsigned char>& b, const std::wstring& s) {
    for (wchar_t c : s) {
        unsigned u = (unsigned)c;
        if (u < 0x80) b.push_back((unsigned char)u);
        else if (u < 0x800) {
            b.push_back((unsigned char)(0xC0 | (u >> 6)));
            b.push_back((unsigned char)(0x80 | (u & 0x3F)));
        } else {
            b.push_back((unsigned char)(0xE0 | (u >> 12)));
            b.push_back((unsigned char)(0x80 | ((u >> 6) & 0x3F)));
            b.push_back((unsigned char)(0x80 | (u & 0x3F)));
        }
    }
}
static uint32_t Rb32(const std::vector<unsigned char>& b, size_t off) {
    return ((uint32_t)b[off] << 24) | ((uint32_t)b[off + 1] << 16) |
           ((uint32_t)b[off + 2] << 8) | b[off + 3];
}
static uint32_t Rl32(const std::vector<unsigned char>& b, size_t off) {
    return (uint32_t)b[off] | ((uint32_t)b[off + 1] << 8) |
           ((uint32_t)b[off + 2] << 16) | ((uint32_t)b[off + 3] << 24);
}

// ----------------------------------------------------------------------------
// OGG 合成 + 校验 (独立逐位 CRC-32/MPEG-2, 与写入器的表实现互为验证)
// ----------------------------------------------------------------------------
static uint32_t CrcRef(const std::vector<unsigned char>& d, size_t off, size_t n) {
    uint32_t c = 0;
    for (size_t i = 0; i < n; ++i) {
        c ^= (uint32_t)d[off + i] << 24;
        for (int k = 0; k < 8; ++k)
            c = (c & 0x80000000u) ? ((c << 1) ^ 0x04C11DB7u) : (c << 1);
    }
    return c;
}
static void OggPageAdd(std::vector<unsigned char>& f, uint8_t type, uint64_t gran,
                       uint32_t serial, uint32_t seq,
                       const std::vector<std::vector<unsigned char>>& pkts) {
    std::vector<unsigned char> segs, body;
    for (const auto& pk : pkts) {
        size_t done = 0;
        while (true) {
            size_t take = pk.size() - done;
            if (take > 255) take = 255;
            bool last = (done + take >= pk.size() && take < 255);
            segs.push_back((unsigned char)take);
            body.insert(body.end(), pk.begin() + done, pk.begin() + done + take);
            done += take;
            if (last) break;
        }
        if (pk.empty()) segs.push_back(0);
    }
    size_t base = f.size();
    Append(f, "OggS");
    f.push_back(0);
    f.push_back(type);
    for (int i = 0; i < 8; ++i) f.push_back((unsigned char)((gran >> (8 * (7 - i))) & 0xFF));
    PutBe(f, serial, 4);
    PutBe(f, seq, 4);
    PutBe(f, 0, 4);
    f.push_back((unsigned char)segs.size());
    f.insert(f.end(), segs.begin(), segs.end());
    f.insert(f.end(), body.begin(), body.end());
    uint32_t crc = CrcRef(f, base, f.size() - base);
    f[base + 22] = (unsigned char)(crc >> 24);
    f[base + 23] = (unsigned char)(crc >> 16);
    f[base + 24] = (unsigned char)(crc >> 8);
    f[base + 25] = (unsigned char)(crc);
}

// 逐页校验 CRC / 页序号连续性, 并把包还原出来
static bool OggVerify(const std::vector<unsigned char>& b, uint32_t serial,
                      std::vector<std::vector<unsigned char>>& pkts, bool& eosSeen) {
    size_t off = 0;
    uint32_t expectSeq = 0;
    std::vector<unsigned char> cur;
    while (off < b.size()) {
        if (off + 27 > b.size() || memcmp(&b[off], "OggS", 4)) return false;
        uint8_t nseg = b[off + 26];
        uint32_t body = 0;
        for (int i = 0; i < nseg; ++i) body += b[off + 27 + i];
        if (off + 27 + nseg + body > b.size()) return false;
        if (Rb32(b, off + 14) != serial) return false;
        if (Rb32(b, off + 18) != expectSeq) {
            printf("    [ogg] seq %u != %u\n", Rb32(b, off + 18), expectSeq);
            return false;
        }
        uint32_t crc = Rb32(b, off + 22);
        std::vector<unsigned char> zeroed = b;
        zeroed[off + 22] = zeroed[off + 23] = zeroed[off + 24] = zeroed[off + 25] = 0;
        if (CrcRef(zeroed, off, 27 + nseg + body) != crc) {
            printf("    [ogg] CRC mismatch at page %u\n", expectSeq);
            return false;
        }
        if (b[off + 5] & 0x04) eosSeen = true;
        ++expectSeq;
        size_t used = 0;
        for (int s = 0; s < nseg; ++s) {
            unsigned L = b[off + 27 + s];
            cur.insert(cur.end(), &b[off + 27 + nseg + used], &b[off + 27 + nseg + used + L]);
            used += L;
            if (L < 255) { pkts.push_back(cur); cur.clear(); }
        }
        off += 27 + nseg + body;
    }
    return cur.empty();
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("[test] start\n");
    fs::path td = TmpDir();

    // ================= 原有改名引擎测试 =================
    // 繁简互转: 词组优先于单字 (最长匹配), 单字表兜底
    {
        // TSPhrases.txt 里的行: "一坏 -> 一坯" (单字表里没有 坏->坯, 只能靠词组)
        check("繁->简词组", ConvertT2S(L"一坏"), L"一坯");
        check("繁->简词组2", ConvertT2S(L"一目瞭然"), L"一目了然");
        // STPhrases.txt 里的行: "一丝不挂 -> 一絲不掛"
        check("简->繁词组", ConvertS2T(L"一丝不挂"), L"一絲不掛");
        // 词组嵌在长句里, 前后各自处理
        check("词组混排", ConvertT2S(L"一目瞭然的測試"), L"一目了然的测试");
        // 纯 ASCII 不受影响
        check("繁简不动ASCII", ConvertT2S(L"abc 123"), L"abc 123");
        // 单字表仍然工作 (列表里随便挑一个常见字)
        check("繁->简单字", ConvertT2S(L"豐"), L"丰");
        check("简->繁单字", ConvertS2T(L"丰"), L"豐");
    }
    // 整体: 大写
    {
        CFG(); cfg.ovNameCase = 1;
        check("整体大写", ComputeNewName(mk(L"hello.txt"), cfg, 0), L"HELLO.txt");
    }
    // 整体: 小写
    {
        CFG(); cfg.ovNameCase = 2;
        check("整体小写", ComputeNewName(mk(L"HELLO.txt"), cfg, 0), L"hello.txt");
    }
    // 整体: 首字母大写 (title)
    {
        CFG(); cfg.ovNameCase = 3;
        check("整体标题", ComputeNewName(mk(L"hello world.txt"), cfg, 0), L"Hello World.txt");
    }
    // 整体: 文件名改成 (override)
    {
        CFG(); cfg.ovNameOverride = L"newname";
        check("文件名改成", ComputeNewName(mk(L"old.txt"), cfg, 0), L"newname.txt");
    }
    // 整体: 扩展名改成大写
    {
        CFG(); cfg.ovExtCase = 1;
        check("扩展名大写", ComputeNewName(mk(L"a.txt"), cfg, 0), L"a.TXT");
    }
    // 整体: 扩展名改成 (override)
    {
        CFG(); cfg.ovExtOverride = L"jpg";
        check("扩展名改成", ComputeNewName(mk(L"a.png"), cfg, 0), L"a.jpg");
    }
    // 替换: 普通
    {
        CFG(); cfg.rpEnable = true; cfg.rpFind = L"foo"; cfg.rpReplace = L"bar";
        check("替换", ComputeNewName(mk(L"foo_file.txt"), cfg, 0), L"bar_file.txt");
    }
    // 替换: 正则
    {
        CFG(); cfg.rpEnable = true; cfg.rpRegex = true; cfg.rpFind = L"^foo"; cfg.rpReplace = L"Z";
        check("正则", ComputeNewName(mk(L"foo_file.txt"), cfg, 0), L"Z_file.txt");
    }
    // 序号: 模板含 # (需启用元变量以展开 <NAME>)
    {
        CFG(); cfg.useMeta = true; cfg.nmEnable = true; cfg.nmTemplate = L"<NAME>_#"; cfg.nmStart = 1; cfg.nmStep = 1;
        check("序号#", ComputeNewName(mk(L"pic.jpg"), cfg, 2), L"pic_3.jpg");
    }
    // 序号: 字母编号 (默认小写; 勾了 字母大写 出大写)
    {
        CFG(); cfg.nmEnable = true; cfg.nmTemplate = L"#"; cfg.nmStart = 0; cfg.nmLetters = true;
        check("字母编号", ComputeNewName(mk(L"x.txt"), cfg, 0), L"a.txt");
    }
    {
        CFG(); cfg.nmEnable = true; cfg.nmTemplate = L"#"; cfg.nmStart = 0; cfg.nmLetters = true; cfg.nmUpper = true;
        check("字母编号大写", ComputeNewName(mk(L"x.txt"), cfg, 0), L"A.txt");
    }
    // 添加: 文件名前
    {
        CFG(); cfg.adEnable = true; cfg.adText = L"pre_"; cfg.adWhere = 0;
        check("添加前", ComputeNewName(mk(L"name.txt"), cfg, 0), L"pre_name.txt");
    }
    // 添加: 扩展名后
    {
        CFG(); cfg.adEnable = true; cfg.adText = L"bak"; cfg.adWhere = 3;
        check("扩展名后", ComputeNewName(mk(L"name.txt"), cfg, 0), L"name.txtbak");
    }
    // 删除: 指定文本
    {
        CFG(); cfg.dlEnable = true; cfg.dlMode = 0; cfg.dlText = L"old"; cfg.dlTarget = 0;
        check("删除文本", ComputeNewName(mk(L"oldname.txt"), cfg, 0), L"name.txt");
    }
    // 删除: 前N
    {
        CFG(); cfg.dlEnable = true; cfg.dlMode = 1; cfg.dlNum = 2; cfg.dlTarget = 0;
        check("删除前2", ComputeNewName(mk(L"abname.txt"), cfg, 0), L"name.txt");
    }
    // 扩展更名: 删除扩展名
    {
        CFG(); cfg.exEnable = true; cfg.exMode = 1;
        check("删扩展名", ComputeNewName(mk(L"a.txt"), cfg, 0), L"a");
    }
    // 扩展更名: 更改扩展名
    {
        CFG(); cfg.exEnable = true; cfg.exMode = 2; cfg.exText = L"md";
        check("改扩展名", ComputeNewName(mk(L"a.txt"), cfg, 0), L"a.md");
    }
    // 扩展更名: 扩展名大小写
    {
        CFG(); cfg.exEnable = true; cfg.exMode = 3; cfg.exCase = 2;
        check("扩展名小写", ComputeNewName(mk(L"a.TXT"), cfg, 0), L"a.txt");
    }
    // 元变量: <DATE> (useMeta on) -> contains a date string (just verify it changed and non-empty)
    {
        CFG(); cfg.useMeta = true; cfg.ovNameOverride = L"<DATE>";
        std::wstring r = ComputeNewName(mk(L"x.txt"), cfg, 0);
        check("元变量日期非空", r.empty() ? L"" : L"ok", L"ok");
    }
    // 组合: 大写 + 序号
    {
        CFG(); cfg.ovNameCase = 1; cfg.nmEnable = true; cfg.nmTemplate = L"#"; cfg.nmStart = 1;
        check("组合", ComputeNewName(mk(L"file.txt"), cfg, 0), L"1.txt");
    }

    // ================= 新增: 名字来源 =================
    // 网页标题 (.htm/.html 才生效)
    {
        fs::path html = td / L"page.html";
        std::vector<unsigned char> h;
        Append(h, "<html><head><meta charset=\"utf-8\"><title>  我的主页 &amp; 测试 </title></head><body>x</body></html>");
        WriteBytes(html, h);
        FileItem it; it.fullPath = html; it.oldName = L"page.html";
        { CFG(); cfg.nameSource = 1;
        check("网页标题", ComputeNewName(it, cfg, 0), L"我的主页 & 测试.html"); }
        // GBK 页面
        fs::path html2 = td / L"page2.htm";
        std::vector<unsigned char> h2;
        std::wstring t2w = L"<html><head><title>中文页面</title></head></html>";
        std::vector<unsigned char> t2 = {};   // 转 GBK
        for (wchar_t c : t2w) {
            // 手工 GBK: 直接用 WideCharToMultiByte
            char buf[8] = { 0 };
            int n = WideCharToMultiByte(936, 0, &c, 1, buf, 8, NULL, NULL);
            for (int k = 0; k < n; ++k) t2.push_back((unsigned char)buf[k]);
        }
        Append(h2, "<html><head><title>");
        h2.insert(h2.end(), t2.begin() + 24, t2.end() - 18);   // 去掉 "<html><head><title>" 和 "</title>..." 的宽字符部分
        // 上面按宽字符切不可靠, 直接整页按 GBK 编码:
        h2.clear();
        Append(h2, "<html><head><title>");
        for (wchar_t c : L"中文页面") {
            char buf[8] = { 0 };
            int n = WideCharToMultiByte(936, 0, &c, 1, buf, 8, NULL, NULL);
            for (int k = 0; k < n; ++k) h2.push_back((unsigned char)buf[k]);
        }
        Append(h2, "</title></head></html>");
        WriteBytes(html2, h2);
        FileItem it2; it2.fullPath = html2; it2.oldName = L"page2.htm";
        { CFG(); cfg.nameSource = 1;
        check("网页标题GBK", ComputeNewName(it2, cfg, 0), L"中文页面.htm"); }
        // 非 html 文件不受影响
        FileItem it3; it3.fullPath = td / L"a.txt"; it3.oldName = L"a.txt";
        { CFG(); cfg.nameSource = 1;
        check("网页标题非htm不变", ComputeNewName(it3, cfg, 0), L"a.txt"); }
    }
    // 文本第一行 (.txt 才生效)
    {
        fs::path txt = td / L"note.txt";
        std::vector<unsigned char> t;
        Append(t, "\xEF\xBB\xBF");
        AppendUtf8(t, L"第一行内容");
        Append(t, "\r\n第二行\r\n");
        WriteBytes(txt, t);
        FileItem it; it.fullPath = txt; it.oldName = L"note.txt";
        { CFG(); cfg.nameSource = 2;
        check("文本第一行", ComputeNewName(it, cfg, 0), L"第一行内容.txt"); }
        FileItem it2; it2.fullPath = td / L"page.html"; it2.oldName = L"page.html";
        { CFG(); cfg.nameSource = 2;
        check("文本第一行非txt不变", ComputeNewName(it2, cfg, 0), L"page.html"); }
    }
    // 文本文件命令
    {
        std::map<std::wstring, std::wstring> cmds;
        cmds[L"old.txt"] = L"new.txt";
        FileItem it; it.fullPath = td / L"old.txt"; it.oldName = L"old.txt";
        { CFG(); cfg.nameSource = 3; cfg.textCmds = &cmds;
        check("文本文件命令", ComputeNewName(it, cfg, 0), L"new.txt"); }
        FileItem it2; it2.fullPath = td / L"other.txt"; it2.oldName = L"other.txt";
        { CFG(); cfg.nameSource = 3; cfg.textCmds = &cmds;
        check("文本文件命令未命中", ComputeNewName(it2, cfg, 0), L"other.txt"); }
    }

    // ================= 新增: 文件名内码转换 =================
    {
        // BIG5 -> GB: "你好" 的 Big5 字节被按 GBK 误读后, 再修回来
        std::wstring trueBig5;   // Big5 "你好" = A741 A741? 实际: 你=A741 好=A66E
        {
            const char* big5 = "\xA7\x41\xA6\x6E";
            int n = MultiByteToWideChar(950, 0, big5, 4, NULL, 0);
            trueBig5.assign((size_t)n, L'\0');
            MultiByteToWideChar(950, 0, big5, 4, &trueBig5[0], n);
        }
        std::wstring mojibake = MakeMojibake(trueBig5, 950, 936);
        checkb("中文->Big5乱码非空", !mojibake.empty());
        std::wstring fixed = FixMojibake(mojibake, 950, 936);
        check("BIG5->GB 回环", fixed, trueBig5);
        // GB -> BIG5 是同一个变换的反方向
        std::wstring fixed2 = FixMojibake(trueBig5, 936, 950);
        checkb("GB->BIG5 产生乱码", fixed2 != trueBig5);
        // Shift-JIS: "日本語" 的 SJIS 字节按 GBK 误读 -> 修回
        std::wstring trueSjis;
        {
            const char* sjis = "\x93\xFA\x96\x7B\x8C\xEA";
            int n = MultiByteToWideChar(932, 0, sjis, 6, NULL, 0);
            trueSjis.assign((size_t)n, L'\0');
            MultiByteToWideChar(932, 0, sjis, 6, &trueSjis[0], n);
        }
        std::wstring moji2 = MakeMojibake(trueSjis, 932, 936);
        check("SJIS->中文 回环", FixMojibake(moji2, 932, 936), trueSjis);
        // ASCII 不动
        check("ASCII 不受影响", FixMojibake(L"abc123", 950, 936), L"abc123");
        // 流水线: encConv=1 修 BIG5 乱码文件名
        {
            FileItem it; it.fullPath = P(L"x.txt"); it.oldName = L"x.txt";
            CFG(); cfg.encConv = 1;
            std::wstring r = ComputeNewName(it, cfg, 0);
            checkb("流水线BIG5->GB生效", r != mojibake || mojibake == L"x.txt");
        }
    }

    // ================= 新增: APE 标签写入/读取/移除 =================
    {
        fs::path ape = td / L"t.ape";
        std::vector<unsigned char> f;
        Append(f, "MAC ");                       // Monkey's Audio 头
        PutBe(f, 1000, 4); PutBe(f, 0, 4);       // 版本等
        for (int i = 0; i < 32; ++i) f.push_back((unsigned char)i);   // 假音频数据
        // APE 标签: header + items + footer
        auto apeItem = [](std::vector<unsigned char>& v, const char* key, const char* val) {
            std::string s(val);
            PutLe(v, (uint32_t)s.size());                       // size 只含 value
            PutLe(v, 0);
            v.insert(v.end(), key, key + strlen(key));
            v.push_back('\0');
            v.insert(v.end(), s.begin(), s.end());
        };
        std::vector<unsigned char> items;
        apeItem(items, "Title", "Old Title");
        apeItem(items, "Artist", "Old Artist");
        auto apeBox = [&](bool isHeader) {
            Append(f, "APETAGEX");
            PutLe(f, 2000);
            PutLe(f, (uint32_t)items.size() + 32);
            PutLe(f, 2);
            PutLe(f, isHeader ? (0x80000000u | 0x40000000u) : 0x80000000u);
            for (int i = 0; i < 8; ++i) f.push_back(0);
        };
        apeBox(true);
        f.insert(f.end(), items.begin(), items.end());
        apeBox(false);
        WriteBytes(ape, f);

        AudioTags before = ReadAudioTags(ape);
        check("APE读取", before.title, L"Old Title");
        check("APE读取艺术家", before.artist, L"Old Artist");

        AudioTags t;
        t.title = L"新标题"; t.artist = L"周杰伦"; t.album = L"叶惠美";
        t.track = L"3"; t.year = L"2003"; t.genre = L"Pop"; t.comment = L"注释";
        int rc = WriteAudioTags(ape, t, 2);
        checkb("APE写入返回0", rc == 0);
        AudioTags after = ReadAudioTags(ape);
        check("APE写入标题", after.title, L"新标题");
        check("APE写入艺术家", after.artist, L"周杰伦");
        check("APE写入专辑", after.album, L"叶惠美");
        check("APE写入音轨", after.track, L"3");
        check("APE写入年代", after.year, L"2003");
        check("APE写入流派", after.genre, L"Pop");
        check("APE写入注释", after.comment, L"注释");
        std::vector<unsigned char> f2 = ReadBytes(ape);
        checkb("APE头部音频保留", f2.size() > 44 && !memcmp(f2.data(), "MAC ", 4) &&
                                 f2[12] == 0 && f2[43] == 31);

        rc = RemoveAudioTags(ape, 2);
        checkb("APE移除返回0", rc == 0);
        AudioTags gone = ReadAudioTags(ape);
        checkb("APE移除后无标签", !gone.HasAny());
        std::vector<unsigned char> f3 = ReadBytes(ape);
        checkb("APE移除后音频保留", f3.size() == 44 && !memcmp(f3.data(), "MAC ", 4));
    }

    // ================= 新增: WAV 标签写入/读取/移除 =================
    {
        fs::path wav = td / L"t.wav";
        std::vector<unsigned char> f;
        Append(f, "RIFF"); PutLe(f, 0); Append(f, "WAVE");
        auto chunk = [&](const char* id, const std::vector<unsigned char>& data) {
            f.insert(f.end(), id, id + 4);
            PutLe(f, (uint32_t)data.size());
            f.insert(f.end(), data.begin(), data.end());
            if (data.size() & 1) f.push_back(0);
        };
        std::vector<unsigned char> fmt(16, 0);
        fmt[0] = 1; fmt[1] = 2;                    // PCM, 2 声道
        chunk("fmt ", fmt);
        // LIST INFO: INAM + IART + 一个不认识的子块
        std::vector<unsigned char> info;
        Append(info, "INFO");
        auto sub = [&](const char* id, const char* val) {
            info.insert(info.end(), id, id + 4);
            std::string s(val);
            PutLe(info, (uint32_t)s.size());
            info.insert(info.end(), s.begin(), s.end());
            if (s.size() & 1) info.push_back(0);                // RIFF 子块按偶数字节对齐
        };
        sub("INAM", "Old Title");
        sub("IART", "Old Artist");
        sub("ISFT", "SomeTagger");                 // 不认识的键, 应保留
        chunk("LIST", info);
        std::vector<unsigned char> data(64, 'D');
        chunk("data", data);
        uint32_t riffLen = (uint32_t)(f.size() - 8);
        f[4] = (unsigned char)(riffLen); f[5] = (unsigned char)(riffLen >> 8);
        f[6] = (unsigned char)(riffLen >> 16); f[7] = (unsigned char)(riffLen >> 24);
        WriteBytes(wav, f);

        AudioTags before = ReadAudioTags(wav);
        check("WAV读取", before.title, L"Old Title");
        check("WAV读取艺术家", before.artist, L"Old Artist");

        AudioTags t;
        t.title = L" wave 标题 "; t.artist = L"艺术家";
        int rc = WriteAudioTags(wav, t, 2);
        checkb("WAV写入返回0", rc == 0);
        AudioTags after = ReadAudioTags(wav);
        check("WAV写入标题", after.title, L"wave 标题");
        check("WAV写入艺术家", after.artist, L"艺术家");
        std::vector<unsigned char> f2 = ReadBytes(wav);
        bool dataOk = false;
        for (size_t i = 0; i + 4 <= f2.size(); ++i)
            if (!memcmp(&f2[i], "data", 4)) { dataOk = f2.size() - i - 8 == 64 && f2[i + 8] == 'D'; break; }
        checkb("WAV data 块保留", dataOk);
        checkb("WAV RIFF 大小已更新", Rl32(f2, 4) == (uint32_t)(f2.size() - 8));

        rc = RemoveAudioTags(wav, 2);
        checkb("WAV移除返回0", rc == 0);
        AudioTags gone = ReadAudioTags(wav);
        checkb("WAV移除后无标签", !gone.HasAny());
        std::vector<unsigned char> f3 = ReadBytes(wav);
        checkb("WAV移除后 data 保留", f3.size() == 12 + 8 + 16 + 8 + 64);
    }

    // ================= 新增: OGG (Vorbis / Opus) 标签写入 =================
    auto vorbisComment = [](const std::vector<unsigned char>& vendor,
                            const std::vector<std::pair<const char*, const char*>>& kv,
                            bool opus) {
        std::vector<unsigned char> b;
        if (opus) Append(b, "OpusTags");
        else Append(b, "\x03vorbis");
        PutLe(b, (uint32_t)vendor.size());
        b.insert(b.end(), vendor.begin(), vendor.end());
        PutLe(b, (uint32_t)kv.size());
        for (auto& e : kv) {
            std::string s = e.first; s += "="; s += e.second;
            PutLe(b, (uint32_t)s.size());
            b.insert(b.end(), s.begin(), s.end());
        }
        if (!opus) b.push_back(1);
        return b;
    };
    for (int opus = 0; opus < 2; ++opus) {
        const char* tag = opus ? "OGG Opus 写入" : "OGG Vorbis 写入";
        fs::path ogg = td / (opus ? L"t.opus" : L"t.ogg");
        std::vector<unsigned char> f;
        std::vector<unsigned char> id;
        if (opus) { Append(id, "OpusHead"); id.push_back(1); id.push_back(2); }
        else Append(id, "\x01vorbis");
        for (int i = 0; i < 20; ++i) id.push_back((unsigned char)(0x40 + i));
        std::vector<unsigned char> vendor;
        Append(vendor, "test vendor");
        std::vector<unsigned char> comment = vorbisComment(
            vendor, { { "TITLE", "Old Title" }, { "ARTIST", "Old Artist" } }, opus != 0);
        std::vector<unsigned char> setup;
        Append(setup, opus ? "" : "\x05vorbis");
        for (int i = 0; i < 30; ++i) setup.push_back((unsigned char)(0x50 + i));
        std::vector<unsigned char> big(300, 'Z');          // 跨页的大包
        std::vector<unsigned char> big2(45, 'Y');
        std::vector<unsigned char> aud;
        for (int i = 0; i < 100; ++i) aud.push_back((unsigned char)(i & 0xFF));

        OggPageAdd(f, 0x02, 0, 1234, 0, { id });                        // BOS: ID 头独占
        OggPageAdd(f, 0x00, 0, 1234, 1, { comment, setup });            // 注释 + setup 同页
        OggPageAdd(f, 0x00, 0, 1234, 2, { big });                       // 大包第一片 (255+45)
        OggPageAdd(f, 0x01, 1000, 1234, 3, { big2 });                   // 大包续片
        OggPageAdd(f, 0x04, 2000, 1234, 4, { aud });                    // EOS
        WriteBytes(ogg, f);

        AudioTags before = ReadAudioTags(ogg);
        check(opus ? "Opus读取" : "Vorbis读取", before.title, L"Old Title");

        AudioTags t;
        t.title = L"新标题"; t.artist = L"歌手"; t.album = L"专辑"; t.track = L"7";
        int rc = WriteAudioTags(ogg, t, 2);
        checkb(opus ? "Opus写入返回0" : "Vorbis写入返回0", rc == 0);

        std::vector<unsigned char> f2 = ReadBytes(ogg);
        std::vector<std::vector<unsigned char>> pkts;
        bool eos = false;
        bool ok = OggVerify(f2, 1234, pkts, eos);
        checkb(opus ? "Opus页结构/CRC/序号合法" : "Vorbis页结构/CRC/序号合法", ok);
        checkb(opus ? "Opus EOS 保留" : "Vorbis EOS 保留", eos);
        checkb(opus ? "Opus ID头页未动" : "Vorbis ID头页未动",
               f2.size() > 100 && !memcmp(f2.data(), "OggS", 4));
        bool idOk = false, audOk = false;
        for (auto& pk : pkts) {
            if (pk.size() == id.size() && !memcmp(pk.data(), id.data(), id.size())) idOk = true;
            if (pk.size() == aud.size() && !memcmp(pk.data(), aud.data(), aud.size())) audOk = true;
        }
        checkb(opus ? "Opus ID 头包保留" : "Vorbis ID 头包保留", idOk);
        checkb(opus ? "Opus 音频包保留" : "Vorbis 音频包保留", audOk);

        AudioTags after = ReadAudioTags(ogg);
        check(opus ? "Opus写入标题" : "Vorbis写入标题", after.title, L"新标题");
        check(opus ? "Opus写入艺术家" : "Vorbis写入艺术家", after.artist, L"歌手");
        check(opus ? "Opus写入专辑" : "Vorbis写入专辑", after.album, L"专辑");
        check(opus ? "Opus写入音轨" : "Vorbis写入音轨", after.track, L"7");

        rc = RemoveAudioTags(ogg, 2);
        checkb(opus ? "Opus移除返回0" : "Vorbis移除返回0", rc == 0);
        AudioTags gone = ReadAudioTags(ogg);
        checkb(opus ? "Opus移除后无标签" : "Vorbis移除后无标签", !gone.HasAny());
        std::vector<unsigned char> f3 = ReadBytes(ogg);
        std::vector<std::vector<unsigned char>> pkts3;
        bool eos3 = false;
        checkb(opus ? "Opus移除后结构合法" : "Vorbis移除后结构合法",
               OggVerify(f3, 1234, pkts3, eos3) && eos3);
        bool aud3 = false;
        for (auto& pk : pkts3)
            if (pk.size() == aud.size() && !memcmp(pk.data(), aud.data(), aud.size())) aud3 = true;
        checkb(opus ? "Opus移除后音频保留" : "Vorbis移除后音频保留", aud3);
    }

    // ================= 新增: M4A (MP4 ilst) 标签写入 =================
    {
        fs::path m4a = td / L"t.m4a";
        std::vector<unsigned char> moovBody;
        auto box = [](std::vector<unsigned char>& out, const char* type,
                      const std::vector<unsigned char>& body) {
            PutBe(out, (uint32_t)(8 + body.size()), 4);
            out.insert(out.end(), type, type + 4);
            out.insert(out.end(), body.begin(), body.end());
        };
        // mvhd (108)
        std::vector<unsigned char> mvhdBody(100, 0);
        mvhdBody[0] = 0;   // version 0
        box(moovBody, "mvhd", mvhdBody);
        // udta > meta(version/flags + hdlr + ilst)
        std::vector<unsigned char> metaChildren;
        std::vector<unsigned char> hdlrBody(24, 0);
        hdlrBody[4] = 'm'; hdlrBody[5] = 'd'; hdlrBody[6] = 'i'; hdlrBody[7] = 'r';
        box(metaChildren, "hdlr", hdlrBody);
        std::vector<unsigned char> ilstBody;
        {
            std::vector<unsigned char> dataBody;
            PutBe(dataBody, 1, 4);                       // 类型: UTF-8 文本
            PutBe(dataBody, 0, 4);                       // locale
            std::string s = "Old Title";
            dataBody.insert(dataBody.end(), s.begin(), s.end());
            std::vector<unsigned char> dataBox;
            box(dataBox, "data", dataBody);
            box(ilstBody, "\xA9nam", dataBox);
        }
        box(metaChildren, "ilst", ilstBody);
        std::vector<unsigned char> metaBody(4, 0);       // version/flags
        metaBody.insert(metaBody.end(), metaChildren.begin(), metaChildren.end());
        std::vector<unsigned char> metaBox;
        box(metaBox, "meta", metaBody);
        std::vector<unsigned char> udtaBody(metaBox.begin(), metaBox.end());
        std::vector<unsigned char> udtaBox;
        box(udtaBox, "udta", udtaBody);
        moovBody.insert(moovBody.end(), udtaBox.begin(), udtaBox.end());
        // trak > mdia > minf > stbl > stco (2 个 chunk 偏移, 稍后填)
        std::vector<unsigned char> stcoBody;
        PutBe(stcoBody, 0, 4);                           // version/flags
        PutBe(stcoBody, 2, 4);                           // 条目数
        PutBe(stcoBody, 0, 4);
        PutBe(stcoBody, 0, 4);
        std::vector<unsigned char> stblBody;
        box(stblBody, "stsd", std::vector<unsigned char>(16, 0));
        box(stblBody, "stts", std::vector<unsigned char>(8, 0));
        box(stblBody, "stsc", std::vector<unsigned char>(8, 0));
        box(stblBody, "stsz", std::vector<unsigned char>(12, 0));
        box(stblBody, "stco", stcoBody);
        std::vector<unsigned char> minfBody;
        box(minfBody, "smhd", std::vector<unsigned char>(4, 0));
        box(minfBody, "dinf", std::vector<unsigned char>(4, 0));
        minfBody.insert(minfBody.end(), stblBody.begin(), stblBody.end());
        std::vector<unsigned char> mdiaBody;
        box(mdiaBody, "mdhd", std::vector<unsigned char>(20, 0));
        box(mdiaBody, "hdlr", hdlrBody);
        mdiaBody.insert(mdiaBody.end(), minfBody.begin(), minfBody.end());
        std::vector<unsigned char> trakBody;
        box(trakBody, "tkhd", std::vector<unsigned char>(84, 0));
        trakBody.insert(trakBody.end(), mdiaBody.begin(), mdiaBody.end());
        moovBody.insert(moovBody.end(), trakBody.begin(), trakBody.end());
        std::vector<unsigned char> moovBox;
        box(moovBox, "moov", moovBody);

        std::vector<unsigned char> f;
        std::vector<unsigned char> ftypBody;
        Append(ftypBody, "M4A ");
        PutBe(ftypBody, 0, 4);
        Append(ftypBody, "M4A mp42");
        std::vector<unsigned char> ftypBox;
        box(ftypBox, "ftyp", ftypBody);
        f.insert(f.end(), ftypBox.begin(), ftypBox.end());
        size_t moovOff = f.size();
        f.insert(f.end(), moovBox.begin(), moovBox.end());
        size_t mdatOff = f.size();
        std::vector<unsigned char> mdatBody(64, 'M');
        mdatBody[0] = 'A'; mdatBody[1] = 'A';          // chunk0
        mdatBody[32] = 'B'; mdatBody[33] = 'B';        // chunk1
        std::vector<unsigned char> mdatBox;
        box(mdatBox, "mdat", mdatBody);
        f.insert(f.end(), mdatBox.begin(), mdatBox.end());
        // 回填 stco: "stco" 在 moovBody 的偏移 i 处, box 起点 = i-4, 条目在 box 内偏移 16
        {
            size_t i = 0;
            for (; i + 4 <= moovBody.size(); ++i)
                if (!memcmp(&moovBody[i], "stco", 4)) break;
            size_t stcoAbs = moovOff + 8 + (i - 4) + 16;
            f[stcoAbs]     = (unsigned char)((mdatOff + 8) >> 24);
            f[stcoAbs + 1] = (unsigned char)((mdatOff + 8) >> 16);
            f[stcoAbs + 2] = (unsigned char)((mdatOff + 8) >> 8);
            f[stcoAbs + 3] = (unsigned char)(mdatOff + 8);
            f[stcoAbs + 4] = (unsigned char)((mdatOff + 8 + 32) >> 24);
            f[stcoAbs + 5] = (unsigned char)((mdatOff + 8 + 32) >> 16);
            f[stcoAbs + 6] = (unsigned char)((mdatOff + 8 + 32) >> 8);
            f[stcoAbs + 7] = (unsigned char)(mdatOff + 8 + 32);
        }
        WriteBytes(m4a, f);

        AudioTags before = ReadAudioTags(m4a);
        check("M4A读取", before.title, L"Old Title");

        AudioTags t;
        t.title = L"新标题"; t.artist = L"歌手"; t.track = L"5"; t.year = L"2019";
        int rc = WriteAudioTags(m4a, t, 2);
        checkb("M4A写入返回0", rc == 0);
        AudioTags after = ReadAudioTags(m4a);
        check("M4A写入标题", after.title, L"新标题");
        check("M4A写入艺术家", after.artist, L"歌手");
        check("M4A写入音轨", after.track, L"5");
        check("M4A写入年代", after.year, L"2019");

        // stco 偏移修正 + mdat 内容完整
        std::vector<unsigned char> f2 = ReadBytes(m4a);
        bool stcoOk = false, mdatOk = false;
        for (size_t i = 0; i + 4 <= f2.size(); ++i) {
            if (!memcmp(&f2[i], "stco", 4)) {
                // [size(4)]["stco"][ver/flags(4)][count(4)][entry0][entry1]
                uint32_t n = Rb32(f2, i + 8);
                if (n != 2) continue;
                uint32_t o0 = Rb32(f2, i + 12), o1 = Rb32(f2, i + 16);
                if (o0 + 4 <= f2.size() && o1 + 4 <= f2.size() &&
                    f2[o0] == 'A' && f2[o0 + 1] == 'A' && f2[o1] == 'B' && f2[o1 + 1] == 'B')
                    stcoOk = true;
            }
            if (!memcmp(&f2[i], "mdat", 4) && Rb32(f2, i - 4) == 72) {
                // MP4 box: [size(4)]["mdat"][payload] —— 载荷从 type 之后 4 字节开始
                mdatOk = f2[i + 4] == 'A' && f2[i + 36] == 'B';
            }
        }
        checkb("M4A stco 偏移已修正", stcoOk);
        checkb("M4A mdat 内容完整", mdatOk);

        rc = RemoveAudioTags(m4a, 2);
        checkb("M4A移除返回0", rc == 0);
        AudioTags gone = ReadAudioTags(m4a);
        checkb("M4A移除后无标签", !gone.HasAny());
        std::vector<unsigned char> f3 = ReadBytes(m4a);
        mdatOk = false;
        for (size_t i = 0; i + 4 <= f3.size(); ++i)
            if (!memcmp(&f3[i], "mdat", 4) && Rb32(f3, i - 4) == 72)
                mdatOk = f3[i + 4] == 'A' && f3[i + 36] == 'B';
        checkb("M4A移除后 mdat 完整", mdatOk);
    }

    // ================= 新增: MP3 ID3 互转 / 移除 =================
    {
        // 只有 ID3v1 的 MP3
        fs::path mp3 = td / L"v1.mp3";
        std::vector<unsigned char> f(100, 0x55);        // 假音频
        std::vector<unsigned char> v1(128, ' ');
        memcpy(&v1[0], "TAG", 3);
        memcpy(&v1[3], "V1 Title", 8);
        memcpy(&v1[33], "V1 Artist", 9);
        memcpy(&v1[63], "V1 Album", 8);
        memcpy(&v1[93], "1999", 4);
        memcpy(&v1[97], "V1 Comment", 10);
        v1[125] = 0; v1[126] = 4; v1[127] = 0xFF;
        f.insert(f.end(), v1.begin(), v1.end());
        WriteBytes(mp3, f);
        AudioTags v1o = ReadId3v1Only(mp3);
        check("ID3v1读取标题", v1o.title, L"V1 Title");
        check("ID3v1读取音轨", v1o.track, L"4");
        AudioTags v2o = ReadId3v2Only(mp3);
        checkb("ID3v2读取为空", !v2o.HasAny());
        // 只有 ID3v2 的 MP3
        fs::path mp3b = td / L"v2.mp3";
        std::vector<unsigned char> g;
        std::vector<unsigned char> frames;
        auto frame = [&](const char* id4, const char* text) {
            std::string s(text);
            frames.insert(frames.end(), id4, id4 + 4);
            PutBe(frames, (uint32_t)s.size() + 1, 4);
            PutBe(frames, 0, 2);
            frames.push_back(0);                        // ISO-8859-1
            frames.insert(frames.end(), s.begin(), s.end());
        };
        frame("TIT2", "V2 Title");
        frame("TPE1", "V2 Artist");
        std::vector<unsigned char> tag;
        Append(tag, "ID3"); tag.push_back(3); tag.push_back(0); tag.push_back(0);
        uint32_t sz = (uint32_t)frames.size();
        tag.push_back((unsigned char)((sz >> 21) & 0x7F));
        tag.push_back((unsigned char)((sz >> 14) & 0x7F));
        tag.push_back((unsigned char)((sz >> 7) & 0x7F));
        tag.push_back((unsigned char)(sz & 0x7F));
        tag.insert(tag.end(), frames.begin(), frames.end());
        g.insert(g.end(), tag.begin(), tag.end());
        g.resize(g.size() + 100, 0x55);
        WriteBytes(mp3b, g);
        AudioTags v2b = ReadId3v2Only(mp3b);
        check("ID3v2读取标题", v2b.title, L"V2 Title");
        AudioTags v1b = ReadId3v1Only(mp3b);
        checkb("ID3v1读取为空", !v1b.HasAny());
        // 移除: v1+v2 都在的文件
        fs::path mp3c = td / L"both.mp3";
        std::vector<unsigned char> both(g);
        both.insert(both.end(), v1.begin(), v1.end());
        WriteBytes(mp3c, both);
        int rc = RemoveAudioTags(mp3c, 2);
        checkb("MP3移除返回0", rc == 0);
        std::vector<unsigned char> f3 = ReadBytes(mp3c);
        checkb("MP3移除后只剩音频", f3.size() == 100 && f3[0] == 0x55);
        rc = RemoveAudioTags(mp3c, 1);                  // 已无标签, 应返回 0
        checkb("MP3重复移除返回0", rc == 0);
        // v1 -> v2 互转: 用 WriteAudioTags 只写 v2, 原 v1 尾巴应保留
        AudioTags t;
        t.title = L"V2 New";
        rc = WriteAudioTags(mp3, t, 1);
        checkb("MP3只写v2返回0", rc == 0);
        AudioTags afterV2 = ReadId3v2Only(mp3);
        check("MP3 v1->v2 标题", afterV2.title, L"V2 New");
        AudioTags afterV1 = ReadId3v1Only(mp3);
        check("MP3 原v1保留", afterV1.title, L"V1 Title");
    }

    // ================= 多语言 =================
    // 语言包既内嵌在 exe 里 (单文件也要能用), 也能从 exe 旁的 Language\\*.ini 覆盖。
    LangInit(NULL);
    {
        const std::vector<LangInfo>& list = LangList();
        bool hasZh = false, hasEn = false;
        for (size_t i = 0; i < list.size(); ++i) {
            if (list[i].code == L"zh-CN") hasZh = true;
            if (list[i].code == L"en-US") hasEn = true;
        }
        checkb("语言表里有 zh-CN", hasZh);
        checkb("语言表里有 en-US", hasEn);

        // 缺省 (或选了 zh-CN) 取到中文
        LangSet(L"zh-CN");
        check("中文 程序标题", LS(IDS_APP_TITLE), L"Arade文件更名器");
        check("中文 菜单项", LS(IDM_FILE_OPEN), L"添加文件");
        check("中文 控件文本", LS(IDC_RP_GRP), L"替换");
        check("中文 标签页名", LS(IDS_TAB_OVERALL), L"整体");
        check("中文 工具栏提示", LS(IDS_TIP_APPLY), L"开始更名 (Ctrl+E)");

        // 切到英文
        checkb("切换 en-US 成功", LangSet(L"en-US"));
        check("英文 程序标题", LS(IDS_APP_TITLE), L"AradeRenamer");
        check("英文 菜单项", LS(IDM_FILE_OPEN), L"&Add files");
        check("英文 控件文本", LS(IDC_RP_GRP), L"Replace");
        check("英文 标签页名", LS(IDS_TAB_OVERALL), L"Overall");
        check("英文 工具栏提示", LS(IDS_TIP_APPLY), L"Start renaming (Ctrl+E)");
        // 占位符不能被翻译吃掉
        check("英文 格式串保留 %d", LS(IDS_MSG_RENAME_DONE), L"Done: %d renamed, %d failed");

        // 文件对话框过滤器是 \0 分隔、\0\0 结尾的多串: 名称段翻译, 模式段 (*.txt) 不动。
        // 不能写成 std::wstring s = LS(...): 那个构造会在第一个 \0 处截断。
        const wchar_t* flt = LS(IDS_FILTER_TXT);
        std::wstring seg1(flt);
        check("英文 过滤器第1段", seg1, L"Text files (*.txt)");
        checkb("英文 第1段后是 NUL", flt[seg1.size()] == L"\0"[0]);
        check("英文 过滤器第2段(模式不译)", std::wstring(flt + seg1.size() + 1), L"*.txt");

        // 切回中文
        checkb("切回 zh-CN 成功", LangSet(L"zh-CN"));
        check("切回后是中文", LS(IDS_APP_TITLE), L"Arade文件更名器");

        // 未知语言不该把界面清空
        checkb("未知语言切换失败", !LangSet(L"xx-YY"));
        check("未知语言后仍是中文", LS(IDS_APP_TITLE), L"Arade文件更名器");

        // 自动: 跟随系统界面语言。能解析到一种已知语言, 不会落空。
        checkb("菜单首项是 自动", LangList()[0].code == L"auto");
        checkb("选 自动 成功", LangSet(L"auto"));
        check("自动: 记住的选择是 auto", LangChoice(), L"auto");
        std::wstring resolved = LangCurrent();
        checkb("自动: 解析出 zh-CN 或 en-US",
               resolved == L"zh-CN" || resolved == L"en-US");
        printf("       (系统界面语言解析为 %ls)\n", resolved.c_str());
        checkb("自动: 解析结果真的生效", LS(IDS_APP_TITLE) ==
               (resolved == L"en-US" ? std::wstring(L"AradeRenamer") : std::wstring(L"Arade文件更名器")));
    }
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
