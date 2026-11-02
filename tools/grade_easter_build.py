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
    """找 blender.exe：easter_run.json 的 blender → PATH → 注册表（.blend 的打开方式）→ 常见安装目录 → Steam → C/D/E 盘前三层目录"""
    if CFG.get('blender') and os.path.exists(CFG['blender']):
        return CFG['blender']
    b = shutil.which('blender')
    if b:
        return b
    try:
        import winreg
        for root, key in ((winreg.HKEY_CLASSES_ROOT, r'blendfile\shell\open\command'), (winreg.HKEY_CLASSES_ROOT, r'Applications\blender.exe\shell\open\command'),
                          (winreg.HKEY_CURRENT_USER, r'Software\Classes\blendfile\shell\open\command'),
                          (winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\blender.exe')):
            try:
                with winreg.OpenKey(root, key) as k:
                    v = winreg.QueryValue(k, None)
                    exe = v.split('"')[1] if v.startswith('"') else v.split(' ')[0]
                    if exe.lower().endswith('blender.exe') and os.path.exists(exe):
                        log('注册表里找到', exe)
                        return exe
            except OSError:
                pass
    except Exception as e:
        log('读注册表失败', e)
    pats = []
    for drive in ('C:', 'D:', 'E:', 'F:'):
        for pf in ('Program Files', 'Program Files (x86)'):
            pats.append(f'{drive}/{pf}/Blender Foundation/*/blender.exe')
            pats.append(f'{drive}/{pf}/Steam/steamapps/common/Blender/blender.exe')
        pats += [f'{drive}/SteamLibrary/steamapps/common/Blender/blender.exe', f'{drive}/Blender*/blender.exe', f'{drive}/*/Blender*/blender.exe',
                 f'{drive}/*/Blender Foundation/*/blender.exe']
    lad = os.environ.get('LOCALAPPDATA', '')
    if lad:
        pats.append(f'{lad}/Programs/Blender Foundation/*/blender.exe')
    c = []
    for p in pats:
        c += glob.glob(p)
    if not c:
        # 最后一招：各盘前三层目录里找（跳过 Windows 和回收站）
        for drive in ('C:/', 'D:/', 'E:/'):
            if not os.path.exists(drive):
                continue
            for dirpath, dirnames, filenames in os.walk(drive):
                depth = dirpath[len(drive):].count(os.sep) + dirpath[len(drive):].count('/')
                dirnames[:] = [d for d in dirnames if d not in ('Windows', '$Recycle.Bin', 'ProgramData', 'System Volume Information') and not d.startswith('.')]
                if depth >= 3:
                    dirnames[:] = []
                if 'blender.exe' in filenames:
                    c.append(os.path.join(dirpath, 'blender.exe'))
    c = sorted(set(x.replace('\\', '/') for x in c))
    log('找到的 blender.exe 候选', c)
    return c[-1] if c else None


BL = find_blender()
PY = shutil.which('py')
log('blender =', BL, '；py =', PY)
ok = True
for step in CFG['steps']:
    kind, script, args = step[0], step[1], step[2:]
    if kind == 'copy':
        # 把 tools 下的底子复制到 Inbox（Inbox 不同步）
        src, dst_dir = f'{ROOT}/{script}', f'{ROOT}/{args[0]}'
        os.makedirs(dst_dir, exist_ok=True)
        for f in glob.glob(f'{src}/*'):
            if os.path.isfile(f):
                shutil.copy(f, f'{dst_dir}/{os.path.basename(f)}')
        log('复制', script, '→', args[0], os.listdir(dst_dir))
        continue
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
