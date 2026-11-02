"""原版峡谷图（L_MegaCanyon）的 Blender 侧：把科幻峡谷包的 Scene_03（有卡通云、行星贴图那一版，渲染图 Renders/1–3 就是它）原样拆成网格导出。
用户 11-01：「这个草坪和渲染效果不太满意。能否先将原地图拉近游戏」。尺度和原包一致（1 米 = 1 米），坐标保留 Blender 世界坐标，
UE 里所有网格摆在原点、按 (x, -y, z) 读，位置自然对上。
- SM_MC_Walls：Cube.056 整个（沟两侧挂下来的楼柱群，两半都要，约 73 万三角形）
- SM_MC_DeckTile：Cube.009 关掉阵列和镜像、只留「自动平滑」几何节点的一块甲板单元（阵列 13×7 + 镜像在 UE 里按实例摆）
- SM_MC_Plane：远景的大平面（甲板一直铺到 7 公里外，外侧面和底面；它中间只有一条缝，没有沟壁，沟壁在 UE 里用方块补）
- SM_MC_Clouds：全部元球云合成一个网格（原包 39 朵，贴着甲板上方 10–30 米）
- SM_MC_Planet / SM_MC_Moon：两个球（细分后），行星半径 375 米、1.1 公里外，大半截在地平线下
- SM_MC_Bits：沟里的小方块件和箭头
blender -b Scene_03.blend -P tools/blender_megacanyon_export.py -- 输出目录
"""
import json
import os
import sys

import bpy
import numpy as np
from mathutils import Vector

OUT = os.path.abspath(sys.argv[sys.argv.index('--') + 1:][0])
os.makedirs(OUT, exist_ok=True)
sc = bpy.context.scene


def log(*a):
    print('[megacanyon]', *a)


def mat(name):
    return bpy.data.materials.get('MC_' + name) or bpy.data.materials.new('MC_' + name)


def world_mesh(objs, name, material, disable=()):
    """几个物体（含修改器）合成一个世界坐标网格，材质槽统一成一个"""
    parts = []
    for o in objs:
        saved = {}
        for m in getattr(o, 'modifiers', []):
            if m.type in disable:
                saved[m.name] = (m.show_viewport, m.show_render)
                m.show_viewport = m.show_render = False
        deps = bpy.context.evaluated_depsgraph_get()
        me = bpy.data.meshes.new_from_object(o.evaluated_get(deps), depsgraph=deps)
        for k, (v, r) in saved.items():
            o.modifiers[k].show_viewport, o.modifiers[k].show_render = v, r
        me.transform(o.matrix_world)
        if o.matrix_world.determinant() < 0:
            me.flip_normals()
        me.materials.clear()
        me.materials.append(mat(material))
        for p in me.polygons:
            p.material_index = 0
        po = bpy.data.objects.new(name + '_part', me)
        sc.collection.objects.link(po)
        parts.append(po)
    bpy.ops.object.select_all(action='DESELECT')
    for p in parts:
        p.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    if len(parts) > 1:
        bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.name = ob.data.name = name
    co = np.empty(len(ob.data.vertices) * 3)
    ob.data.vertices.foreach_get('co', co)
    co = co.reshape(-1, 3)
    tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
    meta['meshes'][name] = {'min': co.min(0).tolist(), 'max': co.max(0).tolist(), 'tris': tris, 'src': [o.name for o in objs]}
    log(name, '←', len(objs), '个物体', tris, '三角形', '范围', co.min(0).round(1), '~', co.max(0).round(1))
    return ob


def export(ob, fbx):
    bpy.ops.object.select_all(action='DESELECT')
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.fbx(filepath=f'{OUT}/{fbx}', use_selection=True, object_types={'MESH'}, apply_unit_scale=True,
                             mesh_smooth_type='FACE', use_mesh_modifiers=False, add_leaf_bones=False, bake_anim=False,
                             axis_forward='-Z', axis_up='Y')


meta = {'meshes': {}}
O = bpy.data.objects
export(world_mesh([O['Cube.056']], 'SM_MC_Walls', 'Wall'), 'mc_walls.fbx')

deck = O['Cube.009']
arr = [m for m in deck.modifiers if m.type == 'ARRAY']
lo = Vector([min(v.co[i] for v in deck.data.vertices) for i in range(3)])
hi = Vector([max(v.co[i] for v in deck.data.vertices) for i in range(3)])
ls = hi - lo
rot = deck.matrix_world.to_3x3()
step_a = rot @ Vector((arr[0].relative_offset_displace[0] * ls[0], arr[0].relative_offset_displace[1] * ls[1], 0))
step_b = rot @ Vector((arr[1].relative_offset_displace[0] * ls[0], arr[1].relative_offset_displace[1] * ls[1], 0))
mirror = next(m for m in deck.modifiers if m.type == 'MIRROR')
meta['deck'] = {'count_a': arr[0].count, 'count_b': arr[1].count, 'step_a': list(step_a), 'step_b': list(step_b),
                'mirror_x': mirror.mirror_object.location.x if mirror.mirror_object else 0.0}
export(world_mesh([deck], 'SM_MC_DeckTile', 'Deck', disable=('ARRAY', 'MIRROR')), 'mc_decktile.fbx')
export(world_mesh([O['Plane']], 'SM_MC_Plane', 'Deck'), 'mc_plane.fbx')
export(world_mesh(sorted((o for o in O if o.name.startswith('Mball') and o.type == 'MESH'), key=lambda o: o.name), 'SM_MC_Clouds', 'Cloud'),
       'mc_clouds.fbx')
export(world_mesh([O['Sphere']], 'SM_MC_Planet', 'Planet'), 'mc_planet.fbx')
export(world_mesh([O['Sphere.001']], 'SM_MC_Moon', 'Moon'), 'mc_moon.fbx')
bits = [o for o in O if o.type == 'MESH' and 'Main' in [c.name for c in o.users_collection]
        and o.name not in ('Cube', 'Cube.009', 'Cube.056') and len(o.data.polygons) >= 1]
export(world_mesh(bits, 'SM_MC_Bits', 'Wall'), 'mc_bits.fbx')

sun = O['Sun']
d = sun.matrix_world.to_3x3() @ Vector((0, 0, -1))
cam = sc.camera
fwd = cam.matrix_world.to_3x3() @ Vector((0, 0, -1))
meta['sun'] = {'dir': list(d), 'energy': sun.data.energy, 'angle': sun.data.angle}
meta['camera'] = {'loc': list(cam.location), 'fwd': list(fwd), 'lens': cam.data.lens, 'sensor': cam.data.sensor_width,
                  'res': [sc.render.resolution_x, sc.render.resolution_y]}
json.dump(meta, open(f'{OUT}/megacanyon.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
log('写出', OUT, '太阳光线方向', [round(v, 3) for v in d], '相机朝向', [round(v, 3) for v in fwd])
