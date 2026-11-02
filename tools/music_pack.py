"""11-02 音乐包：把任意格式的曲子转成游戏直接读的 wav（48k 立体声 16 位，响度 -16 LUFS，和打进包里的曲子一样响）。
用法（要 PATH 里有 ffmpeg）：
  py -3 tools/music_pack.py "D:\\Music\\某首.mp3" battle_OldStreet        → D:\\RobotSlice\\MusicPack\\battle_OldStreet.wav
  py -3 tools/music_pack.py "D:\\Music\\某首.flac" battle  D:\\RobotSlice_Build\\Windows\\MusicPack   （直接放进打包版）
名字 = <位置> 或 <位置>_<地图名去掉 L_>；位置：menu room draft intro round clutch battle defprep boss1 boss2 results defwin range；
地图：Street Nature TechPark Dreamfield Pillars Soviet Moon OldStreet Range。规则见 MechMusic.cpp「音乐包」和 tools/音乐包说明.txt。"""
import os
import shutil
import subprocess
import sys

SLOTS = 'menu room draft intro round clutch battle defprep boss1 boss2 results defwin range'.split()
MAPS = 'Street Nature TechPark Dreamfield Pillars Soviet Moon OldStreet Range'.split()


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    src, name = sys.argv[1], sys.argv[2]
    out_dir = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'MusicPack')
    slot, _, mp = name.partition('_')
    if slot not in SLOTS or (mp and mp not in MAPS):
        sys.exit(f'名字 {name} 不对：位置要是 {" / ".join(SLOTS)}，地图（可不写）要是 {" / ".join(MAPS)}')
    if not shutil.which('ffmpeg'):
        sys.exit('找不到 ffmpeg（winget install ffmpeg，或者自己用别的软件导出 48k 16 位 wav 放进 MusicPack）')
    os.makedirs(out_dir, exist_ok=True)
    dst = os.path.join(out_dir, name + '.wav')
    cmd = ['ffmpeg', '-y', '-v', 'error', '-i', src, '-vn', '-af', 'loudnorm=I=-16:TP=-1.5:LRA=11', '-ar', '48000', '-ac', '2', '-c:a', 'pcm_s16le', dst]
    subprocess.run(cmd, check=True)
    help_src = os.path.join(os.path.dirname(os.path.abspath(__file__)), '音乐包说明.txt')
    if os.path.exists(help_src) and not os.path.exists(os.path.join(out_dir, '说明.txt')):
        shutil.copyfile(help_src, os.path.join(out_dir, '说明.txt'))
    print(f'{dst}  {os.path.getsize(dst) / 1e6:.1f} MB')


main()
