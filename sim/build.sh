#!/usr/bin/env bash
# Build the VStorm virtual screen (WebAssembly) from the real firmware sources.
# Needs only clang + wasm-ld (LLVM >= 15) and python3. No Emscripten / ESP-IDF.
#   ./build.sh          -> web/vstorm-fw3.wasm  (AMOLED-1.8, firmware3/ - the active board)
#   ./build.sh fw1      -> web/vstorm-fw1.wasm  (original LCD-1.47, firmware/)
#   ./build.sh fw3i     -> web/vstorm-fw3i.wasm (new Imperial UI for the AMOLED-1.8, sim/ui_imperial/)
#   ./build.sh all
set -euo pipefail
cd "$(dirname "$0")"
TARGET=${1:-fw3}
if [ "$TARGET" = all ]; then "$0" fw3i; "$0" fw3; exec "$0" fw1; fi
SIM=$PWD
FW1=$SIM/../firmware/main
DEPS=$SIM/.deps
OUT=$SIM/build/$TARGET
case "$TARGET" in
  fw1) FW=$FW1;               DEFS=(-DSIM_DEFAULT_W=320 -DSIM_DEFAULT_H=172 -DSIM_LV_MEM_KB=64);;
  # firmware3 uses LV_MEM_CUSTOM (system heap), so give LVGL a roomy pool
  fw3) FW=$SIM/../firmware3/main; DEFS=(-DSIM_DEFAULT_W=368 -DSIM_DEFAULT_H=448 -DSIM_LV_MEM_KB=256);;
  fw3i) FW=$SIM/ui_imperial;      DEFS=(-DSIM_DEFAULT_W=368 -DSIM_DEFAULT_H=448 -DSIM_LV_MEM_KB=512);;
  *) echo "unknown target $TARGET (fw1|fw3|all)"; exit 1;;
esac
mkdir -p "$DEPS" "$OUT"

LVGL_TAG=v8.4.0   # matches firmware/dependencies.lock
if [ ! -d "$DEPS/lvgl" ]; then
  git clone --depth 1 -b "$LVGL_TAG" https://github.com/lvgl/lvgl.git "$DEPS/lvgl"
fi

# fonts the firmware embeds via EMBED_FILES -> C arrays
# + for fw3i: the default skin PNGs -> RGB565 (little-endian, LV_COLOR_16_SWAP=0)
SKINS=""
[ "$TARGET" = fw3i ] && SKINS="$SIM/../design/skins/default"
python3 - "$FW1" "$OUT/sim_assets.c" "$SKINS" <<'PY'
import os, struct, sys, zlib
fw, out, skins = sys.argv[1], sys.argv[2], sys.argv[3]

def png_rgb(path):
    """Minimal PNG reader (8-bit RGB/RGBA, non-interlaced) - no Pillow needed."""
    data = open(path, "rb").read()
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if typ == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and ctype in (2, 6), f"{path}: need 8-bit RGB/RGBA"
            bpp = 3 if ctype == 2 else 4
        elif typ == b"IDAT":
            idat += body
        pos += 12 + n
    raw, stride = zlib.decompress(idat), w * bpp
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(h):
        ft, line = raw[i], bytearray(raw[i + 1:i + 1 + stride]); i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b, c = prev[x], (prev[x - bpp] if x >= bpp else 0)
            if ft == 1: line[x] = (line[x] + a) & 255
            elif ft == 2: line[x] = (line[x] + b) & 255
            elif ft == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif ft == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line); prev = line
    return w, h, bpp, rows

def blob(f, sym, data):
    f.write(f"const unsigned {sym}_len = {len(data)};\n")
    f.write(f"const uint8_t {sym}[] = {{" + ",".join(map(str, data)) + "};\n")

with open(out, "w") as f:
    f.write("#include <stdint.h>\n")
    for sym, fn in (("aurebesh_ttf", "aurebesh.ttf"), ("aureng_ttf", "aurebesh_english.ttf")):
        data = open(f"{fw}/{fn}", "rb").read()
        f.write(f"const unsigned {sym}_len = {len(data)};\n")
        f.write(f"const uint8_t {sym}_start[] = {{")
        f.write(",".join(str(b) for b in data))
        f.write("};\n")
    if skins:
        for name in ("main", "lock", "settings_value", "settings_wide", "callsign"):
            w, h, bpp, rows = png_rgb(os.path.join(skins, name + ".png"))
            assert (w, h) == (368, 448), name
            px = bytearray()
            for row in rows:
                for x in range(0, w * bpp, bpp):
                    r, g, b = row[x], row[x + 1], row[x + 2]
                    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
                    px += bytes((v & 255, v >> 8))
            blob(f, f"skin_{name}_px", px)
PY

CC=${CC:-clang}
CFLAGS=(--target=wasm32 -O2 -ffreestanding -nostdlib -fno-builtin -mbulk-memory
        -DUI_SIM "${DEFS[@]}" -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE
        -isystem "$SIM/libc/include" -I "$SIM" -I "$SIM/include" -I "$FW" -I "$SIM/../firmware3/main" -I "$FW1" -I "$DEPS/lvgl"
        -Wno-incompatible-library-redeclaration -Wno-unknown-attributes)

# the voice DSP is firmware/main/dsp.c for both targets (firmware3/audio.c carries the same math inline)
if [ "$TARGET" = fw3i ]; then
  SRCS=("$FW/ui_imperial.c" "$FW1/dsp.c" "$SIM/src/sim_main.c" "$SIM/src/sim_hal_fw3.c" "$SIM/src/sim_hal_fw3i.c" "$SIM/libc/libc.c" "$OUT/sim_assets.c")
else
  SRCS=("$FW/ui.c" "$FW1/dsp.c" "$SIM/src/sim_main.c" "$SIM/src/sim_hal_$TARGET.c" "$SIM/libc/libc.c" "$OUT/sim_assets.c")
fi
mapfile -t LV < <(find "$DEPS/lvgl/src" -name '*.c' -not -path '*/draw/nxp/*' -not -path '*/draw/renesas/*' \
                  -not -path '*/draw/stm32_dma2d/*' -not -path '*/draw/swm341_dma2d/*' -not -path '*/draw/arm2d/*' \
                  -not -path '*/draw/sdl/*' -not -path '*/gpu/*')

OBJS=()
for s in "${SRCS[@]}" "${LV[@]}"; do
  o="$OUT/obj/$(echo "${s#"$SIM"/}" | sed 's|\.\./||g; s|/|_|g').o"
  mkdir -p "$OUT/obj"
  if [ ! -f "$o" ] || [ "$s" -nt "$o" ] || [ "$SIM/lv_conf.h" -nt "$o" ]; then
    "$CC" "${CFLAGS[@]}" -c "$s" -o "$o" &
    while [ "$(jobs -r | wc -l)" -ge "$(nproc)" ]; do wait -n; done
  fi
  OBJS+=("$o")
done
wait

wasm-ld --no-entry --export-dynamic --allow-undefined --gc-sections \
  --initial-memory=8388608 -z stack-size=262144 \
  -o "$SIM/web/vstorm-$TARGET.wasm" "${OBJS[@]}"
echo "built $SIM/web/vstorm-$TARGET.wasm ($(stat -c %s "$SIM/web/vstorm-$TARGET.wasm") bytes)"
