"""11-02 读本地文件里和关键词有关的行（kind=py，只读）：环境变量没法传参，所以要查的文件和关键词写在下面 TARGETS。标签 [src]。"""
import os
import re

import unreal

ROOT = 'D:/RobotSlice'
TARGETS = [
    ('lookdev/make_shoulder_tactical.py', r'.'),
    ('lookdev/make_shoulder_kits.py', r'.'),
]
import glob as _g
for _d in ():
    for _f in sorted(_g.glob(f'D:/RobotSlice/{_d}/*')):
        unreal.log_warning(f'[src] 文件 {_d}/{os.path.basename(_f)} {os.path.getsize(_f) // 1024} KB')
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
        if re.search(pat, l) and n < 400 and (('tactical' in path and i < 300) or ('kits' in path and i < 32)):
            log(f'{i + 1:5d}| {l.rstrip()[:230]}')
            n += 1
