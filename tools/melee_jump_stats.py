"""刀剑连招段间跳变统计（10-31）：读 -MechMeleeFeelTest 写的 CSV（Saved/MeleeFeel/<名字>.csv），按连招段与段之间的衔接处汇总。
PYTHONIOENCODING=utf-8 py -3 tools/melee_jump_stats.py Saved/MeleeFeel/改前.csv Saved/MeleeFeel/改后.csv [--blades 1,4,6]
每处衔接取「下一段开始前 1 帧到开始后 3 帧」的窗口（都是网格空间、世界尺寸厘米，去掉了机体平移和转身）：
  刀尖位移 = 右手刀刀尖这一帧走了多远（双刀另算左手刀尖），二阶差 = 这一帧和上一帧的位移差（|p − 2p₋₁ + p₋₂|，姿势一跳就大）；
  手腕同理；骨骼 = 22 根骨头每帧转角的最大值、角速度突变（度）。
「段内」= 同一把刀出招期间、衔接窗口以外的帧（挥刀本身有多快，作对照）。"""
import csv
import sys
from collections import defaultdict

NAMES = ['双手刀', '快刀', '单手刀', '大剑', '双刀', '钝器', '匕首']
COLS = [('tipr_d', '刀尖位移'), ('tipr_dd', '刀尖二阶差'), ('tipl_d', '左刀尖位移'), ('tipl_dd', '左刀尖二阶差'), ('handr_d', '右腕位移'), ('handr_dd', '右腕二阶差'),
        ('max_bone_deg', '骨骼转角'), ('max_jerk_deg', '骨骼突变')]
args = [a for a in sys.argv[1:] if not a.startswith('--')]
only = None
if '--blades' in sys.argv:
    only = {int(x) for x in sys.argv[sys.argv.index('--blades') + 1].split(',')}
    args = [a for a in args if a != sys.argv[sys.argv.index('--blades') + 1]]


def load(path):
    rows = list(csv.DictReader(open(path, encoding='utf-8-sig')))
    by = defaultdict(list)
    for r in rows:
        by[int(r['blade'])].append(r)
    return by


def stats(rows, start=False):
    """返回 (衔接窗口里每处的峰值列表, 段内峰值)；start = 只看起手（待机 → 出招第一帧）"""
    junc = []
    marks = []
    for i in range(1, len(rows)):
        a, b = rows[i - 1], rows[i]
        # 下一段开始：连招段号变了（轻击 1→2→3→4、轻击 → 重斩），而且上一帧也在出招
        if not start and int(a['move']) > 0 and int(b['move']) > 0 and (a['step'] != b['step'] or a['move'] != b['move']):
            marks.append(i)
        if start and int(a['move']) == 0 and int(b['move']) > 0:
            marks.append(i)
    inwin = set()
    for i in marks:
        w = range(max(0, i - 1), min(len(rows), i + 4))
        inwin.update(w)
        junc.append({c: max(float(rows[k][c]) for k in w) for c, _ in COLS if c in rows[0]})
    body = {}
    for c, _ in COLS:
        if c not in rows[0]:
            continue
        vals = [float(r[c]) for k, r in enumerate(rows) if k not in inwin and int(r['move']) > 0]
        body[c] = max(vals) if vals else 0.0
    return junc, body


data = [(p, load(p)) for p in args]
blades = sorted({b for _, d in data for b in d})
for b in blades:
    if only and b not in only:
        continue
    print(f'== {NAMES[b] if b < len(NAMES) else b}')
    for p, d in data:
        if b not in d:
            continue
        junc, body = stats(d[b])
        tag = p.replace('\\', '/').split('/')[-1][:-4]
        line = []
        for c, name in COLS:
            if c not in body:
                continue
            if c.startswith('tipl') and b != 4:
                continue
            vals = [j[c] for j in junc]
            line.append(f'{name} 衔接 最大 {max(vals):.0f} 平均 {sum(vals) / len(vals):.0f} / 段内 {body[c]:.0f}' if vals else f'{name} —')
        print(f'  [{tag}] 衔接 {len(junc)} 处：' + '；'.join(line))
        sj, _ = stats(d[b], True)
        if sj:
            print('      起手（待机→出招，%d 处）' % len(sj) + ' '.join(f'{name}{max(j[c] for j in sj):.0f}' for c, name in COLS if c in sj[0] and not (c.startswith('tipl') and b != 4)))
        for k, j in enumerate(junc):
            print(f'      第{k + 1}处 ' + ' '.join(f'{name}{j[c]:.0f}' for c, name in COLS if c in j and not (c.startswith('tipl') and b != 4)))
