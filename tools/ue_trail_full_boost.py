"""刀光单独把白天补偿加满并多补一点（指数 0.85 → 1.1）：刀光是叠加发光的拖尾，亮地面上补到刚好还是很淡"""
import unreal
# 指数 >1 只在白天多补（夜里补偿系数约 1.2，1.2^1.1 几乎不变；白天约 16000，^0.1 ≈ 2.6 倍）：叠加发光在亮地面上本来就显得淡
EXP = 1.1
mel = unreal.MaterialEditingLibrary
P = unreal.MaterialProperty.MP_EMISSIVE_COLOR
mat = unreal.load_asset('/Game/AAAVFX/Course_01/Material/M_SwordTrail_01')
out = mel.get_material_property_input_node(mat, P)
# 自发光可能先进了叠加用的乘法（TrailAdditive），往上找到 FxDayBoost
while out and str(out.get_editor_property('desc')) == 'TrailAdditive':
    out = mel.get_inputs_for_material_expression(mat, out)[0]
done = False
if out and str(out.get_editor_property('desc')) == 'FxDayBoost':
    for n in mel.get_inputs_for_material_expression(mat, out):
        if isinstance(n, unreal.MaterialExpressionPower):
            n.set_editor_property('const_exponent', EXP)
            done = True
if done:
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat, False)
unreal.log(f'[trailboost] 改了 {done}')
