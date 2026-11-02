"""11-02 去组件 · 肩部装备（kind=gamepy，任意地图）：用自建件重跑 make_shoulder_game.py / make_shoulder_tactical.py，
输出到 Inbox/_肩部候选/游戏模型_kitfree、游戏模型_战术_kitfree（原来的不动）。日志 [kfsh]；收回用了组件的 12 件的预览图（新的）。
第二项 grade_kitfree_shoulder_old.py 收原来的同名预览图做对比。"""
import glob
import json
import os
import shutil
import subprocess
import time

import unreal

ROOT = 'D:/RobotSlice'
log = lambda *a: unreal.log_warning('[kfsh] ' + ' '.join(str(x) for x in a))
cfg = json.load(open(f'{ROOT}/tools/easter_run.json', encoding='utf-8'))
blender = cfg.get('blender') or 'blender'
OUT = {'game': f'{ROOT}/Inbox/_肩部候选/游戏模型_kitfree', 'tactical': f'{ROOT}/Inbox/_肩部候选/游戏模型_战术_kitfree'}
shots = f'{ROOT}/Saved/Screenshots/KitFreeShoulder'
os.makedirs(shots, exist_ok=True)
for f in glob.glob(f'{shots}/*'):
    os.remove(f)
for which, out in OUT.items():
    shutil.rmtree(out, ignore_errors=True)
    t0 = time.time()
    r = subprocess.run([blender, '-b', '--factory-startup', '-P', f'{ROOT}/tools/kitfree_shoulder_run.py', '--', which, out], capture_output=True,
                       text=True, timeout=600, encoding='utf-8', errors='replace')
    lines = (r.stdout or '').splitlines() + (r.stderr or '').splitlines()
    log('==', which, '退出码', r.returncode, f'{time.time() - t0:.0f} 秒')
    for l in [l for l in lines if any(k in l for k in ('[kitfree]', '[game]', '[tactical]', 'Error', 'Traceback', 'File "'))][:50]:
        log(which, '|', l[:240])
PICK = ['游戏模型_kitfree/SM_GrappleLauncher.png', '游戏模型_kitfree/SM_GrappleClaw.png', '游戏模型_kitfree/SM_NetLauncher.png',
        '游戏模型_kitfree/SM_Artillery.png', '游戏模型_kitfree/SM_SmokeLauncher.png', '游戏模型_kitfree/SM_EmpLauncher.png',
        '游戏模型_kitfree/SM_ShieldEmitter.png', '游戏模型_战术_kitfree/SM_BurstPack_a.png', '游戏模型_战术_kitfree/SM_LaunchSpring_a.png',
        '游戏模型_战术_kitfree/SM_ShockRing_a.png', '游戏模型_战术_kitfree/SM_MineLayer_a.png', '游戏模型_战术_kitfree/SM_Mine_a.png']
for k, p in enumerate(PICK):
    src = f'{ROOT}/Inbox/_肩部候选/{p}'
    if os.path.exists(src):
        shutil.copy(src, f'{shots}/{k:02d}_{os.path.basename(p)}')
    else:
        log('没有预览图', p)
t_end = time.time()


def tick(dt):
    if time.time() - t_end > 2.0:
        unreal.unregister_slate_post_tick_callback(h)
        unreal.SystemLibrary.execute_console_command(None, 'quit')


h = unreal.register_slate_post_tick_callback(tick)
