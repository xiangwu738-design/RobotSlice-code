"""11-02 云端打包（本地测试流程 kind=py）：按 打包.bat 生成一份「云端版」，在独立进程里开跑，本脚本马上返回。

- 打包.bat 不在仓库里（云端改不到），所以照抄它，只做三处改动：
  ① -map= 换成 Config/DefaultGame.ini 里 MapsToCook 的全部地图（打包.bat 里可能还是旧的几张，没有石林 / 苏联 / 月球 / 老街）；
  ② %~dp0 换成工程目录（生成的 bat 不在工程根目录）；③ 去掉 pause。
- 生成的 bat 和日志在 Saved/CloudPackage/：打包.bat 输出写 package_<时间>.log，结束时退出码写 done_<时间>.txt
  （README「打包」：烘焙留下的着色器进程会占着日志文件，结束标记必须写另一个文件）。
- 打包要几十分钟，测试流程一项最多 900 秒，所以必须脱离测试流程的进程树（DETACHED_PROCESS + 尽量 BREAKAWAY_FROM_JOB）。
- 进度 / 结果用 tools/ue_package_status.py 看。打包期间别推需要编译的提交（会和打包抢编辑器 DLL）。
日志标签 [pack]。"""
import os
import re
import subprocess
import time

import unreal

ROOT = os.path.abspath(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).replace('\\', '/').rstrip('/')
OUT = f'{ROOT}/Saved/CloudPackage'


def log(*a):
    unreal.log_warning('[pack] ' + ' '.join(str(x) for x in a))


def maps_to_cook():
    names = []
    for line in open(f'{ROOT}/Config/DefaultGame.ini', encoding='utf-8-sig', errors='replace'):
        m = re.match(r'\s*\+MapsToCook=\(FilePath="([^"]+)"\)', line)
        if m:
            names.append(m.group(1))
    return names


def read_bat(path):
    raw = open(path, 'rb').read()
    for enc in ('utf-8-sig', 'gbk', 'mbcs'):
        try:
            return raw.decode(enc), enc
        except (UnicodeDecodeError, LookupError):
            pass
    return raw.decode('utf-8', errors='replace'), 'utf-8'


def main():
    bat = f'{ROOT}/打包.bat'
    if not os.path.exists(bat):
        log('错误：找不到', bat)
        return
    text, enc = read_bat(bat)
    log(f'打包.bat（编码 {enc}）原文：')
    for i, line in enumerate(text.splitlines(), 1):
        log(f'  {i:3d}| {line}')
    maps = maps_to_cook()
    log('MapsToCook：', len(maps), '张', ' '.join(m.rsplit('/', 1)[-1] for m in maps))
    if not maps:
        log('错误：DefaultGame.ini 里没读到 MapsToCook')
        return
    joined = '+'.join(maps)
    n_map = 0

    def fix_map(m):
        nonlocal n_map
        n_map += 1
        q = m.group(1) or ''
        return f'-map={q}{joined}{q}'

    out_lines = []
    for line in text.splitlines():
        if re.match(r'\s*pause\b', line, re.I):
            continue
        line = re.sub(r'-map=("?)[^\s"]+\1', fix_map, line, flags=re.I)
        line = line.replace('%~dp0', ROOT.replace('/', '\\') + '\\')
        out_lines.append(line)
    if n_map == 0:
        log('警告：打包.bat 里没找到 -map=，按原样打（地图只按 DefaultGame.ini 的 MapsToCook）')
    else:
        log(f'-map= 换成 {len(maps)} 张图（{n_map} 处）')
    os.makedirs(OUT, exist_ok=True)
    stamp = time.strftime('%Y%m%d_%H%M%S')
    logf = f'{OUT}/package_{stamp}.log'.replace('/', '\\')
    done = f'{OUT}/done_{stamp}.txt'.replace('/', '\\')
    inner = f'{OUT}/打包_云端_{stamp}.bat'
    # 原 bat 里可能有 exit /b，所以包一层：外层 call 内层，内层输出进日志，外层写退出码
    open(inner, 'w', encoding=enc if enc != 'utf-8-sig' else 'utf-8').write('\r\n'.join(out_lines) + '\r\n')
    outer = f'{OUT}/run_{stamp}.bat'
    chcp = '' if enc == 'gbk' or enc == 'mbcs' else 'chcp 65001 >nul\r\n'
    open(outer, 'w', encoding='utf-8').write(
        '@echo off\r\n' + chcp + f'cd /d "{ROOT}"\r\n' +
        f'echo started %date% %time% > "{done}.started"\r\n' +
        f'call "{inner.replace("/", chr(92))}" > "{logf}" 2>&1 < nul\r\n' +
        f'echo %ERRORLEVEL% > "{done}"\r\n')
    flags = 0x00000008 | 0x00000200  # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP
    cmd = ['cmd.exe', '/c', outer.replace('/', '\\')]
    p = None
    for extra, name in ((0x01000000, '脱离作业对象'), (0, '')):  # CREATE_BREAKAWAY_FROM_JOB
        try:
            p = subprocess.Popen(cmd, creationflags=flags | extra, close_fds=True, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL, cwd=ROOT)
            log('已在独立进程开跑', f'pid {p.pid}', name)
            break
        except OSError as e:
            log('启动失败', name or '普通', e)
    if not p:
        log('错误：打包进程没起来')
        return
    log('日志', logf)
    log('结束标记', done)
    time.sleep(20)
    if p.poll() is not None:
        log(f'警告：打包进程 20 秒内就退出了（退出码 {p.returncode}），看日志：')
        try:
            for line in open(logf, encoding='utf-8', errors='replace').read().splitlines()[-30:]:
                log('  |', line)
        except OSError as e:
            log('读日志失败', e)
    else:
        log('20 秒后还在跑，正常')


main()
