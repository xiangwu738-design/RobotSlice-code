"""AI 用高处的统计（城区 / 相位电子园区自动对战日志）：读 MechAIController 的 -MechAIStats 输出
（[aistat] 每台 10 秒一行累计、卡住 / 打转事件，[aipos] 每 2.5 秒位置），加上 GameMode 的「掉出地图」「比赛结束」。
py -3 -I tools/ai_high_stats.py 日志1 [日志2 ...]        每个日志一行，最后一行是合计
输出：高处时间占比（脚比街面高 5 米以上，含喷在空中；括号里是站着的）、卡住 / 打转 / 掉出次数、对局有没有正常结束、
去高点次数（到达 / 放弃）、找路追人次数（追丢）、区域分布（园区：广场内 / 东西两侧 / 基地 / 南北边带；城区：大道 / 六个街区）。
"""
import collections
import re
import sys

CUM = re.compile(r'\[aistat\] (\S+) 累计：存活 (\d+) 秒 高处 (\d+) 秒（站着 (\d+)）卡住 (\d+) 打转 (\d+) 掉出 (\d+) 去高点 (\d+) 到达 (\d+) 放弃 (\d+) '
                 r'找路追 (\d+) 追丢 (\d+) 高处逻辑 (\S+)')
POS = re.compile(r'\[aipos\] (\S+) (-?\d+) (-?\d+) (-?[\d.]+) (\d)')


def area_tp(x, y):
    if x < -62 and abs(y) < 24:
        return '西基地'
    if x > 62 and abs(y) < 24:
        return '东基地'
    if abs(x) < 39 and abs(y) < 43:
        return '广场内'
    if y > 55:
        return '北边带'
    if y < -55:
        return '南边带'
    return '东侧' if x > 0 else '西侧'


def area_city(x, y):
    col = 'W' if x < -45 else ('E' if x > 45 else 'M')
    row = 'N' if y > 13 else ('S' if y < -13 else '大道')
    return row if row == '大道' else col + row


def one(path):
    rows = open(path, encoding='utf-8', errors='ignore').read().splitlines()
    text = '\n'.join(rows[:4000])
    tp = 'L_TechPark' in text or 'TechPark' in path
    last = {}
    events = collections.Counter()
    area = collections.Counter()
    hbands = collections.Counter()
    npos = 0
    for line in rows:
        m = CUM.search(line)
        if m:
            last[m.group(1)] = [int(v) for v in m.groups()[1:12]] + [m.group(13)]
            continue
        if '[aistat]' in line and '卡住：' in line:
            events['卡住'] += 1
        if '[aistat]' in line and '原地打转' in line:
            events['打转'] += 1
        m = POS.search(line)
        if m:
            x, y, h = int(m.group(2)), -int(m.group(3)), float(m.group(4))
            area[area_tp(x, y) if tp else area_city(x, y)] += 1
            hbands['街面' if h < 5 else '5–12 米' if h < 12 else '12–25 米' if h < 25 else '25 米以上'] += 1
            npos += 1
    fell = sum(1 for l in rows if '掉出地图' in l)
    end = [l.split('[battle]')[1].strip() for l in rows if '[battle]' in l and '比赛结束' in l]
    built = [l.split('[high]')[1].strip() for l in rows if '[high] 高处图建好' in l]
    tot = [sum(v[i] for v in last.values()) for i in range(11)]
    alive = max(1, tot[0])
    mode = next(iter(last.values()))[11] if last else '?'
    return {
        'name': path.replace('\\', '/').split('/')[-1], 'map': '园区' if tp else '城区', 'mode': mode, 'alive': tot[0], 'high': tot[1], 'stand': tot[2],
        'stuck': tot[3], 'spin': tot[4], 'fall_ai': tot[5], 'fall_gm': fell, 'goup': tot[6], 'arrive': tot[7], 'fail': tot[8],
        'chase': tot[9], 'chasefail': tot[10], 'end': end[-1] if end else '没结束', 'area': area, 'npos': npos, 'hbands': hbands,
        'built': built[-1][:120] if built else '', 'pct': 100.0 * tot[1] / alive, 'pct_stand': 100.0 * tot[2] / alive}


def show(r):
    ar = '，'.join(f'{k} {100 * v / max(1, r["npos"]):.0f}%' for k, v in r['area'].most_common())
    hb = '，'.join(f'{k} {100 * v / max(1, r["npos"]):.0f}%' for k, v in r['hbands'].most_common())
    print(f'[stats] {r["name"]}（{r["map"]}，高处逻辑 {r["mode"]}）：存活 {r["alive"]} 机体秒，高处 {r["pct"]:.1f}%（站着 {r["pct_stand"]:.1f}%）；'
          f'卡住 {r["stuck"]}、打转 {r["spin"]}、掉出 {max(r["fall_ai"], r["fall_gm"])}；去高点 {r["goup"]}（到 {r["arrive"]}、放弃 {r["fail"]}），'
          f'找路追 {r["chase"]}（追丢 {r["chasefail"]}）；结束：{r["end"]}')
    print(f'[stats]    高度：{hb}')
    print(f'[stats]    区域：{ar}')


res = [one(p) for p in sys.argv[1:]]
for r in res:
    show(r)
if len(res) > 1:
    for mode in sorted(set(r['mode'] for r in res)):
        for mp in sorted(set(r['map'] for r in res)):
            g = [r for r in res if r['mode'] == mode and r['map'] == mp]
            if not g:
                continue
            alive = max(1, sum(r['alive'] for r in g))
            area = sum((r['area'] for r in g), collections.Counter())
            npos = max(1, sum(r['npos'] for r in g))
            print(f'[stats] 合计 {mp} 高处逻辑 {mode}（{len(g)} 局）：高处 {100 * sum(r["high"] for r in g) / alive:.1f}%（站着 {100 * sum(r["stand"] for r in g) / alive:.1f}%），'
                  f'卡住 {sum(r["stuck"] for r in g)}、打转 {sum(r["spin"] for r in g)}、掉出 {sum(max(r["fall_ai"], r["fall_gm"]) for r in g)}，'
                  f'正常结束 {sum(1 for r in g if r["end"] != "没结束")}/{len(g)}；区域 ' + '，'.join(f'{k} {100 * v / npos:.0f}%' for k, v in area.most_common()))
