"""打印机体骨骼网格的包围盒、LOD、材质、骨架、物理资产，排查游戏里看不见的问题。"""
import unreal
m = unreal.load_asset('/Game/Robot/Meshes/SKM_Robot_v3')
b = m.get_bounds()
unreal.log(f'[probe] bounds origin={b.origin} extent={b.box_extent} r={b.sphere_radius}')
unreal.log(f'[probe] imported_bounds={m.get_imported_bounds().box_extent}')
unreal.log(f'[probe] skeleton={m.skeleton.get_path_name()} physics={m.get_editor_property("physics_asset")}')
unreal.log(f'[probe] materials={[(str(s.material_slot_name), s.material_interface.get_path_name() if s.material_interface else None) for s in m.materials]}')
sms = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
try:
    unreal.log(f'[probe] lods={sms.get_lod_count(m)} lod0 verts={sms.get_num_verts(m, 0)}')
except Exception as e:
    unreal.log(f'[probe] lod ? {e}')
for k in ('lod_info',):
    try:
        info = m.get_editor_property(k)
        unreal.log(f'[probe] lod_info n={len(info)} screen={[i.get_editor_property("screen_size") for i in info]}')
    except Exception as e:
        unreal.log(f'[probe] {k} ? {e}')
for p in ('positive_bounds_extension', 'negative_bounds_extension', 'has_vertex_colors', 'enable_per_poly_collision'):
    try:
        unreal.log(f'[probe] {p}={m.get_editor_property(p)}')
    except Exception as e:
        unreal.log(f'[probe] {p} ? {e}')
mat = unreal.load_asset('/Game/Robot/Materials/M_RobotMaster') or unreal.load_asset('/Game/Robot/M_RobotMaster')
if mat:
    unreal.log(f'[probe] master={mat.get_path_name()} skel_usage={mat.get_editor_property("used_with_skeletal_mesh")} blend={mat.get_editor_property("blend_mode")} shading={mat.get_editor_property("shading_model")}')
