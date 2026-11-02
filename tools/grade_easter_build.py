"""11-02 云端驱动彩蛋建模（本地测试流程 kind=gamepy，任意地图，比如 "/Game/Maps/L_Range"）：
按 tools/easter_run.json 依次跑 Blender / 系统 Python 步骤（每步的输出尾巴打到日志 [eastrun]），
把 collect 里的图复制到 Saved/Screenshots/EasterBuild（让测试流程收回），build.json 摘要打到日志，然后退出游戏。
某一步失败就停，后面的不跑。"""
import glob
import json
import os
import shutil
import subprocess
import time

import unreal

ROOT = 'D:/RobotSlice'
log = lambda *a: unreal.log_warning('[eastrun] ' + ' '.join(str(x) for x in a))
CFG = json.load(open(f'{ROOT}/tools/easter_run.json', encoding='utf-8'))


def find_blender():
    b = shutil.which('blender')
    if b:
        return b
    c = sorted(glob.glob('C:/Program Files/Blender Foundation/Blender */blender.exe') + glob.glob('D:/Program Files/Blender Foundation/Blender */blender.exe') +
               glob.glob('D:/Blender*/blender.exe') + glob.glob('C:/Blender*/blender.exe'))
    return c[-1] if c else None


BL = find_blender()
PY = shutil.which('py')
log('blender =', BL, '；py =', PY)
ok = True
for step in CFG['steps']:
    kind, script, args = step[0], step[1], step[2:]
    if kind == 'blender':
        if not BL:
            log('找不到 blender.exe，停')
            ok = False
            break
        cmd = [BL, '-b', '--factory-startup', '-P', f'{ROOT}/{script}', '--'] + args
    else:
        cmd = [PY, '-3', f'{ROOT}/{script}'] + args if PY else ['python', f'{ROOT}/{script}'] + args
    t0 = time.time()
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, encoding='utf-8', errors='replace')
    out = (r.stdout or '').splitlines()
    err = (r.stderr or '').splitlines()
    log(f'== {script} {" ".join(args)} 退出码 {r.returncode}，{time.time() - t0:.0f} 秒')
    for line in out[-40:]:
        log('  |', line)
    for line in err[-15:]:
        log('  ERR |', line)
    if r.returncode != 0:
        ok = False
        break
for f in CFG.get('log_files', []):
    p = f'{ROOT}/{f}'
    if os.path.exists(p):
        txt = open(p, encoding='utf-8', errors='replace').read()
        log(f'== {f}（前 3000 字）')
        for line in txt[:3000].splitlines():
            log('  ', line)
dst = f'{ROOT}/Saved/Screenshots/EasterBuild'
os.makedirs(dst, exist_ok=True)
for i, f in enumerate(CFG.get('collect', [])):
    p = f'{ROOT}/{f}'
    if os.path.exists(p):
        shutil.copy(p, f'{dst}/{i:02d}_{os.path.basename(p)}')
        log('收回', f)
    else:
        log('没有', f)
log('完成' if ok else '中途停了')
t_end = time.time()


def tick(dt):
    if time.time() - t_end > 3.0:
        unreal.unregister_slate_post_tick_callback(h)
        unreal.SystemLibrary.execute_console_command(None, 'quit')


h = unreal.register_slate_post_tick_callback(tick)
