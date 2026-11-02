"""Blender + BlenderGIS：导入马布尔峡谷（Marble Canyon, AZ，纳瓦霍桥附近直段）的 3DEP 高程（ASCII 栅格，UTM 12N），
按地理参考放进场景，再旋转 / 缩放到游戏坐标（X 沿峡谷、Y 横向、谷底 Z=0，单位厘米），往下打射线烘成高度图。
插件只在本次会话里从 Inbox/_tools 加载，不写用户的 Blender 配置。
blender -b --factory-startup --python bgis_bake.py -- <out.npz> <out.blend>"""
import os, sys, json, math, time, traceback
os.environ["IMAGEIO_NO_INTERNET"] = "1"
import bpy, addon_utils
import numpy as np
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

argv = sys.argv[sys.argv.index("--") + 1:]
OUT_NPZ, OUT_BLEND = argv[0], argv[1]
DEM = r"D:\RobotSlice\Inbox\_canyon\dem\marble"
P = json.load(open(r"D:\RobotSlice\tools\canyon\canyon_frame.json"))

# ---- 加载 BlenderGIS
sd = bpy.context.preferences.filepaths.script_directories.new()
sd.directory = r"D:\RobotSlice\Inbox\_tools\BlenderGIS\src"
sd.name = "bgis_tools"
bpy.utils.refresh_script_paths()
addon_utils.modules_refresh()
mod = addon_utils.enable("BlenderGIS", default_set=True, handle_error=lambda e: traceback.print_exc())
assert mod is not None, "BlenderGIS 启用失败"
prefs = bpy.context.preferences.addons["BlenderGIS"].preferences
crs = json.loads(prefs.predefCrsJson)
crs.append(["EPSG:26912", "NAD83 / UTM 12N", "Marble Canyon AZ"])
prefs.predefCrsJson = json.dumps(crs)
print("[bgis] 已启用", mod.bl_info["version"])

for o in list(bpy.data.objects):
    bpy.data.objects.remove(o)
from BlenderGIS.geoscene import GeoScene
geo = GeoScene(bpy.context.scene)
geo.crs = "EPSG:26912"
geo.setOriginPrj(*P["center_utm"])

objs = {}
for name in ("marble_near_2m", "marble_far_14m"):
    t0 = time.time()
    r = bpy.ops.importgis.asc_file(filepath=os.path.join(DEM, name + ".asc"), fileCRS="EPSG:26912", importMode="MESH", newlines=True)
    o = bpy.context.view_layer.objects.active or bpy.data.objects[-1]
    o.name = name
    objs[name] = o
    print(f"[bgis] 导入 {name}: {r} 顶点 {len(o.data.vertices)} 尺寸 {tuple(round(v) for v in o.dimensions)} 用时 {time.time() - t0:.1f}s")
print("[bgis] 场景原点（UTM）", geo.getOriginPrj(), geo.crs)

# ---- UTM（相对场景原点，米）→ 游戏（厘米）
th = math.radians(P["axis_deg"])
d = Vector((math.cos(th), math.sin(th), 0)); p = Vector((-d.y, d.x, 0))
sx, sy, sz = P["sx"], P["sy"], P["sz"]
# X = (t - t0) * sx,  Y = -(u - u0) * sy,  Z = (z - zr) * sz
M = Matrix(((d.x * sx, d.y * sx, 0, -P["t0"] * sx),
            (-p.x * sy, -p.y * sy, 0, P["u0"] * sy),
            (0, 0, sz, -P["zr"] * sz),
            (0, 0, 0, 1)))
for o in objs.values():
    o.matrix_world = M @ o.matrix_world


def bake(o, xs, ys):
    dg = bpy.context.evaluated_depsgraph_get()
    me = o.evaluated_get(dg).to_mesh()
    mw = o.matrix_world
    verts = [mw @ v.co for v in me.vertices]
    polys = [tuple(pl.vertices) for pl in me.polygons]
    bvh = BVHTree.FromPolygons(verts, polys, all_triangles=False)
    H = np.full((len(ys), len(xs)), np.nan, np.float32)
    down = Vector((0, 0, -1))
    for j, y in enumerate(ys):
        for i, x in enumerate(xs):
            hit = bvh.ray_cast(Vector((x, y, 1e7)), down)
            if hit[0] is not None:
                H[j, i] = hit[0].z
    o.evaluated_get(dg).to_mesh_clear()
    return H


t0 = time.time()
nx = np.arange(-15000, 15000.1, 50.0); ny = np.arange(-7000, 7000.1, 50.0)
near = bake(objs["marble_near_2m"], nx, ny)
print(f"[bake] 近景 {near.shape} 无值 {np.isnan(near).sum()} 用时 {time.time() - t0:.1f}s 范围 {np.nanmin(near):.0f}~{np.nanmax(near):.0f}")
t0 = time.time()
fx = np.arange(-160000, 160000.1, 1000.0); fy = np.arange(-120000, 120000.1, 1000.0)
far = bake(objs["marble_far_14m"], fx, fy)
print(f"[bake] 远景 {far.shape} 无值 {np.isnan(far).sum()} 用时 {time.time() - t0:.1f}s 范围 {np.nanmin(far):.0f}~{np.nanmax(far):.0f}")
np.savez_compressed(OUT_NPZ, near=near, nx=nx, ny=ny, far=far, fx=fx, fy=fy, frame=json.dumps(P))
os.makedirs(os.path.dirname(OUT_BLEND), exist_ok=True)
bpy.ops.wm.save_as_mainfile(filepath=OUT_BLEND)
print("[bake] 完成", OUT_NPZ)
