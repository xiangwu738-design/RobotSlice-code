"""读一个 Blender 资产库文件：列出标了资产的物体 / 集合（名字、分类、三角面、尺寸、材质贴图），存下文件里自带的资产缩略图。
不渲染，只用 CPU。blender -b 文件.blend --python blend_asset_survey.py -- 输出目录"""
import bpy, sys, os, json, re
out = sys.argv[sys.argv.index('--') + 1]
os.makedirs(out, exist_ok=True)

cats = {}
cat_file = os.path.join(os.path.dirname(bpy.data.filepath), 'blender_assets.cats.txt')
if os.path.exists(cat_file):
    for line in open(cat_file, encoding='utf-8', errors='replace'):
        p = line.strip().split(':')
        if len(p) == 3 and len(p[0]) == 36:
            cats[p[0]] = p[1]


def obj_tris(o, seen):
    if o.name in seen:
        return 0
    seen.add(o.name)
    t = 0
    if o.type == 'MESH' and o.data:
        t = sum(len(p.vertices) - 2 for p in o.data.polygons)
        for m in o.modifiers:
            if m.type == 'SUBSURF' and m.show_render:
                t *= 4 ** m.render_levels
            elif m.type in ('ARRAY',) and m.show_render:
                t *= max(1, m.count)
            elif m.type == 'MIRROR' and m.show_render:
                t *= 2
    for c in o.children:
        t += obj_tris(c, seen)
    if o.instance_type == 'COLLECTION' and o.instance_collection:
        for c in o.instance_collection.all_objects:
            t += obj_tris(c, seen)
    return t


def objs_of(idb):
    if isinstance(idb, bpy.types.Collection):
        return list(idb.all_objects)
    res, stack = [], [idb]
    while stack:
        o = stack.pop()
        res.append(o)
        stack.extend(o.children)
    return res


def bbox(objs):
    from mathutils import Vector
    lo, hi = Vector((1e9,) * 3), Vector((-1e9,) * 3)
    n = 0
    for o in objs:
        if o.type != 'MESH':
            continue
        for c in o.bound_box:
            w = o.matrix_world @ Vector(c)
            lo = Vector(map(min, lo, w)); hi = Vector(map(max, hi, w)); n += 1
    return [round(hi[i] - lo[i], 3) for i in range(3)] if n else [0, 0, 0]


def save_preview(idb, path):
    pv = idb.preview
    if not pv or pv.image_size[0] == 0:
        return False
    w, h = pv.image_size
    img = bpy.data.images.new('pv_tmp', w, h, alpha=True)
    img.pixels.foreach_set(pv.image_pixels_float[:])
    img.filepath_raw = path
    img.file_format = 'PNG'
    img.save()
    bpy.data.images.remove(img)
    return True


items = []
for coll in (bpy.data.objects, bpy.data.collections):
    for idb in coll:
        if not idb.asset_data:
            continue
        objs = objs_of(idb)
        mats = sorted({s.material.name for o in objs for s in getattr(o, 'material_slots', []) if s.material})
        k = len(items)
        safe = re.sub(r'[\/:*?"<>|]', '_', idb.name)
        pv = save_preview(idb, os.path.join(out, f'{k:04d}_{safe}.png'))
        items.append(dict(i=k, name=idb.name, kind=type(idb).__name__, cat=cats.get(idb.asset_data.catalog_id, ''),
                          tris=obj_tris(idb, set()) if isinstance(idb, bpy.types.Object) else sum(obj_tris(o, set()) for o in objs if not o.parent),
                          size=bbox(objs), mats=mats[:6], preview=pv))

imgs = [dict(name=i.name, packed=bool(i.packed_file), exists=(bool(i.packed_file) or os.path.exists(bpy.path.abspath(i.filepath))), path=i.filepath, size=list(i.size))
        for i in bpy.data.images if i.type == 'IMAGE']
summary = dict(file=os.path.basename(bpy.data.filepath), blender=bpy.app.version_string, assets=len(items),
               objects=len(bpy.data.objects), meshes=len(bpy.data.meshes), materials=len(bpy.data.materials),
               images=len(imgs), images_missing=sum(not i['exists'] for i in imgs), images_packed=sum(i['packed'] for i in imgs),
               items=items, image_list=imgs[:80])
json.dump(summary, open(os.path.join(out, '_survey.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(f'[survey] {summary["file"]} 资产 {len(items)} 物体 {summary["objects"]} 材质 {summary["materials"]} 贴图 {len(imgs)}（缺 {summary["images_missing"]}，打包 {summary["images_packed"]}）')
