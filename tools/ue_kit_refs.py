"""11-02 机体用了哪些外部素材（kind=py，只读）：扫 lookdev 下所有 .py，找引用外部 .blend / .fbx / 组件库 / 素材包的地方，
按文件列出引用了什么（路径字符串、append / link 调用、kit / 组件 / 液压 关键词附近的行）。标签 [kitref]。"""
import glob
import os
import re

import unreal

ROOT = 'D:/RobotSlice'
log = lambda *a: unreal.log_warning('[kitref] ' + ' '.join(str(x) for x in a))
pat = re.compile(r'(?i)(\.blend|\.fbx|\.obj|\.glb|Inbox|组件|零件|机甲\d?|素材包|kit\b|--kit|libraries\.load|append|link\(|Hydraulic|液压|asset)')
files = sorted(glob.glob(f'{ROOT}/lookdev/**/*.py', recursive=True))
log('lookdev 里', len(files), '个 .py')
for f in files:
    try:
        lines = open(f, encoding='utf-8', errors='replace').read().splitlines()
    except OSError:
        continue
    hits = [(i + 1, l.strip()) for i, l in enumerate(lines) if pat.search(l) and not l.strip().startswith('#')]
    if not hits:
        continue
    log('==', os.path.relpath(f, ROOT), len(lines), '行，相关', len(hits), '行')
    seen = set()
    for n, l in hits:
        k = l[:120]
        if k in seen:
            continue
        seen.add(k)
        log(f'   {n}: {l[:200]}')
        if len(seen) >= 14:
            break
