"""找动作包里能首尾接上的攻击片段（10-31 连招段间跳变）：读 tools/blade_clip_curves.py 写的轨迹 JSON，对每一对片段 A → B，
在 A 命中之后、离结尾 0.35 秒以前（排除「收回待机」）找和 B 第 0 帧姿势最像的那一刻（骨盆、胸、两手腕、两前臂的位置 + 两只手的朝向，均方差，厘米）。
同一套连招切开的片段（双匕首 Attack1_1 / 1_2 / 1_3、Attack5_1 / 5_2 / 5_3 …）差 0，那一刻就是切点（UMechAnimInstance 的 FChainCut 表）。
另外列出各攻击片段的命中时刻（和 tools/ue_import_locomotion.py 的 find_hits 同一个算法）。
py -3 tools/blade_chain_match.py <轨迹.json> [包名前缀，如 TwinDaggers]"""
import json
import math
import sys

D = json.load(open(sys.argv[1], encoding='utf-8'))
prefix = sys.argv[2] if len(sys.argv) > 2 else ''
FEAT = ['pelvis', 'spine_05', 'hand_r', 'hand_l', 'lowerarm_r', 'lowerarm_l']


def sub(a, b):
    return [a[i] - b[i] for i in range(3)]


def norm(a):
    return math.sqrt(sum(x * x for x in a))


def hits(c):
    F, fps = c['frames'], c['fps']
    sp = [norm(sub(F[i + 1]['hand_r'], F[i]['hand_r'])) * fps for i in range(len(F) - 1)]
    skip = int(0.08 * fps)
    top = max(sp[skip:])
    out = []
    for i in range(max(1, skip), len(sp) - 1):
        if sp[i] >= sp[i - 1] and sp[i] > sp[i + 1] and sp[i] > top * 0.7:
            lo = hi = i
            while lo - 1 >= skip and sp[lo - 1] > sp[i] * 0.5:
                lo -= 1
            while hi + 1 < len(sp) and sp[hi + 1] > sp[i] * 0.5:
                hi += 1
            t = (lo + hi) / 2 / fps
            if not out or t - out[-1] > 0.25:
                out.append(t)
    return out


def feat(c, t):
    F = c['frames']
    fr = F[min(len(F) - 1, max(0, int(round(t * c['fps']))))]
    out = []
    for b in FEAT:
        out += fr[b]
    for b in ('hand_r', 'hand_l'):
        for j in range(3):
            out += [x * 40 for x in fr[b + '_ax'][j]]
    return out


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)) / (len(a) / 3))


keys = [k for k in D if k.split('/')[-1].startswith(prefix) and 'Attack' in k and 'All' not in k]
H = {}
for k in keys:
    h = hits(D[k])
    if h:
        H[k] = h[0]
pairs = []
for a in keys:
    if a not in H:
        continue
    A = D[a]
    ts = [t / 120 for t in range(int(H[a] * 120) + 1, int((A['length'] - 0.35) * 120))]
    fa = [(t, feat(A, t)) for t in ts]
    if not fa:
        continue
    for b in keys:
        if a == b:
            continue
        fb = feat(D[b], 0)
        d, t = min((dist(f, fb), t) for t, f in fa)
        pairs.append((d, a.split('/')[-1], b.split('/')[-1], t, H[a]))
pairs.sort()
print('命中时刻：', {k.split('/')[-1]: round(v, 3) for k, v in sorted(H.items())})
print('最能接上的（姿势差 厘米、A、B、A 的切点 秒、A 的命中 秒）：')
for d, a, b, t, h in pairs[:20]:
    print('  %6.2f  %s → %s  切点 %.3f（命中后 %.3f）' % (d, a, b, t, t - h))
