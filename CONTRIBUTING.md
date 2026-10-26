# 贡献指南 (Contributing to AradeRenamer)
Welcome to submit translated manuals (e.g., 用户手册_en-US.html).

感谢你对 AradeRenamer 的关注！这是一个纯 C++ / Win32 API 从零手写的极客工具。如果你愿意提交 Bug、建议或代码，我非常欢迎。

为了保持项目的纯粹和高性能，请在参与前阅读以下指南：

## 🐛 一、如何报告 Bug
如果你发现了 Bug，请在 [Issues](你的Issues链接) 中提交，并尽量包含以下信息：
1. **系统环境**：Windows 版本（如 Win10 22H2 / Win11 23H2）及是否为 64 位。
2. **复现步骤**：你是怎么操作导致出错的（越详细越好）。
3. **相关文件特征**：如果是特定文件出错，请说明文件格式（如 `.mp3`, `.txt`）或文件名是否有特殊字符。
4. **截图或日志**：如果有弹窗报错，请截图。

> **维护者 TODO**：上面的 `你的Issues链接` 是仓库地址还没定下来的占位符，仓库建好后替换成真实的
> Issues 地址；同时改 `src/main.cpp` 里的 `kSponsorRepo` 常量（搜 `kSponsorRepo`），
> 那是程序内「赞助/联系」窗口的「开源地址」按钮指向的链接。

## 💡 二、如何提功能建议
欢迎各种极客脑洞！但在提议前，请参考以下原则：
*   **克制**：这个工具主打极简、绿色、单文件。尽量不加非必要的臃肿功能。
*   **零依赖**：强烈不建议为了一个小功能引入庞大的第三方库。

## 🛠️ 三、如何提交代码 (Pull Requests)

### 1. 硬性约束（非常重要）
*   **严禁引入第三方库**：本项目坚持纯 Win32 API，**禁止**引入 Qt、Boost、GDI+、甚至是大体积的 C++ 包装库（除非是 Windows 系统自带的 DLL）。
*   **保持体积小巧**：代码改动不要导致 exe 体积大幅增加。
*   **多语言支持**：所有新增的界面文字，**必须**通过 `LS(IDS_xxx)` 宏调用；代码里写**中文字面量**由脚本接管，
    译文手写到 `Language/lang_en-US.ini`（及其它手写语言包）。`lang_zh-CN.ini` 是脚本生成的，**不要手改**，
    详见第 3 节。

### 2. 提交步骤
1. Fork 本仓库并 Clone 到本地。
2. 新建一个分支（如 `feature/new-tab` 或 `fix/crash-bug`）。
3. 提交你的更改（请确保编译通过且无明显警告）。
4. **提交前跑一遍语言脚本**（见第 3 节）：`python tools/extract_lang.py` → 补英文 → `python tools/apply_lang.py`，
   并把 `src/lang_ids.h` / `src/lang_keys.inc` / `Language/lang_zh-CN.ini` 三个生成物一起提交。
5. 发起 Pull Request，并描述你改了什么、为什么这么改。

### 3. 多语言支持规范 (i18n)

本项目采用 `.ini` 语言包机制，当前内置简体中文（zh-CN）和英语（en-US）。

**唯一来源是代码里的中文字面量**，不是任何一个 .ini：

- `Language/lang_zh-CN.ini`、`src/lang_ids.h`、`src/lang_keys.inc` 全部由 `tools/extract_lang.py`
  从 `resources/AradeRenamer.rc` 和 `src/main.cpp` 生成，**勿手改**（改文案 = 改代码里的字面量后重跑脚本）。
- `Language/lang_en-US.ini`（以及将来新增的语言包）是**手写**的。

*   **硬性要求**：所有新增或修改的界面文字，**必须**在代码中通过 `LS(IDS_xxx)` 宏调用。
*   **改一条界面文案 / 加一句新文字的标准流程**（顺序不能反）：
    1. 在 `src/main.cpp`（或 `.rc`）里直接写**中文字面量**。
    2. 跑 `python tools/extract_lang.py` —— 生成键名、写进 `lang_zh-CN.ini`、
       并重新生成 `src/lang_ids.h` / `src/lang_keys.inc`。
    3. 把新键的译文手写到 `Language/lang_en-US.ini`（脚本会列出哪个包漏翻了哪些键、哪个包没写署名）。
    4. 跑 `python tools/apply_lang.py` —— 把代码里的中文字面量替换成 `LS(IDS_xxx)`。
    5. 再跑一次 `extract_lang.py` 确认三个生成物 **零 diff**（稳定点）。
    > `apply_lang.py` 有顺序守卫：ini 里还没有这个键就替换的话，运行期 `LS()` 会拿到空串，
    > 脚本会**报错退出并且不改文件**。所以在第 2 步之前跑 apply 一定会被挡下来。
*   **如果你想提交新的语言包（例如日语、繁体中文）**：
    1. 复制 `Language/lang_en-US.ini` 作为模板，重命名为 `lang_ja-JP.ini` 等格式。
       **英语是翻译源语言**：从英文翻成你的语言，比从中文翻更容易找到对应的习惯说法，
       大多数译者（和机器翻译）也是以英文为中间语言。
       en-US 是手写的，偶尔会比刚新增的键落后几条——翻之前先跑一次
       `python tools/extract_lang.py`，它会列出这个包漏了哪些键；
       `lang_zh-CN.ini` 由脚本生成、**键永远和代码同步、最全**，拿它当对照表查缺补漏。
    2. 改文件头三行元信息：`lang.name`（菜单上显示的名字）/ `lang.code`（BCP-47 码）/
       **`lang.author`（写上你自己的署名）**。
       `lang.author` 记的是**这个语言包的译者**，不是程序作者；重跑 `extract_lang.py` 会保留它，
       也会报出哪个包忘了写。程序会把它显示在「选项 > 语言」菜单里：`English  (yjhua2005)`
       这样，用户一眼能看出这门语言是谁翻的（没写就不显示括号）。
    3. 逐条翻译；翻不动的键整行删掉即可，运行期自动回退简体中文，半成品语言包也能用。
    4. 注释只有**整行以 `;` 或 `#` 开头**才算——`ParseLang()` 只看行首一个字符，
       **行尾注释不生效**，会被当成值的一部分（写 `lang.name = 日本語 # 菜单名`
       会让语言菜单显示出一整串）。

### 4. 新增控件要进自适应布局

标签页里的控件宽度**不是写死的**：`LayoutTabPage()` 里每个控件声明一个 `minW`（按中文定的原设计宽度）
和 `gap`（与下一个控件的间距），运行时用 `FitW()` 量当前语言下文字实际要占多宽，取两者中较大的那个，
后一个控件顺次右移（见 `LayoutRow()`）。中文下量出来就等于 `minW`，英文下才撑开。

所以**新加/挪动控件时**：

*   把它放进对应页的 `RowItem` 数组里，填好 `minW` / `gap`，不要另外写死坐标；
*   `minW` 按中文宽度估（微软雅黑 9pt 一个汉字约 13 base px，勾选框再加约 20 px）；
*   加完之后**中英两种语言都跑一遍**确认不截断、不重叠；
*   一排放不下时脚本会按「多要出来的宽度」等比收回，不用手动调，但如果某个译文本身太长，
    优先精简那条译文（之前 `First/Last N characters` → `First/Last N chars` 就是这么处理的）。

### 5. 构建

```
build.cmd            :: Windows 下双击即可（内部调 build.sh）
bash build.sh        :: Git Bash / WSL
```

需要 MSVC（Visual Studio 2022 BuildTools）+ Windows 10 SDK，产物是 `bin\AradeRenamer.exe`。

> **构建前先关掉正在运行的 AradeRenamer**，否则链接器无法覆盖 exe（`LNK1104`）。
> 语言包是以 RCDATA 内嵌进 exe 的，改了 `Language\*.ini` 之后**必须重新构建**才会生效。

## ⚖️ 四、开源协议与版权声明

本项目采用 [MIT License](LICENSE) 开源。

**非常重要：**
为了保持项目的长期清晰，**你提交的 Pull Request（包括代码、文档等）一旦被合并，即视为你同意将你的贡献以本项目的 MIT 协议进行授权。** 你的版权依然归你，但你授予了项目和所有使用者免费使用的权利。

*(不需要签署复杂的 CLA 协议，只需提交 PR 即可视为同意)*

---
再次感谢你的支持！如果代码帮到了你，欢迎在“关于”里点个 ❤️ 请作者喝杯咖啡。