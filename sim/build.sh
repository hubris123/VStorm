#!/usr/bin/env bash
# Build the VStorm virtual screen (WebAssembly) from the real firmware sources.
# Needs only clang + wasm-ld (LLVM >= 15) and python3. No Emscripten / ESP-IDF.
#   ./build.sh            -> web/vstorm.wasm
set -euo pipefail
cd "$(dirname "$0")"
SIM=$PWD
FW=$SIM/../firmware/main
DEPS=$SIM/.deps
OUT=$SIM/build
mkdir -p "$DEPS" "$OUT"

LVGL_TAG=v8.4.0   # matches firmware/dependencies.lock
if [ ! -d "$DEPS/lvgl" ]; then
  git clone --depth 1 -b "$LVGL_TAG" https://github.com/lvgl/lvgl.git "$DEPS/lvgl"
fi

# fonts the firmware embeds via EMBED_FILES -> C arrays
python3 - "$FW" "$OUT/sim_assets.c" <<'PY'
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
        -DUI_SIM -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE
        -isystem "$SIM/libc/include" -I "$SIM" -I "$SIM/include" -I "$FW" -I "$DEPS/lvgl"
        -Wno-incompatible-library-redeclaration -Wno-unknown-attributes)

SRCS=("$FW/ui.c" "$FW/dsp.c" "$SIM/src/sim_main.c" "$SIM/src/sim_hal.c" "$SIM/libc/libc.c" "$OUT/sim_assets.c")
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
  -o "$SIM/web/vstorm.wasm" "${OBJS[@]}"
echo "built $SIM/web/vstorm.wasm ($(stat -c %s "$SIM/web/vstorm.wasm") bytes)"
