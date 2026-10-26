# AradeRenamer — Arade文件更名器（Win32 C++）
Documentation is currently in Chinese only. Translation PRs are highly welcomed!

🚀 极致轻量：不到 4MB（现在动辄几百兆的软件？不存在的）。

🛡️ 安全纯净：无 Electron、无第三方库、无联网后门，单文件免安装。

💡 底层硬核：原生支持 MP3/FLAC/Opus 标签解析与 OpenCC 繁简互转。

🆚 对比传统工具：兼容 Win10/Win11，完美替代失传多年的经典“拖把更名器”。


AradeRenamer 是一款现代化的纯 Win32 批量更名工具，致敬并重写了经典国产批量更名器（如“拖把更名器”等同时代作品）的核心功能。
原版软件多为 Delphi 编写且未开源，因此本项目采用 净室开发（Clean-room implementation） 模式：完全基于其暴露的外部功能进行独立重写，绝无任何反编译、逆向工程或源码提取行为。
我们在保持极致轻量的同时，修复了右键功能，加入了现代 API、多语言支持与更多底层极客功能，右键菜单写入 `HKEY_CLASSES_ROOT`（对所有用户生效，装 / 卸时按需申请一次管理员权限）。覆盖文件、文件夹、目录背景三大场景。
配合自研的单实例（Single Instance）合并逻辑，无论多选多少个文件，都能瞬间在一个窗口内完成聚合。提供彻底的卸载清理机制，绝不留系统垃圾。

技术栈：**原生 Win32 API + C++（MSVC）**，界面仿照经典「标签式」批量改名器布局（菜单栏 / 工具栏 / 8 个标签页 / 8 列文件列表 / 状态栏），静态链接、无第三方运行库依赖。

## 界面布局

- **菜单栏**：文件(F) / 更名(P) / 标签(T) / 选项(O) / 帮助(H)
- **工具栏**：添加文件 / 添加目录 / 移除 / 更名 / 撤销 / 清空 / 预览 / 置顶 / **❤️ 赞助联系（最右侧）** + 第二排（标签三件套 / 繁简 / 文本命令 / 导出 / 操作记录 / 改日期）
- **标签页（8 个，可叠加生效）**：
  - **整体**：文件名大小写（大写 / 小写 / 首字母大写）、`文件名改成`、扩展名大小写、`扩展名改成`、文件过滤
  - **替换**：查找 / 替换为，首字符 `:` 触发正则（`$1` 反向引用），忽略大小写
  - **序号**：模板 `#`=序号、`<NAME>`=原名、`<SELF>`=当前名；起始 / 步长 / 位数补零；字母编号 `0=A`（模板无扩展名时自动保留原扩展名）
  - **添加**：添加内容，位置可选 文件名前 / 文件名后 / 扩展名前 / 扩展名后 / 指定位置
  - **删除**：按 指定文本 / 前 N 个 / 后 N 个 / 第 N 个起删 M 个 删除，作用范围 文件名 / 扩展名
  - **扩展更名**：添加 / 删除 / 更改 扩展名，或扩展名大小写；扩展命令可叠加
  - **标签命名**：`%1..%6` 标签表达式命名（无标签文件保持原名）
  - **标签写入**：字段填写 / 文件名表达式反解；保存（打勾行）/ 全写（整个列表）
- **文件列表（9 列）**：打勾 / 文件名 / 预览 / 扩展名 / 日期 / 路径 / 大小 / 识别类型 / 结果
- **状态栏**：左侧显示 文件数 / 待更名数 / 冲突数，右侧显示「N 个文件」

> 各标签页的规则**叠加**生效（与原版一致）：先整体 → 替换 → 删除 → 添加 → 序号 → 扩展更名 → 整体扩展名 → CRC32。

## 已实现的功能

| 功能 | 位置 | 说明 |
|------|------|------|
| 整体改名 | 整体 | 文件名大小写、直接改名（`文件名改成`，支持元变量）、扩展名大小写/改名、文件过滤 |
| 查找 / 替换 | 替换 | 普通文本；首字符 `:` 触发正则，`$1` 反向引用；忽略大小写 |
| 序号 | 序号 | `#`=序号，`<NAME>`/`<SELF>` 引用；位数补零；字母编号 `0→A`；自动保留原扩展名 |
| 添加 / 删除 | 添加 / 删除 | 前/后/指定位置插入；按文本或前/后 N 个删除 |
| 扩展更名 | 扩展更名 | 添加 / 删除 / 更改 扩展名，或扩展名大小写；扩展命令 |
| 元变量 | 各框（启用时） | `<DATE>` `<TIME>` `<NAME>` `<EXT>` `<.EXT>` `<PARENT>` `<SELF>` `<R>`(随机) `<N>`(序号) |
| CRC32 改名 | 更名菜单 | 以文件内容 CRC32（8 位大写十六进制）作新文件名，保留扩展名 |
| 繁简互转 | 更名/标签菜单 | 文件名整体转换或标签逐字段转换（内置 OpenCC 字表） |
| 内码转换 | 更名/标签菜单 | BIG5↔GB、日文 Shift-JIS↔中文：修「按错误代码页误读」的名字/标签 |
| 以网页标题更名 | 更名菜单 | 读 `<title>` 作新名（自动识别 UTF-8/GBK/Big5/SJIS，解 HTML 实体）；**只对 .htm/.html 生效** |
| 以文本第一行更名 | 更名菜单 | 取 .txt 第一行作新名；只对 .txt 生效 |
| 调用文本文件命令 | 更名菜单/工具栏 | 命令文件每行 `原文件名=新文件名`，按原名匹配给出新名 |
| 实时预览 / 冲突检测 | 列表「预览」列 | 任一参数变动即刷新，状态栏显示冲突数 |
| 应用 / 撤销 | 更名 / 撤销 | `MoveFile` 改名；目标已存在自动加 `(2)` 后缀；撤销回退上一批 |
| 文件来源 | 工具栏 / 拖拽 / 命令行 | 添加文件、添加文件夹、递归子目录、拖拽、`%1`/`%V` 命令行预载入、右键菜单 |
| 单实例 | 启动路径 | 命名互斥体认出第一个实例；资源管理器多选文件时每个文件会各调一次程序，后续调用经 WM_COPYDATA 把文件并进已有窗口（去重），只会开一个窗口 |
| 音频标签读写 | 标签页 + 标签菜单 | 见下表 |
| 操作记录 | 更名菜单 | 记录每批更名；保存/打开 .xto，打开时挂撤销批次 |
| 导出列表 / 导出标签 | 文件/标签菜单 | UTF-8 BOM 文本；标签导出即「标签文本格式」，可再导入 |
| 更改文件日期 | 工具栏 | 批量改创建/访问/修改时间 |
| 资源管理器右键菜单 | 选项菜单 | 注册/卸载（写 `HKEY_CLASSES_ROOT`，所有用户生效，**按需提权**、弹一次 UAC）；随附 `AradeRenamer.reg` |
| 发送到菜单 | 选项菜单 | 注册/卸载（在 SendTo 文件夹放 .lnk 快捷方式，非注册表；多选文件一次全部传入） |
| 用户手册 | 帮助菜单 | 打开同目录 `用户手册.html` |

### 音频标签覆盖

| 容器 | 标签方案 | 读 | 写 | 移除 |
|------|----------|----|----|------|
| MP3 | ID3v1 / ID3v2.2-2.4 | ✔ | ✔ | ✔（按设置选 v1/v2/全部） |
| FLAC | VORBIS_COMMENT 块 | ✔ | ✔ | ✔ |
| OGG (Vorbis) | Vorbis comment（写入重算分页 CRC、重排页序号） | ✔ | ✔ | ✔（换空注释包） |
| Opus | OpusTags | ✔ | ✔ | ✔ |
| APE / WavPack | APEv2 | ✔ | ✔ | ✔ |
| M4A / AAC (MP4) | moov/udta/meta/ilst（写入含 stco/co64 偏移修正） | ✔ | ✔ | ✔（清空 ilst） |
| WAV | RIFF LIST-INFO | ✔ | ✔ | ✔ |

> 写入只动标签结构：不认识的 frame / block 原样保留，音频数据逐块搬运，临时文件 + 原子替换，
> 失败不留半成品。OGG/Opus 只写单路流（Vorbis/Opus），多路复用/链式文件不写。
> ID3v1↔v2 互转、标签文本文件导入导出（Tab 分隔 8 列）见 标签 菜单与 `用户手册.html`。

## 构建

前置条件：
- **Visual Studio 2019 / 2022**（Community / Professional / BuildTools 都行），带「使用 C++ 的桌面开发」
- **Windows SDK 10**（任意 10.0.x，提供 `rc.exe` / `mt.exe` / comctl32 v6）
- **Git Bash**（脚本是 bash；`cmd` 下可用 `build.cmd`，它只是转调 `build.sh`）

在 Git Bash 中执行：

```bash
cd AradeRenamer
bash build.sh
```

`build.sh` 会先用 `vswhere.exe` **自动探测** VS 安装目录，再取 `VC/Tools/MSVC` 和 `SDK/Include` 下**版本号最新**的那个工具集，然后设好 `INCLUDE`/`LIB`/`PATH`：编译资源（`rc.exe`）、编译 C++（`cl.exe`，`/utf-8` 支持中文源码）、用 `mt.exe` 嵌入 comctl32 v6 清单，产出 **bin/AradeRenamer.exe**（`/MT` 静态链接 CRT，单文件、绿色免安装）。

> 升版本号改 `resources/AradeRenamer.rc` 里的 `VS_VERSION_INFO` 即可（exe 属性里的「版本」
> 标签页读的是它）。输出文件名不带版本号；要发带版本号的包就从 `bin\` 拷一份到 `dist\` 再改名。

想固定工具链版本就提前设环境变量：

```bash
VS_ROOT="C:/Program Files/Microsoft Visual Studio/2022/Community" SDKVER=10.0.26100.0 bash build.sh
```

> 数据文件（`data\`）和语言包（`Language\`）都已随仓库带上，clone 下来直接能编，不需要额外下载。

## 使用

1. 点「添加文件」/「添加目录」或把文件拖进列表（也可右键文件 → `AradeRenamer 更名`，或拖把式预载入）。
2. 在任意标签页设置规则，下方列表「预览」列实时显示新名；「整体」页可用「文件过滤」限定作用文件。
3. 点「更名」执行（冲突自动加后缀），点「撤销」回退上一批；「结果」列显示 成功 / 跳过 / 失败。
4. 完整功能说明见程序内 帮助 → 用户手册（`用户手册.html`）。

> **多选文件 / 单实例**：资源管理器里选中多个文件右键「AradeRenamer 更名」时，系统会为每个文件各调用一次程序；
> 本程序是单实例的——第一个调用建立窗口，其余调用把文件合并进同一个窗口的列表（重复文件自动去重），不会开一堆窗口。

### 常用示例
- 批量加前缀：`添加` 页勾选，内容填 `前缀_`，位置选「文件名前」。
- 按日期重命名：`整体` 页 `文件名改成` 填 `<DATE>`（需用到元变量，引擎在启用相关标签页时展开）。
- 序号重排：`序号` 页模板 `<NAME>_#`，起始 1 / 步长 1；字母编号勾选后 `0→A`。
- 正则去空格：`替换` 页，查找填 `: +`，替换为空，勾「正则表达式」。
- 改扩展名：`扩展更名` 页选「更改扩展名」，扩展名填 `jpg`。
- 修乱码文件名：`更名` 菜单 → 文件名 BIG5 -&gt; GB（或 Shift-JIS 转换）。
- 按标签写名字：`标签命名` 页表达式填 `%3.%1 - %2`。

## 文件结构

```
AradeRenamer/
├── README.md              # 本文件：项目说明 / 构建 / 目录结构
├── LICENSE                # MIT 协议全文（本程序自身的开源协议）
├── build.sh / build.cmd   # 一键编译脚本（MSVC + rc.exe，自动探测 VS / SDK 版本）
├── buildBACKUP.sh         # 早期备份脚本（与 build.sh 只差编译选项，留档用，构建请用 build.sh）
├── AradeRenamer.reg    # 右键菜单注册表模板（导入前需替换路径占位符，导入需管理员权限）
├── resources/             # 资源源文件：.rc 与 ICON/ 同处，manifest 也在这里
│   ├── AradeRenamer.rc     # 对话框模板（自包含样式常量 + UTF-8 code_page + comctl32 样式）
│   │                      #   内部相对引用解析自项目根目录：resources\ICON\、data\pinyin.txt、
│   │                      #   data\license_*、data\dictionary\、Language\、收款码 sponsor_*.jpg
│   ├── ICON/              # 工具栏/程序图标（.ico，AradeRenamer.rc 从这里引用）
│   ├── sponsor_*.jpg       # 收款码成品图（由 tools\prep_sponsor.cmd 生成，打进 exe）
│   └── AradeRenamer.manifest  # comctl32 v6 清单（由 mt.exe 嵌入 exe）
├── src/                   # 全部 C++ 源码
│   ├── main.cpp           # Win32 对话框、菜单/工具栏、标签页切换、列表、预览、应用/撤销、拖拽、命令行、标签命令、右键菜单注册
│   ├── engine.h / engine.cpp  # 纯 C++ 改名引擎（流水线 + 音频标签读写/移除 + 编码转换 + 网页标题/文本首行）
│   ├── conv.h / conv.cpp  # 简繁转换字表（OpenCC 数据）
│   ├── lang.h / lang.cpp  # 多语言：语言包解析、LS() 查找、界面文本刷新
│   ├── lang_ids.h         # 语言包键 -> ID（tools/extract_lang.py 生成，勿手改）
│   ├── lang_keys.inc      # 键名表（同上生成，lang.cpp 里 #include）
│   ├── resource.h         # 控件与对话框 ID
│   └── test_engine.cpp    # 引擎无界面单元测试（142 项断言全过）
├── Language/              # 语言包：lang_zh-CN.ini / lang_en-US.ini（也内嵌进 exe）
├── tools/                 # 项目脚本（随项目保留）
│   ├── extract_lang.py    # 从 .rc + main.cpp 抽取语言包键（生成 lang_ids.h / lang_keys.inc）
│   ├── apply_lang.py      # 把 src/main.cpp 中文字面量批量替换为 LS(IDS_xxx)
│   └── prep_sponsor.ps1 / .cmd   # 收款码预处理：REF\ 原图 → 去元数据/限尺寸/压 JPEG → resources\ + docs\sponsor\
│   └── download_data.cmd  # 下载最新字典到对应目录
├── data/                  # 运行/编译期数据：pinyin.txt、license_*、dictionary/（编译时 RCDATA 进 exe）
├── docs/                  # 用户手册.html（F1 打开）、CHANGELOG.md
│   └── sponsor/           # 收款码成品图（README 直接引用本地文件，不依赖外链图床）
├── Add-ons/               # 可选的 gbkfix.txt 自定义修正表（运行时从磁盘读，不打进 exe）
├── .gitattributes         # 行尾 / 编码规则（含 gbkfix.txt 必须原样入库的例外）
├── bin/                   # 当前构建产物（AradeRenamer.exe、test_engine.exe）—— 不入库
├── build/                 # 编译中间产物（.obj / .res，由 build.sh 生成）—— 不入库
├── dist/                  # 发布快照（带版本号的 AradeRenamer_*.exe，手动归档）—— 不入库
├── BACKUP/                # 本地备份目录（个人/本地，不入库）
├── screenshots/           # 设计阶段截图（不入库）
└── thirdtools/            # 个人工具（图标生成脚本 icon*.py 等，不入库）
```

## 测试

改名引擎、简繁转换与标签读写都是纯逻辑（文件级），可用 `test_engine.cpp` 做无界面验证
（142 项断言：改名流水线全部模式、元变量、名字来源（网页标题/文本首行/文本命令）、
内码转换回环、繁简互转（词组最长匹配 + 单字兜底）、APE/OGG(Vorbis+Opus)/M4A/WAV/MP3 的
标签写入-读回-移除、OGG 分页 CRC/页序号/颗粒位置/EOS 校验、M4A stco 偏移修正与 mdat 完整性、
语言包解析/中英切换/缺键回退/未知语言不破坏界面）。

```bash
cl /EHsc /MT /std:c++17 /utf-8 /DUNICODE /D_UNICODE \
   src/test_engine.cpp src/engine.cpp src/conv.cpp src/lang.cpp build/AradeRenamer.res \
   /Fobuild/ /link /SUBSYSTEM:CONSOLE /OUT:bin/test_engine.exe user32.lib kernel32.lib advapi32.lib
./bin/test_engine.exe
```

> 词典是 exe 里的 RCDATA 资源，所以测试要一起链 `build/AradeRenamer.res`（先 `bash build.sh`
> 或 `rc.exe /fo build/AradeRenamer.res resources/AradeRenamer.rc` 生成），否则 `LoadResText` 读不到字典，转换用例会全空。

## 数据文件（哪些打进 exe，哪些留在磁盘）

| 文件 | 去向 | 说明 |
|------|------|------|
| `data/pinyin.txt` | RCDATA `IDR_PINYIN` | 汉字转拼音表 |
| `data/dictionary/TSCharacters.txt` | RCDATA `IDR_DICT_T2S` | 繁→简 单字 |
| `data/dictionary/STCharacters.txt` | RCDATA `IDR_DICT_S2T` | 简→繁 单字 |
| `data/dictionary/TSPhrases.txt` | RCDATA `IDR_DICT_T2S_PHRASES` | 繁→简 词组（最长匹配，优先于单字） |
| `data/dictionary/STPhrases.txt` | RCDATA `IDR_DICT_S2T_PHRASES` | 简→繁 词组 |
| `Add-ons/gbkfix.txt` | **运行时从磁盘读** | 用户自定义修正表：改了不用重新编译；文件不存在就跳过。这是故意不打进 exe 的 |
| `data/license_opencc.txt` / `data/license_pinyin_data.txt` | RCDATA | 「关于 → 开源协议」里展示 |
| `resources/sponsor_wechat.jpg` / `sponsor_alipay.jpg` / `sponsor_paypal.jpg` | RCDATA `IDR_SPONSOR_*` | 「关于 → 赞助/联系」里的三个收款码；由 `tools\prep_sponsor.cmd` 从 `REF\` 生成，见「赞助与支持」 |
| `Language/lang_zh-CN.ini` / `lang_en-US.ini` | RCDATA `IDR_LANG_*` + 磁盘副本 | 界面语言包（见下节） |

所以 **exe 单独拷走就能用**（简繁/拼音/中英文语言包都带在身上）。要跟目录一起发的只有两个可选件：

| 随附文件 | 是否必须 | 缺了会怎样 |
|----------|----------|------------|
| `用户手册.html` | 建议 | 「帮助 > 用户手册」/ F1 会提示找不到（程序找 exe 同目录，其次 `docs\` 子目录） |
| `Add-ons\gbkfix.txt` | 可选 | 简繁转换的自定义修正表不生效，其余功能不受影响 |

## 界面语言（i18n）

`选项 > 语言` 运行时切换，当前选择存 `HKCU\Software\AradeRenamer\Language`。
菜单第一项是**自动**：跟随 Windows 界面语言挑一个最接近的可用语言（挑不到退回简体中文）。
全新安装默认是简体中文。
回退链：**磁盘 `Language\*.ini`（按 lang.code 匹配）→ exe 内嵌副本 → 简体中文**。
往 `Language\` 放一个新 .ini（内写 `lang.name` / `lang.code` / `lang.author`）就多一种语言，
不用改代码；某个键没翻译就自动退回中文，半成品语言包也能用。

### 加一门语言（给贡献者）

1. 复制 `Language\lang_en-US.ini`（**英语是翻译源语言**）作为模板，按目标语言改名，例如 `lang_ja-JP.ini`
2. 改文件头三行元信息：`lang.name`（菜单上显示的名字）/ `lang.code`（BCP-47 码）/ **`lang.author`（你的署名）**
3. 逐条翻译；翻不动的键整行删掉即可，运行期自动回退中文

`lang.author` 记的是**这个语言包的译者**，不是程序作者——将来别人补语言包时改这一行就行。
程序会把它显示在「选项 &gt; 语言」菜单里：`English  (yjhua2005)`，用户一眼看得出这门语言是谁翻的
（没写 `lang.author` 就只显示语言名）。
跑 `python tools/extract_lang.py` 会顺带检查：哪个包漏翻了哪些键、哪个包没写 `lang.author`。
注意 `lang_zh-CN.ini` 是脚本生成的（勿手改），其它包都是手写的——英文包偶尔会比新键落后几条，
拿 `lang_zh-CN.ini` 当对照表查缺补漏即可。

> 注释只有**整行以 `;` 或 `#` 开头**才算（`ParseLang()` 就看行首一个字符）。
> 写成 `lang.name = 日本語   # 菜单名` 的话，`# 菜单名` 会被当成值的一部分，
> 语言菜单里就显示出这么一长串——行尾注释不生效。

键名由 `tools/extract_lang.py` 从 `resources/AradeRenamer.rc` 和 `src/main.cpp` 抽取（生成
`src/lang_ids.h` / `src/lang_keys.inc` / `Language/lang_zh-CN.ini`），改了界面文案重跑它即可；
`tools/apply_lang.py` 负责把 `src/main.cpp` 里的中文字面量批量替换成 `LS(IDS_xxx)`。
**顺序固定：先 `extract_lang.py` 再 `apply_lang.py`**——中间那步 `lang_zh-CN.ini` 是脚本生成的，
不要手改它（改文案请改代码里的字面量并重跑）。`apply_lang.py` 会挡住「ini 里还没这个键就替换」
的情况（那样运行期会拿到空串）。`extract_lang.py` 顺带核对其它语言包，
`lang_en-US.ini` 少翻译哪些键会直接列出来。
刻意不本地化的：扩展命令关键字（要在框里输入）、中文数字（功能本身）、
数据协议头、语言未加载前的致命错误提示。

### 控件宽度会跟着语言走

标签页里的控件不是写死宽度的：每个控件声明一个 `minW`（原设计宽度，按中文定的）和
`gap`（与下一个控件的间距），运行时用 `FitW()` 量出当前语言下文字实际要占多宽，取
「`minW` 与文字宽度中较大的那个」，后一个控件顺次右移（见 `LayoutRow()`）。

- **中文**下量出来就等于 `minW`，排版和原来一模一样
- **英文**普遍比中文长一半，这时才撑开——以前会把 `Filename to upper case` 截成
  `Filename to uppe`，现在不会了
- 一排放不下时按各控件「多要出来的宽度」**等比收回**，不会越界也不会互相压住

所以新增语言包时一般不用动布局；只有整排放不下（比如某个语言特别长）才需要精简那几条的译文。
切换语言会重新 `Layout()` 一次，宽度是按新语言重算的。

## ❤️ 赞助与支持

如果这个小工具帮到了你，可以扫码赞助——程序内有**两个入口**：主界面**工具栏最右侧的 ❤️ 按钮**，
以及 **帮助 → 关于 → 赞助/联系**，打开的是同一个窗口，里面有三个码，
外加开源地址、QQ 群号（一键复制）和邮箱。

| 微信 | 支付宝 | PayPal |
|------|--------|--------|
| ![微信收款码](docs/sponsor/sponsor_wechat.jpg) | ![支付宝收款码](docs/sponsor/sponsor_alipay.jpg) | ![PayPal](docs/sponsor/sponsor_paypal.jpg) |

> PayPal 目前是**占位图**（暂时复用支付宝的图），换成真正的 PayPal 二维码后同步更新本节。

「开源地址」和「邮箱」两行是**点了就走**的链接：先把地址**复制到剪贴板**（那一行闪一下「已复制」），
再把链接交给系统默认程序打开。打开动作跑在后台线程上，主线程不会被卡住。

**http/https 一律交给 `ShellExecuteEx`**，不要自己按注册表拼命令行再 `CreateProcess`——
Chromium 系浏览器（QQ 浏览器 / Chrome / Edge）已经在运行时，硬起一个新进程会因实例/profile 冲突
直接 `DebugBreak` 崩掉（退出码 `0x80000003` = STATUS_BREAKPOINT），界面上的表现就是浏览器弹一个
错误报告框、页面还打不开。同理也**不要加 `SEE_MASK_NOCLOSEPROCESS`**：那个 flag 会逼着
ShellExecute 走 CreateProcess，绕过「把 URL 递给已运行实例」的机制，一样会崩。

只有 `mailto` 需要特殊照顾：某些机器上 mailto 关联指向的程序早被卸载了
（比如 `foxmail.Url.mailto` → 已不存在的 `Foxmail.exe`），`ShellExecute` 遇到这种情况只返回一个
失败码、界面毫无反应，还会先走一遍 DDE 握手卡三十多秒。所以先用 `AssocHealthy()` 查一下关联指向的
程序是否还在，在就走系统；不在才自己按 `SOFTWARE\Clients\Mail` 找候补客户端（例如 Outlook）来起。
全都失败才弹框，告诉用户地址已经在剪贴板里。

### 收款码图片怎么来的（以及为什么这样处理）

原始截图放在本地目录 `REF\`（个人参考图，不入库），由 `tools\prep_sponsor.cmd` 一步加工：

```
REF\WeChat.png / REF\Alipay.jpg / REF\PayPal.jpg
        │  tools\prep_sponsor.cmd   （需要 C:\tools\magick.exe）
        ├─ -strip 去掉 EXIF / ICC / 缩略图等全部元数据
        ├─ 限边长 720px、白底压平、JPEG quality 85
        ├─ resources\sponsor_*.jpg   → 以 RCDATA 打进 exe
        └─ docs\sponsor\sponsor_*.jpg → 给本 README 用（加 -SkipDocs 可跳过）
```

这样处理是为了**尽量不触发杀毒软件误报**：

- 磁盘上只发布一个 exe，收款码以 RCDATA 藏在资源里，**不附带任何散落的图片/脚本文件**；
- 图片去掉了元数据，也不带任何可执行内容；
- 解码走系统自带的 `gdiplus.dll`（运行时 `LoadLibrary` + `GetProcAddress` 拿函数地址），
  不引入第三方 DLL，**exe 的导入表和以前完全一样**，不会因为多出一个依赖而被判定为可疑；
- 脚本是**手动**跑的，`build.sh` 不会自动调用它，构建过程不碰外部程序。

换图流程：覆盖 `REF\` 里的原图 → 跑一次 `tools\prep_sponsor.cmd` → `build.cmd` 重新编译。
没跑过脚本时程序不会崩，收款码位置会显示「先运行 tools\prep_sponsor.cmd」的提示。

## 📜 开源协议

本程序以 **MIT License** 开源，全文见仓库根目录 [`LICENSE`](LICENSE)。

程序内 **帮助 → 关于 → 开源协议** 会列出本程序用到的第三方开源数据：

| 组件 | 来源 | 用途 | 协议 |
|------|------|------|------|
| AradeRenamer 本体 | 本仓库 | — | MIT |
| 简繁转换词表 | [OpenCC](https://github.com/BYVoid/OpenCC) 的 `dictionary/` 数据 | 文件名繁简互转 | Apache-2.0 |
| 拼音数据 | [pinyin-data](https://github.com/mozillazg/pinyin-data) | 汉字转拼音命名 | MIT |

> 这两份数据都以 RCDATA 打进 exe，许可证原文（`data\license_opencc.txt`、
> `data\license_pinyin_data.txt`）在同一个对话框里可以直接读全文。

**Apache-2.0 声明（OpenCC 词表）**：本仓库**原样**使用 OpenCC 的 `dictionary/` 数据，
未做任何修改（`resources/AradeRenamer.rc` 里也是直接引用上游文件，一个字节都没改）；
升级词表就是覆盖同名文件重新编译。Apache-2.0 全文见 `data/license_opencc.txt`。

版本历史见 [`docs/CHANGELOG.md`](docs/CHANGELOG.md)；当前版本 **v1.0.0**（exe 属性「详细信息」
里也能看到 `1.0.0.0`）。

## 📮 联系作者

- 开发者：**yjhua2005**
- 开源地址：<https://github.com/yjhua2005/AradeRenamer/>（Issue / PR 都欢迎）
- QQ 群：**484508921**（程序内「赞助/联系」窗口可一键复制群号）
- 个人 QQ：**10683223**
- 邮箱：<10683223@qq.com>

### 关于杀毒软件误报

> 本软件纯 Win32 API 开发，无任何联网/后门行为。若遇到杀毒软件误报，请将本程序加入信任区，或自行从源码编译。

程序没有网络代码（不链 wininet/winhttp/ws2_32），文件读写只发生在你自己加进列表的文件上；
右键菜单写 `HKEY_CLASSES_ROOT`（提升后会落到 `HKLM\Software\Classes`），「发送到」只写当前用户的
SendTo 文件夹，两者都能在同一个菜单里卸载干净。不放心可以按上面的说明自己编译一份。

**关于管理员权限**（右键菜单/发送到集成）：

| 场景 | 写入位置 | 要不要管理员 |
|---|---|---|
| 程序内「选项 > 添加 / 卸载右键菜单(管理员)」 | `HKEY_CLASSES_ROOT\...`（**所有用户**） | **需要**，但不用你自己操心：程序发现没提权会弹一次询问框，点「确定」后以管理员身份重启自己来完成（**弹一次 UAC**） |
| 以管理员身份运行 exe 后再点该命令 | 同上 | 已经在管理员权限下，直接写，不再弹 UAC |
| 手动导入随附的 `AradeRenamer.reg` | `HKEY_CLASSES_ROOT` | **需要**：注册表编辑器 / reg 导入要管理员权限 |
| 发送到菜单 | `%APPDATA%\...\SendTo\` 快捷方式 | **不需要**（不是注册表） |

程序平时**不常驻管理员权限**（清单里没声明 `requireAdministrator`），只有装 / 卸右键菜单这一下才提权。
写入被系统拒绝时（例如公司机器禁改注册表，或 UAC 被取消），程序会明确提示「没有权限写入注册表
（访问被拒绝）」，不会静默失败。

📖 Documentation Note
The full user manual (用户手册.html) is currently available in Chinese only.
Why? Because the author is a hardcore Chinese developer. 🌏
If you are an English user, please use your browser's built-in "Translate to English" feature (Right-click -> Translate), or simply ask ChatGPT to translate it.
PRs for an English manual are always welcomed!

