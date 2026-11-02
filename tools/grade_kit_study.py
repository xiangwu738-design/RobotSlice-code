"""11-02 去组件（kind=gamepy，任意地图）第一步：读机体脚本里加载组件库零件的代码 + 量组件零件尺寸。只读。
① 把 lookdev 里和组件有关的函数 / 入口逐行打到日志 [kitsrc]（每行一条，日志只收单行）；
② 用 Blender 跑 lookdev/kit/kit_inspect.py、kit_profile.py，把输出的 [kit] 行打到 [kitdim]。"""
import json
import os
import re
import subprocess
import time

import unreal

ROOT = 'D:/RobotSlice'
cfg = json.load(open(f'{ROOT}/tools/easter_run.json', encoding='utf-8'))
blender = cfg.get('blender') or 'blender'


def log(tag, *a):
    unreal.log_warning(f'[{tag}] ' + ' '.join(str(x) for x in a))


def dump_defs(path, name_re, ctx_after=70, max_lines=420):
    """文件里名字匹配 name_re 的 def / 顶层赋值，各打 ctx_after 行"""
    p = f'{ROOT}/{path}'
    if not os.path.exists(p):
        log('kitsrc', '没有', path)
        return
    lines = open(p, encoding='utf-8', errors='replace').read().splitlines()
    log('kitsrc', '####', path, len(lines), '行')
    out = 0
    i = 0
    while i < len(lines) and out < max_lines:
        l = lines[i]
        if re.match(r'\s*def\s+(' + name_re + r')\b', l) or re.match(r'(' + name_re + r')\s*=', l):
            ind = len(l) - len(l.lstrip())
            log('kitsrc', f'{path}:{i + 1}')
            j = i
            while j < len(lines) and j < i + ctx_after and out < max_lines:
                if j > i and lines[j].strip() and (len(lines[j]) - len(lines[j].lstrip())) <= ind and re.match(r'\s*(def|class)\s', lines[j]):
                    break
                log('kitsrc', f'{j + 1:5d}| {lines[j][:220]}')
                out += 1
                j += 1
            i = j
        else:
            i += 1


def dump_head(path, n):
    p = f'{ROOT}/{path}'
    if not os.path.exists(p):
        log('kitsrc', '没有', path)
        return
    lines = open(p, encoding='utf-8', errors='replace').read().splitlines()
    log('kitsrc', '####', path, len(lines), '行（前', n, '行）')
    for k, l in enumerate(lines[:n]):
        log('kitsrc', f'{k + 1:5d}| {l[:220]}')


dump_head('lookdev/factions/dingshan/ds_common.py', 30)
dump_defs('lookdev/factions/dingshan/ds_common.py', r'kit\w*|KIT\w*|load_kit\w*|_kit\w*')
dump_head('lookdev/factions/dingshan/flat/flat_run.py', 18)
dump_head('lookdev/factions/dingshan/flat/ds_flat.py', 45)
dump_defs('lookdev/factions/baita/baita_common.py', r'kit\w*|KIT\w*', max_lines=160)
dump_defs('lookdev/robot_lookdev.py', r'kit\w*|KIT\w*|add_kit\w*|load_kit\w*', max_lines=260)
dump_head('lookdev/rig/factions/run_export.py', 40)
for script in ('lookdev/kit/kit_inspect.py', 'lookdev/kit/kit_profile.py'):
    p = f'{ROOT}/{script}'
    if not os.path.exists(p):
        log('kitdim', '没有', script)
        continue
    try:
        r = subprocess.run([blender, '-b', '--factory-startup', '-P', p], capture_output=True, text=True, timeout=600, encoding='utf-8', errors='replace')
        lines = (r.stdout or '').splitlines()
        keep = [l for l in lines if l.startswith('[kit') or 'Hydraulics' in l or 'Pipes' in l or 'hutch' in l or 'BodyKit' in l]
        log('kitdim', '####', script, '输出', len(lines), '行，相关', len(keep), '行')
        for l in keep[:200]:
            log('kitdim', l[:240])
        if r.returncode != 0:
            log('kitdim', '退出码', r.returncode, (r.stderr or '')[-300:].replace('\n', ' / '))
    except Exception as e:
        log('kitdim', script, '跑不了', e)
t_end = time.time()


def tick(dt):
    if time.time() - t_end > 2.0:
        unreal.unregister_slate_post_tick_callback(h)
        unreal.SystemLibrary.execute_console_command(None, 'quit')


h = unreal.register_slate_post_tick_callback(tick)
