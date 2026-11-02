"""11-02 去组件试点（kind=gamepy，任意地图）：新纪中型改前 / 改后各跑一次 tools/kitfree_run.py（Blender），
图收进 Saved/Screenshots/KitFree（测试流程收回，最多 12 张），Blender 输出里 [kitfree] / [ds] / Error 的行打到 [kitfree]。
改后的 .blend 存在 lookdev/factions/dingshan/flat/kitfree/DS_M2_kitfree.blend（原来的 DS_M2.blend 不动）。"""
import glob
import json
import os
import shutil
import subprocess
import time

import unreal

ROOT = 'D:/RobotSlice'
log = lambda *a: unreal.log_warning('[kitfree] ' + ' '.join(str(x) for x in a))
cfg = json.load(open(f'{ROOT}/tools/easter_run.json', encoding='utf-8'))
blender = cfg.get('blender') or 'blender'
base = f'{ROOT}/Saved/KitFree/pilot'
shots = f'{ROOT}/Saved/Screenshots/KitFree'
shutil.rmtree(base, ignore_errors=True)
os.makedirs(shots, exist_ok=True)
for f in glob.glob(f'{shots}/*'):
    os.remove(f)
kdir = f'{ROOT}/lookdev/factions/dingshan/flat/kitfree'
os.makedirs(kdir, exist_ok=True)
for mode in ('before', 'after'):
    out = f'{base}/{mode}'
    args = [blender, '-b', '--factory-startup', '-P', f'{ROOT}/tools/kitfree_run.py', '--', mode, out]
    if mode == 'after':
        args.append(f'{kdir}/DS_M2_kitfree.blend')
    t0 = time.time()
    r = subprocess.run(args, capture_output=True, text=True, timeout=900, encoding='utf-8', errors='replace')
    lines = (r.stdout or '').splitlines() + (r.stderr or '').splitlines()
    log('==', mode, '退出码', r.returncode, f'{time.time() - t0:.0f} 秒', '输出', len(lines), '行')
    for l in [l for l in lines if '[kitfree]' in l or '[ds]' in l or 'Error' in l or 'Traceback' in l or 'File "' in l][:60]:
        log(mode, '|', l[:240])
    pngs = sorted(glob.glob(f'{out}/**/*.png', recursive=True))
    log(mode, '图', len(pngs), [os.path.relpath(p, out) for p in pngs][:12])
    for p in pngs[:6]:
        shutil.copy(p, f'{shots}/{mode}_{os.path.basename(p)}')
t_end = time.time()


def tick(dt):
    if time.time() - t_end > 2.0:
        unreal.unregister_slate_post_tick_callback(h)
        unreal.SystemLibrary.execute_console_command(None, 'quit')


h = unreal.register_slate_post_tick_callback(tick)
