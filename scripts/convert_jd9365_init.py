#!/usr/bin/env python3
"""Convert xiaozhi jd9365_lcd_init_cmd_t tables to LovyanGFX Panel_DSI getInitParams lists."""

import re
import sys
from pathlib import Path

CMD_RE = re.compile(
    r"\{0x([0-9A-Fa-f]+),\s*\(uint8_t\[\]\)\{(.*?)\},\s*(\d+),\s*(\d+)\}"
)


def parse_cmds(text: str) -> list[tuple[int, list[int], int]]:
    cmds = []
    for m in CMD_RE.finditer(text):
        cmd = int(m.group(1), 16)
        params_str = m.group(2).strip()
        params = []
        if params_str:
            for p in params_str.split(","):
                p = p.strip()
                if p.startswith("0x"):
                    params.append(int(p, 16))
                elif p:
                    params.append(int(p))
        delay = int(m.group(4))
        cmds.append((cmd, params, delay))
    return cmds


def split_lists(cmds: list[tuple[int, list[int], int]]) -> list[tuple[list[tuple[int, list[int]]], int]]:
    lists: list[tuple[list[tuple[int, list[int]]], int]] = []
    current: list[tuple[int, list[int]]] = []
    trailing_delay = 0
    for cmd, params, delay in cmds:
        current.append((cmd, params))
        if delay > 0:
            lists.append((current, delay))
            current = []
            trailing_delay = 0
        else:
            trailing_delay = 0
    if current:
        lists.append((current, trailing_delay))
    return lists


def emit_header(lists) -> str:
    lines = [
        "#pragma once",
        "",
        "#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>",
        "#if SOC_MIPI_DSI_SUPPORTED",
        "",
        "namespace lgfx {",
        "inline namespace v1 {",
        "",
        "/** JD9365 init for Waveshare ESP32-P4-WIFI6-Touch-LCD-4C (720x720). */",
        "struct Panel_JD9365_Waveshare4C : public Panel_DSI {",
        " protected:",
        "  const uint8_t* getInitParams(size_t listno) const override {",
    ]
    for i, (cmds, _) in enumerate(lists):
        lines.append(f"    static constexpr uint8_t list{i}[] = {{")
        lines.append("      // len(cmd+params), cmd, params...")
        for cmd, params in cmds:
            total_len = 1 + len(params)
            byte_str = ", ".join(f"0x{b:02X}" for b in [total_len, cmd] + params)
            lines.append(f"      {byte_str},")
        lines.append("      0,  // end")
        lines.append("    };")
        lines.append("")
    lines.append("    switch (listno) {")
    for i in range(len(lists)):
        lines.append(f"      case {i}: return list{i};")
    lines.append("      default: return nullptr;")
    lines.append("    }")
    lines.append("  }")
    lines.append("")
    lines.append("  size_t getInitDelay(size_t listno) const override {")
    lines.append("    switch (listno) {")
    for i, (_, delay) in enumerate(lists):
        if delay:
            lines.append(f"      case {i}: return {delay};")
    lines.append("      default: return 0;")
    lines.append("    }")
    lines.append("  }")
    lines.append("};")
    lines.append("")
    lines.append("}  // namespace v1")
    lines.append("}  // namespace lgfx")
    lines.append("")
    lines.append("#endif")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("/tmp/jd9365_4c_init.txt")
    dst = (
        Path(sys.argv[2])
        if len(sys.argv) > 2
        else Path(__file__).resolve().parent.parent
        / "include/hardware/panel_jd9365_waveshare_4c.hpp"
    )
    text = src.read_text()
    cmds = parse_cmds(text)
    if not cmds:
        print("No commands parsed", file=sys.stderr)
        return 1
    lists = split_lists(cmds)
    dst.write_text(emit_header(lists))
    print(f"Wrote {len(cmds)} cmds in {len(lists)} lists -> {dst}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
