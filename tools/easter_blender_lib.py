"""彩蛋模型（相位机械狗 / 维克托无人机，11-02）Blender 脚本的公用部分。不单独运行，由下面几个脚本 import：
  tools/blender_easter_survey.py   底子模型普查（尺寸、物体、骨架、材质、动画 + 六向预览图）
  tools/blender_easter_parts.py    外壳零件来源普查（阵营机体每根骨头上的零件、组件库 .blend 在哪）
  tools/blender_easter_dog.py      机械狗：清理底子 → 缩放 → 套相位零件外壳 → 自建骨架 → 导出 SK_PDog.fbx（+ 动作）
  tools/blender_easter_drone.py    无人机：清理底子 → 缩放 → 拆旋翼 → 套维克托零件外壳 → 导出 SM_VDrone_Body.fbx / SM_VDrone_Rotor.fbx
  tools/blender_easter_preview.py  打开 build 存下的 .blend 重拍预览图（不重建）
参数都在 tools/easter_dog.json / easter_drone.json，命令行可以 键=值 临时改（见 load_config）。

约定（和 README「机体资源流程」一致）：
- 内部单位厘米（场景单位 0.01，导出时不再缩放）。配置里写米（*_m），读进来 × 100。
- 坐标：Blender 里朝 +X、左 +Y、上 +Z；导进 UE 是 (x, -y, z)，即 UE 网格空间朝 +X、右 +Y、上 +Z（Actor 前方），不用再转 90 度。
- 材质槽只有三个：Shell（涂装外壳）/ Frame（深石墨灰骨架件）/ Accent（品牌色件和灯）。
  颜色属性 EasterMask（R 通道）= 1 的面是灯（UE 里自发光 = Accent × AccentGlow × R），= 0 的 Accent 面只是品牌色漆。
- 外壳零件只取现有资源：阵营机体 lookdev/rig/factions/<台>.blend（按骨头 / 材质 / 散件挑）、机甲组件库 .blend（资产编号或名字）、
  现成的 FBX（例如 Inbox/_肩部候选/游戏模型）。不在这里生成新几何（只有 UCX 碰撞盒和预览用的标记）。
"""
import glob
import json
import math
import os
import re
import sys
import time
import traceback

import bmesh
import bpy
from mathutils import Matrix, Vector

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, 'tools')
EASTER = os.path.join(ROOT, 'Inbox', '_easter')
DIRS = {k: os.path.join(EASTER, k) for k in ('survey', 'build', 'export', 'preview', 'logs', 'parts')}
M = 100.0  # 配置里的米 → 内部厘米
SLOTS = ('Shell', 'Frame', 'Accent')
MASK = 'EasterMask'
MODEL_EXTS = ('.blend', '.fbx', '.glb', '.gltf')
AX = {'+X': Vector((1, 0, 0)), '-X': Vector((-1, 0, 0)), '+Y': Vector((0, 1, 0)), '-Y': Vector((0, -1, 0)),
      '+Z': Vector((0, 0, 1)), '-Z': Vector((0, 0, -1))}


class EasterError(Exception):
    """能说清楚原因的错误：打一行中文说明就退出，不打整段堆栈"""


# ---------------------------------------------------------------- 日志：屏幕 + Inbox/_easter/logs/<步骤>_<时间>.txt
class Log:
    def __init__(self, tag, step):
        os.makedirs(DIRS['logs'], exist_ok=True)
        self.tag = tag
        self.path = os.path.join(DIRS['logs'], f'{step}_{time.strftime("%m%d_%H%M%S")}.txt')
        self.f = open(self.path, 'w', encoding='utf-8')
        self.warnings = []
        self(f'日志 {self.path}')
        self(f'Blender {bpy.app.version_string}，工程 {ROOT}，参数 {script_args()}')

    def __call__(self, *a):
        line = f'[{self.tag}] ' + ' '.join(str(x) for x in a)
        print(line, flush=True)
        try:
            self.f.write(line + '\n')
            self.f.flush()
        except Exception:
            pass

    def warn(self, *a):
        msg = ' '.join(str(x) for x in a)
        self.warnings.append(msg)
        self('警告：' + msg)

    def close(self):
        if self.warnings:
            self(f'共 {len(self.warnings)} 条警告：')
            for w in self.warnings:
                self('  - ' + w)
        self(f'日志在 {self.path}')
        self.f.close()


LOG = None


def log(*a):
    (LOG or print)(*a)


def warn(*a):
    if LOG:
        LOG.warn(*a)
    else:
        print('警告：', *a)


def run_main(tag, step, main):
    """统一入口：建日志、跑 main()、出错时把原因（和堆栈）写进日志，退出码非 0"""
    global LOG
    LOG = Log(tag, step)
    code = 0
    try:
        main()
    except EasterError as e:
        LOG('失败：' + str(e))
        code = 2
    except Exception:
        LOG('脚本出错（把这个日志发回来）：\n' + traceback.format_exc())
        code = 1
    LOG.close()
    if code:
        sys.exit(code)


def script_args():
    return sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []


def rel(p):
    try:
        return os.path.relpath(p, ROOT).replace('\\', '/')
    except ValueError:
        return p


def absp(p):
    if not p:
        return p
    return os.path.normpath(p if os.path.isabs(p) else os.path.join(ROOT, p))


def fmt(v, k=1.0 / M, nd=2):
    """厘米向量 → 米字符串"""
    return '(' + ', '.join(f'{x * k:.{nd}f}' for x in v) + ')'


# ---------------------------------------------------------------- 配置
def load_config(which, args):
    """读 tools/easter_<which>.json；参数里 --config 路径 换文件；键=值 改单项（值按 JSON 解析，解析不了当字符串；键可以用点：pieces.3.size=0.7）"""
    path = os.path.join(TOOLS, f'easter_{which}.json')
    rest = []
    i = 0
    while i < len(args):
        if args[i] == '--config' and i + 1 < len(args):
            path = absp(args[i + 1])
            i += 2
            continue
        rest.append(args[i])
        i += 1
    if not os.path.exists(path):
        raise EasterError(f'找不到配置 {path}')
    cfg = json.load(open(path, encoding='utf-8'))
    log(f'配置 {rel(path)}')
    flags = []
    for a in rest:
        if '=' not in a or a.startswith('--'):
            flags.append(a)
            continue
        k, v = a.split('=', 1)
        try:
            val = json.loads(v)
        except ValueError:
            val = v
        node = cfg
        keys = k.split('.')
        for kk in keys[:-1]:
            node = node[int(kk)] if isinstance(node, list) else node.setdefault(kk, {})
        last = keys[-1]
        if isinstance(node, list):
            node[int(last)] = val
        else:
            node[last] = val
        log(f'命令行改配置 {k} = {val!r}')
    cfg['_flags'] = flags
    return cfg


# ---------------------------------------------------------------- 找 / 读底子模型
def find_base(which, explicit=''):
    """底子模型：配置 base_file 指定就用它；否则在 Inbox/_easter/<which>/ 下递归找 .blend > .fbx > .glb > .gltf（同类取最大的）"""
    if explicit:
        p = absp(explicit)
        if not os.path.exists(p):
            raise EasterError(f'配置里的 base_file 不存在：{p}')
        return p
    d = os.path.join(EASTER, which)
    if not os.path.isdir(d):
        raise EasterError(f'没有目录 {d}：把 Sketchfab 下载的模型解压到这里（见 CLOUD_需要用户做.md）')
    cands, zips = [], []
    for dp, _dn, fn in os.walk(d):
        for f in fn:
            ext = os.path.splitext(f)[1].lower()
            if ext in MODEL_EXTS:
                cands.append(os.path.join(dp, f))
            elif ext in ('.zip', '.rar', '.7z'):
                zips.append(os.path.join(dp, f))
    if not cands:
        hint = f'（里面有压缩包 {", ".join(os.path.basename(z) for z in zips)}，先解压）' if zips else ''
        raise EasterError(f'{d} 里没有 .blend / .fbx / .glb / .gltf {hint}')
    cands.sort(key=lambda p: (MODEL_EXTS.index(os.path.splitext(p)[1].lower()), -os.path.getsize(p)))
    for c in cands:
        log(f'  候选底子 {rel(c)} {os.path.getsize(c) / 1e6:.1f} MB')
    if len(cands) > 1:
        warn(f'{which} 目录里有 {len(cands)} 个模型文件，用 {os.path.basename(cands[0])}；要换就在配置里写 base_file')
    return cands[0]


def import_fbx(path):
    try:
        bpy.ops.import_scene.fbx(filepath=path, automatic_bone_orientation=False)
    except (AttributeError, RuntimeError) as e:
        log(f'import_scene.fbx 不能用（{e}），改用 wm.fbx_import')
        bpy.ops.wm.fbx_import(filepath=path)


def load_base(path):
    """读底子：.blend 直接打开（用物体最多的场景）；其他格式先清空场景再导入。返回底子的全部物体"""
    ext = os.path.splitext(path)[1].lower()
    log(f'读底子 {rel(path)}')
    if ext == '.blend':
        bpy.ops.wm.open_mainfile(filepath=path)
        sc = max(bpy.data.scenes, key=lambda s: len(s.objects))
        if bpy.context.window:
            bpy.context.window.scene = sc
        objs = list(sc.objects)
    else:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        before = set(bpy.data.objects)
        if ext == '.fbx':
            import_fbx(path)
        else:
            bpy.ops.import_scene.gltf(filepath=path)
        objs = [o for o in bpy.data.objects if o not in before]
    sc = bpy.context.scene
    sc.unit_settings.system = 'METRIC'
    for o in objs:
        try:
            o.hide_set(False)
        except Exception:
            pass
        o.hide_viewport = False
    log(f'底子物体 {len(objs)} 个：' + ', '.join(f'{t}×{sum(1 for o in objs if o.type == t)}' for t in sorted({o.type for o in objs})))
    return objs


def ensure_scene_units_cm():
    sc = bpy.context.scene
    sc.unit_settings.system = 'METRIC'
    sc.unit_settings.scale_length = 0.01


# ---------------------------------------------------------------- 包围盒
class Box:
    def __init__(self, lo, hi):
        self.lo, self.hi = Vector(lo), Vector(hi)

    @staticmethod
    def of(points):
        pts = list(points)
        if not pts:
            return None
        return Box([min(p[i] for p in pts) for i in range(3)], [max(p[i] for p in pts) for i in range(3)])

    @property
    def center(self):
        return (self.lo + self.hi) * 0.5

    @property
    def size(self):
        return self.hi - self.lo

    @property
    def half(self):
        return (self.hi - self.lo) * 0.5

    def at(self, u):
        c, h = self.center, self.half
        return Vector([c[i] + u[i] * h[i] for i in range(3)])

    def union(self, o):
        return Box([min(self.lo[i], o.lo[i]) for i in range(3)], [max(self.hi[i], o.hi[i]) for i in range(3)]) if o else self

    def to_json(self):
        return {'min_m': [round(x / M, 4) for x in self.lo], 'max_m': [round(x / M, 4) for x in self.hi], 'size_m': [round(x / M, 4) for x in self.size]}

    def __repr__(self):
        return f'{fmt(self.lo)}..{fmt(self.hi)} 尺寸 {fmt(self.size)} 米'


def mesh_coords(me, mat=None):
    n = len(me.vertices)
    flat = [0.0] * (3 * n)
    me.vertices.foreach_get('co', flat)
    vs = [Vector(flat[i * 3:i * 3 + 3]) for i in range(n)]
    if mat is not None:
        vs = [mat @ v for v in vs]
    return vs


def obj_box(o):
    """物体（修改器之前的网格）的世界包围盒。按顶点现算（me.transform 之后 o.bound_box 要等场景刷新才变，不能用）"""
    if o.type != 'MESH' or not o.data.vertices:
        return None
    import numpy as np
    n = len(o.data.vertices)
    a = np.empty(n * 3, dtype=np.float64)
    o.data.vertices.foreach_get('co', a)
    a = a.reshape(-1, 3)
    mw = o.matrix_world
    if mw != Matrix.Identity(4):
        R = np.array([[mw[i][j] for j in range(3)] for i in range(3)])
        a = a @ R.T + np.array([mw[0][3], mw[1][3], mw[2][3]])
    return Box(a.min(axis=0).tolist(), a.max(axis=0).tolist())


def objs_box(objs):
    b = None
    for o in objs:
        ob = obj_box(o)
        if ob:
            b = ob.union(b) if b else ob
    return b


def tris_of(me):
    return sum(len(p.vertices) - 2 for p in me.polygons)


# ---------------------------------------------------------------- 底子的朝向 / 缩放
def axis_rotation(forward, up='+Z'):
    """把底子的 forward 轴转到 +X、up 轴转到 +Z 的旋转（4×4）"""
    return basis_map([[forward, '+X'], [up, '+Z']]).to_4x4()


def basis_map(spec):
    """[[源轴, 目标轴], [源轴, 目标轴]] → 3×3 旋转：源的第一根轴转到第一根目标轴，第二根同理（两根要垂直）"""
    if not spec:
        return Matrix.Identity(3)
    try:
        a1, b1 = AX[spec[0][0]], AX[spec[0][1]]
        a2, b2 = AX[spec[1][0]], AX[spec[1][1]]
    except (KeyError, IndexError, TypeError):
        raise EasterError(f'orient 写法不对：{spec}（例 [["-Y","+X"],["+Z","+Z"]]，轴只能是 ±X ±Y ±Z）')
    if abs(a1.dot(a2)) > 0.01 or abs(b1.dot(b2)) > 0.01:
        raise EasterError(f'orient 的两根轴要互相垂直：{spec}')
    S = Matrix((a1, a2, a1.cross(a2))).transposed()
    T = Matrix((b1, b2, b1.cross(b2))).transposed()
    return T @ S.inverted()


def name_tokens(name):
    s = re.sub(r'([a-z])([A-Z])', r'\1_\2', name)
    return [t for t in re.split(r'[^A-Za-z0-9]+', s.lower()) if t]


FRONT_TOKENS = {'front', 'fore', 'fl', 'fr', 'lf', 'rf', 'frontleft', 'frontright', 'head', 'face', 'nose', 'camera'}
BACK_TOKENS = {'back', 'hind', 'rear', 'bl', 'br', 'lb', 'rb', 'rl', 'rr', 'lr', 'hl', 'hr', 'backleft', 'backright', 'tail'}


def guess_forward(objs, fallback):
    """按物体 / 骨头名字里的 front / hind 之类猜前方：名字是「前」的点的平均位置 - 「后」的；找不到就用 fallback"""
    fr, bk = [], []
    for o in objs:
        pts = []
        if o.type == 'ARMATURE':
            for b in o.data.bones:
                pts.append((b.name, o.matrix_world @ ((b.head_local + b.tail_local) * 0.5)))
        elif o.type == 'MESH':
            bb = obj_box(o)
            if bb:
                pts.append((o.name, bb.center))
        for n, p in pts:
            t = set(name_tokens(n))
            if t & FRONT_TOKENS:
                fr.append(p)
            elif t & BACK_TOKENS:
                bk.append(p)
    if len(fr) >= 1 and len(bk) >= 1:
        d = sum(fr, Vector()) / len(fr) - sum(bk, Vector()) / len(bk)
        d.z = 0
        if d.length > 1e-6:
            ax = '+X' if abs(d.x) >= abs(d.y) and d.x > 0 else '-X' if abs(d.x) >= abs(d.y) else '+Y' if d.y > 0 else '-Y'
            log(f'按名字猜前方 = {ax}（前 {len(fr)} 个名字、后 {len(bk)} 个名字）')
            return ax, 'names'
    log(f'名字里看不出前后，前方用默认 {fallback}（Sketchfab 的 glTF 一般朝 -Y；看 survey 图确认，不对就在配置里写 forward）')
    return fallback, 'fallback'


def is_ground_plane(o, others_box):
    """自动去掉底下的大平板（展示台 / 地面）：很薄、面积比其余的大很多、在最下面"""
    b = obj_box(o)
    if not b or not others_box:
        return False
    s = sorted(b.size)
    flat = s[0] < 0.02 * max(s[2], 1e-6)
    big = b.size.x * b.size.y > 3.0 * max(others_box.size.x * others_box.size.y, 1e-9)
    low = b.center.z <= others_box.lo.z + 0.1 * others_box.size.z
    return flat and big and low


def prepare_base(objs, cfg, mode):
    """去掉相机 / 灯 / 地面 / 排除名单；把剩下的根物体挂到一个空物体 EASTER_BASE 下（以后只动它，不 apply，底子的动画不受影响）。
    mode='dog'：按长度缩放、脚底贴 z=0、前后左右居中；mode='drone'：按水平最大跨度缩放、包围盒中心放原点。返回 (底子网格物体, 底子骨架, 变换信息)"""
    sc = bpy.context.scene
    ex = cfg.get('exclude_objects') or ''
    keep = []
    for o in objs:
        if o.type in ('CAMERA', 'LIGHT', 'LIGHT_PROBE', 'SPEAKER'):
            bpy.data.objects.remove(o)
            continue
        if ex and re.search(ex, o.name) and o.type == 'MESH':
            log(f'  按 exclude_objects 去掉 {o.name}')
            o.hide_render = True
            o.hide_viewport = True
            continue
        keep.append(o)
    meshes = [o for o in keep if o.type == 'MESH' and not o.hide_render and o.data.vertices]
    hidden = [o.name for o in keep if o.type == 'MESH' and o.hide_render]
    if hidden:
        warn(f'底子里有 {len(hidden)} 个不渲染的网格没算进来：{hidden[:12]}')
    if cfg.get('auto_remove_ground', True) and len(meshes) > 1:
        for o in list(meshes):
            rest = objs_box([m for m in meshes if m is not o])
            if is_ground_plane(o, rest):
                log(f'  自动去掉地面 / 展示台 {o.name}（{obj_box(o)}）')
                o.hide_render = True
                o.hide_viewport = True
                meshes.remove(o)
    if not meshes:
        raise EasterError('底子里没有可用的网格（全被排除了？看 exclude_objects）')
    arms = [o for o in keep if o.type == 'ARMATURE']
    for a in arms:
        a.data.pose_position = 'REST'
    bpy.context.view_layer.update()
    root = bpy.data.objects.new('EASTER_BASE', None)
    sc.collection.objects.link(root)
    for o in keep:
        if o.parent is None and o.name in sc.objects:
            mw = o.matrix_world.copy()
            o.parent = root
            o.matrix_parent_inverse = Matrix.Identity(4)
            o.matrix_world = mw
    bpy.context.view_layer.update()
    raw = world_box(meshes)
    log(f'底子原始包围盒（Blender 单位）{tuple(round(x, 4) for x in raw.lo)}..{tuple(round(x, 4) for x in raw.hi)} 尺寸 {tuple(round(x, 4) for x in raw.size)}，'
        f'场景单位 ×{sc.unit_settings.scale_length}')
    fwd = cfg.get('forward', 'auto')
    how = 'config'
    if fwd == 'auto':
        fwd, how = guess_forward(keep, cfg.get('forward_fallback', '-Y'))
    up = cfg.get('up', '+Z')
    R = axis_rotation(fwd, up)
    root.matrix_world = R
    bpy.context.view_layer.update()
    b = world_box(meshes)
    if mode == 'dog':
        length = b.size.x
        if b.size.y > b.size.x * 1.15:
            warn(f'转完以后 Y 方向（{b.size.y:.3f}）比 X（前后，{b.size.x:.3f}）还长：forward 可能不对（现在 {fwd}），看 survey 图')
        target = cfg['target_length_m'] * M
        s = target / max(length, 1e-9)
        log(f'前方 {fwd}（{how}）、上方 {up}；前后长度 {length:.4f} → {cfg["target_length_m"]} 米，缩放 ×{s:.4f}')
    else:
        span = max(b.size.x, b.size.y)
        target = cfg['target_span_m'] * M
        s = target / max(span, 1e-9)
        log(f'前方 {fwd}（{how}）、上方 {up}；水平跨度 {span:.4f} → {cfg["target_span_m"]} 米，缩放 ×{s:.4f}')
    log(f'  （内部单位厘米：底子按米算的话等于放大 {s / M:.3f} 倍）')
    S = Matrix.Scale(s, 4)
    root.matrix_world = S @ R
    bpy.context.view_layer.update()
    b = world_box(meshes)
    if mode == 'dog':
        t = Vector((-b.center.x, -b.center.y, -b.lo.z))
    else:
        t = -b.center
    root.matrix_world = Matrix.Translation(t) @ S @ R
    bpy.context.view_layer.update()
    b = world_box(meshes)
    log(f'摆好以后包围盒 {b}')
    return meshes, arms, {'forward': fwd, 'forward_how': how, 'up': up, 'scale': s, 'box': b}


def world_box(objs):
    """按求值后的网格（含修改器、骨架绑定姿势）算世界包围盒"""
    deps = bpy.context.evaluated_depsgraph_get()
    b = None
    for o in objs:
        oe = o.evaluated_get(deps)
        try:
            me = oe.to_mesh()
        except RuntimeError:
            continue
        if me and me.vertices:
            ob = Box.of(mesh_coords(me, oe.matrix_world))
            b = ob.union(b) if b else ob
        oe.to_mesh_clear()
    if b is None:
        raise EasterError('底子网格是空的')
    return b


# ---------------------------------------------------------------- 底子 → 工作网格（世界坐标烤进顶点，记下每个顶点来自哪根骨头 / 哪个物体）
def bake_world_mesh(o, name):
    """o 求值后（修改器、绑定姿势）烤成世界坐标的新物体；返回 (新物体, 每个顶点的来源 [(kind, 名字)])"""
    deps = bpy.context.evaluated_depsgraph_get()
    oe = o.evaluated_get(deps)
    me = bpy.data.meshes.new_from_object(oe, preserve_all_data_layers=True, depsgraph=deps)
    me.transform(oe.matrix_world)
    if oe.matrix_world.determinant() < 0:
        me.flip_normals()
    gnames = [g.name for g in o.vertex_groups]
    arm = next((m.object for m in o.modifiers if m.type == 'ARMATURE' and m.object), None)
    deform = set(b.name for b in arm.data.bones if b.use_deform) if arm else set()
    src = []
    for v in me.vertices:
        best, bw = None, 0.0
        for g in v.groups:
            if g.group < len(gnames) and g.weight > bw and (not deform or gnames[g.group] in deform):
                best, bw = gnames[g.group], g.weight
        if arm and best:
            src.append(('bone', arm.name + '/' + best))
        elif o.parent and o.parent_type == 'BONE' and o.parent_bone:
            src.append(('bone', o.parent.name + '/' + o.parent_bone))
        else:
            src.append(('object', o.name))
    # 去掉底子的权重层（之后按自己的骨头重新分组）
    bm = bmesh.new()
    bm.from_mesh(me)
    dl = bm.verts.layers.deform.active
    if dl:
        bm.verts.layers.deform.remove(dl)
    bm.to_mesh(me)
    bm.free()
    for ca in list(me.color_attributes):
        me.color_attributes.remove(ca)
    nob = bpy.data.objects.new(name, me)
    nob['easter_src_name'] = o.name
    bpy.context.scene.collection.objects.link(nob)
    return nob, src


def bake_base(meshes):
    """底子的每个网格 → 世界坐标工作物体（base_000…）；返回 (工作物体, 每个工作物体的顶点来源)"""
    work, srcs = [], []
    for i, o in enumerate(meshes):
        w, s = bake_world_mesh(o, f'base_{i:03d}')
        if not w.data.vertices:
            bpy.data.objects.remove(w)
            continue
        work.append(w)
        srcs.append(s)
    for o in meshes:
        o.hide_render = True
    log(f'底子工作网格 {len(work)} 个，共 {sum(len(w.data.vertices) for w in work)} 顶点、{sum(tris_of(w.data) for w in work)} 三角')
    return work, srcs


def decimate_to(objs, budget, tag):
    """底子太重就按比例减面（塌陷），每个物体同一个比例"""
    total = sum(tris_of(o.data) for o in objs)
    if not budget or total <= budget:
        log(f'{tag} 三角面 {total}，不用减面（上限 {budget}）')
        return total
    ratio = max(0.02, budget / total)
    deps = None
    for o in objs:
        mod = o.modifiers.new('easter_decimate', 'DECIMATE')
        mod.ratio = ratio
        mod.use_collapse_triangulate = True
        deps = bpy.context.evaluated_depsgraph_get()
        oe = o.evaluated_get(deps)
        me = bpy.data.meshes.new_from_object(oe, preserve_all_data_layers=True, depsgraph=deps)
        o.modifiers.remove(mod)
        old = o.data
        o.data = me
        bpy.data.meshes.remove(old)
    after = sum(tris_of(o.data) for o in objs)
    log(f'{tag} 三角面 {total} → {after}（减面比例 {ratio:.3f}，上限 {budget}）')
    return after


# ---------------------------------------------------------------- 散件（连通的一块）
def components(bm):
    """bmesh 里按顶点相连分块，返回 [[面, ...], ...]"""
    seen = set()
    out = []
    for f in bm.faces:
        if f.index in seen:
            continue
        stack, comp = [f], []
        seen.add(f.index)
        while stack:
            g = stack.pop()
            comp.append(g)
            for v in g.verts:
                for h in v.link_faces:
                    if h.index not in seen:
                        seen.add(h.index)
                        stack.append(h)
        out.append(comp)
    return out


def faces_box(faces):
    return Box.of(v.co for f in faces for v in f.verts)


def part_stats(faces):
    b = faces_box(faces)
    s = sorted(b.size, reverse=True)
    c = b.center
    rs = [((v.co.x - c.x) ** 2 + (v.co.y - c.y) ** 2) ** 0.5 for f in faces for v in f.verts]
    return {'box': b, 'long': s[0] / max(s[1], 1e-6), 'round': s[1] / max(s[0], 1e-6), 'flat': s[2] / max(s[0], 1e-6),
            'diag': b.size.length, 'hollow': min(rs) / max(max(rs), 1e-6), 'tris': sum(len(f.verts) - 2 for f in faces)}


def keep_faces(bm, keep):
    """只留 keep（面的集合）里的面"""
    kill = [f for f in bm.faces if f not in keep]
    if kill:
        bmesh.ops.delete(bm, geom=kill, context='FACES')
    loose = [v for v in bm.verts if not v.link_faces]
    if loose:
        bmesh.ops.delete(bm, geom=loose, context='VERTS')


def obj_from_bm(name, bm, materials):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    for m in materials:
        me.materials.append(m)
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    return o


def subset_object(o, face_ids, name):
    """复制 o 里 face_ids 这些面成新物体（o 不动，面号一直有效）"""
    ids = set(face_ids)
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bm.faces.ensure_lookup_table()
    keep_faces(bm, set(f for f in bm.faces if f.index in ids))
    new = obj_from_bm(name, bm, list(o.data.materials))
    bm.free()
    return new


def split_object(o, face_ids, name):
    """把 o 里 face_ids 这些面拆成新物体（o 里删掉）"""
    ids = set(face_ids)
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bm.faces.ensure_lookup_table()
    bm2 = bm.copy()
    bm2.faces.ensure_lookup_table()
    keep_faces(bm2, set(f for f in bm2.faces if f.index in ids))
    new = obj_from_bm(name, bm2, list(o.data.materials))
    bm2.free()
    keep_faces(bm, set(f for f in bm.faces if f.index not in ids))
    bm.to_mesh(o.data)
    bm.free()
    return new


# ---------------------------------------------------------------- 零件来源
_FACTION = {}


def faction_object(cfg, key):
    """阵营机体 .blend 里绑在骨架上的那个大网格（每个零件整块挂一根骨头，按主权重骨头就能把零件挑出来）"""
    if key in _FACTION:
        return _FACTION[key]
    path = absp(cfg.get('faction_blend', 'lookdev/rig/factions/{key}.blend').format(key=key))
    if not os.path.exists(path):
        raise EasterError(f'找不到阵营机体 {path}（README「阵营机体」：lookdev/rig/factions/run_export.py 导出时会存 .blend；'
                          f'路径不一样就改配置 faction_blend）')
    with bpy.data.libraries.load(path, link=False) as (src, dst):
        dst.objects = list(src.objects)
    objs = [o for o in dst.objects if o]
    meshes = [o for o in objs if o.type == 'MESH' and len(o.vertex_groups) > 0]
    if not meshes:
        meshes = [o for o in objs if o.type == 'MESH']
    if not meshes:
        raise EasterError(f'{path} 里没有网格')
    o = max(meshes, key=lambda m: len(m.data.vertices))
    log(f'阵营机体 {key}：{rel(path)} 网格 {o.name}（{len(o.data.vertices)} 顶点、{tris_of(o.data)} 三角，骨头组 {len(o.vertex_groups)} 个，'
        f'材质 {[m.name for m in o.data.materials if m]}）')
    _FACTION[key] = o
    return o


def faction_bmesh(o):
    """阵营网格 → (bmesh 世界坐标, 每个顶点主骨头名)"""
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bm.transform(o.matrix_world)
    names = [g.name for g in o.vertex_groups]
    dl = bm.verts.layers.deform.active
    dom = {}
    for v in bm.verts:
        d = v[dl] if dl else None
        if d:
            gi = max(d.items(), key=lambda kv: kv[1])[0]
            dom[v.index] = names[gi] if gi < len(names) else ''
        else:
            dom[v.index] = ''
    if dl:
        bm.verts.layers.deform.remove(dl)
    bm.verts.index_update()
    bm.faces.index_update()
    return bm, dom


def face_bone(f, dom):
    """面属于哪根骨头：顶点主骨头里占多数的那根（零件整块挂一根骨头，一般全一样）"""
    names = [dom[v.index] or '' for v in f.verts]
    return max(set(names), key=names.count)


def pick_parts(comps, pick, tag):
    if not comps:
        return []
    st = [(c, part_stats(c)) for c in comps]
    st.sort(key=lambda cs: -cs[1]['diag'])
    if pick in (None, 'all'):
        return [f for c, _ in st for f in c]
    if pick == 'largest':
        return list(st[0][0])
    big = [cs for cs in st if cs[1]['diag'] >= st[0][1]['diag'] * 0.08] or st  # 太小的碎件不要
    if pick == 'longest':
        return list(max(big, key=lambda cs: cs[1]['long'])[0])
    if pick == 'roundest':
        return list(max(big, key=lambda cs: cs[1]['round'] - cs[1]['flat'] * 0.3)[0])
    if pick == 'flattest':
        return list(min(big, key=lambda cs: cs[1]['flat'])[0])
    idx = pick if isinstance(pick, list) else [pick]
    out = []
    for i in idx:
        if not isinstance(i, int) or i >= len(st):
            raise EasterError(f'{tag}：pick {i} 超出范围（共 {len(st)} 块，按大小从 0 编号，见 blender_easter_parts.py 的输出）')
        out += st[i][0]
    return out


def extract_faction(cfg, src, tag):
    o = faction_object(cfg, src['key'])
    bm, dom = faction_bmesh(o)
    bre = src.get('bone')
    mre = src.get('material')
    mats = list(o.data.materials)
    keep = set()
    for f in bm.faces:
        if bre and not re.fullmatch(bre, face_bone(f, dom)):
            continue
        if mre:
            mn = mats[f.material_index].name if f.material_index < len(mats) and mats[f.material_index] else ''
            if not re.search(mre, mn):
                continue
        keep.add(f)
    keep_faces(bm, keep)
    bm.faces.index_update()
    comps = components(bm)
    chosen = set(pick_parts(comps, src.get('pick', 'all'), tag))
    keep_faces(bm, chosen)
    if not bm.faces:
        bm.free()
        raise EasterError(f'{tag}：{src["key"]} 里按 bone={bre!r} material={mre!r} 挑不出面（用 blender_easter_parts.py {src["key"]} 看有哪些骨头 / 材质）')
    ob = obj_from_bm(tag, bm, mats)
    bm.free()
    log(f'  {tag}：阵营 {src["key"]} bone={bre!r} material={mre!r} pick={src.get("pick", "all")} → {len(comps)} 块里取了 {tris_of(ob.data)} 三角')
    return ob


def find_kit_lib(cfg, lib):
    """组件库 .blend：写全路径就用它；只写名字（如「机甲」）就在 kit_dirs 里按文件名找"""
    if lib.lower().endswith('.blend') and os.path.exists(absp(lib)):
        return absp(lib)
    hits = []
    for d in cfg.get('kit_dirs', ['Inbox', 'lookdev']):
        base = absp(d)
        for dp, dn, fn in os.walk(base):
            if dp[len(base):].count(os.sep) > 5:
                dn[:] = []
                continue
            for f in fn:
                if f.lower().endswith('.blend') and os.path.splitext(f)[0] == lib:
                    hits.append(os.path.join(dp, f))
    if not hits:
        raise EasterError(f'找不到组件库「{lib}」.blend（在 {cfg.get("kit_dirs")} 里按文件名找的）：跑 blender_easter_parts.py find 看候选，然后在零件的 src.lib 写全路径')
    if len(hits) > 1:
        warn(f'组件库「{lib}」有多个同名文件，用第一个：{[rel(h) for h in hits]}')
    return hits[0]


def kit_name_by_index(cfg, path, item):
    """blend_asset_survey.py 的编号 → 资产名：优先读它存的 _survey.json；没有就按同样的顺序（物体在前、集合在后，按名字排）数"""
    stem = os.path.splitext(os.path.basename(path))[0]
    for d in cfg.get('kit_survey_dirs', []):
        p = os.path.join(absp(d), stem, '_survey.json')
        if os.path.exists(p):
            items = json.load(open(p, encoding='utf-8'))['items']
            hit = next((it for it in items if it['i'] == item), None)
            if hit:
                return hit['name'], hit['kind']
    with bpy.data.libraries.load(path, link=False, assets_only=True) as (src, _dst):
        names = [(n, 'Object') for n in sorted(src.objects)] + [(n, 'Collection') for n in sorted(src.collections)]
    if item >= len(names):
        raise EasterError(f'组件库 {stem} 只有 {len(names)} 个资产，没有 #{item}')
    warn(f'组件库 {stem} #{item} 按名字排序数出来是「{names[item][0]}」；如果和 _survey.json / 总览图对不上，零件里改写 "name"')
    return names[item]


def extract_kit(cfg, src, tag):
    path = find_kit_lib(cfg, src['lib'])
    if src.get('name'):
        name, kind = src['name'], src.get('kind', '')
    else:
        name, kind = kit_name_by_index(cfg, path, int(src['item']))
    with bpy.data.libraries.load(path, link=False) as (s, d):
        if kind in ('', 'Object') and name in s.objects:
            d.objects = [name]
            kind = 'Object'
        elif name in s.collections:
            d.collections = [name]
            kind = 'Collection'
        else:
            raise EasterError(f'组件库 {rel(path)} 里没有资产「{name}」')
    if kind == 'Object':
        root = bpy.data.objects[name]
        objs = [root] + list(root.children_recursive)
    else:
        objs = list(bpy.data.collections[name].all_objects)
    tmp = bpy.data.collections.new('EASTER_TMP')
    bpy.context.scene.collection.children.link(tmp)
    for o in objs:
        if o.name not in tmp.objects:
            tmp.objects.link(o)
    bpy.context.view_layer.update()
    parts = [bake_world_mesh(o, f'{tag}_tmp')[0] for o in objs if o.type == 'MESH']
    ob = join_objects(parts, tag)
    bpy.context.scene.collection.children.unlink(tmp)
    log(f'  {tag}：组件库 {rel(path)} 「{name}」（{kind}，{len(objs)} 个物体）→ {tris_of(ob.data)} 三角')
    return ob


def extract_fbx(cfg, src, tag):
    paths = sorted(glob.glob(absp(src['path'])))
    if src.get('pick'):
        paths = [p for p in paths if re.search(src['pick'], os.path.basename(p))]
    if not paths:
        raise EasterError(f'{tag}：没找到 FBX {src["path"]}（pick={src.get("pick")!r}）')
    path = paths[0]
    before = set(bpy.data.objects)
    import_fbx(path)
    new = [o for o in bpy.data.objects if o not in before]
    bpy.context.view_layer.update()
    parts = [bake_world_mesh(o, f'{tag}_tmp')[0] for o in new if o.type == 'MESH']
    for o in new:
        bpy.data.objects.remove(o)
    if not parts:
        raise EasterError(f'{tag}：{path} 里没有网格')
    ob = join_objects(parts, tag)
    log(f'  {tag}：FBX {rel(path)} → {tris_of(ob.data)} 三角（候选 {len(paths)} 个）')
    return ob


def extract_source(cfg, src, tag):
    kind = src.get('type')
    if kind == 'faction':
        return extract_faction(cfg, src, tag)
    if kind == 'kit':
        return extract_kit(cfg, src, tag)
    if kind == 'fbx':
        return extract_fbx(cfg, src, tag)
    raise EasterError(f'{tag}：src.type 只能是 faction / kit / fbx，现在是 {kind!r}')


def join_objects(objs, name):
    """合并成一个物体（材质槽按材质合并、顶点组按名字合并、颜色属性同名合并）"""
    objs = [o for o in objs if o and o.type == 'MESH' and o.data.vertices]
    if not objs:
        raise EasterError(f'合并 {name}：没有网格')
    for o in bpy.context.scene.objects:
        o.select_set(False)
    for o in objs:
        if o.name not in bpy.context.view_layer.objects:
            bpy.context.scene.collection.objects.link(o)
        o.hide_viewport = False
        o.hide_set(False)
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    if len(objs) > 1:
        bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.name = name
    ob.data.name = name
    return ob


# ---------------------------------------------------------------- 材质槽：Shell / Frame / Accent + 灯遮罩
_SLOT_MATS = {}


def slot_materials(preview_colors):
    """建（或拿）三个槽的材质，名字就是槽名。底子里同名的材质先改名，免得变成 Shell.001"""
    if _SLOT_MATS and all(_SLOT_MATS[s].name == s for s in SLOTS):
        return _SLOT_MATS
    for s in SLOTS:
        m = bpy.data.materials.get(s)
        if m and not m.get('easter_slot'):
            m.name = s + '_base'
    for s in SLOTS:
        m = bpy.data.materials.get(s) or bpy.data.materials.new(s)
        m['easter_slot'] = s
        col = hex_rgb(preview_colors.get(s, '#808080'))
        m.diffuse_color = (*col, 1.0)
        m.metallic = 0.6 if s == 'Frame' else 0.2
        m.roughness = 0.45
        _SLOT_MATS[s] = m
    return _SLOT_MATS


def hex_rgb(h):
    h = h.lstrip('#')
    c = [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    return [x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c]


def principled(m):
    if not m or not m.use_nodes or not m.node_tree:
        return None
    return next((n for n in m.node_tree.nodes if n.bl_idname == 'ShaderNodeBsdfPrincipled'), None)


def mat_traits(m):
    """材质的特征：有没有底色贴图、自发光、底色亮度 / 饱和度"""
    t = {'textured': False, 'emission': False, 'lum': 0.5, 'sat': 0.0}
    if not m:
        return t
    p = principled(m)
    col = list(m.diffuse_color[:3])
    if p:
        bc = p.inputs.get('Base Color')
        if bc:
            t['textured'] = bc.is_linked
            col = list(bc.default_value[:3])
        es = p.inputs.get('Emission Strength')
        ec = p.inputs.get('Emission Color') or p.inputs.get('Emission')
        if es and ec and es.default_value > 0 and (ec.is_linked or max(ec.default_value[:3]) > 0.01):
            t['emission'] = True
    if m.use_nodes and m.node_tree and any(n.bl_idname == 'ShaderNodeEmission' for n in m.node_tree.nodes):
        t['emission'] = True
    t['lum'] = 0.2126 * col[0] + 0.7152 * col[1] + 0.0722 * col[2]
    t['sat'] = (max(col) - min(col)) / max(max(col), 1e-6)
    return t


def classify_material(m, rules):
    """按规则表 [[正则 或 @emission/@textured/@bright, 槽, 是否灯], ...] 从上往下第一个匹配的"""
    name = m.name if m else '(无材质)'
    tr = mat_traits(m)
    for pat, slot, glow in rules:
        if pat == '@emission':
            hit = tr['emission']
        elif pat == '@textured':
            hit = tr['textured']
        elif pat == '@bright':
            hit = not tr['textured'] and (tr['lum'] > 0.25 or tr['sat'] > 0.35)
        else:
            hit = re.search(pat, name) is not None
        if hit:
            if slot not in SLOTS:
                raise EasterError(f'材质规则 {pat} 的槽 {slot} 不对（只能是 {SLOTS}）')
            return slot, bool(glow), pat
    return 'Frame', False, '(默认)'


def apply_slots(o, rules, preview_colors, force_slot=None, force_glow=None, tag=''):
    """把 o 的材质换成三个槽，按原材质分；写灯遮罩颜色属性。返回 {槽: 三角面数}"""
    mats = slot_materials(preview_colors)
    me = o.data
    old = list(me.materials)
    mapping = []
    for m in old:
        slot, glow, why = classify_material(m, rules)
        if force_slot:
            slot = force_slot
        if force_glow is not None:
            glow = bool(force_glow)
        mapping.append((SLOTS.index(slot), glow))
        log(f'    材质 {m.name if m else None} → {slot}{"（灯）" if glow else ""}  [{why}{"，强制" if force_slot or force_glow is not None else ""}]')
    if not mapping:
        slot = force_slot or 'Frame'
        mapping = [(SLOTS.index(slot), bool(force_glow))]
        log(f'    （没有材质）→ {slot}')
    n = len(me.polygons)
    idx = [0] * n
    me.polygons.foreach_get('material_index', idx)
    new_idx = [mapping[i if i < len(mapping) else 0][0] for i in idx]
    glow_face = [mapping[i if i < len(mapping) else 0][1] for i in idx]
    me.materials.clear()
    for s in SLOTS:
        me.materials.append(mats[s])
    me.polygons.foreach_set('material_index', new_idx)
    for ca in list(me.color_attributes):
        me.color_attributes.remove(ca)
    ca = me.color_attributes.new(MASK, 'BYTE_COLOR', 'CORNER')
    cols = [0.0] * (4 * len(me.loops))
    for p in me.polygons:
        g = 1.0 if glow_face[p.index] else 0.0
        for li in p.loop_indices:
            cols[li * 4:li * 4 + 4] = (g, 0.0, 0.0, 1.0)
    ca.data.foreach_set('color', cols)
    try:
        me.color_attributes.active_color = ca
        me.color_attributes.render_color_index = 0
    except Exception:
        pass
    counts = {s: 0 for s in SLOTS}
    for p, si in zip(me.polygons, new_idx):
        counts[SLOTS[si]] += len(p.vertices) - 2
    counts['glow'] = sum(len(p.vertices) - 2 for p in me.polygons if glow_face[p.index])
    return counts


# ---------------------------------------------------------------- 摆零件
ANCHORS = {'top': (2, 1), 'bottom': (2, -1), 'front': (0, 1), 'back': (0, -1), 'left': (1, 1), 'right': (1, -1), 'center': (None, 0)}


def transform_mesh(o, mat4):
    o.data.transform(mat4)
    if mat4.determinant() < 0:
        o.data.flip_normals()
    o.data.update()


def place_piece(o, spec, target, tag):
    """把零件（世界坐标网格）按 spec 转向、缩放、摆到 target 盒子上。返回零件最后的盒子。
    spec：orient / long / length_m / tilt / fit + size / abs_m / stretch / anchor + inset / at / push / offset_m"""
    R = basis_map(spec.get('orient')).to_4x4()
    c = obj_box(o).center
    transform_mesh(o, R @ Matrix.Translation(-c))
    if spec.get('long'):
        b = obj_box(o)
        i = max(range(3), key=lambda k: b.size[k])
        cur = Vector([1.0 if k == i else 0.0 for k in range(3)])
        want = AX[spec['long']]
        transform_mesh(o, cur.rotation_difference(want).to_matrix().to_4x4())
    b = obj_box(o)
    sizes = b.size
    if spec.get('length_m'):
        s = spec['length_m'] * M / max(max(sizes), 1e-6)
        how = f'最长边 {spec["length_m"]} 米'
    elif spec.get('abs_m') is not None:
        a = spec['abs_m']
        if isinstance(a, (int, float)):
            s = a * M / max(max(sizes), 1e-6)
            how = f'最长边 {a} 米'
        else:
            rs = [a[k] * M / max(sizes[k], 1e-6) for k in range(3) if a[k]]
            s = min(rs)
            how = f'绝对尺寸 {a} 米'
    else:
        fit = spec.get('fit', 'xyz')
        size = spec.get('size', 1.0)
        size = [size] * 3 if isinstance(size, (int, float)) else size
        rs = [size['xyz'.index(ax)] * target.size['xyz'.index(ax)] / max(sizes['xyz'.index(ax)], 1e-6) for ax in fit]
        s = min(rs) if rs else 1.0
        how = f'按目标的 {fit} × {size}'
    st = spec.get('stretch') or [1, 1, 1]
    transform_mesh(o, Matrix.Diagonal((s * st[0], s * st[1], s * st[2], 1.0)))
    if spec.get('tilt'):
        rx, ry, rz = (math.radians(v) for v in spec['tilt'])
        transform_mesh(o, Matrix.Rotation(rz, 4, 'Z') @ Matrix.Rotation(ry, 4, 'Y') @ Matrix.Rotation(rx, 4, 'X'))
    b = obj_box(o)
    anchor = spec.get('anchor', 'center')
    if anchor not in ANCHORS:
        raise EasterError(f'{tag}：anchor 只能是 {list(ANCHORS)}')
    axis, sign = ANCHORS[anchor]
    inset = spec.get('inset', 0.3)
    at = [0.0, 0.0, 0.0]
    push = [0.0, 0.0, 0.0]
    if axis is not None:
        at[axis] = float(sign)
        push[axis] = sign * (1.0 - 2.0 * inset)
    if spec.get('at') is not None:
        at = list(spec['at'])
    if spec.get('push') is not None:
        push = list(spec['push'])
    off = Vector(spec.get('offset_m') or (0, 0, 0)) * M
    pos = target.at(at) + Vector([push[k] * b.half[k] for k in range(3)]) + off
    transform_mesh(o, Matrix.Translation(pos - b.center))
    nb = obj_box(o)
    log(f'  {tag}：缩放 ×{s:.3f}（{how}），摆在 {anchor} at={at} push={[round(p, 2) for p in push]} → {nb}')
    return nb


def mirror_copy(o, axis, center, name):
    """按 center 所在的平面镜像复制（法线翻回来）"""
    k = 'xyz'.index(axis)
    me = o.data.copy()
    n = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(n)
    d = [1.0, 1.0, 1.0, 1.0]
    d[k] = -1.0
    c = Vector((0, 0, 0))
    c[k] = center[k]
    transform_mesh(n, Matrix.Translation(c) @ Matrix.Diagonal(d) @ Matrix.Translation(-c))
    for k2, v in o.items():
        n[k2] = v
    return n


def build_pieces(cfg, boxes, rules_key='piece_slot_rules', is_bone=None):
    """按配置 pieces 依次做零件：取来源 → 摆 → 分槽。每个零件摆好后自己的盒子也登记进 boxes（后面的零件可以 to 它）。
    返回 [(物体, spec, 骨头)]；可选零件（required=false）失败只警告"""
    out = []
    cache = {}
    rules = cfg[rules_key]
    colors = cfg.get('preview_colors', {})
    for spec in cfg.get('pieces', []):
        name = spec['name']
        if spec.get('enabled', True) is False:
            log(f'零件 {name}：关着（enabled=false）')
            continue
        targets = spec.get('to', 'body')
        targets = targets if isinstance(targets, list) else [targets]
        log(f'零件 {name} → {targets}')
        try:
            made = []
            alts = spec['src'] if isinstance(spec['src'], list) else [spec['src']]
            chosen = None  # 第一个目标试出来能用的来源，其余目标用同一个
            for t in targets:
                if t not in boxes:
                    raise EasterError(f'{name}：目标 {t} 不存在（可用：{sorted(boxes)}）')
                errs = []
                for ai, alt in enumerate(alts if chosen is None else [alts[chosen]]):
                    src = dict(alt)
                    if spec.get('side_swap', True) and src.get('type') == 'faction' and src.get('bone') and re.search(r'_(fr|br)_', t):
                        src['bone'] = re.sub(r'_l\b', '_r', src['bone'])
                    key = json.dumps({k: v for k, v in src.items() if k not in ('orient', 'long')}, sort_keys=True)
                    try:
                        if key not in cache:
                            cache[key] = extract_source(cfg, src, f'{name}_src')
                            cache[key].hide_render = True
                            cache[key].hide_viewport = True
                        if chosen is None:
                            chosen = ai
                            if len(alts) > 1:
                                log(f'  {name}：用第 {ai + 1} 个来源（共 {len(alts)} 个备选）')
                        break
                    except EasterError as e:
                        errs.append(str(e))
                        key = None
                if key is None:
                    raise EasterError(f'{name}：所有来源都不行：' + ' / '.join(errs))
                pspec = dict(spec)
                for k in ('orient', 'long', 'tilt'):
                    if k in alts[chosen]:
                        pspec[k] = alts[chosen][k]
                o = bpy.data.objects.new(f'{name}_{t}' if len(targets) > 1 else name, cache[key].data.copy())
                bpy.context.scene.collection.objects.link(o)
                pb = place_piece(o, pspec, boxes[t], o.name)
                bone = spec.get('bone') or (t if is_bone and is_bone(t) else 'body')
                o['easter_piece'] = name
                o['easter_bone'] = bone
                made.append((o, t, bone, pb))
                if spec.get('mirror'):
                    m = mirror_copy(o, spec['mirror'], boxes[t].center, o.name + '_mirror')
                    mb = obj_box(m)
                    mbone = spec.get('mirror_bone') or bone
                    m['easter_bone'] = mbone
                    made.append((m, t, mbone, mb))
                    log(f'  {m.name}：镜像（{spec["mirror"]}）→ {mb}')
            for o, t, bone, pb in made:
                cnt = apply_slots(o, rules, colors, spec.get('slot'), spec.get('glow'), o.name)
                log(f'    {o.name} 三角 {tris_of(o.data)}，分槽 {cnt}，骨头 {bone}')
                out.append((o, spec, bone))
            # 登记盒子：<名字> = 全部（含镜像）的合包围盒；<名字>_0、_1… = 每一件（镜像的那件排在原件后面）
            union = None
            for i, (_o, _t, _b, pb) in enumerate(made):
                union = pb.union(union) if union else pb
                boxes[f'{name}_{i}'] = pb
            boxes[name] = union
        except EasterError as e:
            if spec.get('required', False):
                raise
            warn(f'零件 {name} 跳过：{e}')
    for o in cache.values():
        if o.name in bpy.context.scene.collection.objects:
            bpy.context.scene.collection.objects.unlink(o)
    return out


# ---------------------------------------------------------------- 预览渲染（Workbench，不要 GPU 渲染器）
def setup_workbench(color_type='MATERIAL', res=(1200, 900)):
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_WORKBENCH'
    sh = sc.display.shading
    sh.light = 'STUDIO'
    sh.color_type = color_type
    sh.show_cavity = True
    sh.cavity_type = 'BOTH'
    sh.show_shadows = False
    sh.show_object_outline = True
    sh.background_type = 'VIEWPORT'
    sh.background_color = (0.16, 0.17, 0.19)
    sc.render.resolution_x, sc.render.resolution_y = res
    sc.render.resolution_percentage = 100
    sc.render.film_transparent = False
    sc.render.image_settings.file_format = 'PNG'


VIEWS = {
    'front34': ('左前 3/4', (1.0, 0.85, 0.55)),
    'rear34': ('右后 3/4', (-1.0, -0.85, 0.5)),
    'side': ('左侧（从 +Y 看）', (0.0, 1.0, 0.08)),
    'front': ('正前（从 +X 看）', (1.0, 0.0, 0.08)),
    'top': ('俯视（上 = +X 前方）', (0.0, 0.0001, 1.0)),
    'px': ('从 +X 看', (1.0, 0.0, 0.0)), 'nx': ('从 -X 看', (-1.0, 0.0, 0.0)),
    'py': ('从 +Y 看', (0.0, 1.0, 0.0)), 'ny': ('从 -Y 看', (0.0, -1.0, 0.0)),
    'pz': ('从 +Z（上）看，图上方 = +Y', (0.0, 0.0, 1.0)), 'iso': ('斜视 (+X, -Y, +Z)', (1.0, -1.0, 0.8)),
}


def render_views(objs, out_dir, prefix, views, sheet, color_type='MATERIAL', res=(1200, 900), note='', extra_hide=()):
    """只显示 objs，按 views 逐张拍。sheet（列表）里追加 {file, title}，最后 write_sheet 写 _sheet.json 给 easter_sheet.py 拼图"""
    os.makedirs(out_dir, exist_ok=True)
    sc = bpy.context.scene
    setup_workbench(color_type, res)
    show = set(o.name for o in objs)
    saved = {}
    for o in sc.objects:
        saved[o.name] = o.hide_render
        o.hide_render = o.name not in show or o.name in extra_hide
    b = None
    for o in objs:
        if o.type == 'MESH':
            ob = obj_box(o)
            if ob:
                b = ob.union(b) if b else ob
    if b is None:
        warn(f'预览 {prefix}：没有可拍的网格')
        return
    cam_data = bpy.data.cameras.get('EASTER_CAM') or bpy.data.cameras.new('EASTER_CAM')
    cam = bpy.data.objects.get('EASTER_CAM') or bpy.data.objects.new('EASTER_CAM', cam_data)
    if cam.name not in sc.collection.objects:
        sc.collection.objects.link(cam)
    sc.camera = cam
    cam_data.lens = 50
    ctr, rad = b.center, max(b.size.length / 2, 1.0)
    for key in views:
        if isinstance(key, str):  # 'front34' 之类：查 VIEWS
            tag, (title, d) = key, VIEWS[key]
        else:  # 自定义：(标签, (标题, 方向))
            tag, (title, d) = key
        d = Vector(d).normalized()
        dist = rad / math.sin(min(cam_data.angle_x, cam_data.angle_y) / 2) * 1.08
        cam.location = ctr + d * dist
        if abs(d.z) < 0.99:
            cam.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()
        else:  # 正上方往下看：top 让图的上方 = +X（前方），pz 让图的上方 = +Y
            cam.rotation_euler = (0.0, 0.0, -math.pi / 2) if key == 'top' else (0.0, 0.0, 0.0)
        cam_data.clip_start = max(rad * 0.005, 0.1)
        cam_data.clip_end = dist + rad * 4
        f = os.path.join(out_dir, f'{prefix}_{tag}.png')
        sc.render.filepath = f
        bpy.ops.render.render(write_still=True)
        sheet.append({'file': os.path.basename(f), 'title': f'{prefix} {title}{("  " + note) if note else ""}'})
        log(f'  预览 {rel(f)}')
    for o in sc.objects:
        if o.name in saved:
            o.hide_render = saved[o.name]


def write_sheet(out_dir, sheet, title, legend=None, extra=None, merge=False):
    """写 _sheet.json。merge=False（build / survey）：上一次清单里、这次没再拍的图删掉；merge=True（preview）：加在上一次的清单后面"""
    os.makedirs(out_dir, exist_ok=True)
    p = os.path.join(out_dir, '_sheet.json')
    old = []
    old_meta = {}
    if os.path.exists(p):
        try:
            old_meta = json.load(open(p, encoding='utf-8'))
            old = old_meta.get('items', [])
        except Exception:
            old = []
    names = set(it['file'] for it in sheet)
    if merge:
        items = [it for it in old if it['file'] not in names] + sheet
        legend = legend or old_meta.get('legend')
        extra = extra or old_meta.get('extra')
    else:
        items = sheet
        for it in old:
            f = os.path.join(out_dir, it['file'])
            if it['file'] not in names and os.path.exists(f):
                os.remove(f)
    data = {'title': title, 'items': items, 'legend': legend or [], 'extra': extra or []}
    json.dump(data, open(p, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    log(f'拼图清单 {rel(p)}（{len(items)} 张）→ 跑 py -3 tools/easter_sheet.py {rel(out_dir)}')


PALETTE = [(0.95, 0.35, 0.3), (0.3, 0.75, 0.95), (0.95, 0.8, 0.25), (0.45, 0.9, 0.4), (0.8, 0.45, 0.95), (0.95, 0.6, 0.2), (0.3, 0.95, 0.8),
           (0.9, 0.4, 0.65), (0.55, 0.6, 0.95), (0.75, 0.9, 0.3), (0.95, 0.95, 0.95), (0.6, 0.4, 0.3)]


def color_legend(groups):
    """groups：[(标签, [物体...])] → 每组一个颜色（Workbench OBJECT 模式），返回图例"""
    legend = []
    for i, (label, objs) in enumerate(groups):
        c = PALETTE[i % len(PALETTE)]
        for o in objs:
            o.color = (*c, 1.0)
        legend.append({'label': label, 'rgb': [round(x * 255) for x in c]})
    return legend


# ---------------------------------------------------------------- 导出
def select_only(objs):
    for o in bpy.context.scene.objects:
        o.select_set(False)
    for o in objs:
        o.hide_viewport = False
        try:
            o.hide_set(False)
        except Exception:
            pass
        o.select_set(True)
    if objs:
        bpy.context.view_layer.objects.active = objs[0]


def export_fbx(path, objs, types, anim=False):
    """厘米制 FBX：场景单位 0.01、顶点已经是厘米，apply_unit_scale 后不再缩放（和 README「机体资源流程」第 4 条一样）"""
    ensure_scene_units_cm()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    select_only(objs)
    kw = dict(filepath=path, use_selection=True, object_types=set(types), apply_unit_scale=True, apply_scale_options='FBX_SCALE_NONE',
              mesh_smooth_type='FACE', use_mesh_modifiers=False, add_leaf_bones=False, bake_anim=anim, use_armature_deform_only=False,
              armature_nodetype='NULL', path_mode='STRIP', embed_textures=False, use_tspace=False)
    if anim:
        kw.update(bake_anim_use_all_actions=False, bake_anim_use_nla_strips=False, bake_anim_force_startend_keying=True,
                  bake_anim_simplify_factor=0.0, bake_anim_step=1.0)
    try:
        bpy.ops.export_scene.fbx(colors_type='SRGB', **kw)
    except TypeError:
        bpy.ops.export_scene.fbx(**kw)
    log(f'导出 {rel(path)}（{os.path.getsize(path) / 1e6:.2f} MB）')


def save_blend(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=path, copy=True)
    log(f'存了 {rel(path)}（用 Blender 打开可以手动检查 / 改）')


def ue_vec(v):
    """Blender 厘米 → UE 网格空间厘米 (x, -y, z)"""
    return [round(v[0], 2), round(-v[1], 2), round(v[2], 2)]


# ---------------------------------------------------------------- 动作（Blender 4.4+ 分层动作 / 5.x 的 fcurve 在 channelbag 里）
def action_fcurves(act):
    try:
        return list(act.fcurves)
    except AttributeError:
        pass
    out = []
    for layer in getattr(act, 'layers', []):
        for strip in layer.strips:
            for cb in getattr(strip, 'channelbags', []):
                out += list(cb.fcurves)
    return out


def assign_action(obj, act):
    ad = obj.animation_data or obj.animation_data_create()
    ad.action = act
    if act is not None and hasattr(ad, 'action_slot') and getattr(act, 'slots', None):
        if ad.action_slot is None:
            want = [s for s in act.slots if getattr(s, 'target_id_type', 'OBJECT') == 'OBJECT']
            best = next((s for s in want if obj.name in s.name_display), None) or (want[0] if want else act.slots[0])
            try:
                ad.action_slot = best
            except Exception as e:
                warn(f'给 {obj.name} 选动作槽失败：{e}')
    return ad


def actions_for(obj):
    """可能属于 obj 的动作：fcurve 指着 pose.bones（骨架）或物体变换"""
    out = []
    for act in bpy.data.actions:
        if act.get('easter_out'):
            continue
        fcs = action_fcurves(act)
        if not fcs:
            continue
        paths = [fc.data_path for fc in fcs]
        if obj.type == 'ARMATURE' and any(p.startswith('pose.bones') for p in paths):
            out.append(act)
        elif obj.type != 'ARMATURE' and any(p in ('location', 'rotation_euler', 'rotation_quaternion', 'scale') for p in paths):
            out.append(act)
    return out


# ---------------------------------------------------------------- 机械狗：身体高度带、腿分段（survey 和 build 共用，结果一样）
def body_band(coords, thr=0.5, nz=48, nx=40):
    """按高度切片，看每片在前后方向占了多少（占用格子比例）。占得最满的那片是身体，上下扩到占用率掉到最大值的 thr 倍为止。
    返回 (身体底, 身体顶, 每片占用率)"""
    z0 = min(v.z for v in coords)
    z1 = max(v.z for v in coords)
    x0 = min(v.x for v in coords)
    x1 = max(v.x for v in coords)
    dz = max((z1 - z0) / nz, 1e-6)
    dx = max((x1 - x0) / nx, 1e-6)
    occ = [set() for _ in range(nz)]
    for v in coords:
        occ[min(nz - 1, int((v.z - z0) / dz))].add(min(nx - 1, int((v.x - x0) / dx)))
    cov = [len(s) / nx for s in occ]
    peak = max(range(nz), key=lambda i: (cov[i], i))
    lim = cov[peak] * thr
    lo = peak
    while lo > 0 and cov[lo - 1] >= lim:
        lo -= 1
    hi = peak
    while hi < nz - 1 and cov[hi + 1] >= lim:
        hi += 1
    return z0 + lo * dz, z0 + (hi + 1) * dz, cov


def profile_text(cov):
    """占用率剖面（从上到下），日志里看身体在哪个高度"""
    chars = ' .:-=+*#%@'
    return ''.join(chars[min(9, int(c * 9.99))] for c in reversed(cov))


LEG_KEYS = ('fl', 'fr', 'bl', 'br')
DOG_BONES = ['root', 'body', 'head', 'tail_antenna'] + [f'leg_{k}_{s}' for k in LEG_KEYS for s in ('upper', 'lower')]


def dog_class(v, body_bottom, knee_z, cx, cy):
    if v.z >= body_bottom:
        return 'body'
    fb = 'f' if v.x >= cx else 'b'
    lr = 'l' if v.y >= cy else 'r'
    return f'leg_{fb}{lr}_{"upper" if v.z >= knee_z else "lower"}'


def dog_split(coords, cfg):
    """身体底 / 膝盖高度（厘米，缩短腿之前）和身体中心。配置 body_bottom_m / knee_m 写了就用写的"""
    bb, bt, cov = body_band(coords, cfg.get('body_band_threshold', 0.5))
    top = max(v.z for v in coords)
    log(f'高度剖面（上→下，越密 = 这一高度前后占得越满）：|{profile_text(cov)}|')
    if cfg.get('body_bottom_m') is not None:
        log(f'身体底 自动 {bb / M:.3f} 米 → 用配置的 {cfg["body_bottom_m"]} 米')
        bb = cfg['body_bottom_m'] * M
    knee = bb * cfg.get('knee_frac', 0.5)
    if cfg.get('knee_m') is not None:
        knee = cfg['knee_m'] * M
    body_pts = [v for v in coords if v.z >= bb]
    bx = Box.of(body_pts) if body_pts else Box.of(coords)
    log(f'身体底 {bb / M:.3f} 米、身体顶 {bt / M:.3f} 米、膝盖 {knee / M:.3f} 米；身体盒 {bx}')
    if bb < 0.2 * top or bb > 0.85 * top:
        warn(f'身体底在总高的 {bb / max(top, 1e-6):.0%}，不太像四足（看 survey 的分段图，必要时配置 body_bottom_m）')
    return bb, knee, bx.center.x, bx.center.y


# ---------------------------------------------------------------- 无人机：找旋翼（survey 和 build 共用）
ROTOR_KEYS = {(1, 1): 'FL', (1, -1): 'FR', (-1, 1): 'BL', (-1, -1): 'BR'}


def find_rotors(objs, cfg):
    """在工作网格（世界坐标、机身中心在原点、朝 +X）里找四个旋翼。
    候选 = 散件里：名字像旋翼（rotor_objects 正则）的物体的全部散件；没有名字命中时按形状：在四个角上、扁平、不是圆环（涵道）。
    每个角取直径最大的候选当种子，种子圆里、高度差不多的候选都算这一个旋翼（桨叶 + 桨毂）。
    配置 rotor_hubs_m（{"FL":[x,y,z],...}，米，摆好以后的坐标）+ rotor_radius_m 时直接按位置取。
    返回 {'FL': {'hub': Vector, 'dia': 厘米, 'parts': [(物体, [面号])]}, ...}（找不到的角没有键）"""
    allb = objs_box(objs)
    span = max(allb.size.x, allb.size.y)
    name_re = cfg.get('rotor_objects') or r'(?i)prop|rotor|blade|fan|propel|helic|screw'
    cands = []
    for o in objs:
        bm = bmesh.new()
        bm.from_mesh(o.data)
        bm.faces.ensure_lookup_table()
        bm.faces.index_update()
        named = re.search(name_re, o.name) is not None or re.search(name_re, o.get('easter_src_name', '')) is not None
        for comp in components(bm):
            st = part_stats(comp)
            b = st['box']
            c = b.center
            dia = max(b.size.x, b.size.y)
            cands.append({'obj': o, 'faces': [f.index for f in comp], 'box': b, 'center': c, 'dia': dia, 'named': named,
                          'quad': (1 if c.x >= 0 else -1, 1 if c.y >= 0 else -1),
                          'corner': abs(c.x) > 0.12 * span and abs(c.y) > 0.12 * span,
                          'flat': b.size.z < 0.35 * max(dia, 1e-6), 'ring': st['hollow'] > 0.6 and st['round'] > 0.7,
                          'size_ok': 0.06 * span < dia < 0.7 * span, 'tris': st['tris']})
        bm.free()
    log(f'旋翼查找：散件 {len(cands)} 块，跨度 {span / M:.2f} 米，名字正则 {name_re!r}')
    hubs = cfg.get('rotor_hubs_m')
    out = {}
    if hubs:
        r = cfg.get('rotor_radius_m', 0.3) * M
        for key, p in hubs.items():
            hp = Vector(p) * M
            mem = [c for c in cands if ((c['center'].x - hp.x) ** 2 + (c['center'].y - hp.y) ** 2) ** 0.5 < r and abs(c['center'].z - hp.z) < r]
            if mem:
                out[key] = {'hub': hp, 'dia': 2 * r, 'parts': [(c['obj'], c['faces']) for c in mem], 'how': '配置 rotor_hubs_m'}
    else:
        named = [c for c in cands if c['named']]
        if named:
            pool, how = named, '名字'
        else:
            pool, how = [c for c in cands if c['corner'] and c['flat'] and not c['ring'] and c['size_ok']], '形状'
        for q, key in ROTOR_KEYS.items():
            qc = [c for c in pool if c['quad'] == q]
            if not qc:
                continue
            seed = max(qc, key=lambda c: c['dia'])
            rr = seed['dia'] * 0.55
            mem = [c for c in qc if ((c['center'].x - seed['center'].x) ** 2 + (c['center'].y - seed['center'].y) ** 2) ** 0.5 < rr
                   and abs(c['center'].z - seed['center'].z) < seed['dia'] * 0.5]
            ub = None
            for c in mem:
                ub = c['box'].union(ub) if ub else c['box']
            out[key] = {'hub': Vector((seed['center'].x, seed['center'].y, ub.center.z)), 'dia': max(ub.size.x, ub.size.y),
                        'parts': [(c['obj'], c['faces']) for c in mem], 'how': how}
    for key in ('FL', 'FR', 'BL', 'BR'):
        r = out.get(key)
        if r:
            log(f'  旋翼 {key}（按{r["how"]}）：桨毂 {fmt(r["hub"])} 米，直径 {r["dia"] / M:.2f} 米，散件 {len(r["parts"])} 块')
        else:
            log(f'  旋翼 {key}：没找到')
    rings = [c for c in cands if c['ring'] and c['corner']]
    if rings:
        log(f'  另有 {len(rings)} 个在角上的圆环（当涵道，留在机身上）')
    big = sorted(cands, key=lambda c: -c['dia'])[:12]
    for c in big:
        log(f'    散件 {c["obj"].get("easter_src_name", c["obj"].name)} 中心 {fmt(c["center"])} 直径 {c["dia"] / M:.2f} 扁 {c["flat"]} 圆环 {c["ring"]} '
            f'在角上 {c["corner"]} 名字命中 {c["named"]} 三角 {c["tris"]}')
    return out


def split_rotors(rotors):
    """把找到的旋翼面从工作网格里拆出来：返回 {'FL': 物体, ...}"""
    by_obj = {}
    for key, r in rotors.items():
        for o, faces in r['parts']:
            by_obj.setdefault(o.name, {}).setdefault(key, []).extend(faces)
    out = {}
    for oname, per in by_obj.items():
        o = bpy.data.objects[oname]
        # 一次拆一个角；拆完面号会变，所以先把每个角的面打上标记再按标记拆
        bm = bmesh.new()
        bm.from_mesh(o.data)
        lay = bm.faces.layers.int.new('easter_rotor')
        bm.faces.ensure_lookup_table()
        keys = list(per)
        for ki, key in enumerate(keys):
            for fi in per[key]:
                bm.faces[fi][lay] = ki + 1
        bm.to_mesh(o.data)
        bm.free()
        for ki, key in enumerate(keys):
            bm = bmesh.new()
            bm.from_mesh(o.data)
            lay = bm.faces.layers.int.get('easter_rotor')
            ids = [f.index for f in bm.faces if f[lay] == ki + 1]
            bm.free()
            part = split_object(o, ids, f'rotor_{key}_{oname}')
            out.setdefault(key, []).append(part)
    res = {}
    for key, parts in out.items():
        res[key] = join_objects(parts, f'rotor_{key}')
        if 'easter_rotor' in res[key].data.attributes:
            res[key].data.attributes.remove(res[key].data.attributes['easter_rotor'])
    for oname in by_obj:
        me = bpy.data.objects[oname].data
        if 'easter_rotor' in me.attributes:
            me.attributes.remove(me.attributes['easter_rotor'])
    return res
