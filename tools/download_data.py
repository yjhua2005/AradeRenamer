#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
download_data.py — 下载 AradeRenamer 所需的 OpenCC 和拼音字典文件。
"""

import os
import sys
import urllib.request
import urllib.error
from pathlib import Path

# 项目根目录 (假设 tools 目录的上一级就是项目根目录)
PROJECT_ROOT = Path(__file__).resolve().parent.parent

# 文件下载配置: 源 URL -> 本地目标路径
DOWNLOAD_ITEMS = [
    (
        "https://raw.githubusercontent.com/BYVoid/OpenCC/master/data/dictionary/STCharacters.txt",
        PROJECT_ROOT / "data" / "dictionary" / "STCharacters.txt"
    ),
    (
        "https://raw.githubusercontent.com/BYVoid/OpenCC/master/data/dictionary/STPhrases.txt",
        PROJECT_ROOT / "data" / "dictionary" / "STPhrases.txt"
    ),
    (
        "https://raw.githubusercontent.com/BYVoid/OpenCC/master/data/dictionary/TSCharacters.txt",
        PROJECT_ROOT / "data" / "dictionary" / "TSCharacters.txt"
    ),
    (
        "https://raw.githubusercontent.com/BYVoid/OpenCC/master/data/dictionary/TSPhrases.txt",
        PROJECT_ROOT / "data" / "dictionary" / "TSPhrases.txt"
    ),
    (
        "https://raw.githubusercontent.com/mozillazg/pinyin-data/master/pinyin.txt",
        PROJECT_ROOT / "data" / "pinyin.txt"
    ),
]

# 用户代理，避免被 GitHub 拒绝
USER_AGENT = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"


def download_file(url: str, dest_path: Path) -> bool:
    """下载单个文件，带简单的进度显示。"""
    print(f"\n[下载] {url}")
    print(f"   -> 保存至: {dest_path}")

    # 确保目标目录存在
    dest_path.parent.mkdir(parents=True, exist_ok=True)

    # 如果文件已存在，跳过下载（可选：如果你想强制覆盖，可以注释掉这几行）
    #if dest_path.exists():
    #   print(f"   [跳过] 文件已存在，跳过下载。 (大小: {dest_path.stat().st_size / 1024:.1f} KB)")
    #    return True

    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})

    try:
        with urllib.request.urlopen(req, timeout=60) as response:
            total_size = int(response.headers.get("Content-Length", 0))
            downloaded = 0
            chunk_size = 8192

            with open(dest_path, "wb") as f:
                while True:
                    chunk = response.read(chunk_size)
                    if not chunk:
                        break
                    f.write(chunk)
                    downloaded += len(chunk)
                    if total_size:
                        percent = downloaded * 100 // total_size
                        sys.stdout.write(f"\r   进度: {percent}% ({downloaded / 1024:.1f} / {total_size / 1024:.1f} KB)")
                        sys.stdout.flush()

        print(f"\n   [完成] 下载成功! (大小: {dest_path.stat().st_size / 1024:.1f} KB)")
        return True

    except urllib.error.HTTPError as e:
        print(f"\n   [错误] HTTP {e.code}: {url}")
        return False
    except urllib.error.URLError as e:
        print(f"\n   [错误] 网络连接失败: {e.reason}")
        return False
    except Exception as e:
        print(f"\n   [错误] 发生未知异常: {e}")
        return False


def main():
    print("=" * 60)
    print("  AradeRenamer 数据字典下载工具")
    print("=" * 60)

    success_count = 0
    for url, dest in DOWNLOAD_ITEMS:
        if download_file(url, dest):
            success_count += 1

    print("\n" + "=" * 60)
    if success_count == len(DOWNLOAD_ITEMS):
        print(f"  [全部完成] 成功处理 {success_count} 个文件。")
        return 0
    else:
        print(f"  [部分失败] 成功 {success_count} / {len(DOWNLOAD_ITEMS)} 个文件。请检查网络后重试。")
        return 1


if __name__ == "__main__":
    sys.exit(main())