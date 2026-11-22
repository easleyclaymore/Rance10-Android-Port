# flat_dump — flat 关键帧/时间线离线分析器

用法（需先编译，参照 tools/build/build.sh 的链接参数）：

    gcc -o /tmp/flatdump tools/offline/flat_dump.c \
        -Isubprojects/libsys4/include build-sdl3/subprojects/libsys4/libsys4.a \
        -ldeflate -lz -lm
    /tmp/flatdump <提取出的 .flat 文件>

输出 hdr 尺寸/fps、各 timeline（type: 3=graphic 4=sound 5=script）的
逐关键帧 pos/scale/area/origin/align/alpha。

用途：flat 渲染对不齐时（如 [TITLE-BTN-RENDER] 的标题按钮——关键帧
要求 CG 画在 (23,23) align=1，渲染忽略即"球画满/金环偏移"），
先用本工具看声明形态，再对照 parts_flat 渲染路径。

flat 提取：afa 表名字是 GBK（官中档），afa_ls.py 按 cp932 解码会
糊掉——按 `game/全CG周目继承存档` 同款思路用 python 直读 AFAH 表
（zlib 解压 + 4pad/len/name/16 结构）按 GBK 字节匹配提取。
