"""刀光：素材包的刀光把颜色去饱和到 0.8（灰），白天看就是一道灰黑的烟；降到 0.2、尾部颜色提亮 3 倍，变成橙色光带"""
import unreal
mel = unreal.MaterialEditingLibrary
for i in (1, 2, 4, 5):
    p = f'/Game/AAAVFX/Course_01/Material/Ex_01_SwordTrail/M_SwordTrail_01_Ins_0{i}'
    mi = unreal.load_asset(p)
    if not mi:
        continue
    mel.set_material_instance_scalar_parameter_value(mi, 'Desaturation', 0.2)
    tail = mel.get_material_instance_vector_parameter_value(mi, 'Tail_Color')
    if tail.r + tail.g + tail.b < 6.0:  # 只提亮一次（可重复跑）
        mel.set_material_instance_vector_parameter_value(mi, 'Tail_Color', unreal.LinearColor(tail.r * 3.0, tail.g * 3.0, tail.b * 3.0, tail.a))
    unreal.EditorAssetLibrary.save_loaded_asset(mi, False)
    unreal.log(f'[tint] {p}')
