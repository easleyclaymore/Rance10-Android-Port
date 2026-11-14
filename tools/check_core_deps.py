#!/usr/bin/env python3
"""Core Layer portability gate for the Rance10 xsystem4 fork.

Core Layer = VM execution, pages/heap, FFI, and the PAL contract. It must
compile without Linux headers and without hardcoded absolute paths.
Adapter Layer = platform-facing files still using POSIX APIs; reported as a
migration backlog (TECH_DEBT 10.4) but not a hard failure yet.
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
# Works both inside <project>/scripts/ and <project>/xsystem4/tools/.
_CANDIDATES = [os.path.dirname(HERE), os.path.dirname(os.path.dirname(HERE))]
ROOT = next((p for p in _CANDIDATES
             if os.path.exists(os.path.join(p, 'src', 'vm.c'))), _CANDIDATES[0])

CORE = [
    'src/vm.c', 'src/page.c', 'src/heap.c', 'src/ffi.c', 'src/pal.c',
    'include/vm.h', 'include/vm/page.h', 'include/vm/heap.h',
    'include/pal.h', 'include/xsystem4.h',
]
ADAPTER = [
    'src/asset_manager.c', 'src/system4.c', 'src/text.c', 'src/video.c',
    'src/debugger_scm.c', 'src/hll/File.c', 'src/hll/FileOperation.c',
    'src/hll/SengokuRanceFont.c', 'src/hll/PCInfo.c', 'src/pal_linux.c',
]
BANNED = [
    'dirent.h', 'sys/stat.h', 'sys/utsname.h', 'sys/mman.h',
    'sys/socket.h', 'sys/epoll.h', 'sys/ioctl.h', 'sys/wait.h',
    'netinet/in.h', 'arpa/inet.h', 'poll.h', 'pthread.h',
    'unistd.h', 'dlfcn.h', 'execinfo.h', 'termios.h', 'glob.h', 'ftw.h',
]
ABS_PATH = re.compile(r'["\'](/usr/|/home/|/data/|/sdcard/|/etc/|/proc/|/system/|/tmp/|/var/)')
# [D170] 门禁扫描的脚本清单 —— 按 tools/ 的角色分层后的新路径。
# 这份清单**必须跟着目录结构走**：路径写错时 scan_scripts 会静默跳过，
# 门禁照样 PASS，等于这道检查悄悄失效了。
SCRIPTS = [
    'tools/run/safe_run.sh', 'tools/run/safe_run_click.sh', 'tools/run/run-rance10.sh',
    'tools/run/run-live.sh', 'tools/run/run-headless.sh', 'tools/run/baseline.sh',
    'tools/run/run-local.sh', 'tools/run/storm.sh',
    'tools/build/build.sh', 'tools/build/build-xsys.sh',
    'tools/env/dep.sh', 'tools/env/inst-deps.sh',
    'tools/lib/common.sh', 'tools/xsys4',
]
# /tmp is fine for screenshot/log scratch files in test tooling, but a
# hardcoded Android absolute path is not: platform detection must go through
# RANCE10_PLATFORM / TERMUX_VERSION (TECH_DEBT "hardcoded 3 places").
SCRIPT_ABS = re.compile(r'/data/data/|/sdcard/')

# [L0] Freeze the degradation baseline. The Core Layer already carries 4
# HACK-POC markers (D95/D96 in heap.c, D120 in vm.c); they are exactly the
# "tolerate a dead slot instead of aborting" layer that keeps ownership bugs
# invisible. While paying that debt down, adding another one must fail the
# gate. Removing one is fine. Diagnostic switches are reported for visibility:
# they are allowed, but every one of them must stay default-off.
DEGRADATION_BASELINE = 4
MARKER = re.compile(r'HACK-POC')
DIAG = re.compile(r'getenv\("XSYS4_')

def count_lines(files, pattern):
    total = 0
    for rel in files:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            continue
        with open(path, encoding='utf-8', errors='replace') as f:
            total += sum(1 for line in f if pattern.search(line))
    return total

def scan(files):
    found, checked = [], 0
    for rel in files:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            found.append((rel, 0, 'MISSING FILE', ''))
            continue
        checked += 1
        for n, line in enumerate(open(path, encoding='utf-8', errors='replace'), 1):
            s = line.strip()
            if s.startswith('#include'):
                for h in BANNED:
                    if h in s:
                        found.append((rel, n, 'linux header <%s>' % h, s))
            if ABS_PATH.search(line):
                found.append((rel, n, 'hardcoded absolute path', s[:100]))
    return found, checked

def scan_scripts(files):
    found = []
    for rel in files:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            continue
        with open(path, encoding='utf-8', errors='replace') as f:
            for n, line in enumerate(f, 1):
                if SCRIPT_ABS.search(line):
                    found.append((rel, n, 'hardcoded Android path', line.strip()[:100]))
    return found

core_hits, core_n = scan(CORE)
ad_hits, ad_n = scan(ADAPTER)
script_hits = scan_scripts(SCRIPTS)
deg = count_lines(CORE, MARKER)
diag = count_lines(CORE, DIAG)
print('Core Layer   : %d files, %d violations' % (core_n, len(core_hits)))
print('Adapter Layer: %d files, %d Linux-only includes (migration backlog)' % (ad_n, len(ad_hits)))
for f, n, why, text in core_hits:
    print('  CORE %s:%d %s | %s' % (f, n, why, text))
print('Adapter breakdown (header -> count):')
tally = {}
for f, n, why, text in ad_hits:
    tally[why] = tally.get(why, 0) + 1
for k, v in sorted(tally.items()):
    print('  %-40s %d' % (k, v))
print('Tooling scripts    : %d files, %d hardcoded Android paths' % (len(SCRIPTS), len(script_hits)))
for f, n, why, text in script_hits:
    print('  SCRIPT %s:%d %s | %s' % (f, n, why, text))
print('Core degradation markers: %d (frozen baseline %d)' % (deg, DEGRADATION_BASELINE))
print('Core diagnostic switches: %d (all must stay default-off)' % diag)
fail = bool(core_hits) or bool(script_hits)
if deg > DEGRADATION_BASELINE:
    print('  CORE new HACK-POC markers: +%d' % (deg - DEGRADATION_BASELINE))
    fail = True
if fail:
    print('RESULT: FAIL')
    sys.exit(1)
print('RESULT: PASS - Core Layer has no Linux-only headers, no hardcoded paths, degradation baseline frozen')
