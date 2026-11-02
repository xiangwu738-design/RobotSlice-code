"""在 Game Animation Sample 工程里跑（只读：重定向出来的动作只在内存里，不保存进示例工程）：
把站立 / 走跑 8 方向 / 下落 / 蹲的循环动作从 UEFN 人形重定向到 UE5 人形（和本工程模板人形同一套骨架），
导出 FBX，另写 locomotion.json（每个动作的时长、根骨速度、同步标记——FBX 不带同步标记，导入时再加回去）。
UnrealEditor-Cmd "<示例工程>.uproject" -run=pythonscript -script="D:/RobotSlice/tools/gasp_export_locomotion.py <输出目录>" -AllowCommandletRendering -RenderOffscreen
"""
import json
import os
import sys

import unreal

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
A = '/Game/Characters/UEFN_Mannequin/Animations/'
DIRS = ['F', 'FR', 'RR', 'BR', 'B', 'BL', 'LL', 'FL']
NAMES = ['Idle/M_Neutral_Stand_Idle_Loop', 'Idle/M_Neutral_Crouch_Idle_Loop', 'Jump/M_Neutral_Jump_Loop_Fall'] + \
    [f'Walk/M_Neutral_Walk_Loop_{d}' for d in DIRS] + [f'Run/M_Neutral_Run_Loop_{d}' for d in DIRS]
TMP = '/Game/__RobotSliceExport'

src_mesh = unreal.load_asset('/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin')
tgt_mesh = unreal.load_asset('/Game/Characters/UE5_Mannequins/Meshes/SKM_Manny_Simple')
rtg = unreal.load_asset('/Game/Characters/UE5_Mannequins/Rigs/RTG_UEFN_to_UE5_Mannequin')
assert src_mesh and tgt_mesh and rtg, (src_mesh, tgt_mesh, rtg)

datas = []
for n in NAMES:
    d = unreal.EditorAssetLibrary.find_asset_data(A + n)
    if not d.is_valid():
        unreal.log_error(f'[gasp] 找不到 {A + n}')
        continue
    datas.append(d)

inputs = unreal.IKRetargetBatchOperationInputs()
inputs.set_editor_property('assets_to_retarget', datas)
inputs.set_editor_property('source_mesh', src_mesh)
inputs.set_editor_property('target_mesh', tgt_mesh)
inputs.set_editor_property('ik_retarget_asset', rtg)
inputs.set_editor_property('target_path', TMP)
inputs.set_editor_property('include_referenced_assets', False)
inputs.set_editor_property('overwrite_existing_files', True)
results = unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
unreal.log(f'[gasp] 重定向完成 {len(results)} 个')

lib = unreal.AnimationLibrary
meta = {}
for d in results:
    seq = d.get_asset()
    if not isinstance(seq, unreal.AnimSequence):
        continue
    name = str(seq.get_name())
    length = lib.get_sequence_length(seq)
    r0 = lib.get_bone_pose_for_time(seq, 'root', 0.0, False).translation
    r1 = lib.get_bone_pose_for_time(seq, 'root', length, False).translation
    markers = [(str(m.marker_name), float(m.time)) for m in lib.get_animation_sync_markers(seq)]
    meta[name] = {
        'length': length,
        'root_delta': [r1.x - r0.x, r1.y - r0.y, r1.z - r0.z],
        'speed': ((r1.x - r0.x) ** 2 + (r1.y - r0.y) ** 2) ** 0.5 / max(length, 1e-3),
        'markers': markers,
    }
    task = unreal.AssetExportTask()
    task.object = seq
    task.filename = f'{OUT}/{name}.fbx'
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    task.exporter = unreal.AnimSequenceExporterFBX()
    opt = unreal.FbxExportOption()
    opt.set_editor_property('export_preview_mesh', False)
    opt.set_editor_property('export_morph_targets', False)
    task.options = opt
    ok = unreal.Exporter.run_asset_export_task(task)
    unreal.log(f'[gasp] {name}: {length:.2f}s 速度 {meta[name]["speed"]:.0f} 标记 {markers[:6]}{"…" if len(markers) > 6 else ""} 导出 {"成功" if ok else "失败"}')

json.dump(meta, open(f'{OUT}/locomotion.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
unreal.log(f'[gasp] 写好 {OUT}/locomotion.json（不保存任何资源，示例工程不变）')
