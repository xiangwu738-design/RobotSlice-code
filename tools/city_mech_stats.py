"""城区图自动对战日志统计：ue_log_mechs.py 打的 [mechs] 行 → 机体高度分布（地面 / 7–15 米的天桥高架框架 / 更高的屋顶）、
有没有出活动范围（210×180 米）或掉到地面以下、各街区停留次数、移动速度（AI 有没有卡住）；外加击毁行数。
py -3 -I tools/city_mech_stats.py Saved/Logs/CityBattle.log"""
import collections
import re
import sys

HX, HY = 105.0, 90.0
rows = open(sys.argv[1], encoding='utf-8', errors='ignore').read().splitlines()
pts, per = [], collections.defaultdict(list)
for line in rows:
    if '[mechs]' not in line:
        continue
    for m in re.finditer(r'(\w+)\((\w*)\) (-?\d+),(-?\d+),(-?\d+) v([\d.]+)', line):
        name = m.group(1)
        x, y, z, v = int(m.group(3)), -int(m.group(4)), int(m.group(5)), float(m.group(6))   # UE y → 布局图 y（北为正）
        pts.append((x, y, z, v))
        per[name].append((x, y, z, v))
kills = sum(1 for l in rows if '击毁' in l and ('[battle]' in l or '[dm]' in l))
fell = [l for l in rows if '掉出地图' in l]
if not pts:
    print('[stats] 没有 [mechs] 行')
    raise SystemExit
ground = sum(1 for p in pts if p[2] < 4)
mid = sum(1 for p in pts if 4 <= p[2] < 16)
high = sum(1 for p in pts if p[2] >= 16)
out = [p for p in pts if abs(p[0]) > HX + 1 or abs(p[1]) > HY + 1 or p[2] < -2]
print(f'[stats] 采样 {len(pts)} 次（{len(per)} 台）：地面 {ground}，7–15 米（天桥 / 高架 / 框架 / 矮屋顶）{mid}，16 米以上 {high}；'
      f'出界或掉到地下 {len(out)}；最高 {max(p[2] for p in pts)} 米；击毁相关日志 {kills} 行；掉出地图 {len(fell)} 次')


def zone(x, y):
    col = 'W' if x < -45 else ('E' if x > 45 else 'M')
    row = 'N' if y > 13 else ('S' if y < -13 else '大道')
    return row if row == '大道' else col + row


z = collections.Counter(zone(p[0], p[1]) for p in pts)
print('[stats] 区域', dict(z.most_common()))
stuck = []
for name, ps in per.items():
    slow = sum(1 for p in ps if p[3] < 0.5)
    if len(ps) >= 6 and slow / len(ps) > 0.6:
        stuck.append(f'{name} 慢 {slow}/{len(ps)}')
print('[stats] 速度 < 0.5 米/秒超过六成采样的机体（可能卡住）：', stuck or '没有')
for l in fell[:5]:
    print('[stats]', l[l.find('掉出'):][:120])
