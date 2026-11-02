"""11-02 机体零件来源摸底（kind=gamepy，任意地图，比如 "/Game/Maps/L_Range"）：lookdev 下所有 .blend（阵营机体设计稿、绑骨稿、v3、肩部装备）
逐个用 Blender 跑 tools/blender_mech_survey.py，摘要打到 [mechsurvey]，然后退出。只读。另外列出 lookdev / Inbox 里文件名像组件库的 .blend。"""
import glob
import json
import os
import re
import subprocess
import time

import unreal

ROOT = 'D:/RobotSlice'
log = lambda *a: unreal.log_warning('[mechsurvey] ' + ' '.join(str(x) for x in a))
cfg = json.load(open(f'{ROOT}/tools/easter_run.json', encoding='utf-8'))
blender = cfg.get('blender') or 'blender'
files = sorted(glob.glob(f'{ROOT}/lookdev/**/*.blend', recursive=True))
files = [f for f in files if not re.search(r'(?i)backup|\.blend\d|_old|归档', f)]
log('lookdev 里', len(files), '个 .blend')
kits = [f for top in ('Inbox', 'lookdev', 'SourceAssets') for f in glob.glob(f'{ROOT}/{top}/**/*.blend', recursive=True)
        if re.search(r'(?i)机甲|零件|组件|kit|mech', os.path.basename(f))]
for k in kits[:30]:
    log('像组件库的文件', os.path.relpath(k, ROOT), f'{os.path.getsize(k) / 1e6:.0f} MB')
os.makedirs(f'{ROOT}/Saved/MechSurvey', exist_ok=True)
for i, f in enumerate(files[:60]):
    out = f'{ROOT}/Saved/MechSurvey/{i:02d}.json'
    try:
        r = subprocess.run([blender, '-b', '--factory-startup', f, '-P', f'{ROOT}/tools/blender_mech_survey.py', '--', out], capture_output=True, text=True,
                           timeout=300, encoding='utf-8', errors='replace')
    except Exception as e:
        log(os.path.relpath(f, ROOT), '跑不了', e)
        continue
    if not os.path.exists(out):
        log(os.path.relpath(f, ROOT), '没出结果', (r.stderr or r.stdout or '')[-200:].replace('\n', ' / '))
        continue
    d = json.load(open(out, encoding='utf-8'))
    log('==', os.path.relpath(f, ROOT), f'{os.path.getsize(f) / 1e6:.0f} MB', '网格物体', d['n_mesh_objects'], '三角面', d['total_tris'],
        '带资产元数据', d['n_asset_objects'])
    if d['libraries']:
        log('   链接的库', d['libraries'][:8])
    log('   物体前缀(名, 个数, 面数)', d['object_prefixes'][:12])
    log('   网格前缀', d['mesh_prefixes'][:8])
    log('   材质前缀', d['material_prefixes'][:8])
    if d['custom_props']:
        log('   自定义属性', d['custom_props'][:12])
    if d['asset_objects']:
        log('   资产物体', d['asset_objects'][:10])
t_end = time.time()


def tick(dt):
    if time.time() - t_end > 2.0:
        unreal.unregister_slate_post_tick_callback(h)
        unreal.SystemLibrary.execute_console_command(None, 'quit')


h = unreal.register_slate_post_tick_callback(tick)
