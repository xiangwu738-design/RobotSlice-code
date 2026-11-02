"""11-02 去组件 · 第一批（kind=gamepy，任意地图）：重建各台的源文件（都另存，不覆盖原来的）——
新纪重型 DS_H1_kitfree.blend、相位三台 白塔_{L1,BM,H2}_kitfree.blend、v3 绑骨工程 lookdev/rig/kitfree/SKM_Robot_v3.blend。
每步一个 Blender 进程，[kitfree] / [ds] / [baita] / [rig] / 报错行打到 [kfsrc]，出的图收进 Saved/Screenshots/KitFreeSrc。"""
import glob
import json
import os
import shutil
import subprocess
import time

import unreal

ROOT = 'D:/RobotSlice'
log = lambda *a: unreal.log_warning('[kfsrc] ' + ' '.join(str(x) for x in a))
cfg = json.load(open(f'{ROOT}/tools/easter_run.json', encoding='utf-8'))
blender = cfg.get('blender') or 'blender'
V3 = f'{ROOT}/lookdev/rig/SKM_Robot_v3.blend'
shots = f'{ROOT}/Saved/Screenshots/KitFreeSrc'
os.makedirs(shots, exist_ok=True)
for f in glob.glob(f'{shots}/*'):
    os.remove(f)
base = f'{ROOT}/Saved/KitFree/src'
shutil.rmtree(base, ignore_errors=True)
steps = [
    ('NEC_H', [blender, '-b', '--factory-startup', '-P', f'{ROOT}/tools/kitfree_run.py', '--', 'after', f'{base}/H1',
               f'{ROOT}/lookdev/factions/dingshan/flat/kitfree/DS_H1_kitfree.blend', '--scheme', 'H1'], f'{base}/H1'),
]
for sch in ('L1', 'BM', 'H2'):
    steps.append((f'PHASE_{sch}', [blender, '-b', '--factory-startup', V3, '-P', f'{ROOT}/tools/kitfree_baita.py', '--', '--scheme', sch,
                                   '--out', f'{base}/{sch}'], f'{base}/{sch}'))
steps.append(('v3', [blender, '-b', '--factory-startup', '-P', f'{ROOT}/tools/kitfree_v3.py'], None))
keys = ('[kitfree]', '[ds]', '[baita]', '[rig]', 'Error', 'Traceback', 'File "', 'assert')
for name, cmd, out in steps:
    t0 = time.time()
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=600, encoding='utf-8', errors='replace')
    except subprocess.TimeoutExpired:
        log('==', name, '超时')
        continue
    lines = (r.stdout or '').splitlines() + (r.stderr or '').splitlines()
    log('==', name, '退出码', r.returncode, f'{time.time() - t0:.0f} 秒')
    for l in [l for l in lines if any(k in l for k in keys)][:30]:
        log(name, '|', l[:240])
    if out:
        for p in sorted(glob.glob(f'{out}/**/*.png', recursive=True))[:2]:
            shutil.copy(p, f'{shots}/{name}_{os.path.basename(p)}')
for p in (f'{ROOT}/lookdev/factions/dingshan/flat/kitfree/DS_H1_kitfree.blend', f'{ROOT}/lookdev/factions/baita/blend/白塔_L1_kitfree.blend',
          f'{ROOT}/lookdev/factions/baita/blend/白塔_BM_kitfree.blend', f'{ROOT}/lookdev/factions/baita/blend/白塔_H2_kitfree.blend',
          f'{ROOT}/lookdev/rig/kitfree/SKM_Robot_v3.blend', f'{ROOT}/lookdev/rig/kitfree/SKM_Robot_v3.fbx'):
    log('文件', os.path.relpath(p, ROOT), '有' if os.path.exists(p) else '没有', time.strftime('%H:%M', time.localtime(os.path.getmtime(p))) if os.path.exists(p) else '')
t_end = time.time()


def tick(dt):
    if time.time() - t_end > 2.0:
        unreal.unregister_slate_post_tick_callback(h)
        unreal.SystemLibrary.execute_console_command(None, 'quit')


h = unreal.register_slate_post_tick_callback(tick)
