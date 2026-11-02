"""核对机体网格和骨架的骨头数、材质槽（命令行：UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script=这个文件 -nullrhi）"""
import unreal
mesh = unreal.load_asset('/Game/Robot/Meshes/SKM_Robot_v3')
sk = mesh.get_editor_property('skeleton')
pose = sk.get_reference_pose()
names = [str(n) for n in unreal.AnimPoseExtensions.get_bone_names(pose)]
unreal.log(f'[bones] 骨架 SK_Mannequin {len(names)} 根，自加：{[n for n in names if n.startswith(("thumb_up", "damper"))]}')
n = None
for fn in ('get_num_bones', 'get_bone_names'):
    try:
        r = getattr(unreal.SkeletalMeshEditorSubsystem(), fn, None) or getattr(mesh, fn, None)
        if r:
            v = r(mesh) if r.__self__ is not mesh else r()
            n = v if isinstance(v, int) else len(v)
            break
    except Exception as e:
        unreal.log(f'[bones] {fn} 不行：{e}')
if n is None:
    try:
        lib = unreal.EditorSkeletalMeshLibrary
        n = len(lib.get_bone_names(mesh)) if hasattr(lib, 'get_bone_names') else None
    except Exception as e:
        unreal.log(f'[bones] EditorSkeletalMeshLibrary 不行：{e}')
if n is None:
    # 退一步：网格的参考骨架 = 用网格算一个绑定姿势
    try:
        p = unreal.AnimPoseExtensions.get_reference_pose(mesh) if hasattr(unreal.AnimPoseExtensions, 'get_reference_pose') else None
        n = len(unreal.AnimPoseExtensions.get_bone_names(p)) if p else None
    except Exception as e:
        unreal.log(f'[bones] 参考姿势不行：{e}')
unreal.log(f'[bones] 网格 SKM_Robot_v3 骨头数 {n}')
unreal.log(f"[bones] 材质槽 {len(mesh.get_editor_property('materials'))}：{[str(m.get_editor_property('material_slot_name')) for m in mesh.get_editor_property('materials')]}")
