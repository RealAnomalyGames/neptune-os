#!/usr/bin/env python3

from pathlib import Path
import sys

root = Path(__file__).resolve().parent.parent
build = root / "build"
audio_dir = root / "src" / "assets" / "audio"

build.mkdir(parents=True, exist_ok=True)

assets = [
    ("startup", audio_dir / "startup.wav"),
    ("poweroff", audio_dir / "poweroff.wav"),
]

header = build / "audio_assets.h"
source = build / "audio_assets.c"

header.write_text(
    "#ifndef NEPTUNE_AUDIO_ASSETS_H\n"
    "#define NEPTUNE_AUDIO_ASSETS_H\n"
    "#include <stdint.h>\n"
    "extern const uint8_t startup_wav[];\n"
    "extern const uint32_t startup_wav_size;\n"
    "extern const uint8_t poweroff_wav[];\n"
    "extern const uint32_t poweroff_wav_size;\n"
    "#endif\n",
    encoding="utf-8",
)

lines = [
    '#include "audio_assets.h"',
    "",
]

for name, path in assets:
    if path.is_file():
        data = path.read_bytes()
        print(f"Embedding {path.relative_to(root)} ({len(data)} bytes)")
    else:
        data = b""
        print(f"WARNING: {path.relative_to(root)} is missing; sound disabled.")

    # Empty C arrays are not standard C, so use one dummy byte and size 0.
    emitted = data if data else b"\x00"

    lines.append(f"const uint8_t {name}_wav[] = {{")
    for start in range(0, len(emitted), 16):
        chunk = emitted[start:start + 16]
        lines.append("    " + ", ".join(f"0x{byte:02x}" for byte in chunk) + ",")
    lines.append("};")
    lines.append(f"const uint32_t {name}_wav_size = {len(data)}u;")
    lines.append("")

source.write_text("\n".join(lines) + "\n", encoding="utf-8")
