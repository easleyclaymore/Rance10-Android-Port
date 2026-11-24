#!/bin/bash
# [D517k] 把 3D 预览的 GLSL 编译成 SPIR-V 并生成 C 头文件。
# 生成物：src/preview3d_vk_spv.h（进版本控制，运行期不依赖 shader 文件）
# 用法：tools/gen-spv.sh
set -euo pipefail
cd "$(dirname "$0")/.."

GLSLC="${GLSLC:-glslc}"
for t in vert frag; do
	if ! command -v "$GLSLC" >/dev/null; then
		echo "缺少 $GLSLC（glslang/shaderc 包），无法编译 SPIR-V" >&2
		exit 1
	fi
done

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
"$GLSLC" -fshader-stage=vert shaders/preview3d_vk.vert -o "$TMP/v.spv"
"$GLSLC" -fshader-stage=frag shaders/preview3d_vk.frag -o "$TMP/f.spv"

python3 - "$TMP/v.spv" "$TMP/f.spv" > src/preview3d_vk_spv.h << 'PY'
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

print('/* 自动生成，勿手改 —— 由 tools/gen-spv.sh 从 shaders/preview3d_vk.{vert,frag} 生成 */')
print('#ifndef SYSTEM4_PREVIEW3D_VK_SPV_H')
print('#define SYSTEM4_PREVIEW3D_VK_SPV_H')
print()
print(emit('preview3d_vk_vert_spv', sys.argv[1]))
print()
print(emit('preview3d_vk_frag_spv', sys.argv[2]))
print()
print('#endif')
PY

echo "生成 src/preview3d_vk_spv.h（$(wc -c < src/preview3d_vk_spv.h) 字节）"
