# 看 K 系列素材包：统计 + 判断是「摊开摆好的」还是「叠在一起的」，叠着的摆成一条街，打光出几张图。只读，不存 .blend。
# blender -b 素材.blend -P 本脚本 -- 输出目录 名字
import json, math, os, sys
import bpy
from mathutils import Vector

args = sys.argv[sys.argv.index('--') + 1:]
OUT, NAME = os.path.abspath(args[0]), args[1]
os.makedirs(OUT, exist_ok=True)
scene = max(bpy.data.scenes, key=lambda s: len(s.objects))
if bpy.context.window:
    bpy.context.window.scene = scene
try:
    bpy.context.preferences.system.gl_texture_limit = 'CLAMP_1024'
except Exception:
    pass


def walk(o):
    yield o
    for c in o.children:
        yield from walk(c)


pieces = []
polys = 0
for o in scene.objects:
    if o.parent is not None:
        continue
    lo, hi, p = [1e12] * 3, [-1e12] * 3, 0
    for x in walk(o):
        if x.type != 'MESH' or x.data is None:
            continue
        p += len(x.data.polygons)
        for corner in x.bound_box:
            w = x.matrix_world @ Vector(corner)
            for i in range(3):
                lo[i], hi[i] = min(lo[i], w[i]), max(hi[i], w[i])
    if hi[0] > lo[0]:
        pieces.append((o, Vector(lo), Vector(hi), p))
        polys += p
if not pieces:
    print('NO MESH', flush=True)
    sys.exit(0)


def pct(vals, q):
    vals = sorted(vals)
    return vals[min(len(vals) - 1, int(len(vals) * q))]


hs = sorted((p[2][2] - p[1][2] for p in pieces), reverse=True)
med_h = hs[min(len(hs) - 1, 10)] if hs else 1.0
pieces = [p for p in pieces if (p[2][2] - p[1][2]) < max(med_h, 1e-3) * 1000]
S = max(med_h, 1e-3) / 20.0
print('SCALE 1 m =', round(S, 4), 'units', flush=True)
ulo = Vector([pct([p[1][i] for p in pieces], 0.02) for i in range(3)])
uhi = Vector([pct([p[2][i] for p in pieces], 0.98) for i in range(3)])
uarea = max((uhi[0] - ulo[0]) * (uhi[1] - ulo[1]), 1.0)
foot = sum((p[2][0] - p[1][0]) * (p[2][1] - p[1][1]) for p in pieces)
stacked = True
sizes = sorted(((round(p[2][0] - p[1][0]), round(p[2][1] - p[1][1]), round(p[2][2] - p[1][2])) for p in pieces), key=lambda s: -s[0] * s[1] * s[2])
heights = sorted((p[2][2] - p[1][2] for p in pieces), reverse=True)
info = {'name': NAME, 'objects': len(scene.objects), 'roots': len(pieces), 'polys_M': round(polys / 1e6, 2), 'span_m': [round(uhi[i] - ulo[i]) for i in range(3)],
        'foot_ratio': round(foot / uarea, 2), 'stacked': stacked, 'top_sizes': sizes[:14], 'tallest': [round(h) for h in heights[:6]],
        'collections': [c.name[:24] for c in scene.collection.children][:16], 'images': len(bpy.data.images), 'cams': [o.name[:20] for o in scene.objects if o.type == 'CAMERA'][:4],
        'names': [p[0].name[:22] for p in sorted(pieces, key=lambda p: -p[3])[:14]]}
json.dump(info, open(f'{OUT}/{NAME}.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print('INFO', info, flush=True)

r = scene.render
r.resolution_x, r.resolution_y, r.resolution_percentage = 1280, 720, 100
r.image_settings.file_format = 'JPEG'
r.image_settings.quality = 86
try:
    r.engine = 'BLENDER_EEVEE'
except TypeError:
    r.engine = 'BLENDER_EEVEE_NEXT'
try:
    scene.eevee.taa_render_samples = 16
except Exception:
    pass
w = bpy.data.worlds.new('preview_world')
w.use_nodes = True
bg = next(n for n in w.node_tree.nodes if n.type == 'BACKGROUND')
bg.inputs[0].default_value = (0.5, 0.56, 0.66, 1)
bg.inputs[1].default_value = 0.7
scene.world = w
sd = bpy.data.lights.new('preview_sun', 'SUN')
sd.energy, sd.color = 3.5, (1.0, 0.92, 0.8)
sd.angle = math.radians(3)
so = bpy.data.objects.new('preview_sun', sd)
so.rotation_euler = (math.radians(58), 0, math.radians(145))
scene.collection.objects.link(so)
cam_data = bpy.data.cameras.new('prev')
cam_data.clip_start, cam_data.clip_end = 0.2 * S, 60000.0 * S
cam = bpy.data.objects.new('prev', cam_data)
scene.collection.objects.link(cam)
scene.camera = cam


def shoot(tag, loc, target, lens=24):
    cam.location = loc
    cam.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
    cam_data.lens = lens
    r.filepath = f'{OUT}/{NAME}_{tag}.jpg'
    bpy.ops.render.render(write_still=True)
    print('SHOT', tag, flush=True)


if stacked:
    # 摊成一条街：40 米宽，大件在前，两边错落
    big = sorted([p for p in pieces if max(p[2][0] - p[1][0], p[2][1] - p[1][1]) >= 4 * S and p[2][2] - p[1][2] >= 4 * S], key=lambda p: -(p[2][2] - p[1][2]))[:44]
    y = {0: 0.0, 1: 0.0}
    used = set()
    for i, (o, lo, hi, _) in enumerate(big):
        side = i % 2
        wid, dep = hi[0] - lo[0], hi[1] - lo[1]
        tx = (24 * S + wid / 2) * (1 if side else -1)
        ty = y[side] + dep / 2
        y[side] += dep + 5 * S
        o.location = o.location + Vector((tx - (lo[0] + hi[0]) / 2, ty - (lo[1] + hi[1]) / 2, -lo[2]))
        used.add(o)
    for p in pieces:
        if p[0] not in used:
            for x in walk(p[0]):
                x.hide_render = True
    bpy.context.view_layer.update()
    L = max(y.values()) if y else 100.0
    me = bpy.data.meshes.new('preview_plane')
    s = max(L, 400 * S) * 2
    me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    mat = bpy.data.materials.new('preview_plane')
    mat.use_nodes = True
    b = next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    b.inputs['Base Color'].default_value = (0.11, 0.1, 0.09, 1)
    me.materials.append(mat)
    scene.collection.objects.link(bpy.data.objects.new('preview_plane', me))
    print('STREET', round(L), 'pieces', len(big), flush=True)
    shoot('street', Vector((0, -35 * S, 7 * S)), Vector((0, 60 * S, 18 * S)), 22)
    shoot('mid', Vector((-8 * S, L * 0.3, 14 * S)), Vector((6 * S, L * 0.3 + 60 * S, 24 * S)), 22)
    shoot('high', Vector((-L * 0.42, -L * 0.14, L * 0.32)), Vector((0, L * 0.4, 10)), 28)
else:
    c = (ulo + uhi) * 0.5
    size = max(uhi[0] - ulo[0], uhi[1] - ulo[1], 1.0)
    shoot('top', c + Vector((0, -0.02 * size, size * 1.05)), c, 30)
    shoot('q', c + Vector((-size * 0.62, -size * 0.62, size * 0.5)), c, 30)
    dg = bpy.context.evaluated_depsgraph_get()
    pts = []
    for ix in range(21):
        for iy in range(21):
            p = Vector((ulo[0] + (uhi[0] - ulo[0]) * ix / 20, ulo[1] + (uhi[1] - ulo[1]) * iy / 20, uhi[2] + 20))
            hit, loc, *_ = scene.ray_cast(dg, p, Vector((0, 0, -1)))
            if hit:
                pts.append(loc.copy())
    if pts:
        z0 = pct([p[2] for p in pts], 0.25)
        low = sorted([p for p in pts if p[2] < z0 + 2], key=lambda p: (p - c).to_2d().length)
        for k, p in enumerate((low[:1] + low[len(low) // 2:len(low) // 2 + 1]) if low else []):
            eye = p + Vector((0, 0, 6.0))
            best, bestd = Vector((1, 0, 0)), -1.0
            for a in range(16):
                d = Vector((math.cos(a * math.pi / 8), math.sin(a * math.pi / 8), 0))
                hit, loc, *_ = scene.ray_cast(dg, eye, d, distance=1500.0)
                dist = (loc - eye).length if hit else 1500.0
                if dist > bestd:
                    best, bestd = d, dist
            shoot(f'eye{k}', eye, eye + best * 50 + Vector((0, 0, 4.0)), 24)
print('DONE', flush=True)
