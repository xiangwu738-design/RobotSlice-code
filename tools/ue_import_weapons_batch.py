"""一次导入多把武器（省得每把都重启一次编辑器）：逐个用 ue_import_weapon.py 的参数跑它。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_import_weapons_batch.py" -AllowCommandletRendering -RenderOffscreen
清单在下面 JOBS 里改：(名字, fbx, 贴图目录, 底色, 自发光强度)"""
import os
import sys

import unreal

ROOT = 'D:/RobotSlice'
SWORD_TEX = f'{ROOT}/Inbox/NO454-20把科幻剑游戏武器3D模型素材/Assets/TEXTURE'
GUN_TINT = '0.42,0.46,0.5'  # 枪包是白色，压成冷灰（和 Gun3 一样）
JOBS = [(f'Sword{n}', f'{ROOT}/SourceAssets/swords/SM_Sword{n}.fbx', f'{SWORD_TEX}/{n}', '1,1,1', '8') for n in ('03', '11', '13', '14', '16', '20')]
JOBS += [(f'Gun{n}', f'{ROOT}/SourceAssets/guns/SM_Gun{n}.fbx', f'{ROOT}/SourceAssets/guns/Gun{n}_tex', GUN_TINT, '0') for n in (2, 6, 9, 11)]
JOBS += [('Pistol7', f'{ROOT}/SourceAssets/guns/SM_Pistol7.fbx', f'{ROOT}/SourceAssets/guns/Pistol7_tex', GUN_TINT, '0')]

script = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ue_import_weapon.py')
code = compile(open(script, encoding='utf-8').read(), script, 'exec')
only = sys.argv[1].split(',') if len(sys.argv) > 1 else None
for job in JOBS:
    if only and job[0] not in only:
        continue
    sys.argv = [script, *job]
    exec(code, {'__name__': '__main__', '__file__': script})
unreal.log('[weapon] 批量导入完成')
