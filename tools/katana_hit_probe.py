# 查刀动作的命中时刻：打印每个片段的长度、Hit 标记时间，以及右手（组件空间）速度曲线（每 1/30 秒一格），
# 用来判断 Hit 标记是不是标在真正挥下去的那一刻。
# 用法：UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/katana_hit_probe.py"
import unreal

CLIPS = ['Combo1_Attack1_InPlace', 'Combo1_Attack2_InPlace', 'Combo1_Attack3_InPlace', 'Combo1_Attack4_InPlace', 'Attack2_InPlace']
CHAIN = ['root', 'pelvis', 'spine_01', 'spine_02', 'spine_03', 'spine_04', 'spine_05', 'clavicle_r', 'upperarm_r', 'lowerarm_r', 'hand_r']
AL = unreal.AnimationLibrary


def hand_pos(seq, t):
    poses = AL.get_bone_poses_for_time(seq, CHAIN, t, False)
    xf = unreal.Transform()
    for p in poses:
        xf = p.multiply(xf)
    return xf.translation


for name in CLIPS:
    seq = unreal.load_asset('/Game/Robot/Anims/Katana/' + name)
    if not seq:
        unreal.log_warning('[probe] 找不到 ' + name)
        continue
    length = AL.get_sequence_length(seq)
    markers = [(m.marker_name, round(m.time, 3)) for m in AL.get_animation_sync_markers(seq)]
    step = 1.0 / 30.0
    t = 0.0
    prev = hand_pos(seq, 0.0)
    speeds = []
    while t + step <= length + 1e-4:
        t += step
        cur = hand_pos(seq, t)
        speeds.append(int(prev.distance(cur) / step))
        prev = cur
    peak = max(range(len(speeds)), key=lambda i: speeds[i]) if speeds else -1
    unreal.log('[probe] %s 长 %.2f 秒 标记 %s 手速峰值 %.2f 秒（%d cm/s）' % (name, length, markers, (peak + 0.5) * step, speeds[peak] if speeds else 0))
    unreal.log('[probe]   手速 ' + ' '.join(str(v) for v in speeds))
