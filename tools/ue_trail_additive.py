"""刀光改成叠加（Additive）：原来是半透明混合，贴图里的烟是深色的，白天亮地面上就成了一道黑烟；
叠加模式下深色部分不显示、亮的部分发光。自发光再乘一次不透明度，保住贴图的形状（叠加模式本身不看不透明度）"""
import unreal
mel = unreal.MaterialEditingLibrary
mat = unreal.load_asset('/Game/AAAVFX/Course_01/Material/M_SwordTrail_01')
E = unreal.MaterialProperty.MP_EMISSIVE_COLOR
O = unreal.MaterialProperty.MP_OPACITY
emi = mel.get_material_property_input_node(mat, E)
opa = mel.get_material_property_input_node(mat, O)
if mat.get_editor_property('blend_mode') != unreal.BlendMode.BLEND_ADDITIVE and emi and opa:
    mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, emi.get_editor_property('material_expression_editor_x') + 250, emi.get_editor_property('material_expression_editor_y'))
    mul.set_editor_property('desc', 'TrailAdditive')
    mel.connect_material_expressions(emi, '', mul, 'A')
    mel.connect_material_expressions(opa, '', mul, 'B')
    mel.connect_material_property(mul, '', E)
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat, False)
    unreal.log('[trailadd] 改成叠加')
else:
    unreal.log(f'[trailadd] 没改：blend={mat.get_editor_property("blend_mode")}')
for i in ('01', '02', '04', '05'):
    mi = unreal.load_asset(f'/Game/AAAVFX/Course_01/Material/Ex_01_SwordTrail/M_SwordTrail_01_Ins_{i}')
    if mi:
        bo = mi.get_editor_property('base_property_overrides')
        unreal.log(f'[trailadd] Ins_{i} 覆盖混合={bo.get_editor_property("override_blend_mode")}')
