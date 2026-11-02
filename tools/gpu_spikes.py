"""统计 Insights 导出的 GPU 帧和 Nanite 流送耗时：py -3 tools/gpu_spikes.py <导出的 csv>"""
import csv
import sys

rows = list(csv.DictReader(open(sys.argv[1], encoding='utf-8', errors='replace')))
gfx = [r for r in rows if r['ThreadName'].endswith('Graphics0')]
frames = sorted(float(r['Duration']) * 1000 for r in gfx if r['TimerName'] == 'Frame' and r['Depth'] == '0')
nanite = sorted(float(r['Duration']) * 1000 for r in gfx if r['TimerName'] == 'Nanite::Streaming')
p = lambda a, q: a[min(len(a) - 1, int(len(a) * q))]
print(f'GPU 帧 {len(frames)}：中位 {p(frames, .5):.1f} 毫秒，99% {p(frames, .99):.1f}，最慢 {frames[-1]:.1f}，超过 30 毫秒 {sum(f > 30 for f in frames)} 帧')
print(f'Nanite 流送：中位 {p(nanite, .5):.2f} 毫秒，99% {p(nanite, .99):.2f}，最慢 {nanite[-1]:.1f}，超过 5 毫秒 {sum(n > 5 for n in nanite)} 次')
