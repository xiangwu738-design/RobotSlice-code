"""看刀光材质实例的参数（颜色、自发光强度），查刀光为什么偏暗"""
import unreal
mel = unreal.MaterialEditingLibrary
for p in ['/Game/AAAVFX/Course_01/Material/Ex_01_SwordTrail/M_SwordTrail_01_Ins_01', '/Game/AAAVFX/Course_01/Material/Ex_01_SwordTrail/M_SwordTrail_01_Ins_02',
          '/Game/AAAVFX/Course_01/Material/Ex_01_SwordTrail/M_SwordTrail_01_Ins_04', '/Game/AAAVFX/Course_01/Material/Ex_01_SwordTrail/M_SwordTrail_01_Ins_05',
          '/Game/AAAVFX/Course_01/Material/Ex_01_SwordTrail/M_AdditiveDe_v01_Ins_01']:
    mi = unreal.load_asset(p)
    if not mi:
        continue
    sc = {str(n): round(mel.get_material_instance_scalar_parameter_value(mi, n), 3) for n in mel.get_scalar_parameter_names(mi)}
    vc = {str(n): [round(c, 3) for c in (lambda v: (v.r, v.g, v.b, v.a))(mel.get_material_instance_vector_parameter_value(mi, n))] for n in mel.get_vector_parameter_names(mi)}
    unreal.log(f'[trail] {p.rsplit("/", 1)[-1]} 标量 {sc} 颜色 {vc}')
