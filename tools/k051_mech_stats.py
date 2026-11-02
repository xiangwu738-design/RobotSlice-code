"""相位电子园区自动对战日志统计：ue_log_mechs.py 打的 [mechs] 行 → 机体高度分布（地面 / 桥面和基地台 / 更高）、
有没有出活动范围（224×160 米）或掉到地面以下、各区域停留次数；外加 [battle] 击毁行数。
py -3 -I tools/k051_mech_stats.py Saved/Logs/TechParkBattle.log"""
import collections
import re
import sys

HX, HY = 112.0, 80.0
rows = open(sys.argv[1], encoding='utf-8', errors='ignore').read().splitlines()
pts = []
for line in rows:
    if '[mechs]' not in line:
        continue
    for m in re.finditer(r'\) (-?\d+),(-?\d+),(-?\d+) v([\d.]+)', line):
        x, y, z, v = int(m.group(1)), -int(m.group(2)), int(m.group(3)), float(m.group(4))   # UE y → 布局图 y（北为正）
        pts.append((x, y, z, v))
kills = sum(1 for l in rows if '[battle]' in l and '击毁了' in l)
rounds = [l.split('[battle]')[1].strip() for l in rows if '[battle]' in l and ('回合结束' in l or '整场' in l or '获胜' in l)]
if not pts:
    print('[stats] 没有 [mechs] 行')
    sys.exit()
band = collections.Counter()
for x, y, z, v in pts:

    band['地面 (<4 米)' if z < 4 else '桥面 / 基地台 / 矮屋顶 (4–12 米)' if z < 12 else '高处 (12–25 米)' if z < 25 else '25 米以上'] += 1
area = collections.Counter()
for x, y, z, v in pts:
    a = '西基地' if x < -62 and abs(y) < 24 else '东基地' if x > 62 and abs(y) < 24 else '广场内' if abs(x) < 39 and abs(y) < 43 else \
        '北边带' if y > 55 else '南边带' if y < -55 else '东侧' if x > 0 else '西侧'
    area[a] += 1
out = [p for p in pts if abs(p[0]) > HX + 1 or abs(p[1]) > HY + 1]
low = [p for p in pts if p[2] < -2]
still = sum(1 for p in pts if p[3] < 0.5)
print(f'[stats] 位置采样 {len(pts)} 个，击毁 {kills} 次；速度 <0.5 米/秒的采样 {still} 个（{100 * still / len(pts):.0f}%，含阵亡等重生）')
print('[stats] 高度：' + '，'.join(f'{k} {v}（{100 * v / len(pts):.0f}%）' for k, v in band.most_common()))
print('[stats] 区域：' + '，'.join(f'{k} {v}' for k, v in area.most_common()))
print(f'[stats] 出活动范围 {len(out)} 个，低于地面 {len(low)} 个', out[:5], low[:5])
for r in rounds[-6:]:
    print('[stats]', r[:120])
