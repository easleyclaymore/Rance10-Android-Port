#!/bin/bash
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig

cd /home/alin/Rance10_Port/xsystem4 || exit 1
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
