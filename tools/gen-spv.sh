#!/bin/bash
# [D517k] 把 GLSL 编译成 SPIR-V 并生成 C 头文件（运行期不依赖 shader 文件）。
# [D521b] 通用化：每组 <name> <vert> <frag> 生成 src/<name>_spv.h。
# 用法：tools/gen-spv.sh
set -euo pipefail
cd "$(dirname "$0")/.."

GLSLC="${GLSLC:-glslc}"
if ! command -v "$GLSLC" >/dev/null; then
	echo "缺少 $GLSLC（glslang/shaderc 包），无法编译 SPIR-V" >&2
	exit 1
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

gen() {
	local name="$1" vert="$2" frag="$3"
	local guard=$(echo "$name" | tr 'a-z' 'A-Z')
	"$GLSLC" -fshader-stage=vert "shaders/$vert" -o "$TMP/v.spv"
	"$GLSLC" -fshader-stage=frag "shaders/$frag" -o "$TMP/f.spv"
	python3 - "$TMP/v.spv" "$TMP/f.spv" "$name" "$guard" > "src/${name}_spv.h" << 'PY'
import sys

def emit(name, path):
    data = open(path, 'rb').read()
    lines = ['static const unsigned char %s[] = {' % name]
    for i in range(0, len(data), 12):
        chunk = ', '.join('0x%02x' % b for b in data[i:i+12])
        lines.append('\t' + chunk + ',')
    lines.append('};')
    lines.append('static const unsigned long %s_size = %d;' % (name, len(data)))
    return '\n'.join(lines)

vpath, fpath, name, guard = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
print('/* 自动生成，勿手改 —— 由 tools/gen-spv.sh 从 shaders/%s.{vert,frag} 生成 */' % name)
print('#ifndef SYSTEM4_%s_SPV_H' % guard)
print('#define SYSTEM4_%s_SPV_H' % guard)
print()
print(emit(name + '_vert_spv', vpath))
print()
print(emit(name + '_frag_spv', fpath))
print()
print('#endif')
PY
	echo "生成 src/${name}_spv.h（$(wc -c < src/${name}_spv.h) 字节）"
}

gen preview3d_vk preview3d_vk.vert preview3d_vk.frag
gen blit2d_vk blit2d_vk.vert blit2d_vk.frag
