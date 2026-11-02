"""11-02 新纪着色对比（kind=gamepy，任意地图）：用去组件后的新纪中型（DS_M2_kitfree.blend）导出两份 FBX——法线原样 / 法线重算——
到 lookdev/rig/factions/kitfree/{keep,fix}/（不碰游戏用的 NEC_M.fbx），再分别重新导入拍四个镜头，图收进 Saved/Screenshots/NecShading。"""
import glob
import json
import os
import shutil
import subprocess
import time

import unreal

ROOT = 'D:/RobotSlice'
log = lambda *a: unreal.log_warning('[necshade] ' + ' '.join(str(x) for x in a))
cfg = json.load(open(f'{ROOT}/tools/easter_run.json', encoding='utf-8'))
blender = cfg.get('blender') or 'blender'
src = f'{ROOT}/lookdev/factions/dingshan/flat/kitfree/DS_M2_kitfree.blend'
shots = f'{ROOT}/Saved/Screenshots/NecShading'
os.makedirs(shots, exist_ok=True)
for f in glob.glob(f'{shots}/*'):
    os.remove(f)
for mode in ('keep', 'fix'):
    out = f'{ROOT}/lookdev/rig/factions/kitfree/{mode}'
    os.makedirs(out, exist_ok=True)
    t0 = time.time()
    r = subprocess.run([blender, '-b', '--factory-startup', src, '-P', f'{ROOT}/tools/kitfree_export.py', '--', '--normals', mode, '--kitfree',
                        '--key', 'NEC_M', '--out', out], capture_output=True, text=True, timeout=900, encoding='utf-8', errors='replace')
    lines = (r.stdout or '').splitlines() + (r.stderr or '').splitlines()
    log('==', mode, '导出 退出码', r.returncode, f'{time.time() - t0:.0f} 秒')
    for l in [l for l in lines if '[kitfree]' in l or '[factions]' in l or 'Error' in l or 'Traceback' in l or 'File "' in l or 'assert' in l.lower()][:40]:
        log(mode, '|', l[:240])
    fbx = f'{out}/NEC_M.fbx'
    if not os.path.exists(fbx):
        log(mode, '没有导出 FBX')
        continue
    r = subprocess.run([blender, '-b', '--factory-startup', '-P', f'{ROOT}/tools/blender_fbx_shots.py', '--', fbx, f'{out}/shot'], capture_output=True,
                       text=True, timeout=600, encoding='utf-8', errors='replace')
    for l in [l for l in ((r.stdout or '') + (r.stderr or '')).splitlines() if '[kitfree]' in l or 'Error' in l or 'Traceback' in l][:12]:
        log(mode, '拍', l[:200])
    for p in sorted(glob.glob(f'{out}/shot_*.png')):
        shutil.copy(p, f'{shots}/{mode}_{os.path.basename(p)}')
t_end = time.time()


def tick(dt):
    if time.time() - t_end > 2.0:
        unreal.unregister_slate_post_tick_callback(h)
        unreal.SystemLibrary.execute_console_command(None, 'quit')


h = unreal.register_slate_post_tick_callback(tick)
