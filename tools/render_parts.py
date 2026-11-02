"""配装表用的部件棚拍图（Blender，透明背景、三点光、自动取景）：步枪、刀、导弹巢。
材质按游戏里的贴图接（步枪压冷灰、刀槽自发光青色、导弹巢按机体配色）。
blender -b --factory-startup -P tools/render_parts.py -- 输出目录"""
import math
import os
import sys

import bpy
from mathutils import Vector

OUT = sys.argv[sys.argv.index('--') + 1]
os.makedirs(OUT, exist_ok=True)
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SWORD_TEX = os.path.join(ROOT, 'Inbox', 'NO454-20把科幻剑游戏武器3D模型素材', 'Assets', 'TEXTURE', '07')
GUN_TEX = os.path.join(ROOT, 'SourceAssets', 'guns', 'Gun3_tex')


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
    except Exception as e:  # 没有显卡就用 CPU
        print('[parts] GPU 不可用：', e)
    sc.cycles.samples = 128
    sc.cycles.use_denoising = True
    sc.render.resolution_x = 1600
    sc.render.resolution_y = 900
    sc.render.film_transparent = True
    sc.view_settings.view_transform = 'AgX'
    world = bpy.data.worlds.new('World') if not sc.world else sc.world
    sc.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes.get('Background')
    bg.inputs['Color'].default_value = (0.32, 0.34, 0.37, 1.0)
    bg.inputs['Strength'].default_value = 0.6


def clear():
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    for m in list(bpy.data.materials):
        bpy.data.materials.remove(m)


def tex(nt, path, color):
    n = nt.nodes.new('ShaderNodeTexImage')
    n.image = bpy.data.images.load(path)
    n.image.colorspace_settings.name = 'sRGB' if color else 'Non-Color'
    return n


def pbr(name, folder, prefix, tint=None, glow=None):
    """按贴图做一个 Principled 材质；tint 乘到底色上，glow 是自发光遮罩的颜色"""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes.get('Principled BSDF')
    files = os.listdir(folder)

    def find(key):
        for f in files:
            if f.startswith(prefix) and f.lower().endswith(key.lower() + '.png'):
                return os.path.join(folder, f)
        return None

    base = find('BaseColor')
    if base:
        t = tex(nt, base, True)
        if tint:
            mix = nt.nodes.new('ShaderNodeMix')
            mix.data_type = 'RGBA'
            mix.blend_type = 'MULTIPLY'
            mix.inputs[0].default_value = 1.0
            nt.links.new(t.outputs['Color'], mix.inputs[6])
            mix.inputs[7].default_value = (*tint, 1.0)
            nt.links.new(mix.outputs[2], bsdf.inputs['Base Color'])
        else:
            nt.links.new(t.outputs['Color'], bsdf.inputs['Base Color'])
    for key, socket in (('Metallic', 'Metallic'), ('Roughness', 'Roughness')):
        p = find(key)
        if p:
            nt.links.new(tex(nt, p, False).outputs['Color'], bsdf.inputs[socket])
    p = find('Normal')
    if p:
        nm = nt.nodes.new('ShaderNodeNormalMap')
        nt.links.new(tex(nt, p, False).outputs['Color'], nm.inputs['Color'])
        nt.links.new(nm.outputs['Normal'], bsdf.inputs['Normal'])
    p = find('Emissive')
    if p and glow:
        nt.links.new(tex(nt, p, False).outputs['Color'], bsdf.inputs['Emission Strength'])
        bsdf.inputs['Emission Color'].default_value = (*glow, 1.0)
    return m


def flat(name, color, metallic=0.6, rough=0.45, emit=None):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes.get('Principled BSDF')
    b.inputs['Base Color'].default_value = (*color, 1.0)
    b.inputs['Metallic'].default_value = metallic
    b.inputs['Roughness'].default_value = rough
    if emit:
        b.inputs['Emission Color'].default_value = (*emit, 1.0)
        b.inputs['Emission Strength'].default_value = 4.0
    return m


def import_fbx(path):
    bpy.ops.import_scene.fbx(filepath=path)
    return [o for o in bpy.data.objects if o.type == 'MESH']


def frame_and_render(meshes, out, view=(0.35, -1.0, 0.28), fill=1.0):
    pts = [o.matrix_world @ Vector(c) for o in meshes for c in o.bound_box]
    lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    c = (lo + hi) / 2
    size = max((hi - lo).x, (hi - lo).y, (hi - lo).z)
    d = Vector(view).normalized()
    cam_data = bpy.data.cameras.new('Cam')
    cam_data.lens = 70
    cam = bpy.data.objects.new('Cam', cam_data)
    bpy.context.collection.objects.link(cam)
    fov = 2 * math.atan(cam_data.sensor_width / 2 / cam_data.lens)
    dist = size * 0.62 / math.tan(fov / 2) / fill
    cam.location = c + d * dist
    cam.rotation_euler = (c - cam.location).to_track_quat('-Z', 'Y').to_euler()
    cam_data.clip_end = dist * 10
    bpy.context.scene.camera = cam

    def area(name, offset, energy, s):
        ld = bpy.data.lights.new(name, 'AREA')
        ld.energy = energy * size * size
        ld.size = s * size
        lo_ = bpy.data.objects.new(name, ld)
        bpy.context.collection.objects.link(lo_)
        lo_.location = c + Vector(offset) * size * 1.6
        lo_.rotation_euler = (c - lo_.location).to_track_quat('-Z', 'Y').to_euler()

    area('Key', (0.8, -1.0, 1.0), 320, 0.8)
    area('Fill', (-1.0, -0.8, 0.3), 90, 1.2)
    area('Rim', (-0.3, 1.2, 0.9), 260, 0.6)
    bpy.context.scene.render.filepath = out
    bpy.ops.render.render(write_still=True)
    print('[parts] 已渲染', out)


setup_render()

# 步枪
clear()
meshes = import_fbx(os.path.join(ROOT, 'SourceAssets', 'guns', 'SM_Gun3.fbx'))
gun = pbr('Gun', GUN_TEX, '3_uv_checker_material_uv_grid_4096x4096', tint=(0.62, 0.66, 0.72))
for o in meshes:
    o.data.materials.clear()
    o.data.materials.append(gun)
frame_and_render(meshes, os.path.join(OUT, '射击_步枪.png'), fill=1.02)

# 刀
clear()
meshes = import_fbx(os.path.join(ROOT, 'SourceAssets', 'swords', 'SM_Sword07.fbx'))
sword = pbr('Sword', SWORD_TEX, '07_DefaultMaterial', glow=(0.25, 0.75, 1.0))
for o in meshes:
    o.data.materials.clear()
    o.data.materials.append(sword)
frame_and_render(meshes, os.path.join(OUT, '近战_刀.png'), view=(0.25, -1.0, 0.35), fill=1.08)

# 导弹巢（材质槽按机体配色）
clear()
meshes = import_fbx(os.path.join(ROOT, 'SourceAssets', 'weapons', 'SM_MissilePod.fbx'))
palette = {
    'MI_Robot_Frame': flat('Frame', (0.05, 0.055, 0.06), 0.7, 0.5),
    'MI_Robot_PaintDark': flat('PaintDark', (0.07, 0.075, 0.08), 0.3, 0.55),
    'MI_Robot_Paint': flat('Paint', (0.32, 0.34, 0.37), 0.3, 0.5),
    'MI_Robot_Accent': flat('Accent', (0.9, 0.32, 0.06), 0.2, 0.45),
    'MI_Robot_Glow': flat('Glow', (0.1, 0.4, 0.6), 0.0, 0.3, emit=(0.25, 0.75, 1.0)),
    'MI_Robot_Chrome': flat('Chrome', (0.8, 0.8, 0.82), 1.0, 0.18),
    'MI_Robot_Nozzle': flat('Nozzle', (0.1, 0.1, 0.1), 0.8, 0.35),
}
for o in meshes:
    for slot in o.material_slots:
        base = slot.material.name.split('.')[0] if slot.material else ''
        slot.material = palette.get(base, palette['MI_Robot_Paint'])
frame_and_render(meshes, os.path.join(OUT, '肩部_导弹.png'), view=(0.7, -1.0, 0.45), fill=0.62)
