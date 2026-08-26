#!/usr/bin/env python3
"""SlowTree 预设转换器: .vtree (VEGTOOL 文本) <-> SlowTreePresets.cpp 内嵌字符串。

设计流程: 同事在 SlowTree 应用里设计模板 -> 保存 .vtree -> 本工具 wrap 成 C++ 片段
-> 手工贴入 Source/TreeGen/SlowTree/SlowTreePresets.cpp(或替换既有模板)。

用法:
  python Tools/convert_vtree_preset.py wrap   <name> <input.vtree> [desc]
  python Tools/convert_vtree_preset.py extract <SlowTreePresets.cpp> <out_dir>

wrap:   读入 .vtree 文本, 输出 C++ raw-string 片段(static const char* k<Name>Vtree)。
        片段中 VEGTOOL 行原样保留(与 VtreeIO 解析器同构, 无任何改写)。
extract: 解析 SlowTreePresets.cpp 中全部 k*Vtree 内嵌字符串, 各写一个 <name>.vtree
        (供对比/回归/转存, 也作为模板设计的备份)。

依赖: 仅标准库。Python 3.8+。
"""
import re
import sys
from pathlib import Path


def usage() -> str:
    return __doc__


def to_identifier(name: str) -> str:
    """'weeping willow' -> 'WeepingWillow'"""
    parts = re.split(r"[^0-9A-Za-z]+", name.strip())
    return "".join(p.capitalize() for p in parts if p) or "Preset"


def cmd_wrap(name: str, vtree_path: str, desc: str) -> str:
    text = Path(vtree_path).read_text(encoding="utf-8", errors="replace")
    # 剥离 BOM/行尾空白, 保留行结构; 首行必须 VEGTOOL(与 VtreeIO::load 一致)
    lines = [line.rstrip("\r\n") for line in text.splitlines()]
    first = next((line for line in lines if line.strip()), "")
    if not first.startswith("VEGTOOL"):
        print(f"warning: {vtree_path} 首行非 VEGTOOL, VtreeIO::load 会拒绝", file=sys.stderr)
    ident = to_identifier(name)
    if desc:
        print(f"// {name}: {desc}")
    print(f'static const char* k{ident}Vtree = R"VT({chr(10).join(lines)})VT";')
    return ""


def cmd_extract(cpp_path: str, out_dir: str) -> str:
    text = Path(cpp_path).read_text(encoding="utf-8")
    # 只匹配预设区(注释 // name: desc 行 + k*Vtree 定义); 预设 0 的 defaultTemplateText
    # 来自 VtreeIO, 不在此文件, 提取不到属正常。
    pattern = re.compile(
        r"//\s*(?P<name>[^\n:]+)(?::\s*(?P<desc>[^\n]*))?\n"
        r'static const char\*\s*k(?P<ident>\w+)Vtree\s*=\s*R"VT\((?P<body>.*?)\)VT";',
        re.DOTALL,
    )
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    for m in pattern.finditer(text):
        fname = re.sub(r"[^0-9A-Za-z_\-]+", "_", m.group("name").strip().lower()) or m.group("ident")
        target = out / f"{fname}.vtree"
        target.write_text(m.group("body"), encoding="utf-8")
        print(f"wrote {target}")
    return ""


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(usage(), file=sys.stderr)
        return 2
    mode, rest = argv[1], argv[2:]
    if mode == "wrap" and len(rest) >= 2:
        return cmd_wrap(rest[0], rest[1], rest[2] if len(rest) > 2 else "")
    if mode == "extract" and len(rest) == 2:
        return cmd_extract(rest[0], rest[1])
    print(usage(), file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
