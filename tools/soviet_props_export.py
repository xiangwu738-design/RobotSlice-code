"""苏联板楼图的街道小品（Sketchfab / Poly Haven）统一导出：按表选物体、合并、缩放到真实尺寸、原点挪到底面中心，写 FBX + 贴图清单。
blender -b --factory-startup -P tools/soviet_props_export.py -- Inbox/_soviet_props/export
之后 py -3 -I tools/soviet_props_pack.py Inbox/_soviet_props/export（贴图整理成 D / N / ARM）→ tools/ue_soviet_import.py。
输出的网格叫 SM_SVS_<名>，材质槽叫 SVS_<名>，UE 里对应 MI_SV_SVS_<名>。
素材来自网上：.blend 只 append，不开自动脚本（不要加 -y）。
"""
import json
import os
import re
import sys

import bpy
from mathutils import Matrix, Vector

OUT = os.path.abspath(sys.argv[sys.argv.index('--') + 1])
os.makedirs(f'{OUT}/fbx', exist_ok=True)
os.makedirs(f'{OUT}/raw', exist_ok=True)
R = 'D:/RobotSlice/Inbox/_soviet_props/'
PH = 'D:/RobotSlice/Inbox/_polyhaven/models/'


def pbr(d, base, normal=None, rough=None, metal=None, ao=None, gloss=None, opacity=None, cap=1024, **kw):
    e = {'cap': cap}
    for k, v in (('base', base), ('normal', normal), ('rough', rough), ('metal', metal), ('gloss', gloss), ('opacity', opacity)):
        if v:
            e[k] = d + v
    e.update(kw)
    return e


ZH = R + 'zhiguli-soviet-cars/textures/'
NV = R + 'lada-niva/textures/'
BS = R + 'old-soviet-bus-stop/textures/'
BS2 = R + 'soviet-bus-stop-2/textures/'
L1 = R + 'soviet-streetlight-pack/textures/'
SL = R + 'rusty-soviet-slide/textures/'
SW = R + 'ussr-swings/source/x/SwingsBaked/textures/'
SS = R + 'seesaw-type-1/source/x/seesaw-type1/'
BN = R + 'old-soviet-bench/textures/'
TR = R + 'old-soviet-trash-can/textures/'
FL = R + 'soviet-flower-bed/textures/'
GA = R + 'soviet-garages-block-psx/textures/'


def niva(n, cap=512):
    return pbr(NV, f'niva_{n}_albedo.jpg', f'niva_{n}_normal.png', f'niva_{n}_roughness.jpg', f'niva_{n}_metallic.jpg', cap=cap)


def zh(c, ext='png'):
    return pbr(ZH, f'zhiguli_albedo_{c}.{ext}', 'zhiguli_normal.png', 'zhiguli_roughness.png', 'zhiguli_metallic.png', cap=1024)


def bs(n):
    return pbr(BS, f'BusStop_low_{n}_BaseColor.png', f'BusStop_low_{n}_Normal.png', f'BusStop_low_{n}_Roughness.png', f'BusStop_low_{n}_Metallic.png', cap=512)


GORKA = pbr(SL, 'Gorka_Albedo.png', 'Gorka_Normal.png', 'Gorka_Metalness-Gorka_Roughness@channels=G.png', 'Gorka_Metalness-Gorka_Roughness@channels=B.png', cap=512)
SWING = pbr(SW, 'MergedBake_Bake1_PBR_Diffuse.png', 'MergedBake_Bake1_PBR_Normal.png', None, 'MergedBake_Bake1_PBR_Metalness.png', gloss='MergedBake_Bake1_PBR_Glossiness.png', cap=512)
HEAD = pbr(L1, 'light3.png', cap=256, rough_v=0.6)
RUST = pbr(L1, 'rust.jpg', cap=256, rough_v=0.8)
# 表：源文件 -> [(网格名, 物体名正则, 缩放（到真实尺寸）, {材质键: 贴图})]；材质键 None = 整个网格一个材质（没带材质槽的模型）
SOURCES = [
    (R + 'zhiguli-soviet-cars/source/zhiguli.fbx', [
        ('Zhiguli_Gray', r'^zhiguli_gray', 1.0, {None: zh('gray')}),
        ('Zhiguli_Blue', r'^zhiguli_blue', 1.0, {None: zh('blue')}),
        ('Zhiguli_Green', r'^zhiguli_green', 1.0, {None: zh('green')}),
        ('Zhiguli_Red', r'^zhiguli_red', 1.0, {None: zh('red', 'jpg')}),
    ]),
    (R + 'lada-niva/source/model.fbx', [
        ('Niva', r'.', 1.09, {'niva_bodyshell': niva('bodyshell', 1024), 'niva_chassis': niva('chassis'), 'niva_interior': niva('interior', 256),
                              'niva_interior_shell': niva('interior_shell', 256), 'niva_accessories': niva('accessories'), 'niva_mirror': niva('mirror', 256),
                              'niva_glass': dict(color=[0.03, 0.04, 0.05], rough_v=0.1, metal_v=0.0), 'niva_needle': dict(color=[0.1, 0.1, 0.1], rough_v=0.5, metal_v=0.0)}),
    ]),
    (R + 'old-soviet-bus-stop/source/BusStop_low.fbx', [
        ('BusStop1', r'.', 1.4, {'seat': bs('seat'), 'base': bs('base'), 'roof': bs('roof'), 'metal': bs('metal'),
                                 'glass': dict(color=[0.05, 0.07, 0.08], rough_v=0.15, metal_v=0.0)}),
    ]),
    (R + 'soviet-bus-stop-2/source/busstop2.fbx', [
        ('BusStop2', r'.', 1.2, {None: pbr(BS2, 'Material_Base_color.png', 'Material_Normal.png', 'Material_Specular_roughness.png', cap=1024)}),
    ]),
    (R + 'soviet-streetlight-pack/source/streetlight pack.fbx', [
        # 原包路灯只有 5.75 米高（真实 9 米左右）：放大 1.6 倍。杆和灯头是分开的物体
        ('Light1', r'^Plane(\.001)?$', 1.6, {'*head': HEAD, '*pole': RUST}),
        ('Light2', r'^Plane\.00[24]$', 1.6, {'*head': HEAD, '*pole': RUST}),
        ('Light3', r'^Plane\.00[35]$', 1.6, {'*head': HEAD, '*pole': RUST}),
    ]),
    (R + 'rusty-soviet-slide/source/Gorka.blend', [('Slide', r'^Gorka', 'h3.0', {None: GORKA})]),
    (R + 'ussr-swings/source/x/SwingsBaked/source/Swings.fbx', [('Swings', r'.', 1.0, {None: SWING})]),
    (R + 'seesaw-type-1/source/x/seesaw-type1/seesaw-type1.blend', [
        ('Seesaw', r'^seesaw-type1$', 1.3, {None: pbr(SS, 'seesaw-type1-texture.png', 'seesaw-type1-normal.png', cap=512, rough_v=0.6)}),
    ]),
    (R + 'soviet-rocket-play-scape/source/Soviet PlayScape.fbx', [
        ('Rocket', r'.', 1.0, {'Circle Bars': dict(color=[0.25, 0.28, 0.45], rough_v=0.6, metal_v=0.0), 'Material': dict(color=[0.55, 0.12, 0.1], rough_v=0.6, metal_v=0.0),
                               'Metal Bars': dict(color=[0.3, 0.3, 0.32], rough_v=0.6, metal_v=0.0), 'Slide': dict(color=[0.45, 0.14, 0.12], rough_v=0.5, metal_v=0.0)}),
    ]),
    (R + 'old-soviet-bench/source/Bench.fbx', [
        ('Bench', r'.', 1.0, {None: pbr(BN, 'Courtyard_DefaultMaterial_BaseColor.png', 'Courtyard_DefaultMaterial_Normal.png', 'Courtyard_DefaultMaterial_Roughness.png',
                                        'Courtyard_DefaultMaterial_Metallic.png', cap=512)}),
    ]),
    # 原包边长 2.86 米、高 2.83（真实约 0.6 米的水泥 / 铁皮垃圾桶）
    (R + 'old-soviet-trash-can/source/trash can.fbx', [('TrashCan', r'.', 'h0.8', {None: pbr(TR, 'TrashCan.png', 'TrashCan_N.png', cap=256, rough_v=0.8)})]),
    (R + 'soviet-flower-bed/source/FlowerBed.fbx', [('FlowerBed', r'.', 1.0, {None: pbr(FL, 'FlowerBed.png', 'FlowerBed_N.png', cap=512, rough_v=0.9)})]),
    # 车库排：原包 13×4.2×2.3 米（比机体矮）→ 放大 1.3 倍到 17×5.4×3 米，当整人高的掩体
    (R + 'soviet-garages-block-psx/source/Garaji.obj', [('Garages', r'.', 1.3, {None: pbr(GA, 'garajiki.png', cap=1024, rough_v=0.85)})]),
    # Poly Haven（真实尺寸）
    (PH + 'covered_car/covered_car_2k.fbx', [
        ('CoveredCar', r'.', 1.0, {None: pbr(PH + 'covered_car/textures/', 'covered_car_diff_2k.jpg', 'covered_car_nor_gl_2k.exr', 'covered_car_rough_2k.exr', cap=1024)}),
    ]),
    (PH + 'fire_hydrant/fire_hydrant_2k.fbx', [
        ('Hydrant', r'.', 1.0, {None: pbr(PH + 'fire_hydrant/textures/', 'fire_hydrant_aged_diff_2k.png', 'fire_hydrant_aged_nor_gl_2k.png', 'fire_hydrant_aged_rough_2k.png', 'fire_hydrant_aged_metal_2k.png', cap=256)}),
    ]),
    (PH + 'dead_tree_trunk/dead_tree_trunk_2k.fbx', [
        ('DeadTrunk', r'.', 1.0, {None: pbr(PH + 'dead_tree_trunk/textures/', 'dead_tree_trunk_diff_2k.jpg', 'dead_tree_trunk_nor_gl_2k.exr', 'dead_tree_trunk_rough_2k.exr', cap=1024)}),
    ]),
    (PH + 'dead_tree_trunk_02/dead_tree_trunk_02_2k.fbx', [
        ('DeadTrunk2', r'.', 1.0, {None: pbr(PH + 'dead_tree_trunk_02/textures/', 'dead_tree_trunk_02_diff_2k.jpg', 'dead_tree_trunk_02_nor_gl_2k.exr', 'dead_tree_trunk_02_rough_2k.exr', cap=1024)}),
    ]),
]


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.view_settings.view_transform = 'Raw'
    sc.render.image_settings.color_mode = 'RGB'


def load(path):
    low = path.lower()
    if low.endswith('.fbx'):
        bpy.ops.import_scene.fbx(filepath=path)
    elif low.endswith('.obj'):
        bpy.ops.wm.obj_import(filepath=path)
    elif low.endswith('.blend'):
        with bpy.data.libraries.load(path, link=False) as (src, dst):
            dst.objects = list(src.objects)
        for o in dst.objects:
            if o is not None:
                bpy.context.scene.collection.objects.link(o)


def exr_to_png(path):
    """Poly Haven 的法线 / 粗糙度是 EXR，PIL 读不了：Blender 转成 8 位 PNG（不经过色彩变换）"""
    if not path.lower().endswith('.exr'):
        return path
    dst = f'{OUT}/raw/' + os.path.splitext(os.path.basename(path))[0] + '.png'
    if not os.path.exists(dst):
        img = bpy.data.images.load(path)
        img.colorspace_settings.name = 'Non-Color'
        sc = bpy.context.scene
        sc.render.image_settings.file_format = 'PNG'
        sc.render.image_settings.color_depth = '8'
        img.save_render(dst, scene=sc)
        bpy.data.images.remove(img)
    return dst


meta, mats = {}, {}
for src, items in SOURCES:
    if not os.path.exists(src):
        print('[svs] 缺源文件', src, flush=True)
        continue
    reset()
    load(src)
    sc = bpy.context.scene
    for name, pat, scale, table in items:
        objs = [o for o in sc.objects if o.type == 'MESH' and re.search(pat, o.name) and o.name != 'boundBox']
        if not objs:
            print('[svs] 没选到物体', name, pat, flush=True)
            continue
        slot_names = {}
        made = []
        dg = bpy.context.evaluated_depsgraph_get()
        for o in objs:
            me = bpy.data.meshes.new_from_object(o.evaluated_get(dg), depsgraph=dg)
            me.transform(o.matrix_world)
            if o.matrix_world.determinant() < 0:
                me.flip_normals()
            if me.uv_layers.active:
                me.uv_layers.active.name = 'UVMap'
                for uv in list(me.uv_layers):
                    if uv.name != 'UVMap':
                        me.uv_layers.remove(uv)
            # 材质映射
            if None in table:
                mat = bpy.data.materials.get(f'SVS_{name}') or bpy.data.materials.new(f'SVS_{name}')
                if me.materials:
                    for i in range(len(me.materials)):
                        me.materials[i] = mat
                else:
                    me.materials.append(mat)
                mats[f'SVS_{name}'] = table[None]
            else:
                for i, m in enumerate(me.materials):
                    key = re.sub(r'\.\d+$', '', m.name) if m else None
                    if '*head' in table:      # 路灯：Plane.00[1-3] 是杆（Material.002-4），其余是灯头
                        key = '*pole' if m and m.name in ('Material.002', 'Material.003', 'Material.004') else '*head'
                    if key not in table:
                        key = next(iter(table))
                        print('[svs] 材质没在表里，用第一个', name, m.name if m else None, flush=True)
                    nm = f'SVS_{name}_{key.strip("*").replace(" ", "_")}'
                    mat = bpy.data.materials.get(nm) or bpy.data.materials.new(nm)
                    me.materials[i] = mat
                    mats[nm] = table[key]
            po = bpy.data.objects.new(name + '_p', me)
            sc.collection.objects.link(po)
            made.append(po)
        bpy.ops.object.select_all(action='DESELECT')
        for p in made:
            p.select_set(True)
        bpy.context.view_layer.objects.active = made[0]
        if len(made) > 1:
            bpy.ops.object.join()
        ob = bpy.context.view_layer.objects.active
        ob.name = ob.data.name = 'SM_SVS_' + name
        vs = [v.co.copy() for v in ob.data.vertices]
        lo = Vector([min(v[i] for v in vs) for i in range(3)])
        hi = Vector([max(v[i] for v in vs) for i in range(3)])
        s = float(scale[1:]) / (hi.z - lo.z) if isinstance(scale, str) else float(scale)
        ob.data.transform(Matrix.Scale(s, 4) @ Matrix.Translation(-Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z))))
        vs = [v.co for v in ob.data.vertices]
        lo = Vector([min(v[i] for v in vs) for i in range(3)])
        hi = Vector([max(v[i] for v in vs) for i in range(3)])
        tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
        meta['SM_SVS_' + name] = {'min': list(lo), 'max': list(hi), 'tris': tris, 'scale': s, 'slots': [m.name for m in ob.data.materials if m]}
        bpy.ops.object.select_all(action='DESELECT')
        ob.select_set(True)
        bpy.ops.export_scene.fbx(filepath=f'{OUT}/fbx/SVS_{name}.fbx', use_selection=True, object_types={'MESH'}, apply_unit_scale=True,
                                 mesh_smooth_type='FACE', use_mesh_modifiers=False, add_leaf_bones=False, bake_anim=False, axis_forward='-Z', axis_up='Y')
        print(f'[svs] {name} {hi.x-lo.x:.2f}x{hi.y-lo.y:.2f}x{hi.z-lo.z:.2f} 米，{tris} 面，缩放 {s:.3f}', flush=True)
        for p in [ob]:
            sc.collection.objects.unlink(p)
    # EXR 转 PNG（要在 reset 之前）
    for m, e in mats.items():
        for k in ('normal', 'rough'):
            if isinstance(e.get(k), str) and e[k].lower().endswith('.exr'):
                e[k] = exr_to_png(e[k])
json.dump({'props': meta, 'materials': mats}, open(f'{OUT}/meshes.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print('[svs] 完成', len(meta), '个网格', len(mats), '个材质', flush=True)
