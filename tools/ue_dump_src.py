"""11-02 读本地文件里和关键词有关的行（kind=py，只读）：环境变量没法传参，所以要查的文件和关键词写在下面 TARGETS。标签 [src]。"""
import os
import re

import unreal

ROOT = 'D:/RobotSlice'
TARGETS = [
    ('lookdev/factions/baita/baita_build.py', r'.'),
    ('lookdev/factions/baita/baita_common.py', r'RL\b|RL\[|robot_lookdev|exec\(|def load|^import|^from|SCHEME|scheme'),
    ('lookdev/robot_lookdev.py', r'add_argument|ARGS\.(blend|export|kit|rig)|def main|COIL\[|spring_len|MAT\[|M\[\'|mat_|def mat|materials.new|MI_Robot'),
    ('lookdev/factions/heitie/hx_common.py', r'def init_rig|def chassis|Kit_|kit|SKM_Robot_v3|V3_BLEND|load\('),
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
        if re.search(pat, l) and n < 300 or ('robot_lookdev' in path and 1385 <= i < 1434):
            log(f'{i + 1:5d}| {l.rstrip()[:230]}')
            n += 1
