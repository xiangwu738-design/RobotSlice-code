"""11-02 读本地文件里和关键词有关的行（kind=py，只读）：环境变量没法传参，所以要查的文件和关键词写在下面 TARGETS。标签 [src]。"""
import os
import re

import unreal

ROOT = 'D:/RobotSlice'
TARGETS = [
    ('lookdev/rig/factions/export_factions.py', r'smooth|sharp|normal|Normal|bevel|Bevel|decimat|Decimat|WEIGHTED|weighted|mesh_smooth_type|use_auto|split|auto_smooth|harden|segments|def |dingshan|NEC'),
    ('lookdev/factions/dingshan/ds_render.py', r'def game_tris|segments|smooth|sharp|normal|WEIGHTED|bevel'),
    ('lookdev/factions/dingshan/ds_common.py', r'smooth|sharp|WEIGHTED|weighted|harden|Bevel|bevel\(|segments|def box|def cyl|def tube|def prism'),
]
log = lambda *a: unreal.log_warning('[src] ' + ' '.join(str(x) for x in a))
for path, pat in TARGETS:
    p = f'{ROOT}/{path}'
    if not os.path.exists(p):
        log('没有', path)
        continue
    lines = open(p, encoding='utf-8', errors='replace').read().splitlines()
    log('####', path, len(lines), '行')
    n = 0
    for i, l in enumerate(lines):
        if re.search(pat, l) and n < 160:
            log(f'{i + 1:5d}| {l.rstrip()[:230]}')
            n += 1
