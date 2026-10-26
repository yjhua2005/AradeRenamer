#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从 resources/AradeRenamer.rc + src/main.cpp 提取全部中文 UI 字符串，生成:
   Language/lang_zh-CN.ini   键 = 当前中文 (简体中文语言包, 单一来源)
   lang_ids.h                键名 -> 数值 ID 映射表 (lang.cpp 用)

键分三类:
  1. .rc 控件文本        键 = 控件 ID 名 (IDC_*), 值 = 控件上的文字
  2. .rc 菜单/对话框标题 键 = 命令 ID 名 (IDM_*), 弹出菜单/标题用合成键 (IDS_MNU_*/IDS_DLG_*)
  3. main.cpp 运行时串   键 = 手工分配的 IDS_* (见下面 MAP), 值 = 字面量原文

用法: python3 tools/extract_lang.py   (在工程根目录运行)
改完界面文案后重跑一次即可刷新语言包; 新加的 IDS_* 要同步补进 MAP 和 resource.h。
"""
import re, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RC   = os.path.join(ROOT, "resources", "AradeRenamer.rc")
# 缺省读 src/main.cpp; 想从替换前的备份重建语言包就传一个路径参数。
# 跳过以 - 开头的开关: apply_lang.py 是 import 本模块拿 MAP/SKIP/CPP 的,
# 它自己的 --dry 也会被算进 sys.argv[1], 不能当成文件路径。
CPP = next((a for a in sys.argv[1:] if not a.startswith('-')),
           os.path.join(ROOT, "src", "main.cpp"))
HDR  = os.path.join(ROOT, "src", "resource.h")
OUT_INI  = os.path.join(ROOT, "Language", "lang_zh-CN.ini")
OUT_IDS  = os.path.join(ROOT, "src", "lang_ids.h")
OUT_INC  = os.path.join(ROOT, "src", "lang_keys.inc")

# ---------------------------------------------------------------------------
# main.cpp 字面量 -> IDS_* 映射 (全文精确匹配, 同一字面量多处出现只占一行)
# 不在表里的中文字面量会被列出来提醒 (脚本尾部打印), 但不会静默漏掉。
# ---------------------------------------------------------------------------
MAP = {
    # ---- 标签页 / 列表列头 / 状态栏 ----
    '整体': 'IDS_TAB_OVERALL', '替换': 'IDS_TAB_REPLACE', '序号': 'IDS_TAB_NUMBER',
    '添加': 'IDS_TAB_ADD', '删除': 'IDS_TAB_DELETE', '扩展更名': 'IDS_TAB_EXT',
    '标签命名': 'IDS_TAB_TAGNAME', '标签写入': 'IDS_TAB_TAGWRITE',
    '一般参数': 'IDS_SET_TAB_GENERAL', '音乐文件标签': 'IDS_SET_TAB_TAG',
    '导出文件列表': 'IDS_SET_TAB_EXPORT', '元变量': 'IDS_SET_TAB_META',
    '界面': 'IDS_SET_TAB_UI',
    '操作记录': 'IDS_REC_TAB',
    '文件名': 'IDS_COL_NAME', '预览': 'IDS_COL_PREVIEW',
    '扩展名': 'IDS_COL_EXT', '日期': 'IDS_COL_DATE', '路径': 'IDS_COL_PATH',
    '大小': 'IDS_COL_SIZE', '识别类型': 'IDS_COL_KIND', '结果': 'IDS_COL_RESULT',
    '变量': 'IDS_META_COL_VAR', '值/表达式': 'IDS_META_COL_VAL',
    '文件夹': 'IDS_KIND_FOLDER', '普通文件': 'IDS_KIND_FILE',
    '%d 个文件': 'IDS_STATUS_FILES',
    '待更名 %d   冲突 %d': 'IDS_STATUS_PENDING_CONFLICT',
    '待更名 %d': 'IDS_STATUS_PENDING',
    '未选': 'IDS_RESULT_UNCHECKED', '跳过': 'IDS_RESULT_SKIP',
    '成功': 'IDS_RESULT_OK', '失败': 'IDS_RESULT_FAIL',
    # ---- 更名结果消息框 ----
    'Arade文件更名器': 'IDS_APP_TITLE',
    '完成: 重命名 %d 个, 失败 %d': 'IDS_MSG_RENAME_DONE',
    '没有可撤销的操作。': 'IDS_MSG_UNDO_EMPTY',
    '已撤销 %d 个重命名': 'IDS_MSG_UNDO_DONE',
    '确定要退出吗？': 'IDS_MSG_CONFIRM_EXIT', '退出': 'IDS_MSG_CONFIRM_EXIT_T',
    # ---- 添加文件/目录 ----
    '选择文件夹': 'IDS_PICK_FOLDER_TITLE',
    # ---- 工具栏提示 ----
    '添加文件': 'IDS_TIP_ADD', '添加目录': 'IDS_TIP_ADDF',
    '预览更名结果 (Ctrl+F)': 'IDS_TIP_PREVIEW', '撤销上一次更名 (Ctrl+Z)': 'IDS_TIP_UNDO',
    '开始更名 (Ctrl+E)': 'IDS_TIP_APPLY', '移除音乐文件标签 (Ctrl+R)': 'IDS_TIP_TAGDEL',
    '提取音乐文件标签命名 (Ctrl+D)': 'IDS_TIP_TAGEXTRACT',
    '写入音乐文件标签 (Ctrl+W)': 'IDS_TIP_TAGWRITE',
    '文件名繁体转简体 (Ctrl+Alt+S)': 'IDS_TIP_T2S', '调用文本文件命令': 'IDS_TIP_TEXTCMD',
    '导出当前文件列表': 'IDS_TIP_EXPLIST', '打开操作记录 (Ctrl+O)': 'IDS_TIP_RECOPEN',
    '保存操作记录 (Ctrl+S)': 'IDS_TIP_RECSAVE', '清除操作记录': 'IDS_TIP_RECCLEAR',
    '更改文件日期': 'IDS_TIP_CHGDATE', '显示操作记录面板': 'IDS_TIP_RECPANEL',
    '赞助 / 联系作者': 'IDS_TIP_SPONSOR',
    '窗口置顶 (总在最前)': 'IDS_TIP_PIN',
    # ---- 标签预览/标签写入页 ----
    '艺术家:': 'IDS_TAGPREV_ARTIST', '  标题:': 'IDS_TAGPREV_TITLE',
    '  音轨:': 'IDS_TAGPREV_TRACK', '  专辑:': 'IDS_TAGPREV_ALBUM',
    '  年代:': 'IDS_TAGPREV_YEAR', '  注释:': 'IDS_TAGPREV_COMMENT',
    '未选中文件': 'IDS_TAGPREV_NONE', '无标签(或非音频文件)': 'IDS_TAGPREV_NOTAG',
    '艺术家 - 标题': 'IDS_TPRESET_ARTIST_TITLE', '标题 (艺术家)': 'IDS_TPRESET_TITLE_ARTIST',
    '音轨.标题': 'IDS_TPRESET_TRACK_TITLE', '音轨.艺术家 - 标题': 'IDS_TPRESET_TRACK_ARTIST_TITLE',
    '音轨.标题 (艺术家)': 'IDS_TPRESET_TRACK_TITLE_ARTIST',
    '艺术家.专辑.音轨.标题': 'IDS_TPRESET_ALL4', '艺术家 - 标题  (年代)': 'IDS_TPRESET_ARTIST_TITLE_YEAR',
    '[全部]': 'IDS_TPRESET_ALL',
    # ---- 设置页下拉 ----
    '空格': 'IDS_WORD_SPACE', '非英文字母和数字': 'IDS_WORD_NONALNUM',
    '只移除ID3v1': 'IDS_REMOVE_V1', '只移除ID3v2': 'IDS_REMOVE_V2',
    'ID3v1和ID3v2同时移除': 'IDS_REMOVE_BOTH',
    '只写入ID3v1': 'IDS_WRITE_V1', '只写入ID3v2': 'IDS_WRITE_V2',
    'ID3v1和ID3v2同时写入': 'IDS_WRITE_BOTH',
    '默认': 'IDS_COLOR_DEFAULT', '灰色世界': 'IDS_COLOR_GRAY',
    '郁闷心情': 'IDS_COLOR_BLUE', '绿色草原': 'IDS_COLOR_GREEN',
    '紫色淡雅': 'IDS_COLOR_PURPLE',
    # ---- 导出/操作记录 ----
    '文件列表.txt': 'IDS_DEF_FILE_LIST', '操作记录.xto': 'IDS_DEF_RECORD',
    '音乐标签.txt': 'IDS_DEF_TAG_EXPORT',
    '无法写入所选文件。': 'IDS_ERR_WRITE_FILE',
    '已导出 %d 项到\\n%s': 'IDS_MSG_LIST_EXPORTED',
    '导出文件列表': 'IDS_TITLE_EXPORT_LIST',
    '保存操作记录': 'IDS_TITLE_REC_SAVE', '打开操作记录': 'IDS_TITLE_REC_OPEN',
    '还没有任何操作记录。': 'IDS_MSG_REC_NONE',
    '已保存 %d 条操作记录到\\n%s': 'IDS_MSG_REC_SAVED',
    '无法打开所选文件。': 'IDS_ERR_OPEN_FILE',
    '这个文件里没有可用的操作记录。': 'IDS_MSG_REC_EMPTY_FILE',
    '有 %d 条记录的文件已不在原地, 未加入列表。': 'IDS_MSG_REC_MISSING',
    # ---- 更改文件日期 ----
    '更改文件日期': 'IDS_TITLE_CHGDATE',
    '所选日期超出可表示的范围。': 'IDS_MSG_DATE_RANGE',
    '完成: 更改日期 %d 个, 失败 %d 个': 'IDS_MSG_DATE_DONE',
    # ---- 标签写入/转换/移除 (RunTagRows 一族) ----
    '标签写入': 'IDS_TITLE_TAGWRITE',
    '未启用标签写入：请先勾选本页左上角的 启用标签写入。': 'IDS_MSG_TAGWRITE_OFF',
    '没有要打勾的文件：先在列表里打勾，或者用 全写 处理整个列表。': 'IDS_MSG_TAGWRITE_NOROWS',
    '页面上没有要写入的内容：先填几个标签项，或用 文件名表达式 从文件名反解。': 'IDS_MSG_TAGWRITE_NOCONTENT',
    '将把标签写入 %d 个文件，文件里原有的同名标签会被覆盖。继续吗？': 'IDS_MSG_TAGWRITE_CONFIRM',
    '跳过文件夹': 'IDS_RES_SKIP_DIR', '已写入': 'IDS_RES_WRITTEN',
    '名字不匹配表达式': 'IDS_RES_NAME_MISMATCH', '备份失败': 'IDS_RES_BAK_FAIL',
    '不支持的格式': 'IDS_RES_UNSUPPORTED', '写入失败': 'IDS_RES_WRITE_FAIL',
    '完成: 写入 %d 个, 跳过 %d 个, 失败 %d 个': 'IDS_MSG_TAGWRITE_DONE',
    '没有要打勾/选中的文件：先在列表里打勾或选中要处理的行。': 'IDS_MSG_TAGOP_NOROWS',
    '将处理 %d 个文件，继续吗？': 'IDS_MSG_TAGOP_CONFIRM',
    '无需处理': 'IDS_RES_NOTHING', '处理失败': 'IDS_RES_OP_FAIL',
    '完成: %d 个, 跳过 %d 个, 失败 %d 个': 'IDS_MSG_TAGOP_DONE',
    '已转换': 'IDS_RES_CONVERTED',
    '标签繁体转简体': 'IDS_TITLE_TAG_T2S', '标签简体转繁体': 'IDS_TITLE_TAG_S2T',
    '标签 BIG5 -> GB': 'IDS_TITLE_TAG_BIG5GB', '标签 GB -> BIG5': 'IDS_TITLE_TAG_GBBIG5',
    '标签日文Shift-JIS转中文': 'IDS_TITLE_TAG_SJISZH', '标签中文转日文Shift-JIS': 'IDS_TITLE_TAG_ZHSJIS',
    '移除音乐文件标签': 'IDS_TITLE_TAG_REMOVE', '已移除': 'IDS_RES_REMOVED',
    'ID3v2 复制到 ID3v1': 'IDS_TITLE_ID3_V2V1', 'ID3v1 复制到 ID3v2': 'IDS_TITLE_ID3_V1V2',
    '已写入ID3v1': 'IDS_RES_WROTE_V1', '已写入ID3v2': 'IDS_RES_WROTE_V2',
    '导出音乐文件标签': 'IDS_TITLE_TAG_EXPORT',
    '没有要打勾/选中的文件：先在列表里打勾或选中要导出的行。': 'IDS_MSG_TAGEXPORT_NOROWS',
    '已导出 %d 行的标签到\\n%s': 'IDS_MSG_TAG_EXPORTED',
    # ---- 标签文本格式 (导出/利用文本文件写入) ----
    '文件名\\t艺术家\\t标题\\t专辑\\t音轨\\t年代\\t流派\\t注释\\r\\n': 'IDS_TAG_TEXT_HEADER',
    '选择标签文本文件': 'IDS_PICK_TAG_TITLE', '选择命令文本文件': 'IDS_PICK_CMD_TITLE',
    '利用文本文件写入标签': 'IDS_TITLE_TAG_FROMTEXT',
    '没有要打勾/选中的文件：先在列表里打勾或选中要写入的行。': 'IDS_MSG_TAGFROMTEXT_NOROWS',
    '没有解析到任何数据行。格式说明：\\n每行一条记录，用 Tab 分隔：\\n  文件名\\\\t艺术家\\\\t标题\\\\t专辑\\\\t音轨\\\\t年代\\\\t流派\\\\t注释\\n字段可以留空；# 或 ; 开头是注释行；首行表头会自动跳过。\\n可以用 标签 > 导出音乐文件标签 生成这个文件，填好后再导回来。': 'IDS_MSG_TAGFROMTEXT_FORMAT',
    '文本文件里的 %d 条记录没有一个匹配当前列表的文件名。\\n（按文件名精确匹配，注意扩展名）': 'IDS_MSG_TAGFROMTEXT_NOMATCH',
    '将把文本文件里的标签写入 %d 个文件，原有同名标签会被覆盖。继续吗？': 'IDS_MSG_TAGFROMTEXT_CONFIRM',
    # ---- 调用文本文件命令 ----
    '调用文本文件命令': 'IDS_TITLE_TEXTCMD',
    '没有解析到任何命令。格式说明：\\n每行一条命令：原文件名=新文件名（也可以用 Tab 分隔）。\\n以 # 或 ; 开头的行是注释。新名不带扩展名时保留原扩展名。\\n例：\\n  旧歌.mp3=新歌.mp3\\n  照片.jpg=旅行': 'IDS_MSG_TEXTCMD_FORMAT',
    '命令文件里的 %d 条命令没有一个匹配当前列表的文件名。': 'IDS_MSG_TEXTCMD_NOMATCH',
    '已载入 %d 条命令，其中 %d 条匹配当前列表；新名见 预览 列。': 'IDS_MSG_TEXTCMD_LOADED',
    # ---- 资源管理器右键菜单 / 发送到 ----
    '所有文件': 'IDS_FILTER_ALL',
    'AradeRenamer 更名': 'IDS_SHELL_VERB', '目录背景': 'IDS_SHELL_CTX_BG',
    '已在资源管理器右键菜单添加 %d 个入口（对当前用户生效）：\\n文件 / 文件夹 / 目录背景 的「AradeRenamer 更名」。': 'IDS_MSG_SHELL_ADDED',
    '已卸载 %d 个右键菜单入口。': 'IDS_MSG_SHELL_REMOVED',
    '没有权限写入注册表（访问被拒绝）。请退出程序，右键 AradeRenamer.exe 选「以管理员身份运行」后再操作一次。': 'IDS_ERR_SHELL_DENIED',
    # 注意: 这里必须是原始字符串(r''), 源码里的 \n 是"反斜杠+n"两个字符,
    # 用普通字符串会被 Python 吃掉变成真换行符, 于是永远匹配不上。
    r'添加 / 卸载资源管理器右键菜单要改 HKEY_CLASSES_ROOT，需要管理员权限。\n\n点「确定」会以管理员身份重新启动本程序来完成（系统会弹一次 UAC 确认）。\n\n也可以自己先退出程序，右键 AradeRenamer.exe 选「以管理员身份运行」，再执行这个命令。': 'IDS_SHELL_NEED_ADMIN',
    r'没能以管理员身份启动（UAC 被取消，或被系统策略阻止）。\n\n请退出程序后右键 AradeRenamer.exe 选「以管理员身份运行」，再执行这个命令。': 'IDS_SHELL_RUNAS_FAILED',
    '添加资源管理器右键菜单': 'IDS_TITLE_SHELL_ADD', '卸载资源管理器右键菜单': 'IDS_TITLE_SHELL_REMOVE',
    '发送到菜单': 'IDS_TITLE_SENDTO',
    '找不到 发送到 文件夹。': 'IDS_ERR_SENDTO_DIR',
    '删除发送到菜单的快捷方式失败。': 'IDS_ERR_SENDTO_DEL',
    '已从 发送到 菜单移除。': 'IDS_MSG_SENDTO_REMOVED',
    '发送到 菜单里本来就没有这一项。': 'IDS_MSG_SENDTO_NOTHAVE',
    '创建快捷方式失败。': 'IDS_ERR_SENDTO_CREATE',
    '用 AradeRenamer 批量更名（多选文件会全部进同一个窗口）': 'IDS_SENDTO_DESC',
    '写入发送到菜单的快捷方式失败。': 'IDS_ERR_SENDTO_WRITE',
    '已添加到 发送到 菜单：\\n%s\\n\\n在资源管理器选中文件 → 右键 → 发送到 → AradeRenamer 更名。\\n多选文件会一次性全部送进程序（单实例，只开一个窗口）。': 'IDS_MSG_SENDTO_ADDED',
    '从 发送到 菜单移除(&V)': 'IDS_MNU_SENDTO_REMOVE', '添加到 发送到 菜单(&S)': 'IDS_MNU_SENDTO_ADD',
    # ---- 用户手册 / 播放 ----
    '用户手册': 'IDS_TITLE_MANUAL',
    '找不到 用户手册.html。\\n它应该和 AradeRenamer.exe 放在同一个目录。': 'IDS_ERR_MANUAL_MISSING',
    '无法打开用户手册（没有关联的浏览器？）。': 'IDS_ERR_MANUAL_OPEN',
    '播放音乐文件': 'IDS_TITLE_PLAY', '无法打开:\\n%s': 'IDS_ERR_PLAY',
    # ---- 网页标题 / 文本第一行 ----
    '列表里没有 .htm/.html 网页文件。\\n以网页文件的标题更名只对网页文件生效，请先添加网页文件。': 'IDS_MSG_WEBTITLE_NOHTML',
    '以网页文件的标题更名': 'IDS_TITLE_WEBTITLE',
    '列表里没有 .txt 文本文件。\\n以文本文件的第一行内容更名只对文本文件生效，请先添加文本文件。': 'IDS_MSG_TEXTLINE1_NOTXT',
    '以文本文件的第一行内容更名': 'IDS_TITLE_TEXTLINE1',
    # ---- 列表右键菜单 ----
    '读入音乐文件标签(&S)': 'IDS_CTX_TAGREAD', '播放音乐文件(&T)': 'IDS_CTX_PLAY',
    '打勾(&U)': 'IDS_CTX_CHECK', '取消打勾(&V)': 'IDS_CTX_UNCHECK',
    '全选(&W)\\tCtrl+A': 'IDS_CTX_SELALL', '全不选(&X)': 'IDS_CTX_SELNONE',
    '删除选择的列表项(&Y)': 'IDS_CTX_DELROWS', '清空文件列表(&Z)': 'IDS_CTX_CLEARLIST',
    # ---- 关于 / 开源协议对话框 ----
    'Version 1.0.0-beta.1 (Built on 2026-10-26)   \\n纯 Win32 API 手写，未链接任何第三方库。\\n更名: 整体改名、替换(含正则)、序号、\\n      添加、删除、扩展更名、元变量、\\n      简繁互转、BIG5<->GB、Shift-JIS 转换、\\n      以 CRC32/网页标题/文本首行/文本命令更名、更改日期。\\n标签: 读写 MP3/FLAC/APE/OGG/M4A/AAC/\\n      WAV/Opus 标签(ID3v1/v2、Vorbis comment)。\\n其它: 操作记录(.xto)、导出列表、拖放添加。': 'IDS_ABOUT_TEXT',
    '本程序用到的开源数据与许可证\\r\\n\\r\\n': 'IDS_LIC_ABOUT_TITLE',
    '1) 简体-繁体中文字词转换数据\\r\\n': 'IDS_LIC_OPENCC_H',
    '   项目: OpenCC  (BYVoid/OpenCC)\\r\\n': 'IDS_LIC_OPENCC_PROJ',
    '   地址: https://github.com/BYVoid/OpenCC\\r\\n': 'IDS_LIC_OPENCC_URL',
    '   许可: Apache License 2.0\\r\\n': 'IDS_LIC_OPENCC_LIC',
    '   用途: 文件名/标签简繁互转的内置字表取自其 STCharacters.txt、TSCharacters.txt\\r\\n': 'IDS_LIC_OPENCC_USE',
    '         等字典文件；另可在运行目录的 Add-ons\\\\gbkfix.txt 中追加自定义字表。\\r\\n\\r\\n': 'IDS_LIC_OPENCC_USE2',
    '   ------- Apache License 2.0 全文 (上游原文，未修改) -------\\r\\n\\r\\n': 'IDS_LIC_OPENCC_FULL',
    '2) 汉字拼音数据\\r\\n': 'IDS_LIC_PINYIN_H',
    '   项目: pinyin-data  (mozillazg/pinyin-data)\\r\\n': 'IDS_LIC_PINYIN_PROJ',
    '   地址: https://github.com/mozillazg/pinyin-data\\r\\n': 'IDS_LIC_PINYIN_URL',
    '   许可: MIT License, Copyright (c) 2016 mozillazg\\r\\n': 'IDS_LIC_PINYIN_LIC',
    '   用途: 汉字转拼音(全拼/首字母)命名所用的内置拼音表。\\r\\n\\r\\n': 'IDS_LIC_PINYIN_USE',
    '   ------- MIT License 全文 (上游原文，未修改) -------\\r\\n\\r\\n': 'IDS_LIC_PINYIN_FULL',
    'AradeRenamer 更名.lnk': 'IDS_SENDTO_LNKNAME',
    # ---- 赞助/联系: 链接拉不起程序时的提示 ----
    '没能打开浏览器：系统里没有可用的关联程序。\\n地址已经复制到剪贴板，请手动粘贴到浏览器打开。': 'IDS_SP_OPEN_FAIL',
    '没能打开邮件程序：系统 mailto 关联的程序已经不存在了。\\n邮箱地址已经复制到剪贴板，请手动发送。': 'IDS_SP_MAIL_NONE',
    # ---- 流派下拉 ----
    '流行': 'IDS_GENRE_POP', '摇滚': 'IDS_GENRE_ROCK', '民谣': 'IDS_GENRE_FOLK',
    '电子': 'IDS_GENRE_ELECTRONIC', '古典': 'IDS_GENRE_CLASSICAL', '嘻哈': 'IDS_GENRE_HIPHOP',
    '爵士': 'IDS_GENRE_JAZZ', '金属': 'IDS_GENRE_METAL', '轻音乐': 'IDS_GENRE_EASY',
    '原声带': 'IDS_GENRE_SOUNDTRACK', '国语': 'IDS_GENRE_MANDARIN', '粤语': 'IDS_GENRE_CANTONESE',
    '欧美': 'IDS_GENRE_WESTERN', '日韩': 'IDS_GENRE_JPKR', '其它': 'IDS_GENRE_OTHER',
    # ---- 序号/扩展预设里的 sentinel ----
    '[清除列表]': 'IDS_CLEAR_LIST',
    # ---- 文件对话框过滤器 (整串, \0 是分隔; 模式部分 *.txt 等不译) ----
    '打开语言文件夹(&O)': 'IDS_MNU_LANG_OPEN_DIR',
    '自动': 'IDS_LANG_AUTO',
    '所有文件\\0*.*\\0': 'IDS_FILTER_ALL_FILES',
    '文本文件 (*.txt)\\0*.txt\\0所有文件 (*.*)\\0*.*\\0': 'IDS_FILTER_TXT',
    '操作记录 (*.xto)\\0*.xto\\0所有文件 (*.*)\\0*.*\\0': 'IDS_FILTER_XTO',
}

# 故意不映射的 (脚本不报警, 原因写在注释里):
#   1) 扩展命令关键词 (汉字转换为拼音/英文字母大小写翻转/...) —— 用户要在"扩展命令"
#      框里输入的功能词, 第一版按决定不做英文, 界面里保持中文。
#   2) "无法创建主窗口。" / "错误" —— 创窗口失败这条路径上语言表还没装好,
#      LS() 只会拿到空串, 所以这条消息连标题一起保持中文 (标题原来是写死的
#      L"Error", 中文界面下弹个英文标题, 已改回中文)。
#   3) "用户手册.html" —— 文件名, 不是界面文本。
SKIP = {
    '汉字转换为拼音', '汉字转换为拼音且首字母大写', '添加第一个汉字的拼音声母',
    '字符转换为数值', '数值转换为字符', '字符顺序颠倒',
    '英文字母大小写翻转', '英文字母一大一小', '英文字母一小一大',
    '无法创建主窗口。', '错误', '用户手册.html',
}

# 菜单弹出项/对话框标题的合成键 (按出现顺序)
POPUP_KEYS = {'文件(&F)': 'IDS_MNU_FILE', '更名(&P)': 'IDS_MNU_RENAME',
              '标签(&T)': 'IDS_MNU_TAG', '选项(&O)': 'IDS_MNU_OPT',
              '帮助(&H)': 'IDS_MNU_HELP', '语言(&L)': 'IDS_MNU_LANGUAGE'}

# rc 里直接写数字而不是符号名的控件 ID (IDOK/IDCANCEL 在 winuser.h, 不在 resource.h)
NUMID_FIX = {1: 'IDOK', 2: 'IDCANCEL'}
DLG_KEYS = {'Arade文件更名器': 'IDS_DLG_MAIN', '设置': 'IDS_DLG_SETTINGS',
            '更改文件日期': 'IDS_DLG_CHGDATE', '关于': 'IDS_DLG_ABOUT',
            '开源许可证': 'IDS_DLG_LICENSE', '赞助与支持': 'IDS_DLG_SPONSOR'}

# ---------------------------------------------------------------------------
def esc(s):
    """ini 值转义: 反斜杠/制表/回车/换行/NUL -> \\X"""
    out = []
    for ch in s:
        if ch == '\\': out.append('\\\\')
        elif ch == '\t': out.append('\\t')
        elif ch == '\r': out.append('\\r')
        elif ch == '\n': out.append('\\n')
        elif ch == '\0': out.append('\\0')
        else: out.append(ch)
    return ''.join(out)

def cpp_unesc(s):
    """把 C++ 源码里的转义序列还原成运行时文本 (ini 里存的是运行时文本)"""
    m = {'n': '\n', 'r': '\r', 't': '\t', '0': '\0', '\\': '\\',
         '"': '"', "'": "'", 'a': '\a', 'b': '\b', 'f': '\f', 'v': '\v'}
    out = []
    i = 0
    while i < len(s):
        if s[i] == '\\' and i + 1 < len(s) and s[i + 1] in m:
            out.append(m[s[i + 1]]); i += 2; continue
        out.append(s[i]); i += 1
    return ''.join(out)

def unesc(s):
    out = []
    i = 0
    while i < len(s):
        if s[i] == '\\' and i + 1 < len(s):
            c = s[i + 1]
            if c == '\\': out.append('\\'); i += 2; continue
            if c == 't': out.append('\t'); i += 2; continue
            if c == 'r': out.append('\r'); i += 2; continue
            if c == 'n': out.append('\n'); i += 2; continue
            if c == '0': out.append('\0'); i += 2; continue
        out.append(s[i]); i += 1
    return ''.join(out)

def main():
    # ---- resource.h: 数值 ID -> 符号名 ----
    id2name = {}
    for line in open(HDR, encoding='utf-8'):
        m = re.match(r'\s*#define\s+(ID[A-Z]_\w+|\w+)\s+(\d+)', line)
        if m:
            id2name[int(m.group(2))] = m.group(1)

    entries = {}   # key -> (value, group)
    groups = {}    # key -> 分组注释

    def add(key, val, grp):
        if key in entries and entries[key][0] != val:
            print('!! 键冲突 %s: %r vs %r' % (key, entries[key][0], val))
        entries[key] = (val, grp)

    # ---- .rc: 对话框标题 + 控件文本 + 菜单 ----
    rc = open(RC, encoding='utf-8').read().split('\n')
    cur_dlg = None
    for i, line in enumerate(rc):
        # 对话框定义
        m = re.match(r'\s*(\w+)\s+DIALOGEX', line)
        if m:
            cur_dlg = m.group(1)
        # CAPTION
        m = re.search(r'CAPTION\s+"([^"]*)"', line)
        if m and cur_dlg:
            cap = m.group(1)
            if cap in DLG_KEYS:
                add(DLG_KEYS[cap], esc(cap), '对话框标题')
            cur_dlg = None
        # 控件文本: CONTROL "text", ID, ... / PUSHBUTTON "text", ID, ... 等
        m = re.match(r'\s*(CONTROL|PUSHBUTTON|LTEXT|CTEXT|EDITTEXT|COMBOBOX|LISTBOX|'
                     r'GROUPBOX|CHECKBOX|RADIOBUTTON|DEFPUSHBUTTON|SCROLLBAR|ICON)\s+'
                     r'"([^"]*)"\s*,\s*(\w+)\s*,', line)
        if m:
            text, cid = m.group(2), m.group(3)
            if re.fullmatch(r'\d+', cid):
                n = int(cid)
                cid = id2name.get(n) or NUMID_FIX.get(n) or ('IDC_' + cid)
            if text and re.search(r'[\u4e00-\u9fff]', text):
                add(cid, esc(text), '对话框控件')
            continue
        # 菜单项: MENUITEM "text", id
        m = re.match(r'\s*MENUITEM\s+"([^"]*)"\s*,\s*(\d+)', line)
        if m:
            text, mid = m.group(1), int(m.group(2))
            name = id2name.get(mid)
            if name and text:
                add(name, esc(text), '菜单')
            continue
        # 弹出菜单: POPUP "text"
        m = re.match(r'\s*POPUP\s+"([^"]*)"', line)
        if m:
            text = m.group(1)
            if text in POPUP_KEYS:
                add(POPUP_KEYS[text], esc(text), '菜单')
            continue

    # ---- main.cpp: 字面量 (相邻的 L"..." L"..." 先合并成 C++ 实际看到的那一个) ----
    cpp = open(CPP, encoding='utf-8').read()
    tok_re = re.compile(r'L"((?:[^"\\]|\\.)*)"')
    toks = [(m.start(), m.end(), m.group(1)) for m in tok_re.finditer(cpp)]
    merged = []          # (start, end, 拼接后的原文)
    cur = None
    for st, en, txt in toks:
        if cur and re.fullmatch(r'\s*', cpp[cur[1]:st]):
            cur = (cur[0], en, cur[2] + txt)
        else:
            if cur: merged.append(cur)
            cur = (st, en, txt)
    if cur: merged.append(cur)
    hit = {}
    for _, _, raw in merged:
        if not re.search(r'[\u4e00-\u9fff]', raw):
            continue
        if raw in SKIP:
            continue
        if raw not in MAP:
            hit[raw] = hit.get(raw, 0) + 1
            continue
        # ini 值 = 运行时文本 (先还原 C++ 转义, 再按 ini 约定转义)
        add(MAP[raw], esc(cpp_unesc(raw)), '运行时文本')

    # ---- main.cpp 里引用的键: 字面量已经被 apply_lang.py 换成 LS(IDS_X) 了, 抽不到值,
    #      所以这些键沿用上一版 ini 里的文本 (改文案请直接改 ini, 或改回字面量重跑)。 ----
    used = set(re.findall(r'\b(IDS_[A-Z0-9_]+)\b', cpp))
    old = {}
    if os.path.exists(OUT_INI):
        for line in open(OUT_INI, encoding='utf-8'):
            line = line.rstrip('\n')
            if not line or line[0] in ';#': continue
            eq = line.find('=')
            if eq < 0: continue
            old[line[:eq].strip()] = line[eq + 1:].strip()
    stale = []
    for k in sorted(used):
        if k in entries: continue
        if k in old:
            add(k, old[k], '运行时文本')
        else:
            stale.append(k)
    if stale:
        print('!! 代码里用了但 ini 里还没有值的键 (%d): %s' % (len(stale), ', '.join(stale)))

    # ---- 输出 ini ----
    # 前面几行是语言包元信息: lang.author 记的是本包的译者/维护者, 外部贡献者加语言包时
    # 就改这一行。这些行沿用上一版文件里手写的 (重跑脚本不该把别人的署名冲掉),
    # 文件里没有才用默认值。
    meta = [('lang.name', '简体中文'), ('lang.code', 'zh-CN'), ('lang.author', 'yjhua2005')]
    order = ['对话框标题', '菜单', '对话框控件', '运行时文本']
    lines = ['; AradeRenamer 简体中文语言包 (由 tools/extract_lang.py 生成, 勿手改——改界面请改代码后重跑)',
             '; 格式: 键=值  ; 或 # 注释  \\n \\t \\r \\0 转义  %d/%s 为运行期占位符',
             '; 新增语言包: 以 Language\\lang_en-US.ini 为模板 (英语是翻译源语言), 改掉',
             ';             lang.name / lang.code / lang.author 再逐条翻译; 本文件键最全,',
             ';             只用来查缺补漏。放进 Language\\ 目录即被识别, 不用改代码。']
    for k, dflt in meta:
        lines.append('%s = %s' % (k, old.get(k, dflt)))
    lines.append('')
    for grp in order:
        keys = [k for k, v in entries.items() if v[1] == grp]
        if not keys:
            continue
        lines.append('; ===== %s (%d) =====' % (grp, len(keys)))
        for k in sorted(keys):
            lines.append('%s = %s' % (k, entries[k][0]))
        lines.append('')

    os.makedirs(os.path.dirname(OUT_INI), exist_ok=True)
    open(OUT_INI, 'w', encoding='utf-8').write('\n'.join(lines) + '\n')

    # ---- 输出 lang_ids.h (IDS_* 自动编号枚举) + lang_keys.inc (键名 -> 数值 ID 表) ----
    # IDM_*/IDC_*/IDOK/IDCANCEL 这些在 resource.h / winuser.h 里已有数值, 直接沿用;
    # 其余 (IDS_*) 从 IDS_BASE 起按字典序自动编号 —— 只在一次编译内一致, 磁盘上的
    # 语言文件按"键名"匹配, 不受编号变化影响。
    IDS_BASE = 6000
    auto = sorted(k for k in entries
                  if k not in NUMID_FIX.values()
                  and not (re.fullmatch(r'ID[A-Z]_\w+', k) and k in id2name.values()))
    idnum = {k: IDS_BASE + i for i, k in enumerate(auto)}
    for k in entries:
        if k not in idnum:
            if k in NUMID_FIX.values():
                idnum[k] = [n for n, v in NUMID_FIX.items() if v == k][0]
            else:
                idnum[k] = [n for n, nm in id2name.items() if nm == k][0]

    hdr = ['// 由 tools/extract_lang.py 生成 —— 勿手改 (改文案请改代码后重跑脚本)',
           '#pragma once',
           '// 语言包字符串 ID。IDM_*/IDC_* 沿用 resource.h; IDS_* 由脚本自动编号。',
           'enum {']
    for k in auto:
        hdr.append('    %s = %d,' % (k, idnum[k]))
    hdr.append('};')
    hdr.append('')
    open(OUT_IDS, 'w', encoding='utf-8').write('\n'.join(hdr) + '\n')

    inc = ['// 由 tools/extract_lang.py 生成 —— 勿手改 (lang.cpp 里 #include 进数组初始化器)']
    for k in sorted(entries):
        inc.append('    { L"%s", %d },' % (k, idnum[k]))
    open(OUT_INC, 'w', encoding='utf-8').write('\n'.join(inc) + '\n')

    print('生成 %s : %d 键' % (OUT_INI, len(entries)))
    print('生成 %s : %d 个 IDS_*' % (OUT_IDS, len(auto)))
    print('生成 %s : %d 键' % (OUT_INC, len(entries)))

    # ---- 其它语言包的完整性 ----
    # zh-CN 是脚本生成的单一来源; 别的包是手写的, 少一个键运行期 LS() 就拿到空串
    # (界面上表现为那一行/那个按钮变空), 而且不报错, 很难发现。这里顺手对一遍。
    lang_dir = os.path.dirname(OUT_INI)
    for fn in sorted(os.listdir(lang_dir)):
        if not fn.endswith('.ini') or fn == os.path.basename(OUT_INI):
            continue
        have = set()
        for line in open(os.path.join(lang_dir, fn), encoding='utf-8'):
            line = line.rstrip('\n')
            if not line or line[0] in ';#':
                continue
            eq = line.find('=')
            if eq > 0:
                have.add(line[:eq].strip())
        miss = [k for k in sorted(entries) if k not in have]
        if miss:
            print('\n!! %s 缺少 %d 个键 (zh-CN 有、它没翻译): %s%s'
                  % (fn, len(miss), ', '.join(miss[:8]),
                     ' ...' if len(miss) > 8 else ''))
        # 元信息: lang.author 记译者署名, 贡献者提交新语言包时最容易漏掉。
        no_meta = [k for k, _ in meta if k not in have]
        if no_meta:
            print('\n!! %s 没有写语言包元信息: %s' % (fn, ', '.join(no_meta)))

    if hit:
        print('\n!! 以下中文字面量没有映射 (共 %d 个), 需要补进 MAP:' % len(hit))
        for lit, n in sorted(hit.items()):
            print('   [%d次] %s' % (n, lit[:80]))
        return 1
    print('全部中文字面量都已映射。')
    return 0

if __name__ == '__main__':
    sys.exit(main())
