#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 src/main.cpp 里剩下的中文 L"..." 字面量替换成 LS(IDS_xxx)。

配合 tools/extract_lang.py 用: 先在 extract_lang.py 的 MAP 里给新字面量分配键名,
再跑本脚本 (它会顺带报告还没分配键的中文串)。

用法: python3 tools/apply_lang.py [--dry]
  --dry  只报告要改多少处, 不写文件
"""
import re, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_lang import MAP, SKIP, CPP, OUT_INI

HAN = re.compile(r'[\u4e00-\u9fff]')


def main():
    dry = '--dry' in sys.argv
    src = open(CPP, encoding='utf-8').read()

    # 相邻的字面量 (中间只有空白) 是 C++ 的字符串拼接, 必须整块一起换。
    tok_re = re.compile(r'L"((?:[^"\\]|\\.)*)"')
    groups = []
    cur = None
    for m in tok_re.finditer(src):
        st, en, txt = m.start(), m.end(), m.group(1)
        if cur and re.fullmatch(r'\s*', src[cur['end']:st]):
            cur['toks'].append((st, en, txt)); cur['end'] = en
        else:
            if cur:
                groups.append(cur)
            cur = {'start': st, 'end': en, 'toks': [(st, en, txt)]}
    if cur:
        groups.append(cur)

    edits = []          # (start, end, 替换文本)
    used = set()        # 本次替换要用到的键
    missing = {}        # 没分到键的中文串
    for g in groups:
        if not any(HAN.search(t[2]) for t in g['toks']):
            continue
        merged = ''.join(t[2] for t in g['toks'])
        if merged in SKIP:
            continue
        if merged in MAP:
            edits.append((g['start'], g['end'], 'LS(%s)' % MAP[merged]))
            used.add(MAP[merged])
            continue
        if len(g['toks']) > 1:
            missing[merged] = missing.get(merged, 0) + 1
            continue
        raw = g['toks'][0][2]
        if raw in SKIP:
            continue
        if raw in MAP:
            edits.append((g['start'], g['end'], 'LS(%s)' % MAP[raw]))
            used.add(MAP[raw])
        else:
            missing[raw] = missing.get(raw, 0) + 1

    print('替换 %d 处' % len(edits))
    if missing:
        print('\n!! 以下中文字面量还没有键 (共 %d), 先补进 extract_lang.py 的 MAP:' % len(missing))
        for lit, n in sorted(missing.items()):
            print('   [%d次] %s' % (n, lit[:90]))

    # 没先跑 extract_lang.py 就直接 apply 的话: 字面量被换成 LS(IDS_X) 了, 而 ini 里
    # 还没有 IDS_X 这一条 —— 运行期拿到的是空串, 界面上就是一片空白, 且不报错。
    # 这里先挡一道, 逼出正确顺序 (先 extract 再 apply)。
    have = set()
    if os.path.exists(OUT_INI):
        for line in open(OUT_INI, encoding='utf-8'):
            line = line.rstrip('\n')
            if not line or line[0] in ';#':
                continue
            eq = line.find('=')
            if eq > 0:
                have.add(line[:eq].strip())
    no_val = sorted(k for k in used if k not in have)
    if no_val:
        print('\n!! 这些键在 %s 里还没有值, 先跑 tools/extract_lang.py 再跑本脚本: %s'
              % (os.path.basename(OUT_INI), ', '.join(no_val)))
        return 1

    if dry or missing:
        return 1 if missing else 0

    out = []
    last = 0
    for st, en, rep in edits:
        out.append(src[last:st]); out.append(rep); last = en
    out.append(src[last:])
    open(CPP, 'w', encoding='utf-8').write(''.join(out))
    print('已写入 %s' % CPP)
    return 0


if __name__ == '__main__':
    sys.exit(main())
