"""月球基地：出生点自检（离建筑 / 零件占地矩形多近、互相隔多远、脚下坡度）。py -3 -I tools/moon_check_spawns.py"""
import json
import math

import numpy as np

W = 'D:/RobotSlice/Inbox/_moon_nasa/work'
L = json.load(open(f'{W}/layout_final.json', encoding='utf-8'))
h = np.load(f'{W}/near.npy').astype(np.float64)
N = h.shape[0]
gy, gx = np.gradient(h, 2.0)
sl = np.degrees(np.arctan(np.hypot(gx, gy)))


def foot_dist(it, x, y):
    r = math.radians(it['yaw'])
    dx, dy = x - it['cx'], y - it['cy']
    lx = dx * math.cos(r) + dy * math.sin(r)
    ly = -dx * math.sin(r) + dy * math.cos(r)
    ox = max(abs(lx) - it['w'] / 2, 0)
    oy = max(abs(ly) - it['d'] / 2, 0)
    return math.hypot(ox, oy)


pts = [('队' + t['name'][-1], t['x'], t['y']) for t in L['team']] + [(f'DM{i + 1}', d['x'], d['y']) for i, d in enumerate(L['dm'])]
for name, x, y in pts:
    best = sorted(((foot_dist(it, x, y), it['group']) for it in L['items'] if it['group'] != 'XLG_C' or not name.startswith('DM1')), key=lambda v: v[0])[:2]
    other = min(math.hypot(x - a, y - b) for n, a, b in pts if n != name)
    i, j = int(x / 2 + N // 2), int(y / 2 + N // 2)
    rocks = min((math.hypot(x - r['x'], y - r['y']) - r['s'] / 2) for r in L['rocks'] if r['c'])
    print(f'{name:5s} ({x:6.0f},{y:6.0f}) 最近建筑 {best[0][1]} {best[0][0]:.1f} 米；{best[1][1]} {best[1][0]:.1f} 米；最近别的点 {other:.0f} 米；最近有碰撞的石头 {rocks:.1f} 米；坡度 {sl[j, i]:.1f}°')
