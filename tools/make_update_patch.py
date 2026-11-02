# 做给朋友的更新包（11-01，用户：「手动把更新包发给他们，给他们一个读取拼接用的工具」）。
# 用 HDiffPatch（tools/hdiffpatch，MIT）比较旧版文件夹和 D:\RobotSlice_Build\Windows，输出 D:\RobotSlice_Build\HX-07_更新_旧版到新版.zip：
#   里面是 hpatchz.exe、补丁文件、应用更新.bat、更新说明。朋友解压到游戏文件夹（RobotSlice.exe 那一层），双击 bat。
# 用法：py -3 tools/make_update_patch.py <旧版 zip 或文件夹> <旧版号> <新版号>
#   例：py -3 tools/make_update_patch.py D:\RobotSlice_Build\HX-07_测试版_20261101b.zip 20261101b 20261101c
# 包里的 Saved（玩家设置）、日志不进补丁；根目录的说明和联机诊断.bat 和 make_friend_zip.py 一样放进去。
import os
import shutil
import subprocess
import sys
import time
import zipfile

BUILD = r'D:\RobotSlice_Build'
SRC = BUILD + r'\Windows'
TOOLS = r'D:\RobotSlice\tools'
HDIFF = TOOLS + r'\hdiffpatch\bin\windows64\hdiffz.exe'
HPATCH = TOOLS + r'\hdiffpatch\bin\windows64\hpatchz.exe'

old_arg, old_ver, new_ver = sys.argv[1:4]
work = BUILD + r'\_patch_' + new_ver
if os.path.exists(work):
    sys.exit('已经有 %s，先挪走再跑' % work)
os.makedirs(work)

# 旧版：zip 就解开（里面有一层 HX-07）
old = old_arg
if old.lower().endswith('.zip'):
    with zipfile.ZipFile(old) as z:
        z.extractall(work + r'\old')
    old = work + r'\old\HX-07'

# 新版：和 make_friend_zip.py 同样的内容，用硬链接摆出来，不占空间也不动原文件
new = work + r'\new'
for folder, dirs, files in os.walk(SRC):
    rel = os.path.relpath(folder, SRC)
    saved = os.path.join('RobotSlice', 'Saved')
    if rel == saved or rel.startswith(saved + os.sep):
        continue
    os.makedirs(os.path.join(new, rel), exist_ok=True)
    for f in files:
        if rel == 'RobotSlice' and f.endswith('.log'):
            continue
        os.link(os.path.join(folder, f), os.path.join(new, rel, f))
shutil.copy2(TOOLS + r'\给朋友的说明.txt', new)
shutil.copy2(TOOLS + r'\联机诊断.bat', new)
with open(new + r'\版本.txt', 'w', encoding='utf-8') as f:
    f.write(new_ver + '\n')

patch = work + r'\update.hdiff'
t0 = time.time()
# -s：流式按块匹配，内存占用小，适合 3 GB 的资源包；-C-xxh128：打补丁时校验旧文件和结果
cmd = [HDIFF, '-s-256', '-p-8', '-c-zstd-19-24', '-C-xxh128', '-d', old, new, patch]
print(' '.join(cmd))
subprocess.run(cmd, check=True)
print('补丁 %.0f 秒，%.1f MB（新版 %.2f GB）' % (time.time() - t0, os.path.getsize(patch) / 1024 ** 2,
      sum(os.path.getsize(os.path.join(a, x)) for a, _, fs in os.walk(new) for x in fs) / 1024 ** 3))

bat = """@echo off
chcp 65001 >nul
cd /d "%~dp0"
title HX-07 更新到 {new}
if not exist RobotSlice.exe (
  echo 请把更新包解压到游戏文件夹（RobotSlice.exe 那一层）再运行。
  pause
  exit /b 1
)
set CUR=未知
if exist 版本.txt set /p CUR=<版本.txt
echo 当前版本：%CUR%
echo 这个更新：{old} 到 {new}
echo 正在校验并更新，请关闭游戏，大约需要几分钟……
"%~dp0hpatchz.exe" -f -C-all . "update_{new}.hdiff" .
if errorlevel 1 (
  echo.
  echo 更新失败，游戏文件没有被改动。通常是版本不对，这个更新只能从 {old} 开始打。
  echo 把这个窗口截图发给开房的人。
  pause
  exit /b 1
)
del "update_{new}.hdiff"
echo.
echo 更新完成，现在是 {new}。可以打开 RobotSlice.exe 了。
pause
""".format(old=old_ver, new=new_ver)

out = BUILD + r'\HX-07_更新_%s到%s.zip' % (old_ver, new_ver)
with zipfile.ZipFile(out, 'w', compression=zipfile.ZIP_DEFLATED) as z:
    z.write(HPATCH, 'hpatchz.exe')
    z.write(patch, 'update_%s.hdiff' % new_ver, compress_type=zipfile.ZIP_STORED)
    z.writestr('应用更新.bat', bat.replace('\n', '\r\n').encode('utf-8'))
print('%s  %.1f MB' % (out, os.path.getsize(out) / 1024 ** 2))
