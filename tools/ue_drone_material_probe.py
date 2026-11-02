# 查肩部无人机模型现在的材质、槽位、UV（只读，不保存）。结果写 Saved/drone_probe.txt
# UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script=D:/RobotSlice/tools/ue_drone_material_probe.py -unattended -nosplash
import unreal

eas = unreal.EditorAssetLibrary
MESHES = ['SM_MissilePod', 'SM_Artillery', 'SM_ShieldEmitter', 'SM_GrappleLauncher', 'SM_NetLauncher', 'SM_SmokeLauncher', 'SM_EmpLauncher', 'SM_GrappleClaw', 'SM_Grenade', 'SM_Shell', 'SM_Missile']
out = []
sub = unreal.EditorStaticMeshLibrary


def base(m):
    while isinstance(m, unreal.MaterialInstance):
        m = m.get_editor_property('parent')
    return m


for n in MESHES:
    p = f'/Game/Weapons/Meshes/{n}'
    mesh = eas.load_asset(p)
    if not mesh:
        out.append(f'{n}: 不存在')
        continue
    b = mesh.get_bounding_box()
    ns = mesh.get_editor_property('nanite_settings')
    out.append(f'== {n} 尺寸 {b.max - b.min} {mesh.get_num_vertices(0)} 顶点 UV通道 {sub.get_num_uv_channels(mesh, 0)} Nanite {ns.get_editor_property("enabled")}')
    for i, s in enumerate(mesh.get_editor_property('static_materials')):
        mi = s.get_editor_property('material_interface')
        bm = base(mi) if mi else None
        out.append(f'  槽{i} {s.get_editor_property("material_slot_name")} → {mi.get_path_name() if mi else None} 母材质 {bm.get_path_name() if bm else None} ')
    desc = mesh.get_static_mesh_description(0)
    if desc:
        cnt = desc.get_vertex_instance_count()
        import random
        ids = list(range(cnt))
        lo = [1e9, 1e9]
        hi = [-1e9, -1e9]
        zero = 0
        for j in ids[:: max(1, cnt // 4000)]:
            vi = unreal.VertexInstanceID(j) if hasattr(unreal, 'VertexInstanceID') else j
            try:
                uv = desc.get_vertex_instance_uv(vi, 0)
            except Exception as e:
                out.append(f'  UV 读取失败 {e}')
                break
            lo = [min(lo[0], uv.x), min(lo[1], uv.y)]
            hi = [max(hi[0], uv.x), max(hi[1], uv.y)]
            if abs(uv.x) < 1e-6 and abs(uv.y) < 1e-6:
                zero += 1
        out.append(f'  顶点实例 {cnt}，UV0 范围 {lo}..{hi}，(0,0) 的抽样点 {zero}')
        # 每个多边形组的三角形数
        try:
            out.append(f'  多边形组 {desc.get_polygon_group_count()} 三角形 {desc.get_triangle_count()}')
        except Exception as e:
            out.append(f'  {e}')

# 机体 / 武器材质的情况
for mp in ['/Game/Robot/Materials/MI_Robot_PaintDark', '/Game/Robot/Materials/MI_Robot_Paint', '/Game/Robot/Materials/MI_Robot_Frame', '/Game/Robot/Materials/MI_Robot_Accent', '/Game/Robot/Materials/MI_Robot_Glow', '/Game/Robot/Materials/MI_Robot_Chrome', '/Game/Robot/Materials/M_RobotMaster', '/Game/Robot/Materials/M_RobotCamo', '/Game/Weapons/Materials/M_WeaponPBR', '/Game/Weapons/Materials/MI_Gun2']:
    a = eas.load_asset(mp)
    if not a:
        out.append(f'{mp}: 不存在')
        continue
    b = base(a)
    out.append(f'-- {mp} 母材质 {b.get_path_name() if b else None}')
    mel = unreal.MaterialEditingLibrary
    if isinstance(a, unreal.MaterialInstance):
        for pn in mel.get_scalar_parameter_names(a):
            out.append(f'    标量 {pn} = {mel.get_material_instance_scalar_parameter_value(a, pn)}')
        for pn in mel.get_vector_parameter_names(a):
            out.append(f'    颜色 {pn} = {mel.get_material_instance_vector_parameter_value(a, pn)}')
        for pn in mel.get_texture_parameter_names(a):
            t = mel.get_material_instance_texture_parameter_value(a, pn)
            out.append(f'    贴图 {pn} = {t.get_path_name() if t else None}')
    else:
        for u in ['used_with_nanite', 'used_with_static_lighting', 'used_with_instanced_static_meshes', 'used_with_skeletal_mesh']:
            try:
                out.append(f'    {u} = {a.get_editor_property(u)}')
            except Exception as e:
                out.append(f'    {u} ? {e}')
        for pn in mel.get_scalar_parameter_names(a):
            out.append(f'    标量 {pn}')
        for pn in mel.get_vector_parameter_names(a):
            out.append(f'    颜色 {pn}')
        for pn in mel.get_texture_parameter_names(a):
            out.append(f'    贴图 {pn}')
with open('D:/RobotSlice/Saved/drone_probe.txt', 'w', encoding='utf-8') as f:
    f.write('\n'.join(out))
unreal.log('[drone] 查完')
