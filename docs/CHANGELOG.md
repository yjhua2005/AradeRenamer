# 更新日志 (Changelog)

本项目所有重要的版本更改都会记录在此文件中。

## [1.0.0] - 2026-10-26

首个公开版本。这一轮主要解决了「赞助 / 联系」入口打不开、英文界面文字挤成一团、
以及快捷键不全这三个问题。

### 修复 (Fixed)

- **点「开源地址」会崩浏览器**：原来是用 `CreateProcess` 自己拼命令行启动浏览器，
  Chromium 内核的 QQ 浏览器会在 500ms 内以 `0x80000003`（STATUS_BREAKPOINT）退出。
  改回 `ShellExecuteEx`，并且**不加 `SEE_MASK_NOCLOSEPROCESS`**（一加它就退化成
  自己 CreateProcess，坑又回来了）。http / https 一律交给系统关联。
- **点「邮箱」没反应**：这台机器的 `mailto` 关联指向已经卸载干净的 Foxmail。
  新增 `AssocHealthy()` 检查关联的程序还在不在，坏了就走
  `SOFTWARE\Clients\Mail` 里登记的默认邮件客户端（Outlook）兜底。
- **加速器抢编辑键**：消息循环里 `TranslateAcceleratorW` 跑在最前，导致在输入框里
  按 Ctrl+A 会全选列表行、Ctrl+Z 会撤销更名。加了 `FocusIsTextInput()` /
  `AccelBlockedByEdit()`，焦点在 Edit / ComboBox 时放行 Ctrl+A/C/V/X/Y/Z/Insert。
- **英文界面文字截断 / 重叠**：标签页里的控件改成了自适应布局（`RowItem` +
  `LayoutRow`，用 `FitW()` 实际量文字宽度再流式排布、超宽时等比压缩），
  英文模式下 52 处截断降到 0，8 个标签页无控件超界；中文布局宽度保持不变。
- **右键菜单项在资源管理器里显示成「Arad」**（严重）：`DoShellMenu()` 写菜单文字时把
  长度写成了 `sizeof(LS(IDS_SHELL_VERB))`，而 `LS()` 返回的是**指针**，x64 下
  `sizeof` 恒为 8 字节 = 4 个 wchar，于是只写进去 `Arad` 四个字符（连结尾的 `\0` 都没写）。
  编译期一声不响，只有在资源管理器右键里才看得见。已改成按 `wcslen` 计算
  （旁边写 Icon 的那行本来就是对的，只有这一行写错）。
  旧机器上升版本后**点一次「选项 &gt; 添加资源管理器右键菜单」**即可把残留的 `Arad`
  覆盖成正确的「AradeRenamer 更名」，顺带补上缺失的 `command` 子键。
- **「目录背景」右键写错了位置**：代码写的是 `Software\Classes\DesktopBackground`
  （桌面空白处右键），而随附的 .reg、手册、以及选项说明写的都是
  `Directory\Background`（文件夹内空白处右键）——两个不同的位置。已统一成
  `Directory\Background`，并在卸载时顺带清掉旧位置留下的键，免得它永远躺在注册表里。
- **写注册表失败时不吭声**：添加 / 卸载资源管理器右键菜单时某一项写失败，只会弹
  「已添加 0 个入口」，用户看不出原因。现在会记下最后一个错误码，遇到
  `ERROR_ACCESS_DENIED` 直接提示「没有权限写入注册表（访问被拒绝）…右键 exe 选
  「以管理员身份运行」后再操作一次」（新增 `IDS_ERR_SHELL_DENIED`，中英都有）。
- **管理员权限说明不全**：README 加了「场景 → 写入位置 → 要不要管理员」对照表
  （程序内菜单始终写 HKCU、免 UAC；要装给所有用户得手工改 HKLM、需要管理员）；
  用户手册 §7.1 加了同样的说明；`AradeRenamer.reg` 文件头也注明了。
- **「关于」对话框的按钮在英文下被截断**：三个按钮是固定 100px 宽，
  "Sponsor / contact"、"Open source licenses" 装不下。改成按当前语言实际量文字宽度
  再排（不够就加宽按钮，三个排不下就加宽整个对话框，标题/说明文字跟着拉宽），
  中文布局保持不变。
- 删掉死代码 `OnRemove()`（和 `DeleteRows()` 重复，从来没被调用过）。
- **切语言后「选项 &gt; 语言」子菜单不跟着变**：这个子菜单是运行时动态建的（条目 ID 是
  `IDM_LANG_BASE+i`），`ApplyLanguage()` 管不到它，而重建函数只在启动时调过一次，
  切到英文后第一项还是「自动」而不是「Auto」。现在 `OnLanguageChanged()` 里会重建它。
- **切语言后「操作记录」面板的横条不跟着变**：那条横条其实是 ListView 的列头，
  在 `WM_INITDIALOG` 里插入后就没再管过。现在 `RefreshRecPanel()` 顺手刷新它。
- **标签预览会拼出悬空标签**：艺术家/年代之类为空时，预览会显示成
  `Artist: Title:xxx`。改成空字段整段跳过。
- **启动失败的提示框标题是英文**：`Error`（正文是中文），已改回中文。这条消息故意不走
  语言包——创窗口失败时语言表还没装好，`LS()` 只会拿到空串（脚本里也标了跳过）。
  （单文件发行时 exe 旁边根本没有这个目录，或者目录是空的）直接 `return` 了，
  把后面补「自动」的代码一起跳过。现在改成跳过扫描但照样补上「自动」。

### 清理 (Cleanup)

- **编译警告清零**（`build.sh` 用的是 `/W4`，之前 8 条）：
  - `C4211`：`RebuildList()` 前向声明写成 `void RebuildList();`（外部链接），
    定义却是 `static`，靠 MSVC 的非标准扩展才没报错 → 声明补 `static`。
  - `C4310`（×6）：UTF-8 BOM 写成 `(char)0xEF`，0xEF 超过 signed char 上限 127。
    位模式没变、写进文件的字节一直是对的，但改用 `unsigned char kBomUtf8[3]`
    更准确地表达「这是一串字节」，两处写入点共用同一个常量。
  - `C4100`（×2）：`ChgDateProc` 的 `lp`、标签写入 lambda 的 `int i` 都没用到，
    改成匿名参数。
  - `C4505`（×2）：`engine.cpp` 里的 `ToInt()` / `CaseModeFromOv()` 是没人调用的
    死代码（main.cpp 有自己的 `ToIntSafe()`），删掉。
- README / CONTRIBUTING 里写的是 `bin\AradeRenamer.exe`，和 `build.sh` 实际产物
  `bin\AradeRenamer_X64_V1.0.exe` 对不上。**已统一为 `AradeRenamer.exe`**：输出文件名
  不带版本号（要发带版本号的包就自己从 `bin\` 拷到 `dist\` 改名），升版本号只改
  `resources/AradeRenamer.rc` 里的 `VS_VERSION_INFO`，提示写在 `build.sh` 顶部。

### 新增 (Added)

- **8 条快捷键**：F1 用户手册、Ctrl+Shift+A 添加文件、Ctrl+Shift+L 导出当前列表、
  Ctrl+Shift+R 清除操作记录、Ctrl+Shift+T 调用文本文件命名、
  Ctrl+Shift+E 导出音乐文件标签、Ctrl+Shift+I 利用文本文件写入标签、
  Ctrl+Shift+O 设置。菜单文字同步显示按键提示。
- **文件列表按 Delete 删除选中行**：挂在列表的 `LVN_KEYDOWN` 上（不进加速器表），
  这样焦点在输入框时 Delete 仍然是「删字符」。有选中行才删，**没选中什么都不做**。
- 右键菜单给「删除选择的列表项」加了 `Del` 提示、「全选」加了 `Ctrl+A`。
- **语言包作者署名**：每个 ini 头部新增 `lang.author`，本项目填 `yjhua2005`；
  以后别人补语言包时写自己的。
- 赞助 / 联系对话框：点链接时顺手把地址复制到剪贴板，浏览器拉不起来时会提示
  「已复制到剪贴板，请手动粘贴」而不是静默失败。
- **exe 有版本信息了**：以前 `resources/AradeRenamer.rc` 里没有 `VS_VERSION_INFO`，
  exe 属性里版本是 `0.0.0.0`、也没有产品名和版权。现在补上了（1.0.0.0 /
  AradeRenamer / Copyright (c) 2026 yjhua2005），沿用 rc 自包含的写法、不引 SDK 头。
- **语言菜单显示译者**：「选项 &gt; 语言」里每一项后面多了个括号写着该语言包 `lang.author`
  的署名，例如 `English  (yjhua2005)`；没写 `lang.author` 的老语言包只显示语言名，不加空括号。

### 变更 (Changed)

- `tools/extract_lang.py`：重新生成 ini 时保留已有的 `lang.*` 元信息（含 author）；
  跨语言包缺键或缺 author 时报警。
- `tools/apply_lang.py`：加了顺序守卫，ini 里缺键就直接报错退出（退出码 1，不改文件），
  不再写一半把代码改坏。
- 英文语言包精简了 `IDC_DL_HEAD` / `IDC_DL_TAIL`（`前 N 个字符` → `First N chars`），
  解决首字母大小写转换那个标签页的挤压。
- **`build.sh` 不再写死 VS / SDK 版本**：原来是硬编码的 `14.44.35207` +
  `10.0.26100.0`，换台机器一定编不过。现在先用 `vswhere.exe` 找 VS，再取
  `VC/Tools/MSVC` 和 `SDK/Include` 下版本号最新的那个；想固定版本就设
  `VS_ROOT` / `SDKVER` 环境变量。
- **新语言包的模板改成英语**：以前说「复制 `lang_zh-CN.ini`」，现在改成「复制 `lang_en-US.ini`」——
  英语是翻译源语言，从英文翻成目标语言最省事。zh-CN 仍然由脚本生成、键最全，
  改成当「查缺补漏的对照表」用。三份文档和两个 ini 的头部注释都同步改了。
- **右键菜单改写到 `HKEY_CLASSES_ROOT`，因此需要管理员权限（程序会自己申请）**：
  以前写 `HKCU\Software\Classes`（只对当前用户、免管理员）。现在写 HKCR，对机器上
  **所有用户**生效——代价是装 / 卸必须提权。做法：先用 `CheckTokenMembership` 判断
  本进程提权没有，没提权就弹询问框，用 `ShellExecute "runas"` 带 `/shell-add`
  （或 `/shell-remove`）重启一份提权副本去写，**弹一次 UAC**；副本在 `wWinMain`
  开头接住参数、干完就退出（不建主窗口、也不参与单实例判定——否则会被当成第二个实例，
  把参数转交给前一个没提权的实例然后退出，白跑一趟）。已经是管理员时不弹 UAC 直接写。
  - 菜单项随之改名为 **添加 / 卸载右键菜单(管理员)**，并补上助记符 `A` / `Z`。
  - 键位置照参考实现改用 `Folder\shell`（比 `Directory\shell` 覆盖面广：文件夹 /
    驱动器 / 库都算），三处是 `*\shell` + `Folder\shell` + `Directory\Background\shell`。
  - 装 / 卸都先清**历史残留键**，包括用户级 `HKCU\Software\Classes` 下的同名键——
    合并视图里 HKCU 优先级更高，旧值会把新写的值盖住（那个显示成 `Arad` 的菜单项
    就是这么赖着不走的）。
  - 新增两条提示：UAC 被取消 / 被策略阻止时给 `IDS_SHELL_RUNAS_FAILED`，
    写入被拒时给 `IDS_ERR_SHELL_DENIED`。
  - 随附的 `AradeRenamer.reg` 同步改成 `HKEY_CLASSES_ROOT`，并写明导入需要管理员权限。

### 文档 (Docs)

- **CONTRIBUTING.md**：i18n 章节整个重写。原来的说法和工具链是矛盾的（写着「en-US 是
  母版」「手改 zh-CN.ini」），实际正好相反——`lang_zh-CN.ini` 是脚本生成的、不能手改，
  唯一来源是代码里的中文字面量。现在写清了五步：
  写代码字面量 → `extract_lang.py` → 手翻 en-US → `apply_lang.py` → 再 extract 验零 diff。
  新语言包模板从「复制 en-US」改成「复制 zh-CN」。新增「新增控件要进自适应布局」和
  「构建」两节。
- **docs/用户手册.html**：§8 补赞助 / 联系入口，§8.1 补 `lang.author` 三行元信息；
  新增 **§9 快捷键一览**（列表 / 文件选项帮助 / 更名标签三张表），原 §9 常见问题顺延为
  §10 并补了「按 Delete 怎么没反应」。
- **README.md**：赞助章节改成正确描述（不再说「自己 CreateProcess」），工具栏清单补上
  最右侧的 ❤️ 按钮，补充 i18n 与 `lang.author` 说明（模板改英语、菜单显示译者）。
- **docs/用户手册.html**：§9 快捷键一览从 2 张表拆成 5 张（文件列表 / 文件 / 更名 / 标签 /
  选项+帮助），把**所有菜单命令都列全了**——原来「选项」「帮助」两个菜单整条没写，
  更名菜单里也缺了预览 / 应用 / 撤销 / 操作记录。没设快捷键的命令一律标「—」并说明原因。
  表格改成两列（原来更名+标签是四列并排，长命令名会被挤窄），按键列加 `white-space: nowrap`
  免得 `Ctrl+Alt+J` 断成两截。§3 更名菜单表补上了缺的 5 条基础命令。

### 发布准备 (为上传 GitHub 做的清理)

- 新增 `.gitattributes`：源码按 UTF-8 入库、Windows 检出转 CRLF（`.rc` / `.reg` 必须 CRLF），
  图片/exe 标二进制；`Add-ons/gbkfix.txt` 单独标 `-text`——它是 2003 年留下的 **GBK** 文件，
  一旦被 Git 当 UTF-8 转换就废了（`conv.cpp` 是先按 UTF-8 试、失败再按 GBK 解的）。
- `AradeRenamer.reg`（原名 `SmartAradeename.reg`，沿用早期拼写 `Aradeename`；
  发布前已改名为 `AradeRenamer.reg`）：里面 6 处写死了开发者本机的
  `D:\Myproject\...` 路径，已换成 `REPLACE_WITH_FULL_PATH_TO_EXE` 占位符并在文件头
  写明「导入前必须替换」，以及「更省事的做法是用程序里的 选项 &gt; 添加资源管理器右键菜单」。
  文件头另补了管理员权限说明（HKCU 免管理员；改 HKLM 给所有用户装才需要）。
- `src/test_engine.cpp` 里写死的测试路径 `D:\test\` 改成系统临时目录下的子目录。
- README 目录树与实际文件对不上的几处已修（不存在的 `docs/Language方案.md` /
  `指令.txt` / `docs/assets/` / `agent-debug/`）；补了构建前置条件、运行时随附文件清单
  （`用户手册.html` 要跟 exe 放一起，否则 F1 打不开）、Apache-2.0 的「原样使用未修改」声明
  和 CHANGELOG 链接。

### 已知问题 (Known issues)

- 赞助面板里的「开源地址」指向 `https://github.com/yjhua2005/AradeRenamer/`——
  本仓库就是按这个名字发布的，**v1.0 上传之后该链接自动生效**；在此之前点开是
  GitHub 的 404 页。程序本身不会崩（地址已复制到剪贴板）。
  若最终仓库名不是 `AradeRenamer`，需要同步改 `src/main.cpp` 的 `kSponsorRepo`、
  `resources/AradeRenamer.rc` 的 `IDC_SP_GITHUB`、README、用户手册和两份 ini。
- 「扩展命令」预设里的功能词（`汉字转换为拼音`、`字符顺序颠倒`…）**固定是中文**，
  界面切成英文也一样——引擎按这些中文词匹配，不能翻译。手册 §2.6 已写明。
- 列表排序用的是 `_wcsicmp`（按码位比）、大小写转换用的是 `towupper/towlower`，
  对带重音的拉丁字母不做语言级折叠（`café.txt` 转大写得到 `CAFé.TXT`）。
  属于已知限制，暂不处理。
