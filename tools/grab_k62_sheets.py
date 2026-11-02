"""11-02 云端看苏联楼型用（只读）：把已有的 K62 缩略图拼图和苏联图的布局 / 总览图复制到 Saved/Screenshots/K62Cat，
让本地测试流程（kind=gamepy，任意地图）收回去，然后退出。不改任何资源。"""
import os
import shutil
import time

import unreal

ROOT = 'D:/RobotSlice'
OUT = f'{ROOT}/Saved/Screenshots/K62Cat'
os.makedirs(OUT, exist_ok=True)
FILES = ['Inbox/_k62/sheet_SM.jpg', 'Inbox/_k62/sheet_MD.jpg', 'Inbox/_k62/sheet_LG.jpg', 'Inbox/_k62/布局.png',
         'docs/苏联板楼_20261102/总览.jpg', 'docs/苏联板楼_20261102/俯视布局.png', 'docs/苏联板楼_20261102/正上方.png']
for i, f in enumerate(FILES):
    src = f'{ROOT}/{f}'
    if os.path.exists(src):
        shutil.copy(src, f'{OUT}/{i:02d}_{os.path.basename(src)}')
        unreal.log_warning(f'[k62grab] 复制 {f}')
    else:
        unreal.log_warning(f'[k62grab] 没有 {f}')
t0 = time.time()


def tick(dt):
    if time.time() - t0 > 4.0:
        unreal.unregister_slate_post_tick_callback(h)
        unreal.SystemLibrary.execute_console_command(None, 'quit')


h = unreal.register_slate_post_tick_callback(tick)
