"""11-02 认 K 系列素材包（kind=gamepy，任意地图，比如 "/Game/Maps/L_Range"）：Inbox 里文件名以 K 开头的 .blend（含子目录 _k51 等之外的原包）
逐个用 Blender 跑 tools/blender_pack_ident.py，结果打到日志 [packid]，然后退出。用户想买正版，要知道每个包对应 Kitbash3D 的哪一套。"""
import glob
import json
import os
import subprocess
import time

import unreal

ROOT = 'D:/RobotSlice'
log = lambda *a: unreal.log_warning('[packid] ' + ' '.join(str(x) for x in a))
cfg = json.load(open(f'{ROOT}/tools/easter_run.json', encoding='utf-8'))
blender = cfg.get('blender') or 'blender'
import re
files = sorted(f for f in glob.glob(f'{ROOT}/Inbox/**/*.blend', recursive=True) if re.match(r'K\d', os.path.basename(f), re.I))
log('找到', len(files), '个 .blend：', ' | '.join(os.path.relpath(f, ROOT) for f in files))
os.makedirs(f'{ROOT}/Saved/PackIdent', exist_ok=True)
for i, f in enumerate(files):
    out = f'{ROOT}/Saved/PackIdent/{i:02d}.json'
    try:
        r = subprocess.run([blender, '-b', f, '-P', f'{ROOT}/tools/blender_pack_ident.py', '--', out], capture_output=True, text=True, timeout=600,
                           encoding='utf-8', errors='replace')
    except Exception as e:
        log(os.path.basename(f), '跑不了', e)
        continue
    if not os.path.exists(out):
        log(os.path.basename(f), '没出结果，blender 尾巴：', (r.stdout or '')[-300:].replace('\n', ' / '), (r.stderr or '')[-300:].replace('\n', ' / '))
        continue
    d = json.load(open(out, encoding='utf-8'))
    log('==', os.path.relpath(f, ROOT), f'{os.path.getsize(f) / 1e6:.0f} MB', '物体', d['n_objects'], '材质', d['n_materials'])
    log('   材质前缀', d['material_prefixes'])
    log('   贴图前缀', d['image_prefixes'])
    log('   集合', d['collections'][:12])
    log('   物体', d['objects_sample'][:8])
t_end = time.time()


def tick(dt):
    if time.time() - t_end > 2.0:
        unreal.unregister_slate_post_tick_callback(h)
        unreal.SystemLibrary.execute_console_command(None, 'quit')


h = unreal.register_slate_post_tick_callback(tick)
