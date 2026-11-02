"""月球基地手感实测统计：从日志里的 [flight] 行切出每一段离地（竖直速度 ≠ 0 的连续段），打印滞空时间、升高、落地速度、水平飞行距离。py -3 -I tools/moon_flight_stats.py 日志"""
import re
import sys

rows = []
for line in open(sys.argv[1], encoding='utf-8', errors='ignore'):
    m = re.search(r'\[flight\] ([\d.]+) (-?[\d.]+) (-?[\d.]+) (-?[\d.]+) ([\d.]+) (-?[\d.]+)', line)
    if m:
        rows.append(tuple(float(x) for x in m.groups()))
    elif '[pilot]' in line and ('跳跃' in line or '闪避' in line or '冲刺' in line):
        print(line[line.index('[pilot]'):].strip()[:120])
    elif '[perf]' in line and '帧/秒' in line:
        last = line[line.index('[perf]'):].strip()
print('采样', len(rows))
air = None
segs = []
for r in rows:
    t, x, y, z, hs, vz = r
    if air is None:
        if abs(vz) > 0.4:
            air = [r]
    else:
        air.append(r)
        if abs(vz) < 0.05 and abs(air[-2][5]) < 0.05:
            segs.append(air)
            air = None
for s in segs:
    t0, t1 = s[0][0], s[-1][0]
    if t1 - t0 < 0.4:
        continue
    z0 = s[0][3]
    top = max(r[3] for r in s)
    land = min(r[5] for r in s[-6:])
    dist = sum(((a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2) ** 0.5 for a, b in zip(s, s[1:]))
    print(f'离地 {t0:6.1f} 秒起 滞空 {t1 - t0:4.1f} 秒 升高 {top - z0:5.1f} 米 最大上升 {max(r[5] for r in s):4.1f} 米/秒 落地前 {land:5.1f} 米/秒 水平飞 {dist:5.1f} 米 最高水平速度 {max(r[4] for r in s):4.1f}')
