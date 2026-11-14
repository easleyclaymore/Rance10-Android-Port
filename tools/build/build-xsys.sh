#!/bin/bash
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
# [D169] 同理，`aarch64-linux-gnu` 是本机的 multiarch 名，别硬编码。
MULTIARCH="$(uname -m)-linux-gnu"
export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:/usr/lib/${MULTIARCH}/pkgconfig:/usr/share/pkgconfig"

# [D169] 原来这里写死 `/home/alin/Rance10_Port/xsystem4` —— 那是另一台
# 机器（aarch64）的绝对路径，换到任何别的机器上直接 cd 失败。
# 按脚本自身位置定位：tools/ 的上一级就是 xsystem4/。
# [D170] 路径锚点：向上找到 tools/lib/common.sh。
_d="$(dirname "$(realpath "$0")")"; while [ ! -f "$_d/lib/common.sh" ] && [ "$_d" != "/" ]; do _d="$(dirname "$_d")"; done; . "$_d/lib/common.sh"
cd "$XSYS4_DIR" || exit 1
rm -rf build

echo "=========== meson setup ==========="
meson setup build 2>&1 | tail -45
rc=${PIPESTATUS[0]}
if [ "$rc" != "0" ]; then
  echo "!!! meson setup 失败 rc=$rc"
  exit 1
fi

echo ""
echo "=========== ninja ==========="
ninja -C build 2>&1 | tail -45
rc=${PIPESTATUS[0]}
if [ "$rc" != "0" ]; then
  echo "!!! ninja 失败 rc=$rc"
  exit 1
fi

echo ""
echo "=========== 产物 ==========="
ls -la build/src/xsystem4 2>&1
file build/src/xsystem4 2>&1
echo ""
echo "=========== 版本自检 ==========="
./build/src/xsystem4 --help 2>&1 | head -15
