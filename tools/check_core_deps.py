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
    'tools/wip-snapshot.sh', 'tools/gen-doc-numbers.py', 'tools/gen-switches.py',
    'tools/gen-manual.py',
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

# [D222] libsys4 是上游 submodule、无推送权限，关键修复只能以
# patches/*.patch 入库（README 构建段写明 apply 步骤）。忘了 apply 时编译
# 照样通过，但 AJP CG 尺寸 / gsave v8 / 泛型 ARRAY 会静默失效，症状极隐蔽。
# 这里逐段核对补丁的新增行是否真的落在子模块工作树里；单独 clone 时 SKIP。
def check_libsys4_patches():
    import glob
    pdir = os.path.join(ROOT, 'patches')
    sub = os.path.join(ROOT, 'subprojects', 'libsys4')
    if not (os.path.isdir(pdir) and os.path.isdir(sub)):
        return None
    problems, verified = [], 0
    for pp in sorted(glob.glob(os.path.join(pdir, '*.patch'))):
        segs, target, adds = [], None, []
        for line in open(pp, encoding='utf-8', errors='replace'):
            if line.startswith('+++ b/'):
                if target and adds:
                    segs.append((target, adds))
                target, adds = line[6:].strip(), []
            elif line.startswith('+') and not line.startswith('+++'):
                adds.append(line[1:].rstrip('\n'))
        if target and adds:
            segs.append((target, adds))
        for tgt, lines in segs:
            fp = os.path.join(sub, tgt)
            if not os.path.exists(fp):
                problems.append('%s: missing %s' % (os.path.basename(pp), tgt))
                continue
            content = open(fp, encoding='utf-8', errors='replace').read()
            missing = [a.strip() for a in lines
                       if a.strip() and a.strip() not in content]
            if missing:
                problems.append('%s -> %s: %d added line(s) not applied, e.g. %r'
                                % (os.path.basename(pp), tgt, len(missing),
                                   missing[0][:70]))
            else:
                verified += 1
    return problems, verified

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
_patch_res = check_libsys4_patches()
if _patch_res is None:
    print('libsys4 patches: SKIP (submodule/patches not present in this checkout)')
else:
    _prob, _ver = _patch_res
    print('libsys4 patches: %d file-segment(s) verified, %d problem(s)' % (_ver, len(_prob)))
    for p in _prob:
        print('  PATCH ' + p)
fail = bool(core_hits) or bool(script_hits) or bool(_patch_res and _patch_res[0])
# [D376] `XSYS4_SKIP_DOC_GATE=1`：跳过「文档 / 手册」两项，只保留代码检查。
#
# 为什么需要：这两项**依赖开发工作区**——`gen-doc-numbers.py` 要求
# 仓库父目录里有 `AGENTS.md`（D291 的约定），`gen-manual.py` 要求能看到
# 游戏目录里的官方手册。CI（单独 clone + 没有游戏素材）里这两条永远不成立，
# 于是门禁在 CI 里 100% 失败，等于没有门禁。CI 只跑代码可移植性 + 补丁落地，
# 文档一致性仍然由本地 `python3 tools/check_core_deps.py` 保证。
import os as _os
import subprocess
_skip_docs = _os.environ.get('XSYS4_SKIP_DOC_GATE') == '1'
# [D208] 文档数字腐化检查（《工程管理》§6 ⑪）：手写数字与生成结果不符即失败。
# 单独克隆 xsystem4 时脚本自己会 SKIP，不会阻塞外部使用者。
_doc = subprocess.run([sys.executable, os.path.join(HERE, 'gen-doc-numbers.py'), '--check'],
                      cwd=ROOT, capture_output=True, text=True)
_doc_out = _doc.stdout.strip().splitlines()
if _skip_docs:
    print('Doc numbers  : SKIP (XSYS4_SKIP_DOC_GATE=1，CI 无工作区上下文)')
else:
    print('Doc numbers  : %s' % (_doc_out[-1] if _doc_out else 'no output'))
    if _doc.returncode != 0:
        for line in _doc_out:
            print('  DOC ' + line)
        fail = True
# [D212] 官方手册生成物校验：md 与 93 张图必须与权威源逐字节一致。
# 单独 clone 时 gen-manual.py 自己会 SKIP，不阻塞外部使用者。
_mn = subprocess.run([sys.executable, os.path.join(HERE, 'gen-manual.py'), '--check'],
                     cwd=ROOT, capture_output=True, text=True)
_mn_out = _mn.stdout.strip().splitlines()
if _skip_docs:
    print('Manual docs  : SKIP (XSYS4_SKIP_DOC_GATE=1，CI 无游戏素材)')
else:
    print('Manual docs  : %s' % (_mn_out[-1] if _mn_out else 'no output'))
    if _mn.returncode != 0:
        for line in _mn_out:
            print('  MANUAL ' + line)
        fail = True
if deg > DEGRADATION_BASELINE:
    print('  CORE new HACK-POC markers: +%d' % (deg - DEGRADATION_BASELINE))
    fail = True
if fail:
    print('RESULT: FAIL')
    sys.exit(1)
print('RESULT: PASS - Core Layer has no Linux-only headers, no hardcoded paths, degradation baseline frozen')
