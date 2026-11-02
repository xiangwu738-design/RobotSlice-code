"""11-02 核对材质用途标记（kind=py）：打印几个母材质的 Nanite / 实例化网格标记和文件修改时间。标签 [usagecheck]。"""
import os
import time

import unreal

for path in ('/Game/Weapons/Materials/M_BladeGhost', '/Game/Robot/Materials/M_RobotMaster'):
    m = unreal.load_asset(path)
    f = 'D:/RobotSlice/Content/' + path[len('/Game/'):] + '.uasset'
    t = time.strftime('%m-%d %H:%M', time.localtime(os.path.getmtime(f))) if os.path.exists(f) else '无文件'
    unreal.log_warning(f'[usagecheck] {path} nanite={m.get_editor_property("used_with_nanite") if m else None} '
                       f'ism={m.get_editor_property("used_with_instanced_static_meshes") if m else None} 文件 {t}')
