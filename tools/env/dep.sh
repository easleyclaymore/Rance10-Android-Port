#!/bin/bash
# [D170] 路径锚点：向上找到 tools/lib/common.sh。
_d="$(dirname "$(realpath "$0")")"; while [ ! -f "$_d/lib/common.sh" ] && [ "$_d" != "/" ]; do _d="$(dirname "$_d")"; done; . "$_d/lib/common.sh"
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
# [D169] 别硬编码 aarch64：换个架构这里就指向不存在的目录。
MULTIARCH="$(uname -m)-linux-gnu"
export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:/usr/lib/${MULTIARCH}/pkgconfig:/usr/share/pkgconfig"

echo "=== 依赖检查 ==="
for p in sdl2 freetype2 libpng libwebp libjpeg libsndfile libffi zlib glew cglm; do
  printf "%-12s " "$p"
  if pkg-config --exists "$p" 2>/dev/null; then
    echo "OK $(pkg-config --modversion $p 2>/dev/null)"
  else
    echo "MISSING"
  fi
done

echo "=== 工具链 ==="
gcc --version 2>/dev/null | head -1
bison --version 2>/dev/null | head -1
flex --version 2>/dev/null
meson --version 2>/dev/null
ninja --version 2>/dev/null
