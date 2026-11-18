/* [D329] SDL3 迁移兼容头 —— 全工程强制包含（meson `-include`）。
 *
 * 为什么需要它：SDL2 的 `<SDL.h>` 以前会**顺带**包含 `<math.h>` /
 * `<stdlib.h>` / `<ctype.h>`，SDL3 把头文件收紧了，于是 `ceilf` / `lroundf` /
 * `free` / `isspace` / `sscanf` 全变成「隐式声明」，32 个编译错误只源于这一件事。
 *
 * 加这里而不是逐个文件补 `#include`，是为了让上游一旦也迁 SDL3 时，
 * 这批补丁不会散在 20 个文件里跟他们的改动打架。
 */
#ifndef XSYS4_SDL3_COMPAT_H
#define XSYS4_SDL3_COMPAT_H

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#endif /* XSYS4_SDL3_COMPAT_H */
