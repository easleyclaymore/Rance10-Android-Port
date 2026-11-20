#!/bin/bash
# 装齐构建依赖（Debian / Ubuntu 系，需 root）。与 README「从源码编译 §1」同一份清单。
#
#   sudo tools/env/inst-deps.sh
#
# 说明：
#   · 这里是「最小可编译」集合 + 强烈建议项。SDL3 在 Ubuntu 24.04 / Debian bookworm
#     的源里通常没有，本脚本只尝试装，装不上会打印从源码编译的提示（见 README §2）。
#   · 想先看缺什么而不装：tools/env/build.sh --check
set -u

if [ "$(id -u)" -ne 0 ]; then
    echo "需要 root：sudo $0" >&2
    exit 1
fi

export DEBIAN_FRONTEND=noninteractive

echo "=== apt update ==="
apt-get update -qq || exit 1

echo "=== 安装依赖 ==="
apt-get install -y --no-install-recommends \
    build-essential pkg-config meson ninja-build flex bison cmake \
    libfreetype-dev libffi-dev libpng-dev libsndfile1-dev libturbojpeg0-dev \
    libwebp-dev zlib1g-dev libdeflate-dev libfontconfig-dev \
    libgl-dev libglew-dev libcglm-dev libgles2-mesa-dev \
    2>&1 | grep -E "^(Setting up|E:|Reading)" | tail -20

echo ""
echo "=== SDL3 ==="
if pkg-config --exists sdl3 2>/dev/null; then
    echo "已就绪：sdl3 $(pkg-config --modversion sdl3)"
else
    # 新发行版（Debian trixie+ / Ubuntu 25.10+）源里才有
    apt-get install -y --no-install-recommends libsdl3-dev >/dev/null 2>&1 || true
    if pkg-config --exists sdl3 2>/dev/null; then
        echo "已装：sdl3 $(pkg-config --modversion sdl3)"
    else
        echo "源里没有 libsdl3-dev —— 需要从源码装 SDL3 3.2.x 到 /usr/local"
        echo "  步骤见 README「从源码编译 §2 SDL3 怎么办」，或跑 CI 的同一套命令"
        echo "  装完记得 export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:\$PKG_CONFIG_PATH"
    fi
fi

echo ""
echo "=== 复查（缺什么会标 MISS）==="
"$(dirname "$(realpath "$0")")/build.sh" --check || true
