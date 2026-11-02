"""双手动作里左手握点在右手坐标系（k=往拇指、n=手心、d=往指尖，厘米）里的位置：推出柄轴方向。
blender -b --factory-startup -P left_rel.py -- 动作.fbx ..."""
import bpy
import sys
import math
from mathutils import Vector

files = sys.argv[sys.argv.index('--') + 1:]


def frame(arm, s):
    P = lambda b: arm.matrix_world @ arm.pose.bones[b].head
    H = P('hand_' + s)
    d = (P('middle_01_' + s) - H).normalized()
    n = d.cross(P('pinky_01_' + s) - P('index_01_' + s)).normalized()
    if n.dot(P('thumb_03_' + s) - H) < 0:
        n = -n
    k = d.cross(n)
    if k.dot(P('thumb_01_' + s) - H) < 0:
        k = -k
    return H, k, n, d


for f in files:
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    bpy.ops.import_scene.fbx(filepath=f, automatic_bone_orientation=False)
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    act = arm.animation_data.action
    f0, f1 = [int(v) for v in act.frame_range]
    unit = 100.0 / arm.matrix_world.to_scale().x if arm.matrix_world.to_scale().x < 0.5 else 100.0
    rows = []
    acc = Vector()
    nacc = 0
    for fr in range(f0, f1 + 1, max(1, (f1 - f0) // 8)):
        bpy.context.scene.frame_set(fr)
        Hr, kr, nr, dr = frame(arm, 'r')
        Hl, kl, nl, dl = frame(arm, 'l')
        sc = 100.0  # 世界米 → 厘米
        Rc = Hr + dr * 0.065 + nr * 0.03
        Lc = Hl + dl * 0.065 + nl * 0.03
        rel = (Lc - Rc)
        v = Vector((rel.dot(kr), rel.dot(nr), rel.dot(dr))) * sc
        # 左手手心朝向在右手坐标系里
        rows.append(f'f{fr}:({v.x:5.1f},{v.y:5.1f},{v.z:5.1f}) |{v.length:4.1f}| 左k·右k {kl.dot(kr):4.1f}')
        if v.length < 40:
            acc += v.normalized()
            nacc += 1
    m = acc.normalized() if nacc else acc
    print(f'[lr] {f.replace(chr(92), "/").rsplit("/", 2)[-2:]} 平均方向(k,n,d) ({m.x:.2f},{m.y:.2f},{m.z:.2f}) n={nacc}  ' + ' '.join(rows))
