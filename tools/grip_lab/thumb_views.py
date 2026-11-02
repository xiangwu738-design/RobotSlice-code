# 拇指握法三视图（10-31 握持第二版）：按 grip_core.py 的解法摆武器、弯手指，每个案例出三张 Workbench 特写，看拇指是「顺着柄躺着」还是「绕过柄压在四指上」。
#   blender -b lookdev/rig/SKM_Robot_v3.blend -P tools/grip_lab/thumb_views.py -- <输出目录> <old|new> [案例名前缀 ...]
#   old = 第一版拇指（C++ 的 -MechGripV1），new = 第二版。日志 [lab] 每个案例一行（各指弯曲角、拇指的 α/β/扭/二三节、拇指肚 / 尖离目标）。
#   图：<模式>_<案例>_1.png 顺着柄轴从小指那侧看截面（离柄心 1.2 厘米处裁掉近处：四指一边包、拇指另一边包），
#       _2.png 从柄外侧偏拇指看（拇指压在四指上的那一面），_3.png 从拇指那侧斜看；白色小球 = 拇指目标点。
#   环境变量 THUMB_LAB_EXTRA=某 .py：先 exec 它（临时加案例，比如充能步枪不同的握把斜度 tilt）。
import bpy
import sys
import math
import os
from mathutils import Vector, Matrix

GRIP_LAB = os.path.dirname(os.path.abspath(__file__))
exec(open(os.path.join(GRIP_LAB, 'grip_core.py'), encoding='utf-8').read())
LAB_EXTRA = os.environ.get('THUMB_LAB_EXTRA')
if LAB_EXTRA:
    exec(open(LAB_EXTRA, encoding='utf-8').read())

args = sys.argv[sys.argv.index('--') + 1:]
OUT, MODE = args[0], args[1]
ONLY = args[2:]
os.makedirs(OUT, exist_ok=True)
ROOT = 'D:/RobotSlice'

cam = setup()
sc = bpy.context.scene
sc.render.resolution_x = sc.render.resolution_y = 420
for case in CASES:
    name = case['name']
    if ONLY and not any(name.startswith(o) for o in ONLY):
        continue
    s = case['side']
    w = load_weapon(f"{ROOT}/SourceAssets/{case['fbx']}.fbx")
    H, k, n, d = hand_frame_rest(s)
    Mw, shape = place_canonical(s, case)
    w.matrix_world = Mw
    mats, log = solve(s, shape, trigger=case.get('trigger', False), v1=(MODE == 'old'))
    apply_pose(mats)
    print(f'[lab] {MODE} {name} {log}', flush=True)
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.45, location=LAST_TARGET)
    mark = bpy.context.active_object
    A = shape.A
    C = shape.C
    tgt = H + d * 5.5 + n * 3.0 + k * 0.5
    # 1：顺着柄轴从小指那侧看，近处裁掉
    dirA = -A if A.dot(k) > 0 else A
    cam.location = C + dirA * 60
    cam.rotation_mode = 'QUATERNION'
    cam.rotation_quaternion = (-dirA).to_track_quat('-Z', 'Y')
    cam.data.clip_start = 60 - 1.2
    cam.data.lens = 150
    sc.render.filepath = f'{OUT}/{MODE}_{name}_1.png'
    bpy.ops.render.render(write_still=True)
    cam.data.clip_start = 1
    cam.data.lens = 70
    # 2：从柄外侧偏拇指看
    shoot(cam, tgt, (n * 1.0 + k * 0.55 - d * 0.2), 32, f'{OUT}/{MODE}_{name}_2.png')
    # 3：从拇指那侧斜看
    shoot(cam, tgt, (k * 1.0 + n * 0.45 - d * 0.35), 32, f'{OUT}/{MODE}_{name}_3.png')
    bpy.data.objects.remove(w, do_unlink=True)
    bpy.data.objects.remove(mark, do_unlink=True)
    for pb in arm.pose.bones:
        pb.matrix_basis.identity()
