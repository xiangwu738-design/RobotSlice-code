"""把资产库里指定的几件单独渲染成大图（Workbench，只看形状），编号和 blend_asset_survey.py 的 _survey.json 一致。
blender -b 库.blend --factory-startup --python blend_render_items.py -- 输出目录 边长 编号[,编号...]|all[:分类]"""
import bpy, sys, os, math
from mathutils import Vector

args = sys.argv[sys.argv.index('--') + 1:]
out, res, pick = os.path.abspath(args[0]), int(args[1]), args[2]
os.makedirs(out, exist_ok=True)

cats = {}
cat_file = os.path.join(os.path.dirname(bpy.data.filepath), 'blender_assets.cats.txt')
if os.path.exists(cat_file):
    for line in open(cat_file, encoding='utf-8', errors='replace'):
        p = line.strip().split(':')
        if len(p) == 3 and len(p[0]) == 36:
            cats[p[0]] = p[1]


def objs_of(idb):
    if isinstance(idb, bpy.types.Collection):
        return list(idb.all_objects)
    res_, stack = [], [idb]
    while stack:
        o = stack.pop()
        res_.append(o)
        stack.extend(o.children)
    return res_


assets = [idb for coll in (bpy.data.objects, bpy.data.collections) for idb in coll if idb.asset_data]
if pick.startswith('all'):
    want = pick.split(':', 1)[1] if ':' in pick else ''
    ids = [k for k, a in enumerate(assets) if not want or cats.get(a.asset_data.catalog_id, '') == want]
else:
    ids = [int(x) for x in pick.split(',')]

sc = bpy.context.scene
sc.render.engine = 'BLENDER_WORKBENCH'
sc.display.shading.light = 'STUDIO'
sc.display.shading.color_type = 'SINGLE'
sc.display.shading.single_color = (0.62, 0.63, 0.66)
sc.display.shading.show_cavity = True
sc.display.shading.cavity_type = 'BOTH'
sc.display.shading.show_shadows = False
sc.display.shading.background_type = 'VIEWPORT'
sc.display.shading.background_color = (0.16, 0.17, 0.19)
sc.render.resolution_x = sc.render.resolution_y = res
sc.render.film_transparent = False
sc.render.image_settings.file_format = 'PNG'

# 资产都放在一个干净的场景里，一次只链进一件
view = bpy.data.scenes.new('render_items')
view.render.engine = sc.render.engine
for k in ('light', 'color_type', 'single_color', 'show_cavity', 'cavity_type', 'show_shadows', 'background_type', 'background_color'):
    setattr(view.display.shading, k, getattr(sc.display.shading, k))
view.render.resolution_x = view.render.resolution_y = res
view.render.image_settings.file_format = 'PNG'
cam_data = bpy.data.cameras.new('cam')
cam_data.lens = 70
cam = bpy.data.objects.new('cam', cam_data)
view.collection.objects.link(cam)
view.camera = cam
holder = bpy.data.collections.new('holder')
view.collection.children.link(holder)

for k in ids:
    a = assets[k]
    objs = objs_of(a)
    for o in list(holder.objects):
        holder.objects.unlink(o)
    for o in objs:
        if o.name not in holder.objects:
            holder.objects.link(o)
    bpy.context.window.scene = view if bpy.context.window else None
    bpy.context.view_layer.update() if bpy.context.view_layer else None
    lo, hi = Vector((1e9,) * 3), Vector((-1e9,) * 3)
    for o in objs:
        if o.type != 'MESH':
            continue
        for c in o.bound_box:
            w = o.matrix_world @ Vector(c)
            lo = Vector(map(min, lo, w)); hi = Vector(map(max, hi, w))
    if lo.x > hi.x:
        continue
    ctr, rad = (lo + hi) / 2, max((hi - lo).length / 2, 0.01)
    d = Vector((1.0, -1.25, 0.8)).normalized()
    cam.location = ctr + d * rad / math.sin(cam_data.angle / 2) * 1.05
    cam.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()
    cam_data.clip_start, cam_data.clip_end = rad * 0.01, rad * 20
    view.render.filepath = os.path.join(out, f'{k:04d}.png')
    bpy.ops.render.render(write_still=True, scene=view.name)
    print(f'[render] {k} {a.name} {cats.get(a.asset_data.catalog_id, "")}')
