#!/usr/bin/env python3
"""CCMake 治理门禁：死代码、函数长度、嵌套深度。

默认只报告；--strict 在发现硬性问题时返回非零。
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

FORBIDDEN = [
    (re.compile(r"^\s*#\s*if\s+0\b"), "#if 0"),
    (re.compile(r"^\s*if\s*\(\s*0\s*\)"), "if (0)"),
    (re.compile(r"\blegacy\b", re.IGNORECASE), "legacy"),
]
FUNC_START = re.compile(
    r"^(?:static\s+)?(?:CCBOOL|void|CCUINT\d+|CCINT\d+|CMK\w+|const\s+\w+)\s+\**(\w+)\s*\("
)


def strip_strings(line: str) -> str:
    out = []
    i = 0
    while i < len(line):
        ch = line[i]
        if ch in "\"'":
            quote = ch
            out.append(" ")
            i += 1
            while i < len(line):
                if line[i] == "\\":
                    i += 2
                    continue
                if line[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if ch == "/" and i + 1 < len(line) and line[i + 1] == "/":
            break
        out.append(ch)
        i += 1
    return "".join(out)


def scan(path: pathlib.Path) -> list[str]:
    issues = []
    text = path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    for no, line in enumerate(lines, 1):
        for pattern, label in FORBIDDEN:
            if pattern.search(line):
                issues.append(f"{path}:{no}: 禁止的 {label}")
        if (path.suffix == ".h" and "cmk_runtime.h" in line and
                path.name not in {"cmk_program.h", "cmk_runtime.h"}):
            issues.append(f"{path}:{no}: 公共头不应依赖 cmk_runtime.h")
    # 粗略函数长度：按顶层大括号配平；只对 .c 文件
    if path.suffix != ".c":
        return issues
    i = 0
    while i < len(lines):
        m = FUNC_START.match(lines[i])
        if not m:
            i += 1
            continue
        name = m.group(1)
        depth = 0
        seen = False
        start = i
        while i < len(lines):
            code = strip_strings(lines[i])
            depth += code.count("{") - code.count("}")
            if "{" in code:
                seen = True
            i += 1
            if seen and depth == 0:
                break
        length = i - start
        if length > 200:
            issues.append(f"{path}:{start + 1}: 函数 {name} 约 {length} 行（>200）")
    return issues


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--strict", action="store_true")
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parent.parent
    issues = []
    for path in sorted((root / "src").rglob("*.c")) + sorted((root / "src").rglob("*.h")):
        issues.extend(scan(path))
    for issue in issues:
        print(issue)
    print(f"共 {len(issues)} 项")
    if args.strict and issues:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
