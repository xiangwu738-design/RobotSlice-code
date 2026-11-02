"""11-02 看云端打包的进度 / 结果（kind=py）：读 Saved/CloudPackage 里最新一次的日志和结束标记，打到 [packstat]。
结束了还会：列出包目录（RobotSlice.exe 在哪、多大）、在打包日志里搜错误和「缺材质用途标记」（README「打包」）。"""
import glob
import os
import re
import time

import unreal

ROOT = os.path.abspath(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).replace('\\', '/').rstrip('/')
OUT = f'{ROOT}/Saved/CloudPackage'


def log(*a):
    unreal.log_warning('[packstat] ' + ' '.join(str(x) for x in a))


def main():
    logs = sorted(glob.glob(f'{OUT}/package_*.log'))
    if not logs:
        log('没有打包日志（ue_package_launch.py 没跑过？）')
        return
    lf = logs[-1]
    stamp = os.path.basename(lf)[len('package_'):-len('.log')]
    done = f'{OUT}/done_{stamp}.txt'
    age = (time.time() - os.path.getmtime(lf)) / 60
    log(f'最新一次 {stamp}，日志 {os.path.getsize(lf) / 1e6:.1f} MB，{age:.0f} 分钟前更新')
    text = open(lf, 'rb').read().decode('utf-8', errors='replace')
    lines = text.splitlines()
    if os.path.exists(done):
        code = open(done, encoding='utf-8', errors='replace').read().strip()
        log('已结束，退出码', code, '（0 = 成功）')
    else:
        log('还在跑（没有结束标记）')
    keys = re.compile(r'BUILD SUCCESSFUL|BUILD FAILED|AutomationTool exiting|Error:|error C\d+|LogCook: Display: Cooked packages|'
                      r'missing usage flag|Archiving to|Copying .* to staging|Success - \d+ error', re.I)
    hits = [l for l in lines if keys.search(l)]
    seen = set()
    out = []
    for l in hits:
        k = re.sub(r'\[[^\]]*\]', '', l)[:200]
        if k in seen:
            continue
        seen.add(k)
        out.append(l.strip()[:300])
    log(f'关键行 {len(out)} 条（最多 60）：')
    for l in out[:60]:
        log('  |', l)
    log('最后 25 行：')
    for l in lines[-25:]:
        log('  |', l.strip()[:300])
    # 打包.bat 的输出只到「Running AutomationTool」（后面 UAT 自己写日志），所以再看 UAT 的日志和包里的 pak
    uat = sorted(glob.glob(os.path.join(os.environ.get('APPDATA', ''), 'Unreal Engine', 'AutomationTool', 'Logs', '*', 'Log.txt')), key=os.path.getmtime)
    if uat:
        u = uat[-1]
        log('UAT 日志', u, time.strftime('%m-%d %H:%M', time.localtime(os.path.getmtime(u))))
        ut = open(u, 'rb').read().decode('utf-8', errors='replace').splitlines()
        pat = re.compile(r'BUILD SUCCESSFUL|BUILD FAILED|AutomationTool exiting|-map=|Cook.*(took|Display: Done)|Packages Cooked|missing usage flag|Error:|ERROR', re.I)
        seen = set()
        for l in ut:
            if pat.search(l):
                k = l.strip()[:220]
                if k not in seen:
                    seen.add(k)
                    log('  UAT|', k)
            if len(seen) > 40:
                break
        for l in ut[-6:]:
            log('  UAT 尾|', l.strip()[:220])
        cooks = sorted(glob.glob(os.path.join(os.path.dirname(u), 'Cook*.txt')))
        for c in cooks[-1:]:
            ct = open(c, 'rb').read().decode('utf-8', errors='replace')
            log('Cook 日志', c, '缺材质用途标记', ct.count('missing usage flag'), '次')
            for l in [x for x in ct.splitlines() if 'missing usage flag' in x][:8]:
                log('  缺标记|', l.strip()[:220])
    for pk in glob.glob('D:/RobotSlice_Build/Windows/RobotSlice/Content/Paks/*'):
        log('pak', os.path.basename(pk), f'{os.path.getsize(pk) / 1e9:.2f} GB', time.strftime('%m-%d %H:%M', time.localtime(os.path.getmtime(pk))))
    # 产物
    for pat in ('D:/RobotSlice_Build/Windows/RobotSlice.exe', 'D:/RobotSlice_Build/**/RobotSlice.exe', f'{ROOT}/Saved/StagedBuilds/**/RobotSlice.exe'):
        for exe in glob.glob(pat, recursive=True):
            log('包：', exe, time.strftime('%m-%d %H:%M', time.localtime(os.path.getmtime(exe))))
            break


main()
