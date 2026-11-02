"""在 RobotSlice 工程里跑：导入 gasp_export_locomotion.py / gasp_retarget_ue4_fbx.py 导出的动作 FBX（模板人形骨架），按 locomotion.json 加回同步标记。
走跑循环找脚步落地标 L / R；名字带 Attack 的攻击动作找右手挥得最快的时刻标 Hit（近战按这个时刻判定命中）。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_import_locomotion.py <导出目录> [目标目录，默认 /Game/Robot/Anims]" -AllowCommandletRendering -RenderOffscreen
"""
import glob
import json
import os
import sys

import unreal

SRC = sys.argv[1]
DEST = sys.argv[2] if len(sys.argv) > 2 else '/Game/Robot/Anims'
SKELETON = '/Game/Characters/Mannequins/Meshes/SK_Mannequin'
TRACK = 'Feet'

CHAIN = {'L': ['root', 'pelvis', 'thigh_l', 'calf_l', 'foot_l'], 'R': ['root', 'pelvis', 'thigh_r', 'calf_r', 'foot_r']}
FPS = 60


def foot_ahead(seq, t, chain, d):
    """脚相对根骨、沿移动方向 d 的前后位置（正 = 在前）"""
    poses = lib.get_bone_poses_for_time(seq, chain, t, False)
    comp = poses[0]
    for p in poses[1:]:
        comp = unreal.MathLibrary.compose_transforms(p, comp)
    rel = comp.translation - poses[0].translation
    return rel.x * d[0] + rel.y * d[1]


def find_plants(seq, m):
    """示例的循环动作不带同步标记（它靠动作匹配），这里自己找：脚沿移动方向伸到最前 = 落地。
    走 / 跑循环里每只脚的每次落地都标上 L / R，运行时按左脚落地对齐各方向的步伐。"""
    delta = m.get('root_delta', [0, 0, 0])
    size = (delta[0] ** 2 + delta[1] ** 2) ** 0.5
    if m.get('speed', 0) < 50 or size < 1:
        return []
    d = (delta[0] / size, delta[1] / size)
    length = m['length']
    n = int(length * FPS)
    out = []
    for side, chain in CHAIN.items():
        v = [foot_ahead(seq, i / FPS, chain, d) for i in range(n)]
        lo, hi = min(v), max(v)
        found = []
        for i in range(n):
            a, b, c = v[i - 1], v[i], v[(i + 1) % n]
            if b >= a and b > c and b > lo + (hi - lo) * 0.6:
                t = i / FPS
                if not found or t - found[-1] > 0.35:
                    found.append(t)
        if len(found) > 1 and length - found[-1] + found[0] < 0.35:
            found.pop()  # 首尾是同一次落地
        out += [(side, t) for t in found]
    return sorted(out, key=lambda x: x[1])


HAND_R = ['root', 'pelvis', 'spine_01', 'spine_02', 'spine_03', 'spine_04', 'spine_05', 'clavicle_r', 'upperarm_r', 'lowerarm_r', 'hand_r']


def hand_pos(seq, t):
    poses = lib.get_bone_poses_for_time(seq, HAND_R, t, False)
    comp = poses[0]
    for p in poses[1:]:
        comp = unreal.MathLibrary.compose_transforms(p, comp)
    return comp.translation


def find_hits(seq, length):
    """攻击动作：右手速度的峰值（超过最大值七成、相隔 0.25 秒以上的都算一次挥砍）。
    开头 0.08 秒不算：有的片段第一帧姿势跳一下（轻击一的手速第一帧 2500 cm/s），会把命中标在 0.02 秒，挥刀全跑到命中之后"""
    n = int(length * FPS)
    pts = [hand_pos(seq, i / FPS) for i in range(n + 1)]
    speed = [(pts[i + 1] - pts[i]).length() * FPS for i in range(n)]
    skip = int(0.08 * FPS)
    top = max(speed[skip:]) if speed[skip:] else 0.0
    hits = []
    for i in range(max(1, skip), n - 1):
        if speed[i] >= speed[i - 1] and speed[i] > speed[i + 1] and speed[i] > top * 0.7:
            # 刀真正砍到是在挥得快的那一段中间，不是手速刚冲上去的那一下：取峰值两边手速还在峰值一半以上的那段的中点
            lo, hi = i, i
            while lo - 1 >= skip and speed[lo - 1] > speed[i] * 0.5:
                lo -= 1
            while hi + 1 < n and speed[hi + 1] > speed[i] * 0.5:
                hi += 1
            t = (lo + hi) / 2 / FPS
            if not hits or t - hits[-1] > 0.25:
                hits.append(t)
    return [('Hit', t) for t in hits]


meta = json.load(open(f'{SRC}/locomotion.json', encoding='utf-8'))
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.AnimationLibrary
skeleton = unreal.load_asset(SKELETON)

for fbx in sorted(glob.glob(f'{SRC}/*.fbx')):
    name = os.path.splitext(os.path.basename(fbx))[0]
    task = unreal.AssetImportTask()
    task.filename = fbx
    task.destination_path = DEST
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = False
    ui = unreal.FbxImportUI()
    ui.import_mesh = False
    ui.import_animations = True
    ui.import_materials = False
    ui.import_textures = False
    ui.skeleton = skeleton
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_ANIMATION
    ad = ui.anim_sequence_import_data
    ad.set_editor_property('animation_length', unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    ad.set_editor_property('import_bone_tracks', True)
    ad.set_editor_property('remove_redundant_keys', False)
    ad.set_editor_property('import_custom_attribute', False)
    try:
        ad.set_editor_property('snap_to_closest_frame_boundary', True)  # 有的商城动作长度差半帧不在帧边界上，不对齐就导不进来
    except Exception:
        pass
    task.options = ui
    tools.import_asset_tasks([task])
    seq = unreal.load_asset(f'{DEST}/{name}')
    if not isinstance(seq, unreal.AnimSequence):
        unreal.log_error(f'[loco] 导入失败 {fbx}')
        continue
    m = meta.get(name, {})
    markers = m.get('markers', []) or find_plants(seq, m)
    if 'Attack' in name:
        markers = find_hits(seq, lib.get_sequence_length(seq))
    if markers:
        lib.remove_animation_sync_markers_by_track(seq, TRACK)
        if TRACK not in [str(t) for t in lib.get_animation_notify_track_names(seq)]:
            lib.add_animation_notify_track(seq, TRACK)
        for mk, t in markers:
            lib.add_animation_sync_marker(seq, mk, t, TRACK)
    unreal.EditorAssetLibrary.save_loaded_asset(seq, False)
    length = lib.get_sequence_length(seq)
    unreal.log(f'[loco] {name}: {length:.2f}s（原 {m.get("length", 0):.2f}s）标记 {[(s, round(t, 2)) for s, t in markers]}')
