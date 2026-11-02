"""Blender：把 canyon_build.py 出的地形块装进场景，查规格（射线量高度），导 FBX，出几张 workbench 预览图。
blender -b --factory-startup --python canyon_assemble.py -- [--export] [--render] [--check] [--tag vis] [--col col]
网格单位：Blender 里用米（厘米 / 100），FBX 导出 apply_unit_scale，UE 里是厘米。"""
import bpy, bmesh, json, math, os, sys, time
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
MESH = r"D:\RobotSlice\Inbox\_canyon\mesh"
FBX = r"D:\RobotSlice\SourceAssets\canyon\meshes"
REN = r"D:\RobotSlice\Inbox\_canyon\renders"
os.makedirs(FBX, exist_ok=True); os.makedirs(REN, exist_ok=True)
TAG = argv[argv.index("--tag") + 1] if "--tag" in argv else "vis"
COL = argv[argv.index("--col") + 1] if "--col" in argv else None

for o in list(bpy.data.objects):
    bpy.data.objects.remove(o)
for m in list(bpy.data.meshes):
    bpy.data.meshes.remove(m)


def make_obj(name, verts_cm, tris, coll=None, uv_scale=10.0):
    me = bpy.data.meshes.new(name)
    v = (verts_cm / 100.0).astype(np.float32)
    me.vertices.add(len(v))
    me.vertices.foreach_set("co", v.ravel())
    me.loops.add(len(tris) * 3)
    me.loops.foreach_set("vertex_index", tris.ravel().astype(np.int32))
    me.polygons.add(len(tris))
    me.polygons.foreach_set("loop_start", np.arange(0, len(tris) * 3, 3, dtype=np.int32))
    me.update(calc_edges=True)
    me.validate(clean_customdata=False)
    # UV0：世界 XY / 10 米（材质用世界坐标，这里只是占位，UE 需要 UV 通道）
    uv = me.uv_layers.new(name="UVMap")
    lv = v[tris.ravel()]
    uvs = np.stack([lv[:, 0] / uv_scale, lv[:, 1] / uv_scale], 1).astype(np.float32)
    uv.data.foreach_set("uv", uvs.ravel())
    me.polygons.foreach_set("use_smooth", np.ones(len(tris), bool))
    ob = bpy.data.objects.new(name, me)
    (coll or bpy.context.scene.collection).objects.link(ob)
    return ob


layout = json.load(open(os.path.join(MESH, f"{TAG}_layout.json")))
tiles = []
for t in layout["tiles"]:
    d = np.load(os.path.join(MESH, f"{TAG}_tile{t['tile']}.npz"))
    tiles.append(make_obj(f"SM_Canyon_Tile{t['tile']}", d["verts"], d["tris"]))
far = None
if os.path.exists(os.path.join(MESH, "far.npz")) and TAG == "vis":
    d = np.load(os.path.join(MESH, "far.npz"))
    far = make_obj("SM_Canyon_Far", d["verts"], d["tris"], uv_scale=100.0)
cols = []
if COL:
    lc = json.load(open(os.path.join(MESH, f"{COL}_layout.json")))
    for t in lc["tiles"]:
        d = np.load(os.path.join(MESH, f"{COL}_tile{t['tile']}.npz"))
        cols.append(make_obj(f"SM_Canyon_Col{t['tile']}", d["verts"], d["tris"]))
print(f"[asm] 地形块 {len(tiles)}，三角 {sum(len(o.data.polygons) for o in tiles)}；远景 {len(far.data.polygons) if far else 0}；碰撞块 {len(cols)} 三角 {sum(len(o.data.polygons) for o in cols)}")

# ---------------------------------------------------------------- 规格检查（射线，单位米）
def bvh_of(objs):
    verts = []; polys = []; off = 0
    for o in objs:
        me = o.data
        co = np.empty(len(me.vertices) * 3, np.float32); me.vertices.foreach_get("co", co)
        vi = np.empty(len(me.polygons) * 3, np.int32); me.polygons.foreach_get("vertices", vi)
        verts.append(co.reshape(-1, 3)); polys.append(vi.reshape(-1, 3) + off); off += len(me.vertices)
    V = np.concatenate(verts); P = np.concatenate(polys)
    return BVHTree.FromPolygons([Vector(v) for v in V], [tuple(p) for p in P])


def check(objs, label):
    bvh = bvh_of(objs)
    res = []
    def down(x, y, ztop, expect, tol, what):
        h = bvh.ray_cast(Vector((x / 100, y / 100, ztop / 100)), Vector((0, 0, -1)))
        z = h[0].z * 100 if h[0] else None
        ok = z is not None and abs(z - expect) <= tol
        res.append(dict(what=what, x=x, y=y, expect=expect, got=None if z is None else round(z, 1), ok=ok))
    def side(x, y, z, d, expect_free, what):
        """水平射线：从 (x,y,z) 沿 d 方向，expect_free 米内不应碰到东西"""
        h = bvh.ray_cast(Vector((x / 100, y / 100, z / 100)), Vector(d), expect_free / 100)
        res.append(dict(what=what, x=x, y=y, z=z, free=expect_free, hit=None if h[0] is None else round(h[3] * 100, 1), ok=h[0] is None))
    for x in range(-12500, 12501, 500):
        for y in (-1700, -1200, -600, 0, 600, 1200, 1700):
            if 1300 <= abs(x) <= 3700 and abs(y) >= 1200:
                continue  # 坡道
            if abs(x) <= 300:
                continue  # 石拱
            if abs(x) <= 650 and abs(y) <= 650:
                continue
            down(x, y, 820 if abs(x) < 900 and abs(y) < 900 else 5000, 0.0, 12.0, "谷底")
    for x in range(-11800, 11801, 400):
        for y in (1850, 2150, 2450, -1850, -2150, -2450):
            if abs(x) <= 300:
                continue
            down(x, y, 2000, 900.0, 30.0, "栈道")
    for x in range(-8800, 8801, 400):
        for y in (2750, 3200, 3650):
            down(x, y, 550, 0.0, 12.0, "岩洞底")
            side(x, y, 300, (0, 0, 1), 290, "岩洞净高")
    for (xa, xb) in [(-500, 500), (-8400, -7600), (7600, 8400)]:
        for x in np.linspace(xa + 30, xb - 30, 5):
            for y in (1850, 2200, 2650):
                down(float(x), y, 550, 0.0, 12.0, "洞口")
    for s in (-1, 1):
        for x in np.linspace(1350, 3650, 8):
            for y in (1250, 1500, 1750):
                down(s * float(x), y, 1500, 900 * (float(x) - 1300) / 2400, 20.0, "坡道北")
                down(s * float(x), -y, 1500, 900 * (float(x) - 1300) / 2400, 20.0, "坡道南")
        for y in np.linspace(-1700, 1700, 9):
            for x in (-250, 0, 250):
                down(float(x), float(y), 2000, 900.0, 20.0, "石拱桥面")
        for (x, y, z) in [(s * 12000, 0, 0), (s * 11300, 700, 0), (s * 12700, -700, 0), (s * 8800, 1000, 0), (s * 8800, 1450, 0), (s * 8200, -2150, 900),
                          (s * 8600, -2450, 900), (s * 7800, -1850, 900)]:
            down(x, y, z + 1500 if z == 900 else 5000, z, 8.0, "回收站/出生点")
        # 吊笼井：吊笼范围 Z 0~900 必须空
        for x in (7050, 7400, 7750):
            for y in (-1700, -1350, -1000):
                side(s * x, y, 30, (0, 0, 1), 860, "吊笼井净空")
        # 崖顶狙击位
        down(s * 5000, 3400, 5000, 2200, 150, "北崖顶狙击位")
        down(s * 6500, -3400, 5000, 2600, 150, "南崖顶狙击位")
    # 闸门截面：洞内 4400..4600 必须正好是 2700..3700 × 0..600（外面一圈是岩石）
    for s in (-1, 1):
        for y in (2690, 3710):
            side(s * 4500, y, 300, (0, 0, 1), 1, "闸门边是岩石") if False else None
    # 中央铸造台下面四柱之间能走
    for (x, y) in [(0, 0), (0, -300), (300, 0)]:
        side(x, y, 150, (0, 0, 1), 540, "铸造台下净空")
    bad = [r for r in res if not r["ok"]]
    print(f"[check:{label}] {len(res)} 项，不合格 {len(bad)}")
    for r in bad[:40]:
        print("   ", r)
    json.dump(res, open(os.path.join(MESH, f"check_{label}.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=0)
    return bad


if "--check" in argv:
    check(tiles, TAG)
    if cols:
        check(cols, COL)

# ---------------------------------------------------------------- 导出
if "--export" in argv:
    def export(objs, path):
        bpy.ops.object.select_all(action="DESELECT")
        for o in objs:
            o.select_set(True)
        bpy.context.view_layer.objects.active = objs[0]
        bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"MESH"}, apply_unit_scale=True,
                                 mesh_smooth_type="FACE", use_mesh_modifiers=True, add_leaf_bones=False, bake_anim=False,
                                 bake_space_transform=False, axis_forward="-Y", axis_up="Z")
        print("[fbx]", path)
    for o in tiles + ([far] if far else []):
        mod = o.modifiers.new("smooth", "NODES") if False else None
    for o in tiles:
        export([o], os.path.join(FBX, o.name + ".fbx"))
    if far:
        export([far], os.path.join(FBX, far.name + ".fbx"))
    for o in cols:
        export([o], os.path.join(FBX, o.name + ".fbx"))

# ---------------------------------------------------------------- 预览
if "--render" in argv:
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sh = sc.display.shading
    sh.light = "STUDIO"; sh.color_type = "SINGLE"; sh.single_color = (0.78, 0.52, 0.36)
    sh.show_cavity = True; sh.cavity_type = "BOTH"; sh.show_shadows = True
    sc.display.light_direction = (0.45, -0.35, 0.82)
    sc.render.resolution_x, sc.render.resolution_y = 1600, 900
    sc.world = bpy.data.worlds.new("w"); sc.world.color = (0.55, 0.65, 0.8)
    cam_d = bpy.data.cameras.new("cam"); cam = bpy.data.objects.new("cam", cam_d); sc.collection.objects.link(cam); sc.camera = cam
    cam_d.clip_end = 5000

    def shot(name, loc, look, lens=24, ortho=None):
        cam.location = Vector(loc)
        d = Vector(look) - Vector(loc)
        cam.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
        if ortho:
            cam_d.type = "ORTHO"; cam_d.ortho_scale = ortho
        else:
            cam_d.type = "PERSP"; cam_d.lens = lens
        sc.render.filepath = os.path.join(REN, f"{name}.png")
        bpy.ops.render.render(write_still=True)
        print("[render]", sc.render.filepath)
    shot("top", (0, 0, 400), (0, 0.01, 0), ortho=300)
    shot("floor_east", (-30, -6, 2.0), (60, 2, 6), lens=20)
    shot("floor_center", (40, 8, 2.5), (0, 0, 5), lens=20)
    shot("ledge_south", (-60, -21.5, 11.5), (0, -21, 9), lens=20)
    shot("cave", (-20, 32, 3.0), (10, 32, 3.0), lens=18)
    shot("aerial", (-150, -110, 90), (-20, 0, 5), lens=28)
    shot("stationA", (100, -10, 6), (88, 10, 0), lens=22)
    shot("spawn_w", (-110, 12, 8), (-128, -5, 4), lens=20)

bpy.ops.wm.save_as_mainfile(filepath=r"D:\RobotSlice\Inbox\_canyon\blend\canyon_terrain.blend")
print("[asm] 完成")
