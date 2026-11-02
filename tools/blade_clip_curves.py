"""刀攻击片段的手 / 刀尖轨迹（Blender 命令行，不碰 UE）：把动作 FBX 一个个读进来，按 60 帧/秒取每帧几根骨头在骨架空间的位置，
写成 JSON 给 tools/blade_clip_phases.py 分析（命中、起手、收刀各在什么时刻，段与段之间的姿势差多少）。
blender -b --factory-startup -P tools/blade_clip_curves.py -- <输出.json> <动作.fbx 通配符> [...]"""
import glob
import json
import sys

import bpy

args = sys.argv[sys.argv.index('--') + 1:]
out_path = args[0]
files = []
for pat in args[1:]:
    files += sorted(glob.glob(pat, recursive=True))
FPS = 60.0
BONES = ['pelvis', 'spine_05', 'head', 'clavicle_l', 'upperarm_l', 'lowerarm_l', 'hand_l', 'clavicle_r', 'upperarm_r', 'lowerarm_r', 'hand_r',
         'thigh_l', 'calf_l', 'foot_l', 'thigh_r', 'calf_r', 'foot_r']
scene = bpy.context.scene
result = {}
for f in files:
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    for a in list(bpy.data.actions):
        bpy.data.actions.remove(a)
    try:
        bpy.ops.import_scene.fbx(filepath=f, automatic_bone_orientation=False)
    except Exception as e:
        print('[curves] 读不了', f, e)
        continue
    arm = next((o for o in bpy.data.objects if o.type == 'ARMATURE'), None)
    if not arm or not arm.animation_data or not arm.animation_data.action:
        continue
    act = arm.animation_data.action
    f0, f1 = act.frame_range
    src_fps = scene.render.fps / scene.render.fps_base
    length = (f1 - f0) / src_fps
    n = int(round(length * FPS))
    # 场景单位换成厘米：按绑定姿势里头顶高度估（人形约 160–175 厘米）
    scene.frame_set(int(f0))
    hz = (arm.matrix_world @ arm.pose.bones['head'].matrix).translation.z
    UNIT = 100.0 if hz < 5 else 1.0
    frames = []
    for i in range(n + 1):
        fr = f0 + i / FPS * src_fps
        scene.frame_set(int(fr), subframe=fr - int(fr))
        row = {}
        for b in BONES:
            pb = arm.pose.bones.get(b)
            if pb is None:
                continue
            m = arm.matrix_world @ pb.matrix
            p = m.translation
            # 手骨的三根轴（骨架空间）：刀身方向在手骨坐标里，按轴换算刀尖
            row[b] = [round(p.x * UNIT, 3), round(p.y * UNIT, 3), round(p.z * UNIT, 3)]  # 厘米
            if b in ('hand_r', 'hand_l'):
                r = m.to_3x3().normalized()
                row[b + '_ax'] = [[round(r[k][j], 4) for k in range(3)] for j in range(3)]
        frames.append(row)
    name = f.replace('\\', '/').split('/')
    result[name[-2] + '/' + name[-1][:-4]] = {'length': length, 'fps': FPS, 'frames': frames}
    print('[curves]', name[-2], name[-1], '%.3f 秒' % length, len(frames), '帧')
json.dump(result, open(out_path, 'w', encoding='utf-8'), ensure_ascii=False)
print('[curves] 写好', out_path, len(result), '个')
