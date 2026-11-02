"""刀光实例 04 / 05 自己覆盖了混合模式（半透明），一并改成叠加"""
import unreal
for i in ('01', '02', '04', '05'):
    p = f'/Game/AAAVFX/Course_01/Material/Ex_01_SwordTrail/M_SwordTrail_01_Ins_{i}'
    mi = unreal.load_asset(p)
    bo = mi.get_editor_property('base_property_overrides')
    was = bo.get_editor_property('blend_mode')
    if bo.get_editor_property('override_blend_mode'):
        bo.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
        mi.set_editor_property('base_property_overrides', bo)
        unreal.MaterialEditingLibrary.update_material_instance(mi)
        unreal.EditorAssetLibrary.save_loaded_asset(mi, False)
    unreal.log(f'[trailinst] Ins_{i} 原来 {was} → {mi.get_editor_property("base_property_overrides").get_editor_property("blend_mode")}')
