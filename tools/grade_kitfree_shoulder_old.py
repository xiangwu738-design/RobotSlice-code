"""11-02 去组件 · 肩部装备对比用（kind=gamepy）：收原来（带组件）的同名 12 张预览图。"""
import glob
import os
import shutil
import time

import unreal

ROOT = 'D:/RobotSlice'
shots = f'{ROOT}/Saved/Screenshots/KitFreeShoulderOld'
os.makedirs(shots, exist_ok=True)
for f in glob.glob(f'{shots}/*'):
    os.remove(f)
PICK = ['游戏模型/SM_GrappleLauncher.png', '游戏模型/SM_GrappleClaw.png', '游戏模型/SM_NetLauncher.png', '游戏模型/SM_Artillery.png',
        '游戏模型/SM_SmokeLauncher.png', '游戏模型/SM_EmpLauncher.png', '游戏模型/SM_ShieldEmitter.png', '游戏模型_战术/SM_BurstPack_a.png',
        '游戏模型_战术/SM_LaunchSpring_a.png', '游戏模型_战术/SM_ShockRing_a.png', '游戏模型_战术/SM_MineLayer_a.png', '游戏模型_战术/SM_Mine_a.png']
for k, p in enumerate(PICK):
    src = f'{ROOT}/Inbox/_肩部候选/{p}'
    if os.path.exists(src):
        shutil.copy(src, f'{shots}/{k:02d}_{os.path.basename(p)}')
t_end = time.time()


def tick(dt):
    if time.time() - t_end > 2.0:
        unreal.unregister_slate_post_tick_callback(h)
        unreal.SystemLibrary.execute_console_command(None, 'quit')


h = unreal.register_slate_post_tick_callback(tick)
