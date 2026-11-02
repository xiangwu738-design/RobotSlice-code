"""11-02 去组件 · 肩部装备换进去（kind=py，只拷文件）：原来的 Inbox/_肩部候选/游戏模型、游戏模型_战术 整个备份到
D:/归档/RobotSlice_肩部去组件前_20261102（已有就不覆盖），再把 *_kitfree 里的 FBX 拷过去覆盖同名的。之后跑现成的
tools/ue_import_shoulder.py、ue_import_shoulder_tactical.py 导入。日志 [kfshcopy]。"""
import glob
import os
import shutil

import unreal

ROOT = 'D:/RobotSlice/Inbox/_肩部候选'
BACK = 'D:/归档/RobotSlice_肩部去组件前_20261102'
log = lambda *a: unreal.log_warning('[kfshcopy] ' + ' '.join(str(x) for x in a))
for d in ('游戏模型', '游戏模型_战术'):
    src, new, bak = f'{ROOT}/{d}', f'{ROOT}/{d}_kitfree', f'{BACK}/{d}'
    fbx = sorted(glob.glob(f'{new}/*.fbx'))
    if not fbx:
        log('错误：', new, '里没有 FBX，不换')
        continue
    if not os.path.exists(bak):
        shutil.copytree(src, bak)
        log('备份', src, '→', bak)
    for f in fbx:
        shutil.copy2(f, f'{src}/{os.path.basename(f)}')
    for f in glob.glob(f'{new}/*.png'):
        shutil.copy2(f, f'{src}/{os.path.basename(f)}')
    log(d, '换了', len(fbx), '个 FBX')
