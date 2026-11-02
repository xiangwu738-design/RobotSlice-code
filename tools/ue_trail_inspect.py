"""看刀光材质：混合模式、自发光链、各实例的参数"""
import unreal
mel = unreal.MaterialEditingLibrary
mat = unreal.load_asset('/Game/AAAVFX/Course_01/Material/M_SwordTrail_01')
unreal.log(f'[trail] 母材质 blend={mat.get_editor_property("blend_mode")} shading={mat.get_editor_property("shading_model")}')
for P in (unreal.MaterialProperty.MP_EMISSIVE_COLOR, unreal.MaterialProperty.MP_OPACITY, unreal.MaterialProperty.MP_BASE_COLOR):
    n = mel.get_material_property_input_node(mat, P)
    unreal.log(f'[trail] {P}: {n.get_class().get_name() if n else None} {n.get_editor_property("desc") if n else ""}')
for i in ('01', '02', '04', '05'):
    mi = unreal.load_asset(f'/Game/AAAVFX/Course_01/Material/M_SwordTrail_01_Ins_{i}')
    if not mi:
        continue
    bo = mi.get_editor_property('base_property_overrides')
    vec = [(str(p.parameter_info.name), p.parameter_value) for p in mi.get_editor_property('vector_parameter_values')]
    sc = [(str(p.parameter_info.name), round(p.parameter_value, 3)) for p in mi.get_editor_property('scalar_parameter_values')]
    tx = [(str(p.parameter_info.name), p.parameter_value.get_name() if p.parameter_value else None) for p in mi.get_editor_property('texture_parameter_values')]
    unreal.log(f'[trail] Ins_{i}: override_blend={bo.get_editor_property("override_blend_mode")} blend={bo.get_editor_property("blend_mode")}')
    unreal.log(f'[trail]   vec={vec}')
    unreal.log(f'[trail]   scalar={sc}')
    unreal.log(f'[trail]   tex={tx}')
