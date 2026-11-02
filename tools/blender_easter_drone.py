"""维克托重工无人机（彩蛋，11-02）：Cyberpunk Drone 底子 → 清理、缩放 → 拆出旋翼 → 套维克托机体零件外壳（方块感装甲、传感器头、武器吊舱、红色灯条）→ 导出。
  blender -b --factory-startup -P tools/blender_easter_drone.py -- [键=值 ...] [--config 别的.json] [--no-render]
参数全在 tools/easter_drone.json，命令行 键=值 临时改，例：target_span_m=1.8  forward=+Y  rotor_objects="(?i)blade"
输出：
  Inbox/_easter/export/SM_VDrone_Body.fbx  机身静态网格：原点 = 机身中心、朝 +X；插槽 Rotor_FL/FR/BL/BR（桨毂中心）、Muzzle（枪口，朝 +X），
                                           FBX 里是 SOCKET_ 空物体；简单碰撞 UCX_SM_VDrone_Body_00（盒子，不含旋翼）
  Inbox/_easter/export/SM_VDrone_Rotor.fbx 旋翼静态网格：原点 = 桨毂，绕本地 Z 转（四个角共用一个，取 rotor_master 那个角的）
  Inbox/_easter/export/VDrone_build.json   插槽位置（UE 坐标）、旋翼直径、尺寸、面数 —— UE 导入脚本按它核对 / 补插槽
  Inbox/_easter/build/VDrone_build.blend、Inbox/_easter/preview/drone/*.png + _sheet.json
"""
import json
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import easter_blender_lib as L  # noqa: E402

NAME = 'VDrone'
BODY = 'SM_VDrone_Body'
ROTOR = 'SM_VDrone_Rotor'
MIRROR = {'FL': (1, 1), 'FR': (1, -1), 'BL': (-1, 1), 'BR': (-1, -1)}


def fill_missing_hubs(rotors, cfg):
    hubs = {k: r['hub'].copy() for k, r in rotors.items()}
    if len(hubs) == 4:
        return hubs
    if not hubs:
        raise L.EasterError('一个旋翼都没找到：看 survey 图 / 日志的散件清单，在 easter_drone.json 写 rotor_objects（旋翼物体名正则）'
                            '或 rotor_hubs_m（四个桨毂位置，米）+ rotor_radius_m')
    if not cfg.get('allow_missing_rotors', True):
        raise L.EasterError(f'只找到旋翼 {sorted(hubs)}（allow_missing_rotors=false）')
    for k, (sx, sy) in MIRROR.items():
        if k in hubs:
            continue
        src_k, src = next(iter(hubs.items()))
        ox, oy = MIRROR[src_k]
        hubs[k] = Vector((src.x * sx * ox, src.y * sy * oy, src.z))
        L.warn(f'旋翼 {k} 没找到，插槽按 {src_k} 镜像放在 {L.fmt(hubs[k])} 米（这个角没有底子的桨叶，靠代码挂的旋翼模型）')
    return hubs


def ucx_box(box, inset, name):
    """简单碰撞：机身盒子往里收一点的长方体（UCX_ 前缀，UE 老 FBX 导入器认成凸包碰撞）"""
    c, h = box.center, box.half * inset
    bm = bmesh.new()
    vs = [bm.verts.new((c.x + sx * h.x, c.y + sy * h.y, c.z + sz * h.z)) for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]
    for f in ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)):
        bm.faces.new([vs[i] for i in f])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    o = L.obj_from_bm(name, bm, [])
    bm.free()
    return o


def build():
    args = L.script_args()
    cfg = L.load_config('drone', args)
    flags = cfg['_flags']
    path = L.find_base('drone', cfg.get('base_file', ''))
    objs = L.load_base(path)
    meshes, arms, info = L.prepare_base(objs, cfg, 'drone')
    if arms:
        L.log(f'底子带骨架 {[a.name for a in arms]}：无人机按静态网格做（绑定姿势），骨架 / 动作不要，旋翼由代码转')
    work, _srcs = L.bake_base(meshes)

    # ---- 旋翼
    rotors = L.find_rotors(work, cfg)
    hubs = fill_missing_hubs(rotors, cfg)
    parts = L.split_rotors(rotors)
    key = cfg.get('rotor_master', 'FL')
    if key not in parts:
        key = next(iter(parts)) if parts else None
    if key is None:
        raise L.EasterError('没有可用的旋翼几何')
    rotor = parts[key]
    for k, o in parts.items():
        if k != key:
            bpy.data.objects.remove(o)
    hub = hubs[key]
    L.transform_mesh(rotor, Matrix.Translation(-hub))
    rb = L.obj_box(rotor)
    if rb.size.z > 0.5 * min(rb.size.x, rb.size.y):
        L.warn(f'旋翼 {key} 不扁（{rb}）：可能不是绕 Z 转的桨，或者把电机也算进来了，看预览图')
    rs = cfg.get('rotor_scale', 1.0)
    if rs != 1.0:
        L.transform_mesh(rotor, Matrix.Scale(rs, 4))
    rotor.name = rotor.data.name = ROTOR
    colors = cfg.get('preview_colors', {})
    L.log(f'旋翼网格（取 {key} 角）：')
    L.apply_slots(rotor, cfg['base_slot_rules'], colors, cfg.get('rotor_slot'))
    rb = L.obj_box(rotor)
    rotor_dia = max(rb.size.x, rb.size.y)
    L.log(f'旋翼 {ROTOR}：直径 {rotor_dia / L.M:.2f} 米、厚 {rb.size.z / L.M:.3f} 米、{L.tris_of(rotor.data)} 三角')

    # ---- 机身
    work = [w for w in work if w.data.polygons]
    L.decimate_to(work, cfg.get('base_max_tris'), '底子机身')
    L.log('底子分槽：')
    for w in work:
        L.log(f'  {w.name}（原 {w.get("easter_src_name")}）')
        L.apply_slots(w, cfg['base_slot_rules'], colors)
    boxes = {'body': L.objs_box(work)}
    for k, h in hubs.items():
        r = rotor_dia * 0.5
        boxes[f'rotor_{k}'] = L.Box(h - Vector((r, r, rb.size.z * 0.5)), h + Vector((r, r, rb.size.z * 0.5)))
    L.log(f'机身盒子（不含旋翼）{boxes["body"]}')
    pieces = L.build_pieces(cfg, boxes)
    sheet = []
    out_dir = os.path.join(L.DIRS['preview'], 'drone')
    legend = []
    if '--no-render' not in flags:
        try:
            groups = {'底子': list(work)}
            for o, _s, _b in pieces:
                groups.setdefault(o.get('easter_piece', o.name), []).append(o)
            legend = L.color_legend(list(groups.items()))
            L.render_views(work + [o for o, _s, _b in pieces], out_dir, 'drone_零件', ['front34', 'top', 'front'], sheet, color_type='OBJECT')
        except Exception as ex:
            L.warn(f'零件图渲染失败：{ex}')
    body = L.join_objects(work + [o for o, _s, _b in pieces], BODY)
    bbox = L.obj_box(body)
    missing_slots = [sname for i, sname in enumerate(L.SLOTS) if not any(p.material_index == i for p in body.data.polygons)]
    if missing_slots:
        L.warn(f'机身材质槽 {missing_slots} 没有面')

    # ---- 枪口
    if cfg.get('muzzle_m'):
        muzzle = Vector(cfg['muzzle_m']) * L.M
        how = '配置 muzzle_m'
    elif cfg.get('muzzle_piece') and cfg['muzzle_piece'] in boxes:
        muzzle = boxes[cfg['muzzle_piece']].at(cfg.get('muzzle_at', (1, 0, 0)))
        how = f'零件 {cfg["muzzle_piece"]}'
    else:
        muzzle = bbox.at((1, 0, -0.5))
        how = '机身前下方（没有武器零件）'
    L.log(f'枪口 Muzzle（{how}）：{L.fmt(muzzle)} 米')

    # ---- 插槽 + 碰撞
    sockets = {f'Rotor_{k}': hubs[k] for k in ('FL', 'FR', 'BL', 'BR')}
    sockets['Muzzle'] = muzzle
    empties = []
    for n, p in sockets.items():
        e = bpy.data.objects.new(f'SOCKET_{n}', None)
        e.empty_display_size = 5.0
        e.empty_display_type = 'ARROWS'
        bpy.context.scene.collection.objects.link(e)
        e.parent = body
        e.matrix_parent_inverse = Matrix.Identity(4)
        e.location = p
        empties.append(e)
    ucx = ucx_box(bbox, cfg.get('collision_inset', 0.92), f'UCX_{BODY}_00')

    tris_b = L.tris_of(body.data)
    tris_r = L.tris_of(rotor.data)
    L.log(f'机身 {tris_b} 三角，旋翼 {tris_r} 三角 × 4；机身盒 {bbox}')

    # ---- 预览：机身 + 四个旋翼挂在插槽上
    if '--no-render' not in flags:
        try:
            inst = []
            for k in ('FL', 'FR', 'BL', 'BR'):
                d = bpy.data.objects.new(f'preview_rotor_{k}', rotor.data)
                bpy.context.scene.collection.objects.link(d)
                d.location = hubs[k]
                inst.append(d)
            L.render_views([body] + inst, out_dir, 'drone', ['front34', 'front', 'side', 'top', 'rear34'], sheet, color_type='MATERIAL',
                           note=f'跨度 {max(bbox.size.x, bbox.size.y) / L.M:.2f} 米 · {tris_b} + 4×{tris_r} 三角')
            L.render_views([rotor], out_dir, 'drone_旋翼', ['front34', 'pz'], sheet, color_type='MATERIAL', note='原点 = 桨毂，绕 Z 转')
            for d in inst:
                bpy.data.objects.remove(d)
        except Exception as ex:
            L.warn(f'预览渲染失败（不影响导出）：{ex}')
        L.write_sheet(out_dir, sheet, f'维克托无人机 SM_VDrone（{os.path.basename(path)}）', legend,
                      [f'枪口 {how}', f'旋翼直径 {rotor_dia / L.M:.2f} 米'])

    # ---- 导出
    exp = L.DIRS['export']
    L.export_fbx(os.path.join(exp, f'{BODY}.fbx'), [body, ucx] + empties, {'MESH', 'EMPTY'})
    L.export_fbx(os.path.join(exp, f'{ROTOR}.fbx'), [rotor], {'MESH'})
    report = {
        'body': '/Game/Easter/Drone/SM_VDrone_Body', 'rotor': '/Game/Easter/Drone/SM_VDrone_Rotor', 'source': L.rel(path),
        'note': '坐标 = UE 网格空间厘米 (x 前, y 右, z 上)；插槽旋转都是 0（Muzzle 朝 +X）', 'scale_from_source': round(info['scale'], 5),
        'forward': info['forward'], 'tris_body': tris_b, 'tris_rotor': tris_r, 'rotor_diameter_cm': round(rotor_dia, 2), 'rotor_master': key,
        'size_m': [round(x / L.M, 3) for x in bbox.size], 'sockets': {n: L.ue_vec(p) for n, p in sockets.items()},
        'rotor_found': sorted(rotors), 'muzzle_from': how,
        'slots': {sname: sum(len(p.vertices) - 2 for p in body.data.polygons if p.material_index == i) for i, sname in enumerate(L.SLOTS)},
        'warnings': list(L.LOG.warnings),
    }
    jp = os.path.join(exp, f'{NAME}_build.json')
    json.dump(report, open(jp, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    L.log(f'报告 {L.rel(jp)}：插槽 {report["sockets"]}')
    L.save_blend(os.path.join(L.DIRS['build'], f'{NAME}_build.blend'))
    L.log('完成；下一步 py -3 tools/easter_sheet.py Inbox/_easter/preview/drone，再跑 UE 导入')


if __name__ == '__main__':
    L.run_main('easter', 'drone', build)
