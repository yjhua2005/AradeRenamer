#pragma once

// ---- Dialog ----
#define IDR_MAINDLG             100
#define IDR_MAINMENU            101

// ---- Core controls ----
#define IDC_TAB                 1200
#define IDC_LIST                1201    // must not be 1001: an editable ComboBox's inner edit has ID 1001
#define IDC_STATUS              1002
#define IDC_PROGRESS            1203
#define IDC_REC_LIST            1210    // 左侧操作记录面板 (代码里创建, 见 操作记录界面.png)

// ---- Group boxes (need real IDs so they can be shown/hidden per tab) ----
#define IDC_OV_GRP_NAME         2009
#define IDC_OV_GRP_EXT          2010
#define IDC_RP_GRP              2106
#define IDC_NM_GRP              2206
#define IDC_AD_GRP              2309
#define IDC_DL_GRP              2409
#define IDC_EX_GRP              2509

// ---- Static labels (also need IDs: the tab show/hide pass can only reach ID'd controls) ----
#define IDC_OV_LB_NAME          2011
#define IDC_OV_LB_EXT           2012
#define IDC_OV_LB_FILTER        2013
#define IDC_RP_LB_FIND          2107
#define IDC_RP_LB_REPL          2108
#define IDC_NM_LB_TPL           2207
#define IDC_NM_LB_START         2208
#define IDC_NM_LB_STEP          2209
#define IDC_AD_LB_TEXT          2310
#define IDC_AD_LB_POS           2311
#define IDC_AD_POS_SPIN         2312   // 位置 的上下箭头
#define IDC_DL_LB_TEXT          2410
#define IDC_DL_LB_NUM           2411
#define IDC_EX_LB_TEXT          2510

// ---- Toolbar buttons ----
#define IDC_TB_ADD              1101   // 添加文件
#define IDC_TB_ADDF             1102   // 添加目录
#define IDC_TB_DEL              1103   // 移除
#define IDC_TB_APPLY            1104   // 更名
#define IDC_TB_UNDO             1105   // 撤销
#define IDC_TB_CLEAR            1106   // 清空
#define IDC_TB_PREVIEW          1107   // 预览
#define IDC_TB_PIN              1108   // 窗口置顶

// ---- Embedded icons (lowest ID = default EXE icon) ----
#define IDI_IC_APP      300   // 程序图标
#define IDI_IC_ADD      301   // 添加文件
#define IDI_IC_ADDF     302   // 添加目录 (folder)
#define IDI_IC_DEL      303   // 移除选中项
#define IDI_IC_CLEAR    304   // 清空列表
#define IDI_IC_PREVIEW  305   // 预览
#define IDI_IC_APPLY    306   // 开始更名
#define IDI_IC_UNDO     307   // 撤销上一次更名
#define IDI_IC_PIN      308   // 窗口置顶
#define IDI_IC_TAGDEL   309   // 移除音乐文件标签
#define IDI_IC_TAGEXT   310   // 提取音乐文件标签命名
#define IDI_IC_TAGWRITE 311   // 写入音乐文件标签
#define IDI_IC_T2S      312   // 文件名繁体转简体
#define IDI_IC_TEXTCMD  313   // 调用文本文件命令
#define IDI_IC_EXPLIST  314   // 导出当前文件列表
#define IDI_IC_RECOPEN  315   // 打开操作记录
#define IDI_IC_RECSAVE  316   // 保存操作记录
#define IDI_IC_RECCLEAR 317   // 清除操作记录
#define IDI_IC_CHGDATE  318   // 更改文件日期
#define IDI_IC_RECPANEL 319   // 显示操作记录面板
#define IDI_IC_SPONSOR  320   // 赞助 / 联系 (爱心)

// ===== Tab 0: 整体 =====
#define IDC_OV_NAME_UPPER       2001   // 文件名改成大写
#define IDC_OV_NAME_LOWER       2002   // 文件名改成小写
#define IDC_OV_NAME_TITLE       2003   // 文件名中单词首字母大写
#define IDC_OV_NAME_TEXT        2004   // 文件名改成 (edit)
#define IDC_OV_EXT_UPPER        2005   // 扩展名改成大写
#define IDC_OV_EXT_LOWER        2006   // 扩展名改成小写
#define IDC_OV_EXT_TEXT         2007   // 扩展名改成 (edit)
#define IDC_OV_FILTER           2008   // 文件过滤 (combo)

// ===== Tab 1: 替换 =====
#define IDC_RP_ENABLE           2101   // 启用替换
#define IDC_RP_FIND             2102   // 查找
#define IDC_RP_REPL             2103   // 替换为
#define IDC_RP_REGEX            2104   // 正则表达式
#define IDC_RP_CASEI            2105   // 忽略大小写

// ===== Tab 2: 序号 =====
#define IDC_NM_ENABLE           2201   // 启用序号
#define IDC_NM_TPL              2202   // 序号模板
#define IDC_NM_START            2203   // 起始
#define IDC_NM_STEP             2204   // 步长
#define IDC_NM_LETTER           2205   // 使用字母编号
#define IDC_NM_LB_PRESET        2210   // 预设
#define IDC_NM_PRESET           2211   // 预设模板下拉
#define IDC_NM_LB_WIDTH         2212   // 位数
#define IDC_NM_WIDTH            2213   // 位数(edit)
#define IDC_NM_PAD              2214   // 不足位补齐
#define IDC_NM_UPPER            2215   // 字母大写
#define IDC_NM_START_SPIN       2216   // 起始 的上下箭头
#define IDC_NM_STEP_SPIN        2217   // 步长 的上下箭头
#define IDC_NM_WIDTH_SPIN       2218   // 位数 的上下箭头

// ===== Tab 3: 添加 =====
#define IDC_AD_ENABLE           2301   // 启用添加
#define IDC_AD_TEXT             2302   // 添加内容
#define IDC_AD_PRE              2303   // 文件名前
#define IDC_AD_SUF              2304   // 文件名后
#define IDC_AD_EXTPRE           2305   // 扩展名前
#define IDC_AD_EXTSUF           2306   // 扩展名后
#define IDC_AD_AT               2307   // 指定位置
#define IDC_AD_POS              2308   // 位置(edit)

// ===== Tab 4: 删除 =====
#define IDC_DL_ENABLE           2401   // 启用删除
#define IDC_DL_TEXT             2402   // 删除指定文本
#define IDC_DL_HEAD             2403   // 删除前N个
#define IDC_DL_TAIL             2404   // 删除后N个
#define IDC_DL_CONTENT          2405   // 文本(edit)
#define IDC_DL_NUM              2406   // 数量(edit)
#define IDC_DL_NAME             2407   // 作用:文件名
#define IDC_DL_EXT              2408   // 作用:扩展名
#define IDC_DL_NUM_SPIN         2412   // 数量 的上下箭头
#define IDC_DL_RANGE            2413   // 从第N个起删M个
#define IDC_DL_START            2414   // 起始(edit)
#define IDC_DL_START_SPIN       2415   // 起始 的上下箭头
#define IDC_DL_LB_START         2416   // 起始

// ===== Tab 5: 扩展更名 =====
#define IDC_EX_ENABLE           2501   // 启用扩展更名
#define IDC_EX_ADD              2502   // 添加扩展名
#define IDC_EX_DEL              2503   // 删除扩展名
#define IDC_EX_CHG              2504   // 更改扩展名
#define IDC_EX_CASE             2505   // 扩展名大小写
#define IDC_EX_TEXT             2506   // 扩展名(edit)
#define IDC_EX_UP               2507   // 大写
#define IDC_EX_LO               2508   // 小写
#define IDC_EX_CMD              2511   // 扩展命令(edit)
#define IDC_EX_PRESET           2512   // 预设方案(listbox)
#define IDC_EX_LB_CMD           2513   // 扩展命令
#define IDC_EX_LB_PRESET        2514   // 预设方案

// ===== Tab 6: 标签命名 =====
#define IDC_TG_ENABLE           2603   // 启用标签命名
#define IDC_TG_LB_EXPR          2604   // 标签表达式
#define IDC_TG_EXPR             2605   // 表达式(edit)
#define IDC_TG_LB_PRE           2606   // 预设
#define IDC_TG_PRESET           2607   // 预设(combo)
#define IDC_TG_B1               2608   // %1 - 艺术家
#define IDC_TG_B2               2609   // %2 - 标题
#define IDC_TG_B3               2610   // %3 - 音轨
#define IDC_TG_B4               2611   // %4 - 专辑
#define IDC_TG_B5               2612   // %5 - 年代
#define IDC_TG_B6               2613   // %6 - 注释
#define IDC_TG_LB_PREV          2614   // 预览
#define IDC_TG_PREVIEW          2615   // 当前文件的六个标签值

// ===== Tab 7: 标签写入 =====
#define IDC_TW_ENABLE           2620   // 启用标签写入
#define IDC_TW_LB_EXPR          2621   // 文件名表达式
#define IDC_TW_EXPR             2622   // 表达式(edit)
#define IDC_TW_LB_PRE           2623   // 预设
#define IDC_TW_PRESET           2624   // 预设(combo)
#define IDC_TW_CLEAR            2625   // 清空
#define IDC_TW_SAVE             2626   // 保存
#define IDC_TW_WRITE            2627   // 全写
#define IDC_TW_LB_ARTIST        2630   // 艺术家
#define IDC_TW_ARTIST           2631
#define IDC_TW_LB_TITLE         2632   // 标题
#define IDC_TW_TITLE            2633
#define IDC_TW_LB_ALBUM         2634   // 专辑
#define IDC_TW_ALBUM            2635
#define IDC_TW_LB_TRACK         2636   // 音轨
#define IDC_TW_TRACK            2637
#define IDC_TW_LB_YEAR          2638   // 年代
#define IDC_TW_YEAR             2639
#define IDC_TW_LB_GENRE         2640   // 流派
#define IDC_TW_GENRE            2641   // 流派(combo)
#define IDC_TW_LB_COMMENT       2642   // 注释
#define IDC_TW_COMMENT          2643

// ===== 设置对话框 (5 页) =====
#define IDD_SETTINGS            102
#define IDC_SET_TAB             2700   // 一般参数/音乐文件标签/导出文件列表/元变量/界面
// ---- 页 0: 一般参数 ----
#define IDC_SET_IGNFOLDER       2711   // 忽略文件夹
#define IDC_SET_WALKALL         2712   // 遍历文件夹下的所有文件
#define IDC_SET_NODATE          2713   // 不获取文件的日期
#define IDC_SET_AUTOPREV        2714   // 文件更名自动预览
#define IDC_SET_SORTINIT        2715   // 文件列表在初始化时排序
#define IDC_SET_FASTPREV        2716   // 快速自动预览(只预览前十项)
#define IDC_SET_AUTOCORR        2717   // 自动更正错误
#define IDC_SET_CONFIRMEXIT     2718   // 退出时确认
#define IDC_SET_LB_WORD         2719   // 文件名单词判断字符
#define IDC_SET_WORDCHARS       2720   // 下拉: 空格 / 非英文字母和数字
// ---- 页 1: 音乐文件标签 ----
#define IDC_SET_ID3V2FIRST      2721   // MP3 ID3v2 标签优先
#define IDC_SET_LB_ID3NOTE      2722   // * 默认为 ID3v1优先
#define IDC_SET_LB_REMOVEM      2723   // 移除ID3标签时
#define IDC_SET_REMOVEMODE      2724   // 下拉
#define IDC_SET_LB_WRITEM       2725   // ID3标签写入时
#define IDC_SET_WRITEMODE       2726   // 下拉
#define IDC_SET_TAGPROMPT       2727   // 标签写入时提示
#define IDC_SET_KEEPDATE        2728   // 保留文件日期
#define IDC_SET_TRACKPAD        2729   // 音轨数字低于两位自动补0
#define IDC_SET_KEEPBLANK       2730   // 保留空白项的标签
#define IDC_SET_TAGBAK          2701   // 写入前先备份原文件 (.bak)
// ---- 页 2: 导出文件列表 ----
#define IDC_SET_EXP_EXT         2731   // 同时导出文件扩展名
#define IDC_SET_EXP_PATH        2732   // 同时导出文件路径
#define IDC_SET_EXP_NOFOLDER    2733   // 不包括文件夹
// ---- 页 3: 元变量 ----
#define IDC_SET_USEMETA         2734   // 使用元变量
#define IDC_SET_METALIST        2735   // 变量 / 值\表达式 两列
// ---- 页 4: 界面 ----
#define IDC_SET_ENHLIST         2736   // 文件列表使用增强界面
#define IDC_SET_GRID            2737   // 显示表格
#define IDC_SET_BIGTB           2738   // 工具栏使用大图标
#define IDC_SET_LB_COLOR        2739   // 文件列表颜色搭配
#define IDC_SET_COLOR           2740   // 下拉: 默认/灰色世界/郁闷心情/绿色草原/紫色淡雅
#define IDC_SET_RESTORE         2741   // 恢复默认

// ---- 更改文件日期对话框 (见 更改文件日期对话框.png) ----
#define IDD_CHGDATE             2800
#define IDC_DT_NOW              2801   // 单选: 使用当前的日期和时间
#define IDC_DT_CUSTOM           2802   // 单选: 使用自定义日期和时间
#define IDC_DT_GRP_CREATE       2803   // 分组: 创建日期
#define IDC_DT_GRP_ACCESS       2804   // 分组: 访问日期
#define IDC_DT_GRP_WRITE        2805   // 分组: 修改日期
#define IDC_DT_CREATE_D         2806   // 创建 日期选择器
#define IDC_DT_CREATE_T         2807   // 创建 时间选择器
#define IDC_DT_ACCESS_D         2808
#define IDC_DT_ACCESS_T         2809
#define IDC_DT_WRITE_D          2810
#define IDC_DT_WRITE_T          2811

// ---- 关于 / 开源协议对话框 (见 帮助--关于.png) ----
#define IDD_ABOUT               2900
#define IDD_LICENSE             2901
#define IDC_ABOUT_TEXT          2902   // 多行说明
#define IDC_ABOUT_LICENSE       2903   // 开源协议按钮
#define IDC_LIC_TEXT            2904   // 只读多行 edit
#define IDC_ABOUT_ICON          2905   // 程序图标
#define IDC_ABOUT_TITLE         2906   // 标题
#define IDC_ABOUT_SPONSOR       2929   // 赞助/联系按钮

// ---- 赞助与支持对话框 (关于 -- 赞助/联系) ----
#define IDD_SPONSOR             2930
#define IDC_SP_QR_WX            2919   // 自绘: 微信收款码 + 名称
#define IDC_SP_QR_ALI           2920   // 自绘: 支付宝收款码
#define IDC_SP_QR_PP            2921   // 自绘: PayPal 收款码
#define IDC_SP_GITHUB           2922   // 自绘可点: 开源地址
#define IDC_SP_QQ               2923   // 自绘可点: QQ 群 (点/按钮 = 复制)
#define IDC_SP_EMAIL            2924   // 自绘可点: 邮箱 (mailto)
#define IDC_SP_AUTHOR           2925   // 开发者 / 个人 QQ
#define IDC_SP_LIC              2926   // 开源协议 MIT
#define IDC_SP_COPY_QQ          2927   // 复制群号按钮
#define IDC_SP_DISCLAIMER       2928   // 免责声明边框
#define IDC_SP_DISC_TEXT        2931   // 自绘: 免责声明正文
// RCDATA: 三张收款码 (由 tools/prep_sponsor.cmd 从 REF\ 生成, 已去元数据)
#define IDR_SPONSOR_WECHAT      2940
#define IDR_SPONSOR_ALIPAY      2941
#define IDR_SPONSOR_PAYPAL      2942
#define IDR_LIC_OPENCC          2910   // RCDATA: OpenCC LICENSE 原文
#define IDR_LIC_PINYIN          2911   // RCDATA: pinyin-data LICENSE 原文
#define IDR_DICT_T2S            2912   // RCDATA: OpenCC TSCharacters.txt (繁->简 单字)
#define IDR_DICT_S2T            2913   // RCDATA: OpenCC STCharacters.txt (简->繁 单字)
#define IDR_PINYIN              2914   // RCDATA: pinyin-data pinyin.txt
#define IDR_DICT_T2S_PHRASES    2915   // RCDATA: OpenCC TSPhrases.txt (繁->简 词组)
#define IDR_DICT_S2T_PHRASES    2916   // RCDATA: OpenCC STPhrases.txt (简->繁 词组)
#define IDR_LANG_ZH_CN          2917   // RCDATA: Language\lang_zh-CN.ini (内置简体中文)
#define IDR_LANG_EN_US          2918   // RCDATA: Language\lang_en-US.ini (内置英语)
// 注意: Add-ons\gbkfix.txt 故意不打进 exe —— 它是运行时从磁盘读的用户自定义修正表,
// 改它不用重新编译; 文件不存在就跳过, 不影响任何功能。

// ---- Toolbar buttons (第二排) ----
#define IDC_TB_TAGDEL           1109   // 移除音乐文件标签
#define IDC_TB_TAGEXTRACT       1110   // 提取音乐文件标签命名
#define IDC_TB_TAGWRITE         1111   // 写入音乐文件标签
#define IDC_TB_T2S              1112   // 文件名繁体转简体
#define IDC_TB_TEXTCMD          1113   // 调用文本文件命令
#define IDC_TB_EXPLIST          1114   // 导出当前文件列表
#define IDC_TB_RECOPEN          1115   // 打开操作记录
#define IDC_TB_RECSAVE          1116   // 保存操作记录
#define IDC_TB_RECCLEAR         1117   // 清除操作记录
#define IDC_TB_CHGDATE          1118   // 更改文件日期
#define IDC_TB_REC_PANEL        1119   // 显示操作记录面板
#define IDC_TB_SPONSOR          1120   // 赞助 / 联系 (钉在工具栏最右边)

// ---- Menu command IDs ----
#define IDR_MAINACC             103    // 快捷键表
#define IDM_FILE_OPEN           4001
#define IDM_FILE_ADDFOLDER      4002
#define IDM_FILE_EXIT           4003
#define IDM_FILE_EXPORT         4004   // 导出当前列表
#define IDM_REN_PREVIEW         4011
#define IDM_REN_APPLY           4012
#define IDM_REN_UNDO            4013
#define IDM_OPT_SETTINGS        4022   // 设置...
#define IDM_HELP_ABOUT          4099
// 更名
#define IDM_REC_OPEN            4100   // 打开操作记录
#define IDM_REC_SAVE            4101   // 保存操作记录
#define IDM_REC_CLEAR           4102   // 清除操作记录
#define IDM_TEXT_NAMING         4103   // 调用文本文件命名
#define IDM_TAG_EXTRACT         4104   // 提取音乐文件标签命名
#define IDM_NAME_T2S            4105   // 文件名繁体转简体
#define IDM_NAME_S2T            4106   // 文件名简体转繁体
#define IDM_NAME_BIG5GB         4107   // 文件名 BIG5 -> GB
#define IDM_NAME_GBBIG5         4108   // 文件名 GB -> BIG5
#define IDM_NAME_SJISZH         4109   // 文件名日文Shift-JIS转中文
#define IDM_NAME_ZHSJIS         4110   // 文件名中文转日文Shift-JIS
#define IDM_NAME_CRC32          4111   // 以文件的 CRC32 更名
#define IDM_NAME_WEBTITLE       4112   // 以网页文件的标题更名
#define IDM_NAME_TEXTLINE1      4113   // 以文本文件的第一行内容更名
// 标签
#define IDM_TAG_REMOVE          4120   // 移除音乐文件标签
#define IDM_TAG_WRITEALL        4121   // 写入音乐文件标签
#define IDM_TAG_EXPORT          4122   // 导出音乐文件标签
#define IDM_TAG_FROMTEXT        4123   // 利用文本文件写入标签
#define IDM_ID3_V1TOV2          4124
#define IDM_ID3_V2TOV1          4125
#define IDM_TAG_BIG5GB          4126
#define IDM_TAG_GBBIG5          4127
#define IDM_TAG_T2S             4128
#define IDM_TAG_S2T             4129
#define IDM_TAG_SJISZH          4130
#define IDM_TAG_ZHSJIS          4131
// 选项 / 帮助
#define IDM_OPT_CENTER          4140   // 窗体始终显示在屏幕中央
#define IDM_OPT_LANG            4141   // Language (占位子菜单)
#define IDM_SHELL_REMOVE        4142   // 卸载资源管理器右键菜单
#define IDM_SHELL_ADD           4143   // 添加资源管理器右键菜单
#define IDM_SENDTO_ADD          4145   // 添加到 发送到 菜单
#define IDM_SENDTO_REMOVE       4146   // 从 发送到 菜单移除
#define IDM_HELP_MANUAL         4144   // 用户手册
// 文件列表右键菜单 (见 右键菜单.png)。Ctrl+A 全选走这里的 id，主菜单上不重复出现。
#define IDM_CTX_TAGREAD         4201   // 读入音乐文件标签
#define IDM_CTX_PLAY            4202   // 播放音乐文件
#define IDM_CTX_CHECK           4203   // 打勾
#define IDM_CTX_UNCHECK         4204   // 取消打勾
#define IDM_CTX_SELALL          4205   // 全选 (Ctrl+A)
#define IDM_CTX_SELNONE         4206   // 全不选
#define IDM_CTX_DELROWS         4207   // 删除选择的列表项
#define IDM_CTX_CLEARLIST       4208   // 清空文件列表
// 选项 > 语言 子菜单是运行时按可用语言动态生成的: 第 i 个语言用 IDM_LANG_BASE + i。
#define IDM_LANG_BASE           4300
#define IDM_LANG_OPEN_DIR       4399   // 打开语言文件夹 (子菜单最后一项)

