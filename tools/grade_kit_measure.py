"""11-02 去组件（kind=gamepy，任意地图）：① Blender 量 11 个组件零件（tools/blender_kit_measure.py）→ [kitm]；
② 列出新纪 / 相位脚本里每处 kit_span / kit_place / kit_coilover 调用（哪件、什么参数）→ [kituse]；③ ds_render.py 的命令行参数 → [kituse]。只读。"""
import glob
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


os.makedirs(f'{ROOT}/Saved/KitFree', exist_ok=True)
out = f'{ROOT}/Saved/KitFree/measure.json'
r = subprocess.run([blender, '-b', '--factory-startup', '-P', f'{ROOT}/tools/blender_kit_measure.py', '--', out], capture_output=True, text=True,
                   timeout=900, encoding='utf-8', errors='replace')
if os.path.exists(out):
    d = json.load(open(out, encoding='utf-8'))
    for name, e in d.items():
        if 'error' in e:
            log('kitm', name, '错误', e['error'])
            continue
        log('kitm', '==', name, e['lib'], '尺寸 uvw', e['size_uvw'], '三角', e['tris'], '材质', e['materials'], '缩放', e['obj_scale'])
        for row in e['profile']:
            log('kitm', name, 'p', row)
else:
    log('kitm', '没出结果', (r.stderr or r.stdout or '')[-400:].replace('\n', ' / '))
pat = re.compile(r'kit_span\(|kit_place\(|kit_coilover\(|kit_source\(|Hydraulics_|Pipes_|hutch_|BodyKit_')
for f in sorted(glob.glob(f'{ROOT}/lookdev/factions/dingshan/**/*.py', recursive=True)) + sorted(glob.glob(f'{ROOT}/lookdev/factions/baita/*.py')) + \
        sorted(glob.glob(f'{ROOT}/lookdev/factions/heitie/*.py')):
    lines = open(f, encoding='utf-8', errors='replace').read().splitlines()
    hits = [(i + 1, l.strip()) for i, l in enumerate(lines) if pat.search(l) and not l.strip().startswith('#') and 'def kit' not in l]
    if hits:
        log('kituse', '####', os.path.relpath(f, ROOT), len(hits), '处')
        for n, l in hits[:40]:
            log('kituse', f'{n}: {l[:230]}')
p = f'{ROOT}/lookdev/factions/dingshan/ds_render.py'
if os.path.exists(p):
    for i, l in enumerate(open(p, encoding='utf-8', errors='replace').read().splitlines()):
        if 'add_argument' in l or 'ARGS.' in l and 'blend' in l:
            log('kituse', f'ds_render {i + 1}: {l.strip()[:220]}')
t_end = time.time()


def tick(dt):
    if time.time() - t_end > 2.0:
        unreal.unregister_slate_post_tick_callback(h)
        unreal.SystemLibrary.execute_console_command(None, 'quit')


h = unreal.register_slate_post_tick_callback(tick)
