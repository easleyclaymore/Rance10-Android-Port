#!/bin/bash
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export DEBIAN_FRONTEND=noninteractive

echo "=== 安装缺失依赖 ==="
apt-get update -qq 2>&1 | tail -2
apt-get install -y --no-install-recommends \
  libwebp-dev libjpeg62-turbo-dev libsndfile1-dev libglew-dev libcglm-dev \
  bison flex pkg-config 2>&1 | grep -E "^(Setting up|E:|Reading)" | tail -12

echo ""
echo "=== 复查 ==="
for p in sdl2 freetype2 libpng libwebp libjpeg libsndfile libffi zlib glew cglm; do
  printf "%-12s " "$p"
  if pkg-config --exists "$p" 2>/dev/null; then
    echo "OK $(pkg-config --modversion $p 2>/dev/null)"
  else
    echo "MISSING"
  fi
done
echo ""
echo "=== bison/flex 实版本 ==="
bison --version 2>/dev/null | head -1
flex --version 2>/dev/null
