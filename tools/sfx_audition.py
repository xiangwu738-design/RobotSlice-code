"""把几条候选音效接成一个试听文件（每条压到同样响、最长 N 秒、中间空 0.8 秒），旁边写一份时间表。
py -3.11 -I sfx_audition.py 输出.wav 最长秒数 音效根目录 标签=相对路径 ..."""
import os, subprocess, sys, json

out, max_len, root = sys.argv[1], float(sys.argv[2]), sys.argv[3]
items = [a.split('=', 1) for a in sys.argv[4:]]
tmp = os.path.join(os.path.dirname(os.path.abspath(out)), '_tmp_audition')
os.makedirs(tmp, exist_ok=True)
parts, lines, t = [], [], 0.0
for n, (label, rel) in enumerate(items):
    src = os.path.join(root, rel)
    p = os.path.join(tmp, f'{n:02d}.wav')
    # 峰值压到 -3 dB，响度大致对齐；超长的淡出截断
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', src, '-t', str(max_len),
                    '-af', f'loudnorm=I=-18:TP=-3:LRA=11,afade=t=out:st={max_len - 0.4}:d=0.4,apad=pad_dur=0.8',
                    '-ar', '48000', '-ac', '2', p], check=True)
    probe = subprocess.run(['ffprobe', '-v', 'error', '-show_entries', 'format=duration', '-of', 'json', p], capture_output=True, text=True)
    dur = float(json.loads(probe.stdout)['format']['duration'])
    lines.append(f'{int(t // 60)}:{t % 60:04.1f}  {label}  ←  {rel}')
    parts.append(p)
    t += dur
lst = os.path.join(tmp, 'list.txt')
open(lst, 'w', encoding='utf-8').write(''.join(f"file '{p}'\n" for p in parts))
subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 'concat', '-safe', '0', '-i', lst, '-c', 'copy', out], check=True)
open(os.path.splitext(out)[0] + '.txt', 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
print('\n'.join(lines))
