import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
"""数一局全 AI 对战里的落水：py -3 -I tools/pillars_ai_stats.py 日志路径
读 [water]（AMechGameMode::WaterTick）、[battle] / [dm] 的击毁、[wateraistat]。"""
import re
import sys

path = sys.argv[1]
text = open(path, encoding='utf-8', errors='replace').read().splitlines()
drown = [l for l in text if '落水损毁' in l and '[water]' in l]
credited = [l for l in drown if not l.rstrip().endswith('场地')]
kills = [l for l in text if '[battle]' in l and '击毁了' in l]
dm_kills = [l for l in text if '[dm]' in l and '被' in l and '击毁' in l]
times = [l for l in text if '比赛结束' in l or '[match]' in l or '到达目标' in l or '时间到' in l]
fall = [l for l in text if '掉出地图' in l]
fatal = [l for l in text if 'Fatal error' in l or 'Assertion failed' in l]
usage = [l for l in text if 'missing usage flag' in l.lower() or 'Default Material will be used' in l]
print('总阵亡', len(kills), '其中落水', len(drown), '（算给别人的', len(credited), '，算场地的', len(drown) - len(credited), '）')
print('掉出地图', len(fall), '崩溃', len(fatal), '材质用途缺失', len(usage))
stats = {}
for l in text:
    m = re.search(r'\[wateraistat\] (\S+) 起飞 (\d+) 绕开 (\d+) 回航 (\d+) 紧急 (\d+)', l)
    if m:
        s = stats.setdefault('sum', [0, 0, 0, 0])
        for i in range(4):
            s[i] += int(m.group(i + 2))
if stats:
    print('AI 动作合计 起飞 / 绕开帧 / 回航 / 紧急帧', stats['sum'])
for l in drown[:14]:
    print(l[l.find('[water]'):][:170])
for l in times[-3:]:
    print(l[30:190])
print('最后几行战况：')
for l in (kills + dm_kills)[-5:]:
    print(l[30:150])
for l in fatal[:3]:
    print(l[:200])
