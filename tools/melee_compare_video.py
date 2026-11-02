"""刀剑手感改前改后并排慢放视频（10-31）：把 -MechMeleeFeelTest -MechMeleeFeelVideo 两次跑的逐帧截图（Saved/Screenshots/MeleeFeel/<标签>/00000.png…，
配 -benchmark -fps=30 两次的帧号一一对应）左右拼起来，每帧标上刀名、连招第几段、这一段的出招时间，用 ffmpeg 按 10 帧/秒合成（= 三分之一速慢放）。
py -3 tools/melee_compare_video.py <左标签> <右标签> <输出.mp4> [--blades 1,4,6] [--fps 10] [--crop x0,y0,x1,y1] [--sheet 输出.jpg 起帧 止帧]
左右两边的 CSV 是 Saved/MeleeFeel/<标签>.csv（取帧号、刀、段号用）。ffmpeg 用 PATH 里的或 winget 装的那个。"""
import csv
import glob
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NAMES = ['双手刀', '快刀', '单手刀', '大剑', '双刀', '钝器', '匕首']
args = sys.argv[1:]


def opt(name, default=None):
    if name in args:
        i = args.index(name)
        v = args[i + 1]
        del args[i:i + 2]
        return v
    return default


blades = opt('--blades')
fps = float(opt('--fps', '10'))
crop = opt('--crop')
sheet = None
if '--sheet' in args:
    i = args.index('--sheet')
    sheet = (args[i + 1], int(args[i + 2]), int(args[i + 3]))
    del args[i:i + 4]
left, right, out = args[0], args[1], args[2]
only = {int(x) for x in blades.split(',')} if blades else None
crop = tuple(int(x) for x in crop.split(',')) if crop else None


def rows(tag):
    return {int(r['frame']): r for r in csv.DictReader(open(os.path.join(ROOT, 'Saved', 'MeleeFeel', tag + '.csv'), encoding='utf-8-sig'))}


L, R = rows(left), rows(right)
shots = os.path.join(ROOT, 'Saved', 'Screenshots', 'MeleeFeel')
font = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 24)
small = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 20)


def label(r):
    if not r:
        return ''
    move = int(r['move'])
    seg = '重斩' if move == 5 else ('第 %s 段' % r['step'] if move > 0 else '待机')
    return '%s  %s  出招 %.3f 秒' % (NAMES[int(r['blade'])], seg, float(r['melee_t']))


def frame(i):
    a = os.path.join(shots, left, '%05d.png' % i)
    b = os.path.join(shots, right, '%05d.png' % i)
    if not (os.path.isfile(a) and os.path.isfile(b)):
        return None
    ims = []
    for p, tag, r in ((a, left, L.get(i)), (b, right, R.get(i))):
        im = Image.open(p).convert('RGB')
        if crop:
            im = im.crop(crop)
        d = ImageDraw.Draw(im)
        d.rectangle((0, 0, im.width, 64), fill=(0, 0, 0))
        d.text((10, 4), tag, fill=(255, 220, 0) if tag == left else (120, 255, 120), font=font)
        d.text((10, 36), label(r), fill=(255, 255, 255), font=small)
        ims.append(im)
    w, h = ims[0].size
    cat = Image.new('RGB', (w * 2 + 6, h), (40, 40, 40))
    cat.paste(ims[0], (0, 0))
    cat.paste(ims[1], (w + 6, 0))
    return cat


frames = sorted(i for i in L if (only is None or int(L[i]['blade']) in only))
if sheet:
    path, a, b = sheet
    cells = [frame(i) for i in range(a, b + 1)]
    cells = [c for c in cells if c]
    w, h = cells[0].size
    sc = 0.5
    cw, ch = int(w * sc), int(h * sc)
    img = Image.new('RGB', (cw, ch * len(cells)), (20, 20, 20))
    for k, c in enumerate(cells):
        img.paste(c.resize((cw, ch)), (0, k * ch))
    img.save(path, quality=86)
    print('[video] 拼图', path, img.size)
tmp = tempfile.mkdtemp(prefix='meleecmp_')
n = 0
for i in frames:
    c = frame(i)
    if c is None:
        continue
    if c.width % 2:
        c = c.crop((0, 0, c.width - 1, c.height))
    if c.height % 2:
        c = c.crop((0, 0, c.width, c.height - 1))
    c.save(os.path.join(tmp, '%05d.png' % n))
    n += 1
ff = shutil.which('ffmpeg') or next(iter(glob.glob(os.path.expandvars(r'%LOCALAPPDATA%\Microsoft\WinGet\Packages\Gyan.FFmpeg*\*\bin\ffmpeg.exe'))), None)
subprocess.run([ff, '-y', '-loglevel', 'error', '-framerate', str(fps), '-i', os.path.join(tmp, '%05d.png'), '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', '20', out], check=True)
shutil.rmtree(tmp, ignore_errors=True)
print('[video] %d 帧 → %s（%.0f 帧/秒播放）' % (n, out, fps))
