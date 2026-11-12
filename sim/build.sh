#!/usr/bin/env bash
# Build the VStorm virtual screen (WebAssembly) from the real firmware sources.
# Needs only clang + wasm-ld (LLVM >= 15) and python3. No Emscripten / ESP-IDF.
#   ./build.sh          -> web/vstorm-fw3.wasm  (AMOLED-1.8, firmware3/ - the active board)
#   ./build.sh fw1      -> web/vstorm-fw1.wasm  (original LCD-1.47, firmware/)
#   ./build.sh all
set -euo pipefail
cd "$(dirname "$0")"
TARGET=${1:-fw3}
if [ "$TARGET" = all ]; then "$0" fw3; exec "$0" fw1; fi
SIM=$PWD
FW1=$SIM/../firmware/main
DEPS=$SIM/.deps
OUT=$SIM/build/$TARGET
case "$TARGET" in
  fw1) FW=$FW1;               DEFS=(-DSIM_DEFAULT_W=320 -DSIM_DEFAULT_H=172 -DSIM_LV_MEM_KB=64);;
  # firmware3 uses LV_MEM_CUSTOM (system heap), so give LVGL a roomy pool
  fw3) FW=$SIM/../firmware3/main; DEFS=(-DSIM_DEFAULT_W=368 -DSIM_DEFAULT_H=448 -DSIM_LV_MEM_KB=256);;
  *) echo "unknown target $TARGET (fw1|fw3|all)"; exit 1;;
esac
mkdir -p "$DEPS" "$OUT"

LVGL_TAG=v8.4.0   # matches firmware/dependencies.lock
if [ ! -d "$DEPS/lvgl" ]; then
  git clone --depth 1 -b "$LVGL_TAG" https://github.com/lvgl/lvgl.git "$DEPS/lvgl"
fi

# fonts the firmware embeds via EMBED_FILES -> C arrays
python3 - "$FW1" "$OUT/sim_assets.c" <<'PY'
import sys
fw, out = sys.argv[1], sys.argv[2]
with open(out, "w") as f:
    f.write("#include <stdint.h>\n")
    for sym, fn in (("aurebesh_ttf", "aurebesh.ttf"), ("aureng_ttf", "aurebesh_english.ttf")):
        data = open(f"{fw}/{fn}", "rb").read()
        f.write(f"const unsigned {sym}_len = {len(data)};\n")
        f.write(f"const uint8_t {sym}_start[] = {{")
        f.write(",".join(str(b) for b in data))
        f.write("};\n")
PY

CC=${CC:-clang}
CFLAGS=(--target=wasm32 -O2 -ffreestanding -nostdlib -fno-builtin -mbulk-memory
        -DUI_SIM "${DEFS[@]}" -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE
        -isystem "$SIM/libc/include" -I "$SIM" -I "$SIM/include" -I "$FW" -I "$FW1" -I "$DEPS/lvgl"
        -Wno-incompatible-library-redeclaration -Wno-unknown-attributes)

# the voice DSP is firmware/main/dsp.c for both targets (firmware3/audio.c carries the same math inline)
SRCS=("$FW/ui.c" "$FW1/dsp.c" "$SIM/src/sim_main.c" "$SIM/src/sim_hal_$TARGET.c" "$SIM/libc/libc.c" "$OUT/sim_assets.c")
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
