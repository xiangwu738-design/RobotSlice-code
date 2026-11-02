"""苏联板楼：改了 tools/soviet_plan.py 以后一步重建（本地测试流程 kind=py 用；它不能串命令，这里串起来）：
1) 用系统 Python 跑 soviet_plan.py（要 numpy / PIL，UE 自带的 Python 没有）→ Inbox/_k62/plan.json、地面遮罩、布局图，自检结果打到日志；
2) ue_soviet_import.py mask（重导地面遮罩）；
3) ue_build_soviet.py（整张重建 L_Soviet）。
第 1 步失败（自检脚本报错）就停，不动地图。日志标签 [replan]。"""
import os
import runpy
import shutil
import subprocess
import sys

import unreal

log = lambda *a: unreal.log_warning('[replan] ' + ' '.join(str(x) for x in a))
ROOT = 'D:/RobotSlice'
py = shutil.which('py')
cmd = [py, '-3', '-I', f'{ROOT}/tools/soviet_plan.py'] if py else ['python', '-I', f'{ROOT}/tools/soviet_plan.py']
log('运行', ' '.join(cmd))
r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, encoding='utf-8', errors='replace')
for line in (r.stdout or '').splitlines():
    log('plan |', line)
for line in (r.stderr or '').splitlines()[-30:]:
    log('plan ERR |', line)
if r.returncode != 0:
    raise RuntimeError(f'soviet_plan.py 退出码 {r.returncode}，不重建地图')
sys.argv = [f'{ROOT}/tools/ue_soviet_import.py', 'mask']
runpy.run_path(f'{ROOT}/tools/ue_soviet_import.py', run_name='__main__')
log('地面遮罩已重导')
sys.argv = [f'{ROOT}/tools/ue_build_soviet.py']
runpy.run_path(f'{ROOT}/tools/ue_build_soviet.py', run_name='__main__')
log('L_Soviet 已重建')
