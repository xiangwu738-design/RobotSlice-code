"""11-02 云端看楼型用（只读）：把 Inbox/_k62/export/buildings.json 里每栋楼的尺寸打到日志（[k62cat]），
再把已有的 K62 缩略图拼图（Inbox/_k62、docs/苏联板楼_20261102 下的 jpg/png）复制到 Saved/Screenshots/K62Cat，让本地测试流程收回去。不改任何资源。
本地测试流程 kind=py 跑。
"""
import glob
import json
import os
import shutil

import unreal

log = lambda *a: unreal.log_warning('[k62cat] ' + ' '.join(str(x) for x in a))
ROOT = 'D:/RobotSlice'
try:
    B = json.load(open(f'{ROOT}/Inbox/_k62/export/buildings.json', encoding='utf-8'))['buildings']
    for k in sorted(B):
        v = B[k]
        mn, mx = v.get('min', [0, 0, 0]), v.get('max', [0, 0, 0])
        extra = {kk: vv for kk, vv in v.items() if kk not in ('min', 'max') and not isinstance(vv, (list, dict))}
        log(k, '宽 %.1f 深 %.1f 高 %.1f' % (mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]), json.dumps(extra, ensure_ascii=False)[:200])
except Exception as e:
    log('读 buildings.json 失败', e)
cands = []
for pat in ('Inbox/_k62/*.jpg', 'Inbox/_k62/*.png', 'Inbox/_k62/*/*sheet*.jpg', 'Inbox/_k62/*/*sheet*.png', 'Inbox/_k62/*/*_SM*.jpg', 'Inbox/_k62/*/*_MD*.jpg', 'Inbox/_k62/*/*_LG*.jpg',
            'docs/苏联板楼_20261102/*.jpg', 'docs/苏联板楼_20261102/*.png', 'docs/*K62*.jpg', 'docs/*k62*.jpg'):
    cands += glob.glob(f'{ROOT}/{pat}')
seen = []
for c in cands:
    c = c.replace('\\', '/')
    if c not in seen:
        seen.append(c)
for c in seen:
    log('图', c, os.path.getsize(c))
out = f'{ROOT}/Saved/Screenshots/K62Cat'
os.makedirs(out, exist_ok=True)
pick = [c for c in seen if any(s in os.path.basename(c) for s in ('SM', 'MD', 'LG', 'sheet', '楼', '总览', '布局'))][:12]
for c in pick:
    shutil.copy(c, f'{out}/{os.path.basename(c)}')
    log('复制', c)
log('完成，共', len(seen), '张候选，复制', len(pick))
