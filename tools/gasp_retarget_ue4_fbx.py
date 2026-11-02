"""在 Game Animation Sample 工程里跑（只读：导入的动作、新建的重定向器都只在内存里，不保存进示例工程）：
把 UE4 人形骨架的 FBX 动作（武士刀、双手锤这类商城动作包的源文件）重定向到 UE5 人形（和本工程模板人形同一套骨架），导出 FBX + 动作信息 json。
重定向器现建：源 = 示例里的 IK_UE4_Mannequin_Retarget，目标 = IK_UE5_Mannequin_Retarget，链按名字自动对应、目标姿势按源自动对齐。
UnrealEditor-Cmd "<示例工程>.uproject" -run=pythonscript -script="D:/RobotSlice/tools/gasp_retarget_ue4_fbx.py <FBX 目录> <输出目录>" -AllowCommandletRendering -RenderOffscreen
"""
import glob
import json
import os
import sys

import unreal

SRC, OUT = sys.argv[1], sys.argv[2]
os.makedirs(OUT, exist_ok=True)
TMP = '/Game/__RobotSliceUE4'
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.AnimationLibrary
SoT = unreal.RetargetSourceOrTarget
unreal.SystemLibrary.execute_console_command(None, 'r.Interchange.FeatureFlags.Import.FBX 0')  # 用老的 FBX 导入器（下面的选项是它的）

skel4 = unreal.load_asset('/Game/Characters/UE4_Mannequin/Meshes/SK_UE4_Mannequin')
mesh4 = unreal.load_asset('/Game/Characters/UE4_Mannequin/Meshes/SKM_UE4_Mannequin')
mesh5 = unreal.load_asset('/Game/Characters/UE5_Mannequins/Meshes/SKM_Manny_Simple')
ik4 = unreal.load_asset('/Game/Characters/UE4_Mannequin/Rigs/IK_UE4_Mannequin_Retarget')
ik5 = unreal.load_asset('/Game/Characters/UE5_Mannequins/Rigs/IK_UE5_Mannequin_Retarget')
assert skel4 and mesh4 and mesh5 and ik4 and ik5, (skel4, mesh4, mesh5, ik4, ik5)

# ---- 导入 FBX 到 UE4 人形骨架
tasks = []
for f in sorted(glob.glob(f'{SRC}/*.fbx')):
    t = unreal.AssetImportTask()
    t.filename = f
    t.destination_path = f'{TMP}/Src'
    t.automated = True
    t.replace_existing = True
    t.save = False
    ui = unreal.FbxImportUI()
    ui.import_mesh = False
    ui.import_animations = True
    ui.import_materials = False
    ui.import_textures = False
    ui.skeleton = skel4
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_ANIMATION
    ui.anim_sequence_import_data.set_editor_property('animation_length', unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    t.options = ui
    tasks.append(t)
tools.import_asset_tasks(tasks)
datas = [d for d in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(f'{TMP}/Src', True)
         if str(d.asset_class_path.asset_name) == 'AnimSequence']
unreal.log(f'[ue4rt] 导入 {len(datas)} 个动作（FBX {len(tasks)} 个）')

# ---- 现建 UE4 → UE5 重定向器
rtg = tools.create_asset('RTG_UE4_to_UE5', TMP, unreal.IKRetargeter, unreal.IKRetargetFactory())
c = unreal.IKRetargeterController.get_controller(rtg)
c.set_ik_rig(SoT.SOURCE, ik4)
c.set_ik_rig(SoT.TARGET, ik5)
c.set_preview_mesh(SoT.SOURCE, mesh4)
c.set_preview_mesh(SoT.TARGET, mesh5)
c.add_default_ops()
c.assign_ik_rig_to_all_ops(SoT.SOURCE, ik4)
c.assign_ik_rig_to_all_ops(SoT.TARGET, ik5)
c.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
c.auto_align_all_bones(SoT.TARGET)
unreal.log(f'[ue4rt] 重定向器：操作 {c.get_num_retarget_ops()} 个')

# ---- 批量重定向（只在内存里），导出 FBX
inputs = unreal.IKRetargetBatchOperationInputs()
inputs.set_editor_property('assets_to_retarget', datas)
inputs.set_editor_property('source_mesh', mesh4)
inputs.set_editor_property('target_mesh', mesh5)
inputs.set_editor_property('ik_retarget_asset', rtg)
inputs.set_editor_property('target_path', f'{TMP}/Out')
inputs.set_editor_property('include_referenced_assets', False)
inputs.set_editor_property('overwrite_existing_files', True)
results = unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
meta = {}
for d in results:
    seq = d.get_asset()
    if not isinstance(seq, unreal.AnimSequence):
        continue
    name = str(seq.get_name())
    length = lib.get_sequence_length(seq)
    r0 = lib.get_bone_pose_for_time(seq, 'root', 0.0, False).translation
    r1 = lib.get_bone_pose_for_time(seq, 'root', length, False).translation
    meta[name] = {'length': length, 'root_delta': [r1.x - r0.x, r1.y - r0.y, r1.z - r0.z], 'speed': 0.0, 'markers': []}
    task = unreal.AssetExportTask()
    task.object = seq
    task.filename = f'{OUT}/{name}.fbx'
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    task.exporter = unreal.AnimSequenceExporterFBX()
    opt = unreal.FbxExportOption()
    opt.set_editor_property('export_preview_mesh', False)
    task.options = opt
    ok = unreal.Exporter.run_asset_export_task(task)
    unreal.log(f'[ue4rt] {name}: {length:.2f}s 导出 {"成功" if ok else "失败"}')
json.dump(meta, open(f'{OUT}/locomotion.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
unreal.log(f'[ue4rt] 完成 {len(meta)} 个 → {OUT}（不保存任何资源，示例工程不变）')
