"""素材目录图：把收件箱里所有武器素材逐个棚拍（透明背景、三点光），给配装表用。
剑包 20 把（FBX + TEXTURE/编号）、枪包 GUN 长枪 12 把、COLT 手枪 10 把（FBX + 各自贴图包）。
最长的轴先转平再拍，素材原始朝向不统一也能拍成横图。
blender -b --factory-startup -P tools/render_catalog.py -- 输出目录 [只拍哪类：剑/长枪/手枪]"""
import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

ARGS = sys.argv[sys.argv.index('--') + 1:]
OUT = ARGS[0]
ONLY = ARGS[1] if len(ARGS) > 1 else ''
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SWORDS = os.path.join(ROOT, 'Inbox', 'NO454-20把科幻剑游戏武器3D模型素材', 'Assets')
GUNPACK = os.path.join(ROOT, 'Inbox', '_x', 'scifi', '9776  Blender次世代科幻3D模型')


def jobs():
    out = []
    for i in range(1, 21):
        # 贴图名有 DefaultMaterial / Material.001 两种，扩展名 png / jpg 都有：按编号前缀找
        out.append(('剑', '%02d' % i, os.path.join(SWORDS, 'fbx', '%02d.fbx' % i), os.path.join(SWORDS, 'TEXTURE', '%02d' % i), '%02d_' % i))
    for i in range(1, 13):
        tex = os.path.join(GUNPACK, 'GUN', 'GUN - Textures - Pack 1', str(i))
        prefix = '%d_uv_checker_material_uv_grid_4096x4096' % i
        if not os.path.isdir(tex):  # 第 5 把贴图只在 Blender 文件夹里，材质名也不一样
            tex = os.path.join(GUNPACK, 'GUN', 'GUN- Blender', str(i), str(i))
            prefix = 'SCIFI Guns - Materials set_'  # 黑 / 玻璃 / 红 / 白四套，按材质槽名分别接
        out.append(('长枪', '%02d' % i, os.path.join(GUNPACK, 'GUN', 'GUN FBX OBG', 'FBX', '%d.fbx' % i), tex, prefix))
    for i in range(1, 11):
        tex = os.path.join(GUNPACK, 'COLT', 'Colt - Textures - Pack 2', str(i))
        if not os.path.isdir(tex):
            tex = os.path.join(GUNPACK, 'GUN', 'COLT- Textures - Pack 2', str(i))
        out.append(('手枪', '%02d' % i, os.path.join(GUNPACK, 'COLT', 'Colt fbx obj', 'FBX', '%d.fbx' % i), tex,
                    '%d_uv_checker_material_uv_grid_4096x4096' % i))
    return [j for j in out if not ONLY or j[0] == ONLY]


def setup_render():
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    try:
        prefs = bpy.context.preferences.addons['cycles'].preferences
        prefs.compute_device_type = 'OPTIX'
        prefs.get_devices()
        for d in prefs.devices:
            d.use = True
        sc.cycles.device = 'GPU'
    except Exception as e:
        print('[catalog] GPU 不可用：', e)
    sc.cycles.samples = 64
    sc.cycles.use_denoising = True
    sc.render.resolution_x = 1200
    sc.render.resolution_y = 675
    sc.render.film_transparent = True
    sc.view_settings.view_transform = 'AgX'
    world = sc.world or bpy.data.worlds.new('World')
    sc.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes.get('Background')
    bg.inputs['Color'].default_value = (0.32, 0.34, 0.37, 1.0)
    bg.inputs['Strength'].default_value = 0.6


def clear():
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    for coll in (bpy.data.meshes, bpy.data.materials, bpy.data.images, bpy.data.cameras, bpy.data.lights):
        for x in list(coll):
            coll.remove(x)


def pbr(folder, prefix):
    m = bpy.data.materials.new('PBR')
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes.get('Principled BSDF')
    files = os.listdir(folder) if os.path.isdir(folder) else []

    def find(key):
        for f in files:
            low = f.lower()
            if f.startswith(prefix) and (low.endswith(key.lower() + '.png') or low.endswith(key.lower() + '.jpg')):
                return os.path.join(folder, f)
        return None

    def tex(path, color):
        n = nt.nodes.new('ShaderNodeTexImage')
        n.image = bpy.data.images.load(path)
        n.image.colorspace_settings.name = 'sRGB' if color else 'Non-Color'
        return n

    p = find('BaseColor')
    if p:
        nt.links.new(tex(p, True).outputs['Color'], bsdf.inputs['Base Color'])
    for key in ('Metallic', 'Roughness'):
        p = find(key)
        if p:
            nt.links.new(tex(p, False).outputs['Color'], bsdf.inputs[key])
    p = find('Normal')
    if p:
        nm = nt.nodes.new('ShaderNodeNormalMap')
        nt.links.new(tex(p, False).outputs['Color'], nm.inputs['Color'])
        nt.links.new(nm.outputs['Normal'], bsdf.inputs['Normal'])
    p = find('Emissive')
    if p:
        nt.links.new(tex(p, False).outputs['Color'], bsdf.inputs['Emission Strength'])
        bsdf.inputs['Emission Color'].default_value = (0.25, 0.75, 1.0, 1.0)
    if not files:
        print('[catalog] 找不到贴图：', folder)
    return m


def bounds(meshes):
    pts = [o.matrix_world @ Vector(c) for o in meshes for c in o.bound_box]
    lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    return lo, hi


def lay_flat(meshes):
    """最长的轴转到 X 上（横着拍）"""
    lo, hi = bounds(meshes)
    ext = hi - lo
    rot = Matrix.Identity(4)
    if ext.y >= ext.x and ext.y >= ext.z:
        rot = Matrix.Rotation(math.radians(90), 4, 'Z')
    elif ext.z >= ext.x and ext.z >= ext.y:
        rot = Matrix.Rotation(math.radians(-90), 4, 'Y')
    if rot != Matrix.Identity(4):
        c = (lo + hi) / 2
        to = Matrix.Translation(c) @ rot @ Matrix.Translation(-c)
        for o in meshes:
            if o.parent is None or o.parent not in meshes:
                o.matrix_world = to @ o.matrix_world
    bpy.context.view_layer.update()


def shoot(meshes, out):
    lo, hi = bounds(meshes)
    c = (lo + hi) / 2
    size = max((hi - lo).x, (hi - lo).y, (hi - lo).z)
    d = Vector((0.3, -1.0, 0.3)).normalized()
    cam_data = bpy.data.cameras.new('Cam')
    cam_data.lens = 70
    cam = bpy.data.objects.new('Cam', cam_data)
    bpy.context.collection.objects.link(cam)
    fov = 2 * math.atan(cam_data.sensor_width / 2 / cam_data.lens)
    dist = size * 0.6 / math.tan(fov / 2)
    cam.location = c + d * dist
    cam.rotation_euler = (c - cam.location).to_track_quat('-Z', 'Y').to_euler()
    cam_data.clip_start = dist * 0.01
    cam_data.clip_end = dist * 10
    bpy.context.scene.camera = cam
    for name, offset, energy, s in (('Key', (0.8, -1.0, 1.0), 320, 0.8), ('Fill', (-1.0, -0.8, 0.3), 90, 1.2), ('Rim', (-0.3, 1.2, 0.9), 260, 0.6)):
        ld = bpy.data.lights.new(name, 'AREA')
        ld.energy = energy * size * size
        ld.size = s * size
        lo_ = bpy.data.objects.new(name, ld)
        bpy.context.collection.objects.link(lo_)
        lo_.location = c + Vector(offset) * size * 1.6
        lo_.rotation_euler = (c - lo_.location).to_track_quat('-Z', 'Y').to_euler()
    bpy.context.scene.render.filepath = out
    bpy.ops.render.render(write_still=True)


setup_render()
os.makedirs(OUT, exist_ok=True)
for kind, num, fbx, texdir, prefix in jobs():
    clear()
    try:
        bpy.ops.import_scene.fbx(filepath=fbx)
    except Exception as e:
        print('[catalog] 导入失败', fbx, e)
        continue
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    for o in [o for o in bpy.data.objects if o.type in ('CAMERA', 'LIGHT')]:
        bpy.data.objects.remove(o, do_unlink=True)
    if prefix.endswith('set_'):
        # 多套材质：按原来的材质槽名（含 black / glass / red / white）分别接对应那套贴图
        sets = {}
        for o in meshes:
            for slot in o.material_slots:
                name = (slot.material.name if slot.material else '').lower()
                key = next((k for k in ('glass', 'red', 'white', 'black') if k in name), 'black')
                if key not in sets:
                    sets[key] = pbr(texdir, prefix + key)
                slot.material = sets[key]
    else:
        mat = pbr(texdir, prefix)
        for o in meshes:
            o.data.materials.clear()
            o.data.materials.append(mat)
    lay_flat(meshes)
    out = os.path.join(OUT, '%s_%s.png' % (kind, num))
    shoot(meshes, out)
    print('[catalog] 已渲染', out)
