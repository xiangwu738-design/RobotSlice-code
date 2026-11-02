"""护盾能量面白天一片白的修正（11-01）：M_ShieldFX 的白天补偿 max(1, 曝光倒数×1.3)^0.85 里，Power 节点的指数连线从来没接上
（ue_import_shoulder.py 写的输入名 'Exponent' 不存在，应该是 'Exp'，连接函数只返回 False 不报错），指数退回节点默认的 2。
shield_fx.hlsl 又按 0.85 反算曝光倒数，白天（曝光倒数约 13000）亮度就差了好几个数量级，README 记的「1 / 0.1 / 0.01 三档都白」就是这个。
这里只把 Power 节点的默认指数改成 0.85（不删节点，材质被 C++ 启动时加载，清空节点重建会崩），顺手把 SM_ShieldDome 的槽挂上 M_ShieldFX。可重复跑。
同样的漏接还在 M_ChaffGlint、ue_make_emp_fx.py、ue_import_combat_fx.py 建的材质里，它们的亮度是按现状调过的，这里不动。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_shield_fix.py" -unattended -nosplash
"""
import os
import shutil

import unreal

eas = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
BACKUP = 'D:/归档/RobotSlice_补模型前_20261101'
PATH = '/Game/Weapons/Materials/M_ShieldFX'
for pkg in (PATH, '/Game/Weapons/Meshes/SM_ShieldDome'):
    src = 'D:/RobotSlice/Content/' + pkg[len('/Game/'):] + '.uasset'
    dst = os.path.join(BACKUP, pkg[len('/Game/'):] + '.uasset')
    if not os.path.exists(dst):
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)

m = eas.load_asset(PATH)
powers = []
todo = [mel.get_material_property_input_node(m, unreal.MaterialProperty.MP_EMISSIVE_COLOR)]
seen = set()
while todo:
    e = todo.pop()
    if not e or e.get_name() in seen:
        continue
    seen.add(e.get_name())
    if isinstance(e, unreal.MaterialExpressionPower):
        powers.append(e)
    todo += mel.get_inputs_for_material_expression(m, e)
assert len(powers) == 1, f'M_ShieldFX 里找到 {len(powers)} 个 Power 节点'
pw = powers[0]
old = pw.get_editor_property('const_exponent')
inputs = [i.get_name() if i else None for i in mel.get_inputs_for_material_expression(m, pw)]
pw.set_editor_property('const_exponent', 0.85)
# 着色器代码（亮度倍数在里面）：去掉 // 注释，中文进不了着色器编译
import re
code = chr(10).join(re.sub('//.*$', '', ln) for ln in open('D:/RobotSlice/tools/shaders/shield_fx.hlsl', encoding='utf-8').read().splitlines())
assert all(ord(ch) < 128 for ch in code)
custom = mel.get_material_property_input_node(m, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
assert isinstance(custom, unreal.MaterialExpressionCustom), custom
custom.set_editor_property('code', code)
mel.recompile_material(m)
eas.save_asset(PATH, only_if_is_dirty=False)

dome = eas.load_asset('/Game/Weapons/Meshes/SM_ShieldDome')
dome.set_material(0, m)
eas.save_asset('/Game/Weapons/Meshes/SM_ShieldDome', only_if_is_dirty=False)
text = f'M_ShieldFX Power 节点的输入 {inputs}，默认指数 {old} → 0.85；SM_ShieldDome Nanite {dome.get_editor_property("nanite_settings").get_editor_property("enabled")}'
unreal.log('[shieldfix] ' + text)
open('D:/RobotSlice/Saved/drone_shield_fix.txt', 'w', encoding='utf-8').write(text)
