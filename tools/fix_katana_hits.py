# 重标刀动作的 Hit 标记（和 ue_import_locomotion.py 的 find_hits 同一套规则：右手速度峰值、跳过开头 0.08 秒的姿势跳变），不重新导入。
# 用法：UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/fix_katana_hits.py"
import unreal

CLIPS = ['Combo1_Attack1_InPlace', 'Combo1_Attack2_InPlace', 'Combo1_Attack3_InPlace', 'Combo1_Attack4_InPlace', 'Attack2_InPlace']
CHAIN = ['root', 'pelvis', 'spine_01', 'spine_02', 'spine_03', 'spine_04', 'spine_05', 'clavicle_r', 'upperarm_r', 'lowerarm_r', 'hand_r']
TRACK = 'Feet'
FPS = 60
AL = unreal.AnimationLibrary


def hand_pos(seq, t):
    xf = unreal.Transform()
    for p in AL.get_bone_poses_for_time(seq, CHAIN, t, False):
        xf = p.multiply(xf)
    return xf.translation


for name in CLIPS:
    path = '/Game/Robot/Anims/Katana/' + name
    seq = unreal.load_asset(path)
    length = AL.get_sequence_length(seq)
    n = int(length * FPS)
    pts = [hand_pos(seq, i / FPS) for i in range(n + 1)]
    speed = [pts[i + 1].distance(pts[i]) * FPS for i in range(n)]
    skip = int(0.08 * FPS)
    top = max(speed[skip:])
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
    old = [round(m.time, 3) for m in AL.get_animation_sync_markers(seq) if str(m.marker_name) == 'Hit']
    AL.remove_animation_sync_markers_by_name(seq, 'Hit')
    if TRACK not in [str(t) for t in AL.get_animation_notify_track_names(seq)]:
        AL.add_animation_notify_track(seq, TRACK)
    for t in hits:
        AL.add_animation_sync_marker(seq, 'Hit', t, TRACK)
    unreal.EditorAssetLibrary.save_loaded_asset(seq, False)
    unreal.log('[hits] %s 旧 %s → 新 %s' % (name, old, [round(t, 3) for t in hits]))
