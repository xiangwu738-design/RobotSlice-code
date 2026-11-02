"""看机体网格的材质槽（槽名 / FBX 里的原名 / 挂的材质）和骨架里有没有减震骨头。"""
import unreal
mesh = unreal.load_asset('/Game/Robot/Meshes/SKM_Robot_v3')
for i, sm in enumerate(mesh.get_editor_property('materials')):
    mi = sm.get_editor_property('material_interface')
    unreal.log(f'[mesh] 槽{i} 名={sm.get_editor_property("material_slot_name")} FBX原名={sm.get_editor_property("imported_material_slot_name")} 材质={mi.get_name() if mi else None}')
sk = mesh.get_editor_property('skeleton')
unreal.log(f'[mesh] 骨架 {sk.get_path_name()} 脏={sk.get_package().is_dirty() if hasattr(sk.get_package(), "is_dirty") else "?"}')
for name in ('damper_knee_a_l', 'damper_ankle_b_r', 'thigh_l'):
    try:
        idx = unreal.SkeletalMeshEditorSubsystem.get_bone_index(mesh, name) if hasattr(unreal.SkeletalMeshEditorSubsystem, 'get_bone_index') else None
    except Exception as e:
        idx = str(e)
    unreal.log(f'[mesh] {name} {idx}')
