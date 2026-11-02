"""11-02 去组件 · 第二批（kind=gamepy，任意地图）：九台都用去组件的源文件、导出前重算法线（tools/kitfree_export.py --normals fix --kitfree），
导到 lookdev/rig/factions/kitfree/fix/<台>.fbx（游戏用的 lookdev/rig/factions/<台>.fbx 不动），每台重新导入拍一张全身斜侧（工作室光），
图收进 Saved/Screenshots/KitFreeExport。[factions] / [kitfree] / 报错行打到 [kfexp]。"""
import glob
import json
import os
import shutil
import subprocess
import time

import unreal

ROOT = 'D:/RobotSlice'
L = f'{ROOT}/lookdev'
log = lambda *a: unreal.log_warning('[kfexp] ' + ' '.join(str(x) for x in a))
cfg = json.load(open(f'{ROOT}/tools/easter_run.json', encoding='utf-8'))
blender = cfg.get('blender') or 'blender'
V3K = f'{L}/rig/kitfree/SKM_Robot_v3.blend'
SRC = {'NEC_L': (None, []), 'NEC_M': (f'{L}/factions/dingshan/flat/kitfree/DS_M2_kitfree.blend', []),
       'NEC_H': (f'{L}/factions/dingshan/flat/kitfree/DS_H1_kitfree.blend', []),
       'PHASE_L': (f'{L}/factions/baita/blend/白塔_L1_kitfree.blend', []), 'PHASE_M': (f'{L}/factions/baita/blend/白塔_BM_kitfree.blend', []),
       'PHASE_H': (f'{L}/factions/baita/blend/白塔_H2_kitfree.blend', []),
       'VHI_L': (V3K, ['--v3', V3K]), 'VHI_M': (V3K, ['--v3', V3K]), 'VHI_H': (V3K, ['--v3', V3K])}
out = f'{L}/rig/factions/kitfree/fix'
os.makedirs(out, exist_ok=True)
shots = f'{ROOT}/Saved/Screenshots/KitFreeExport'
os.makedirs(shots, exist_ok=True)
for f in glob.glob(f'{shots}/*'):
    os.remove(f)
keys = ('[kitfree]', '[factions]', 'Error', 'Traceback', 'File "', 'AssertionError')
for key, (blend, extra) in SRC.items():
    cmd = [blender, '-b', '--factory-startup'] + ([blend] if blend else []) + ['-P', f'{ROOT}/tools/kitfree_export.py', '--', '--normals', 'fix',
                                                                            '--kitfree'] + extra + ['--key', key, '--out', out]
    t0 = time.time()
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=400, encoding='utf-8', errors='replace')
    except subprocess.TimeoutExpired:
        log('==', key, '超时')
        continue
    lines = (r.stdout or '').splitlines() + (r.stderr or '').splitlines()
    log('==', key, '退出码', r.returncode, f'{time.time() - t0:.0f} 秒')
    for l in [l for l in lines if any(k in l for k in keys) and 'could not find any file path' not in l][:14]:
        log(key, '|', l[:240])
    fbx = f'{out}/{key}.fbx'
    if not os.path.exists(fbx) or os.path.getmtime(fbx) < t0:
        log(key, '没有新导出的 FBX')
        continue
    r = subprocess.run([blender, '-b', '--factory-startup', '-P', f'{ROOT}/tools/blender_fbx_shots.py', '--', fbx, f'{out}/{key}', '全身斜侧'],
                       capture_output=True, text=True, timeout=300, encoding='utf-8', errors='replace')
    for p in glob.glob(f'{out}/{key}_*.png'):
        shutil.copy(p, f'{shots}/{key}.png')
t_end = time.time()


def tick(dt):
    if time.time() - t_end > 2.0:
        unreal.unregister_slate_post_tick_callback(h)
        unreal.SystemLibrary.execute_console_command(None, 'quit')


h = unreal.register_slate_post_tick_callback(tick)
