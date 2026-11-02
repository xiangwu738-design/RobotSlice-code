"""在 RobotSlice 工程里一次导入 tools/anim_pick.py 挑好的各组动作（Inbox/_retarget_pick/<组> → /Game/Robot/Anims/<组>），
每组都走 ue_import_locomotion.py（加攻击的 Hit 标记），省得每组重启一次编辑器。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_import_anim_sets.py [组,组...]" -AllowCommandletRendering -RenderOffscreen"""
import os
import sys

import unreal

ROOT = 'D:/RobotSlice'
PICK = f'{ROOT}/Inbox/_retarget_pick'
script = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ue_import_locomotion.py')
code = compile(open(script, encoding='utf-8').read(), script, 'exec')
only = sys.argv[1].split(',') if len(sys.argv) > 1 else None
# 用老的 FBX 导入器：新的（Interchange）要求动作长度正好落在帧边界上，有些商城动作差半帧就整个导不进来
for cvar in ('Interchange.FeatureFlags.Import.FBX', 'r.Interchange.FeatureFlags.Import.FBX', 'Interchange.FeatureFlags.Import.FBX.Animation'):
    unreal.SystemLibrary.execute_console_command(None, f'{cvar} 0')
for group in sorted(os.listdir(PICK)):
    src = f'{PICK}/{group}'
    if not os.path.isfile(f'{src}/locomotion.json') or (only and group not in only):
        continue
    unreal.log(f'[sets] 导入 {group}')
    sys.argv = [script, src, f'/Game/Robot/Anims/{group}']
    exec(code, {'__name__': '__main__', '__file__': script})
unreal.log('[sets] 完成')
