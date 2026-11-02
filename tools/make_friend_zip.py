# 把打包好的独立版压成给朋友的 zip：D:\RobotSlice_Build\HX-07_测试版_日期.zip
# 排除包里的 Saved（本机测试留下的设置 / 日志 / 截图）和测试日志；根目录放「给朋友的说明.txt」。
# 已经压缩过的资源包（.pak .ucas .utoc）直接存，不再压一遍。
import datetime
import os
import sys
import zipfile

SRC = r'D:\RobotSlice_Build\Windows'
README = r'D:\RobotSlice\tools\给朋友的说明.txt'
# 同一天第二次打包：加个后缀（py -3 tools/make_friend_zip.py b），不覆盖之前发出去的那个
SUFFIX = sys.argv[1] if len(sys.argv) > 1 else ''
OUT = r'D:\RobotSlice_Build\HX-07_测试版_%s%s.zip' % (datetime.date.today().strftime('%Y%m%d'), SUFFIX)
if os.path.exists(OUT):
    sys.exit('已经有 %s，加个后缀再跑' % OUT)
ROOT = 'HX-07'
STORE = {'.pak', '.ucas', '.utoc', '.zip'}

count = 0
with zipfile.ZipFile(OUT, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for folder, dirs, files in os.walk(SRC):
        rel = os.path.relpath(folder, SRC)
        if rel == os.path.join('RobotSlice', 'Saved') or rel.startswith(os.path.join('RobotSlice', 'Saved') + os.sep):
            continue
        for f in files:
            if rel == 'RobotSlice' and f.endswith('.log'):
                continue
            path = os.path.join(folder, f)
            arc = os.path.join(ROOT, rel, f) if rel != '.' else os.path.join(ROOT, f)
            mode = zipfile.ZIP_STORED if os.path.splitext(f)[1].lower() in STORE else zipfile.ZIP_DEFLATED
            z.write(path, arc, compress_type=mode)
            count += 1
    z.write(README, os.path.join(ROOT, '给朋友的说明.txt'))
    # 10-31：联机诊断启动器（带 -MechNetDiag 开游戏，退出后打开日志文件夹），和 RobotSlice.exe 放一起
    z.write(r'D:\RobotSlice\tools\联机诊断.bat', os.path.join(ROOT, '联机诊断.bat'))
print('%s  %d 个文件  %.2f GB' % (OUT, count + 2, os.path.getsize(OUT) / 1024 ** 3))
