#!/bin/bash
# 一键构建（Linux 桌面 / 开发机）。README「从源码编译」指的就是这个脚本。
#
# 用法：
#   tools/env/build.sh                     # 构建到 ./build（release）
#   tools/env/build.sh -d build-sdl3       # 指定构建目录
#   tools/env/build.sh -t debug            # debug 构建（带符号，慢）
#   tools/env/build.sh --gl                # 强制桌面 GL + GLEW（与 Release 二进制一致）
#   tools/env/build.sh --gles              # 强制 OpenGL ES 3（嵌入式 / Android 路线）
#   tools/env/build.sh --clean             # 先删构建目录
#   tools/env/build.sh --check             # 只检查依赖，不构建
#
# 做的事，按顺序：
#   1. 摘掉 PYTHONPATH / PYTHONHOME —— 否则 meson 死在 ModuleNotFoundError: encodings
#   2. 检查 subprojects/libsys4 是否已拉取（gitlink，空目录 = 忘了 --recurse-submodules）
#   3. 应用 patches/ 里的 libsys4 补丁（幂等：已应用过会跳过）
#   4. 用 pkg-config 检查依赖，缺什么就打印对应的 apt 包名
#   5. meson setup + ninja
#
# 退出码：0 成功；1 构建失败；2 依赖/源码树不完整（先按提示装东西，不会白跑一遍编译）。
set -uo pipefail

REPO="$(cd "$(dirname "$(realpath "$0")")/../.." && pwd)"
BUILD_DIR="build"
BUILDTYPE="release"
GL_MODE=""          # "" = 跟随 meson 默认（auto）；"disabled" / "enabled"
CLEAN=0
CHECK_ONLY=0
JOBS=""

die() { echo "ERROR: $*" >&2; exit 2; }

while [ $# -gt 0 ]; do
    case "$1" in
        -d|--build-dir) BUILD_DIR="$2"; shift 2 ;;
        -t|--type)      BUILDTYPE="$2"; shift 2 ;;
        -j)             JOBS="$2"; shift 2 ;;
        --gl)           GL_MODE="disabled"; shift ;;   # -Dopengles=disabled
        --gles)         GL_MODE="enabled";  shift ;;
        --clean)        CLEAN=1; shift ;;
        --check)        CHECK_ONLY=1; shift ;;
        -h|--help)      sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) die "未知参数: $1（--help 看用法）" ;;
    esac
done

cd "$REPO" || die "进不去 $REPO"

# 1. Python 环境隔离（踩过：桌面环境里 PYTHONHOME 指向别的解释器，meson 直接崩）
unset PYTHONPATH PYTHONHOME PYTHONDONTWRITEBYTECODE

# 2. libsys4 子模块
if [ ! -f subprojects/libsys4/meson.build ]; then
    echo "subprojects/libsys4 是空的 —— 先拉子模块：" >&2
    echo "    git submodule update --init --recursive" >&2
    exit 2
fi

# 3. 本地补丁（gsave v8 / ajp_get_metrics）。不应用会在链接期报 undefined reference。
for p in patches/0001-libsys4-gsave-v8.patch patches/0002-libsys4-ajp-metrics.patch; do
    [ -f "$p" ] || die "缺补丁文件 $p"
    if git -C subprojects/libsys4 apply --check --reverse "$REPO/$p" >/dev/null 2>&1; then
        echo "[patch] 已应用: $(basename "$p")"
    elif git -C subprojects/libsys4 apply "$REPO/$p"; then
        echo "[patch] 已应用: $(basename "$p")"
    else
        die "补丁打不上: $p（子模块工作树被改过？试试 git -C subprojects/libsys4 checkout .）"
    fi
done

# 4. 依赖检查。pkg-config 名 → apt 包名（Debian/Ubuntu 24.04+）
check_pc() {
    # $1=pkg-config 名  $2=apt 包名  $3=说明（可选）
    local name="$1" pkg="$2" note="${3:-}"
    if pkg-config --exists "$name" 2>/dev/null; then
        printf '  [OK]   %-12s %s\n' "$name" "$(pkg-config --modversion "$name" 2>/dev/null)"
        return 0
    fi
    printf '  [MISS] %-12s 需要 apt 包: %s %s\n' "$name" "$pkg" "$note"
    return 1
}

echo "== 依赖检查 =="
MISSING=0
for t in gcc meson ninja pkg-config bison flex; do
    if command -v "$t" >/dev/null 2>&1; then
        printf '  [OK]   %s\n' "$t"
    else
        printf '  [MISS] %s（apt: build-essential meson ninja-build bison flex pkg-config）\n' "$t"
        MISSING=1
    fi
done
# 注意：SDL3 在 Ubuntu 24.04 / Debian bookworm 的源里通常没有，见 README「SDL3 怎么办」。
check_pc sdl3          libsdl3-dev    "（老发行版没有 → 从源码装，见 README）" || MISSING=1
check_pc freetype2     libfreetype-dev || MISSING=1
check_pc libpng        libpng-dev     || MISSING=1
check_pc libwebp       libwebp-dev    || MISSING=1
check_pc libturbojpeg  libturbojpeg0-dev || MISSING=1
check_pc sndfile       libsndfile1-dev || MISSING=1
check_pc libffi        libffi-dev     || MISSING=1
check_pc zlib          zlib1g-dev     || MISSING=1
# 以下为「二选一」/ 可选依赖，缺了只影响对应后端或功能
if pkg-config --exists glesv2 2>/dev/null; then
    printf '  [OK]   glesv2       %s（走 GLES 路线，不需要 glew）\n' "$(pkg-config --modversion glesv2)"
elif pkg-config --exists glew 2>/dev/null; then
    printf '  [OK]   glew         %s（走桌面 GL 路线）\n' "$(pkg-config --modversion glew)"
else
    echo '  [MISS] glesv2 / glew —— 至少装一个：libgles2-mesa-dev 或 libglew-dev libgl-dev'
    MISSING=1
fi
pkg-config --exists cglm   || echo '  [warn] cglm 缺失 —— meson 会用 subprojects 里的 fallback 现编（首次构建多花一点时间）'
pkg-config --exists fontconfig || echo '  [warn] fontconfig 缺失 —— 中文版靠它动态找 CJK 字体，建议装 libfontconfig-dev'
pkg-config --exists chibi-scheme || echo '  [warn] chibi-scheme 缺失 —— 只有内置 Scheme 调试器受影响'

if [ "$MISSING" -ne 0 ]; then
    echo ""
    echo "依赖不全。Ubuntu/Debian 一把装（24.04 实测）："
    echo "  sudo apt install -y build-essential pkg-config meson ninja-build flex bison \\"
    echo "    libfreetype-dev libffi-dev libpng-dev libsndfile1-dev libturbojpeg0-dev \\"
    echo "    libwebp-dev zlib1g-dev libdeflate-dev libgl-dev libglew-dev libcglm-dev \\"
    echo "    libfontconfig-dev"
    exit 2
fi
[ "$CHECK_ONLY" -eq 1 ] && { echo "== 依赖检查通过（--check，不构建）=="; exit 0; }

# 5. 构建
[ "$CLEAN" -eq 1 ] && rm -rf "$BUILD_DIR"
SETUP_ARGS=(setup "$BUILD_DIR" "--buildtype=$BUILDTYPE")
[ -n "$GL_MODE" ] && SETUP_ARGS+=("-Dopengles=$GL_MODE")

if [ -f "$BUILD_DIR/build.ninja" ]; then
    echo "== 复用已有构建目录 $BUILD_DIR =="
    meson configure "$BUILD_DIR" "--buildtype=$BUILDTYPE" >/dev/null || die "meson configure 失败"
    [ -n "$GL_MODE" ] && meson configure "$BUILD_DIR" "-Dopengles=$GL_MODE" >/dev/null
else
    echo "== meson setup $BUILD_DIR ($BUILDTYPE${GL_MODE:+, opengles=$GL_MODE}) =="
    meson "${SETUP_ARGS[@]}" || die "meson setup 失败（日志在 $BUILD_DIR/meson-logs/meson-log.txt）"
fi

echo "== ninja =="
if [ -n "$JOBS" ]; then
    ninja -C "$BUILD_DIR" -j "$JOBS" || die "编译失败"
else
    ninja -C "$BUILD_DIR" || die "编译失败"
fi

BIN="$BUILD_DIR/src/xsystem4"
[ -x "$BIN" ] || die "构建结束但没找到 $BIN"
echo ""
echo "== 完成 =="
printf '  %s  %s\n' "$BIN" "$(du -h "$BIN" | cut -f1)"
echo "  跑：$BIN /path/to/ゲームディレクトリ"
echo "  仅想在无头环境验证能不能启动：tools/run/run-headless.sh"
