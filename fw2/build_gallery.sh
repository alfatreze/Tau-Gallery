#!/usr/bin/env bash
# Tau Gallery M0 test firmware (fw2/gallery): the viewer, mode switch, landing-read and interrupt test page. A small hot-RAM program: no audio, no cold image, no assets.
# Output: work/diagnostics/gallery/tau.rom and tau-cold.bin (the cold file is the test payload that data slot 6 reads: a copy of the shipped tau-cold.bin, 147 KB, at least 128 KB for the landing reads).
# Usage: bash fw2/build_gallery.sh        (needs the bitstream of tools/gallery_m0b_qsf_append.txt: CORE_VERSION 0x4D50331B, viewer, landing path and interrupt block)
set -euo pipefail
FW2="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"; ROOT="$(cd -- "$FW2/.." && pwd)"
BIN="${RISCV_TOOLCHAIN_BIN:-$(python3 "$ROOT/tools/toolchain.py" bin)}" || exit 1
GCC="${CC_RISCV:-$BIN/riscv-none-elf-gcc}"; OBJCOPY="${OBJCOPY_RISCV:-$BIN/riscv-none-elf-objcopy}"; SIZE="${SIZE_RISCV:-$BIN/riscv-none-elf-size}"; NM="$BIN/riscv-none-elf-nm"
OUT="${TAU_BUILD_OUT:-$ROOT/work/diagnostics}/gallery"; mkdir -p "$OUT"
rm -f "$OUT/fw2.elf" "$OUT/tau.rom"
SRCS=("$FW2/platform/start.S" "$FW2/platform/platform.c" "$FW2/platform/libc_min.c" "$FW2/gfx/gfx.c" "$FW2/stream/slot.c"
      "$FW2/gallery/gallery_pattern.c" "$FW2/gallery/gallery_hw.c" "$FW2/gallery/gallery_main.c")
DEFS=(-DP2_DIAG=0 -DP2_GALLERY=1)
CFLAGS=(-march=rv32im -mabi=ilp32 -mno-relax -O2 -ffreestanding -nostartfiles -nostdlib -ffunction-sections -fdata-sections -Wall -Wextra -Wno-unused-parameter)
OBJ="$OUT/obj"; rm -rf "$OBJ"; mkdir -p "$OBJ"; : > "$OUT/build.log"; OBJS=()
for src in "${SRCS[@]}"; do
    o="$OBJ/$(basename "${src%.*}").o"
    "$GCC" "${DEFS[@]}" "${CFLAGS[@]}" -c "$src" -o "$o" 2>> "$OUT/build.log" || { grep -v "LOAD segment with RWX" "$OUT/build.log" >&2; echo "*** Gallery COMPILE FAILED ***" >&2; exit 1; }
    OBJS+=("$o")
done
"$GCC" "${CFLAGS[@]}" -Wl,--gc-sections -T "$FW2/platform/link.ld" -o "$OUT/fw2.elf" "${OBJS[@]}" -lgcc 2>> "$OUT/build.log" || { grep -v "LOAD segment with RWX" "$OUT/build.log" >&2; echo "*** Gallery LINK FAILED ***" >&2; exit 1; }
grep -v "LOAD segment with RWX" "$OUT/build.log" >&2 || true
"$SIZE" "$OUT/fw2.elf"
[ "$("$NM" "$OUT/fw2.elf" | awk '$3 == "_start" { print $1 }')" = "00000000" ] || { echo "*** _start is not at address 0 ***" >&2; exit 1; }
"$OBJCOPY" -O binary -R .cold_data -R .cold_text "$OUT/fw2.elf" "$OUT/tau.rom"; chmod 0644 "$OUT/tau.rom"
cp "$ROOT/dist/Assets/tau_gallery/alfatreze.Tau Gallery/tau-cold.bin" "$OUT/tau-cold.bin" 2>/dev/null || cp "$ROOT/dist/Assets/tau_gallery/common/tau-cold.bin" "$OUT/tau-cold.bin"
echo "built [gallery] -> $OUT/tau.rom ($(wc -c < "$OUT/tau.rom") bytes)"
