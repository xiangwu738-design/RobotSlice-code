"""11-02 自建武器（去网盘素材）· 步枪试点：无托步枪，按代码里 SM_Gun3 的标定点从零建。
用户：「武器模型也能模仿现在的精度制作吗」「可以，尽量精细」。
  blender -b --factory-startup -P tools/blender_weapon_rifle.py -- <输出目录>
坐标（米，导出 apply_unit_scale → UE 厘米，和原来的武器同一套：原点在握把附近、枪管沿 +X、上 +Z）：
  枪口 (0.77, 0, 0.17)（MechWeapons.cpp 步枪那一行的枪口），
  右手握把：中心 (-0.018, 0, 0.007)、轴 (0.2, 0, 0.98)、前后半厚 0.044、左右半宽 0.022、圆角 0.016、沿轴 -0.062..0.07（SetGrip），
  左手护木：中心 (0.30, 0, 0.115)、左右半宽 0.016、上下半高 0.025。
材质槽直接用机体的材质实例名（MI_Robot_*），导入后挂 /Game/Robot/Materials 的同名实例：涂装跟机体，MI_Robot_Glow 由代码换成队伍色。
导出前：30° 硬边 + 加权法线（同机体导出的法线修正）。输出 <输出目录>/SM_Gun3.fbx、预览图 SM_Gun3_*.png。"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

OUT = os.path.abspath(sys.argv[sys.argv.index('--') + 1])
os.makedirs(OUT, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene

COL = {'MI_Robot_PaintDark': (0.20, 0.22, 0.24), 'MI_Robot_Paint': (0.42, 0.45, 0.48), 'MI_Robot_Frame': (0.12, 0.125, 0.13),
       'MI_Robot_Accent': (0.81, 0.37, 0.11), 'MI_Robot_Glow': (0.2, 0.85, 1.0), 'MI_Robot_Plastic': (0.06, 0.065, 0.07),
       'MI_Robot_Rubber': (0.04, 0.04, 0.045), 'MI_Robot_Lens': (0.05, 0.12, 0.14)}
MATS = {}
for n, c in COL.items():
    m = bpy.data.materials.new(n)
    m.diffuse_color = (*c, 1)
    MATS[n] = m
PARTS = {n: bmesh.new() for n in COL}


def _merge(dst, tmp, matrix=None):
    if matrix is not None:
        bmesh.ops.transform(tmp, matrix=matrix, verts=tmp.verts)
    me = bpy.data.meshes.new('_t')
    tmp.to_mesh(me)
    tmp.free()
    dst.from_mesh(me)
    bpy.data.meshes.remove(me)


def box(mat, lo, hi, bevel=0.0, segs=2, matrix=None):
    t = bmesh.new()
    bmesh.ops.create_cube(t, size=1.0)
    lo, hi = Vector(lo), Vector(hi)
    s = hi - lo
    bmesh.ops.transform(t, matrix=Matrix.Translation((lo + hi) / 2) @ Matrix.Diagonal((s.x, s.y, s.z, 1)), verts=t.verts)
    if bevel > 0:
        bmesh.ops.bevel(t, geom=list(t.edges), offset=min(bevel, min(s) * 0.45), segments=segs, affect='EDGES', profile=0.5)
    _merge(PARTS[mat], t, matrix)


def cyl(mat, p0, p1, r0, r1=None, segs=24, bevel=0.0, matrix=None):
    r1 = r0 if r1 is None else r1
    p0, p1 = Vector(p0), Vector(p1)
    d = p1 - p0
    t = bmesh.new()
    bmesh.ops.create_cone(t, cap_ends=True, cap_tris=False, segments=segs, radius1=r0, radius2=r1, depth=d.length)
    if bevel > 0:
        caps = [e for e in t.edges if all(abs(abs(v.co.z) - d.length / 2) < 1e-6 for v in e.verts)]
        bmesh.ops.bevel(t, geom=caps, offset=bevel, segments=2, affect='EDGES', profile=0.5)
    rot = Vector((0, 0, 1)).rotation_difference(d.normalized()).to_matrix().to_4x4()
    m = Matrix.Translation((p0 + p1) / 2) @ rot
    _merge(PARTS[mat], t, matrix @ m if matrix is not None else m)


def tube_path(mat, pts, r, segs=10):
    t = bmesh.new()
    rings = []
    for i, p in enumerate(pts):
        p = Vector(p)
        a = Vector(pts[max(i - 1, 0)])
        b = Vector(pts[min(i + 1, len(pts) - 1)])
        tg = (b - a).normalized()
        side = tg.cross(Vector((0, 1, 0)))
        if side.length < 1e-4:
            side = tg.cross(Vector((0, 0, 1)))
        side.normalize()
        up = side.cross(tg).normalized()
        rings.append([t.verts.new(p + r * (math.cos(2 * math.pi * k / segs) * side + math.sin(2 * math.pi * k / segs) * up)) for k in range(segs)])
    for a, b in zip(rings, rings[1:]):
        for k in range(segs):
            t.faces.new((a[k], a[(k + 1) % segs], b[(k + 1) % segs], b[k]))
    t.faces.new(list(reversed(rings[0])))
    t.faces.new(rings[-1])
    _merge(PARTS[mat], t)


def both_sides(fn, *a, **k):
    """左右对称件：y 取正负各一份（参数里的 y 用 Y 占位符写正值，这里镜像）"""
    fn(*a, **k)
    fn(*a, **dict(k, matrix=Matrix.Scale(-1, 4, (0, 1, 0))))


Z = 0.17          # 枪膛轴高度（枪口 z）
HW = 0.028        # 机匣半宽

# ---------------------------------------------------------------- 机匣（无托：弹匣在握把后面，枪托就是机匣后段）
box('MI_Robot_PaintDark', (-0.31, -HW, 0.075), (0.18, HW, 0.205), bevel=0.008)
box('MI_Robot_PaintDark', (-0.33, -HW - 0.002, 0.02), (-0.20, HW + 0.002, 0.2), bevel=0.01)                 # 后段（贴腮 + 枪托）
box('MI_Robot_Paint', (-0.29, -HW - 0.003, 0.10), (0.12, HW + 0.003, 0.185), bevel=0.004)                    # 侧面亮色装甲板（左右各一块，薄）
box('MI_Robot_PaintDark', (0.12, -HW + 0.004, 0.09), (0.20, HW - 0.004, 0.2), bevel=0.012)                   # 机匣前端收口
box('MI_Robot_Rubber', (-0.348, -HW - 0.004, 0.015), (-0.33, HW + 0.004, 0.205), bevel=0.006)                # 托底板
for z in (0.05, 0.09, 0.13, 0.17):
    box('MI_Robot_Frame', (-0.351, -HW + 0.006, z - 0.004), (-0.345, HW - 0.006, z + 0.004))                 # 托底板防滑槽
box('MI_Robot_Frame', (-0.30, -0.012, 0.0), (-0.23, 0.012, 0.022), bevel=0.004)                              # 托下面的背带环座
tube_path('MI_Robot_Frame', [(-0.29, 0, 0.002), (-0.285, 0, -0.012), (-0.265, 0, -0.018), (-0.245, 0, -0.012), (-0.24, 0, 0.002)], 0.004, 8)
# 抛壳窗（右侧凹进去的一块）+ 防尘盖
box('MI_Robot_Frame', (-0.13, HW - 0.001, 0.125), (-0.03, HW + 0.002, 0.17), bevel=0.003)
box('MI_Robot_PaintDark', (-0.125, HW + 0.002, 0.13), (-0.035, HW + 0.004, 0.165), bevel=0.002)
# 拉机柄（左侧）
box('MI_Robot_Frame', (0.06, -HW - 0.004, 0.165), (0.15, -HW + 0.001, 0.178), bevel=0.002)
box('MI_Robot_Frame', (0.13, -HW - 0.02, 0.163), (0.148, -HW - 0.004, 0.18), bevel=0.004)
# 侧面：点缀条、发光条（队伍色）、螺丝
both_sides(box, 'MI_Robot_Accent', (-0.27, HW + 0.002, 0.186), (0.10, HW + 0.005, 0.193))
both_sides(box, 'MI_Robot_Glow', (-0.06, HW + 0.002, 0.106), (0.09, HW + 0.004, 0.111))
for x in (-0.27, -0.17, -0.07, 0.03, 0.10):
    both_sides(cyl, 'MI_Robot_Frame', (x, HW + 0.001, 0.115), (x, HW + 0.006, 0.115), 0.0045, segs=10)
# 顶面：皮卡汀尼导轨 + 齿
box('MI_Robot_Frame', (-0.14, -0.012, 0.204), (0.17, 0.012, 0.214), bevel=0.002)
for i in range(16):
    x = -0.13 + i * 0.019
    box('MI_Robot_Frame', (x, -0.013, 0.214), (x + 0.011, 0.013, 0.221), bevel=0.0015)

# ---------------------------------------------------------------- 全息瞄具
box('MI_Robot_PaintDark', (-0.02, -0.02, 0.221), (0.10, 0.02, 0.232), bevel=0.003)                           # 底座
box('MI_Robot_PaintDark', (0.0, -0.024, 0.232), (0.105, 0.024, 0.248), bevel=0.004)                          # 下框
for s in (-1, 1):
    box('MI_Robot_PaintDark', (0.0, s * 0.024 - 0.004, 0.246), (0.105, s * 0.024 + 0.004, 0.282), bevel=0.003)  # 两侧框
box('MI_Robot_PaintDark', (0.0, -0.028, 0.28), (0.105, 0.028, 0.29), bevel=0.004)                            # 顶框
box('MI_Robot_Lens', (0.085, -0.02, 0.248), (0.088, 0.02, 0.28))                                            # 前镜片
box('MI_Robot_Lens', (0.012, -0.02, 0.248), (0.015, 0.02, 0.28))                                            # 后镜片
box('MI_Robot_Glow', (0.05, -0.002, 0.262), (0.053, 0.002, 0.266))                                          # 红点（队伍色）
cyl('MI_Robot_Frame', (0.04, -0.028, 0.237), (0.04, -0.034, 0.237), 0.006, segs=12)                          # 调节旋钮
cyl('MI_Robot_Frame', (0.07, -0.028, 0.237), (0.07, -0.034, 0.237), 0.005, segs=12)

# ---------------------------------------------------------------- 护木（左手托这里：中心 x 0.30、宽 3.2 厘米、高 5 厘米）+ 枪管护套 + 枪管 + 制退器
box('MI_Robot_PaintDark', (0.19, -0.016, 0.09), (0.43, 0.016, 0.142), bevel=0.005)
for i in range(6):
    x = 0.22 + i * 0.033
    both_sides(box, 'MI_Robot_Frame', (x, 0.0155, 0.103), (x + 0.018, 0.0175, 0.13), bevel=0.002)             # 散热槽
box('MI_Robot_Frame', (0.19, -0.010, 0.14), (0.43, 0.010, 0.16), bevel=0.002)                               # 护木和枪管之间的脊
box('MI_Robot_Frame', (0.24, -0.006, 0.084), (0.40, 0.006, 0.09), bevel=0.001)                              # 护木下沿导轨
cyl('MI_Robot_Frame', (0.18, 0, Z), (0.62, 0, Z), 0.024, segs=24, bevel=0.002)                              # 枪管护套
for x in (0.24, 0.32, 0.40, 0.48, 0.56):
    cyl('MI_Robot_PaintDark', (x, 0, Z), (x + 0.012, 0, Z), 0.0265, segs=24)                                # 护套箍
for i in range(7):
    x = 0.255 + i * 0.045
    box('MI_Robot_PaintDark', (x, -0.006, Z + 0.021), (x + 0.025, 0.006, Z + 0.027), bevel=0.002)             # 顶上散热孔盖
cyl('MI_Robot_Frame', (0.62, 0, Z), (0.70, 0, Z), 0.0135, segs=20)                                          # 枪管
cyl('MI_Robot_Frame', (0.64, 0, Z), (0.655, 0, Z), 0.018, segs=20)                                          # 准星座箍
cyl('MI_Robot_PaintDark', (0.70, 0, Z), (0.77, 0, Z), 0.021, segs=20, bevel=0.002)                          # 制退器
for x in (0.71, 0.73, 0.75):
    both_sides(box, 'MI_Robot_Frame', (x, 0.016, Z - 0.009), (x + 0.012, 0.0215, Z + 0.009))                   # 制退器侧孔
cyl('MI_Robot_Frame', (0.765, 0, Z), (0.771, 0, Z), 0.016, segs=20)                                         # 枪口环

# ---------------------------------------------------------------- 握把（按 SetGrip 的数）+ 扳机护圈 + 扳机
u = Vector((0.2, 0, 0.98)).normalized()
d = Vector((u.z, 0, -u.x))                       # 握把正前方
n = Vector((0, 1, 0))
c = Vector((-0.018, 0, 0.007)) + u * ((-0.062 + 0.07) / 2)
R = Matrix((d, n, u)).transposed().to_4x4()
G = Matrix.Translation(c) @ R
box('MI_Robot_Plastic', (-0.044, -0.022, -0.066), (0.044, 0.022, 0.066), bevel=0.016, segs=3, matrix=G)
for k in range(3):                               # 前面三道指槽
    t = -0.04 + k * 0.03
    cyl('MI_Robot_Plastic', (0.04, -0.02, t), (0.04, 0.02, t), 0.009, segs=12, matrix=G)
for k in range(5):                               # 两侧防滑纹
    t = -0.045 + k * 0.02
    both_sides(box, 'MI_Robot_Rubber', (-0.03, 0.0215, t - 0.004), (0.03, 0.0235, t + 0.004), matrix=G)
box('MI_Robot_Frame', (-0.042, -0.02, -0.074), (0.042, 0.02, -0.064), bevel=0.004, matrix=G)                  # 握把底盖
tube_path('MI_Robot_Frame', [(0.04, 0, 0.076), (0.085, 0, 0.072), (0.098, 0, 0.045), (0.088, 0, 0.012), (0.06, 0, 0.0)], 0.006, 10)   # 扳机护圈
box('MI_Robot_Frame', (0.052, -0.004, 0.035), (0.062, 0.004, 0.072), bevel=0.003)                             # 扳机
box('MI_Robot_Frame', (0.045, -0.012, 0.06), (0.06, 0.012, 0.078), bevel=0.002)                               # 扳机座

# ---------------------------------------------------------------- 弹匣（在握把后面，略往前倾）+ 弹匣井 + 卡笋
box('MI_Robot_Frame', (-0.22, -0.026, 0.03), (-0.06, 0.026, 0.08), bevel=0.004)                               # 弹匣井
M = Matrix.Translation((-0.145, 0, -0.035)) @ Matrix.Rotation(math.radians(-8), 4, 'Y')
box('MI_Robot_PaintDark', (-0.045, -0.018, -0.09), (0.045, 0.018, 0.075), bevel=0.005, matrix=M)
for z in (-0.06, -0.02, 0.02):
    both_sides(box, 'MI_Robot_Frame', (-0.04, 0.0175, z - 0.006), (0.04, 0.020, z + 0.006), bevel=0.002, matrix=M)   # 弹匣加强筋
box('MI_Robot_Frame', (-0.05, -0.021, -0.102), (0.05, 0.021, -0.088), bevel=0.004, matrix=M)                  # 弹匣底板
box('MI_Robot_Accent', (-0.048, -0.0215, -0.097), (-0.03, 0.0215, -0.093), matrix=M)                          # 底板点缀
box('MI_Robot_Frame', (-0.065, -0.008, 0.035), (-0.055, 0.008, 0.06), bevel=0.002)                            # 弹匣卡笋

# ---------------------------------------------------------------- 合成、法线、导出
objs = []
for name, bm in PARTS.items():
    if not bm.verts:
        bm.free()
        continue
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    me.materials.append(MATS[name])
    ob = bpy.data.objects.new(name, me)
    sc.collection.objects.link(ob)
    objs.append(ob)
bpy.ops.object.select_all(action='DESELECT')
for o in objs:
    o.select_set(True)
bpy.context.view_layer.objects.active = objs[0]
bpy.ops.object.join()
gun = bpy.context.active_object
gun.name = gun.data.name = 'SM_Gun3'
me = gun.data
me.polygons.foreach_set('use_smooth', [True] * len(me.polygons))
me.set_sharp_from_angle(angle=math.radians(30))
wn = gun.modifiers.new('wn', 'WEIGHTED_NORMAL')
wn.mode, wn.weight, wn.keep_sharp = 'FACE_AREA', 50, True
bpy.ops.object.modifier_apply(modifier='wn')
uv = me.uv_layers.new(name='UVMap')        # 材质不靠 UV（机体材质按位置算），给一层免得导入器报警
me.calc_loop_triangles()
lo = Vector([min(v.co[i] for v in me.vertices) for i in range(3)])
hi = Vector([max(v.co[i] for v in me.vertices) for i in range(3)])
print(f'[weapon] SM_Gun3（自建）三角 {len(me.loop_triangles)}，范围 x {lo.x:.3f}..{hi.x:.3f} y {lo.y:.3f}..{hi.y:.3f} z {lo.z:.3f}..{hi.z:.3f} 米，'
      f'槽 {[m.name for m in me.materials]}', flush=True)
bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, 'SM_Gun3.fbx'), use_selection=True, object_types={'MESH'}, apply_unit_scale=True,
                         mesh_smooth_type='FACE')

# ---------------------------------------------------------------- 预览（Workbench、工作室光、按材质上色）
sc.render.engine = 'BLENDER_WORKBENCH'
sh = sc.display.shading
sh.light, sh.color_type, sh.show_cavity, sh.show_specular_highlight = 'STUDIO', 'MATERIAL', True, True
sh.background_type, sh.background_color = 'VIEWPORT', (0.42, 0.44, 0.47)
sc.render.resolution_x, sc.render.resolution_y = 1400, 700
cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
sc.collection.objects.link(cam)
sc.camera = cam
ctr = (lo + hi) / 2
for name, dvec, ortho in (('左侧', (0, -1, 0), 1.25), ('右前', (0.8, 0.9, 0.35), None), ('左后上', (-0.7, -0.8, 0.6), None)):
    dv = Vector(dvec).normalized()
    if ortho:
        cam.data.type, cam.data.ortho_scale = 'ORTHO', ortho
        cam.location = ctr + dv * 3
    else:
        cam.data.type, cam.data.lens = 'PERSP', 50
        cam.location = ctr + dv * 1.45
    cam.rotation_euler = (ctr - cam.location).to_track_quat('-Z', 'Y').to_euler()
    sc.render.filepath = os.path.join(OUT, f'SM_Gun3_{name}.png')
    bpy.ops.render.render(write_still=True)
print('[weapon] 预览图', OUT, flush=True)
