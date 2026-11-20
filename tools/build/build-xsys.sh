#!/bin/bash
# 兼容壳：从零重建（等价于 `tools/env/build.sh --clean`）。
#
# 2026-11-20 起构建入口统一到 tools/env/build.sh —— 那里多了依赖预检、
# PYTHON* 环境隔离、libsys4 补丁幂等应用、可选构建目录/构建类型/GL 后端。
# 本脚本只保留旧调用方式，避免文档与肌肉记忆失效。
#
#   tools/build/build-xsys.sh            # = env/build.sh --clean（默认 build/）
#   tools/build/build-xsys.sh -d dir     # 其余参数原样透传
set -u
HERE="$(dirname "$(realpath "$0")")"
exec "$HERE/../env/build.sh" --clean "$@"
