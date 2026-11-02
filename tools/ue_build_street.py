"""在 RobotSlice 工程里跑：把 blender_export_scene.py 导出的「赛博朋克街道」重建成 /Game/Maps/L_Street。
- 贴图（pack_textures.py 整理过的）→ /Game/Env/Street/Textures，按 Blender 材质建 M_WorldPBR 的材质实例（世界坐标投影、湿地面积水）
- scene.fbx → /Game/Env/Street/Meshes，每个物体一个网格（Nanite、复杂碰撞）
- 在 L_LightingBase（只有天空灯光）上按 scene.json 摆物体、布灯，加和小巷一样的调色后期，路面挪到高度 0
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="ue_build_street.py" -AllowCommandletRendering -RenderOffscreen
"""
import json
import math
import os
import re

import unreal

SRC = 'D:/RobotSlice/SourceAssets/street'
DEST = '/Game/Env/Street'
SRC_MAP = '/Game/Maps/L_LightingBase'
DST_MAP = '/Game/Maps/L_Street'
TAG = 'Street'
eas = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
scene = json.load(open(f'{SRC}/scene.json', encoding='utf-8'))

# Blender 材质 → UE 材质。world：(贴图名, 铺贴厘米, 湿度, 积水, 色调, 自发光强度)；mi：现成材质实例；
# emis：发光（颜色取 Blender 的，强度倍数）；flat：纯色 (颜色, 粗糙度, 金属度)
MATS = {
    '3x MT Asphalt': ('mi', '/Game/Env/Materials/MI_Asphalt'),
    '3x MT Concrete': ('world', 'Concrete006', 300, 0.35, 0.3, (0.75, 0.77, 0.8), 0),
    '3x MT Concrete 2': ('world', 'Concrete006', 300, 0.35, 0.3, (0.7, 0.72, 0.76), 0),
    '3x MT Tiles': ('world', 'Tiles072', 200, 0.5, 0.35, (0.8, 0.82, 0.86), 0),
    'Black': ('flat', (0.02, 0.022, 0.025), 0.45, 0.6),
    'Cyber': ('world', 'Chip005', 160, 0.2, 0, (1, 1, 1), 0),
    'Cyber 2': ('world', 'Chip001', 160, 0.2, 0, (1, 1, 1), 0),
    'MT_Fasade': ('world', 'Facade011', 6000, 0.1, 0, (1, 1, 1), 4.0),
    'MT_Magazin': ('world', 'Smear002', 200, 0.2, 0, (0.8, 0.8, 0.8), 0),
    'MT_Metall': ('world', 'Metal002', 150, 0.3, 0, (0.8, 0.82, 0.85), 0),
    'MT_Metall 2': ('world', 'MetalPlates001', 200, 0.3, 0, (0.8, 0.82, 0.85), 0),
    'MT_Metall Yellow': ('world', 'PaintedMetal016', 150, 0.3, 0, (1, 1, 1), 0),
    'MT_Plast': ('world', 'Smear002', 200, 0.3, 0, (0.75, 0.77, 0.8), 0),
    'MT_Wall': ('world', 'Tiles002', 200, 0.15, 0, (0.85, 0.85, 0.88), 0),
    'MT_Wall 2': ('world', 'Tiles002', 200, 0.15, 0, (0.7, 0.7, 0.74), 0),
}
EMIS_GAIN = 5.0     # Blender 发光强度 → UE 自发光倍数
LIGHT_GAIN = 2.0    # Blender 瓦数 → UE 亮度；再高在雾里整条街会发白（第一次用 6 倍就是一片白）


def task(filename, dest, name=None, options=None):
    t = unreal.AssetImportTask()
    t.filename = filename
    t.destination_path = dest
    if name:
        t.destination_name = name
    t.automated = True
    t.replace_existing = True
    t.save = True
    if options:
        t.options = options
    return t


# ---------------------------------------------------------------- 贴图
tasks = []
for f in sorted(os.listdir(f'{SRC}/ue_tex')):
    name = os.path.splitext(f)[0]
    if not eas.does_asset_exist(f'{DEST}/Textures/{name}'):
        tasks.append(task(f'{SRC}/ue_tex/{f}', f'{DEST}/Textures', name))
if tasks:
    tools.import_asset_tasks(tasks)
    for t in tasks:
        tex = eas.load_asset(f'{DEST}/Textures/{t.destination_name}')
        if '_nor_gl' in t.destination_name:
            tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
            tex.set_editor_property('srgb', False)
        elif '_arm' in t.destination_name:
            tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_MASKS)
            tex.set_editor_property('srgb', False)
        eas.save_loaded_asset(tex)


# ---------------------------------------------------------------- 材质
def lin(c):
    return unreal.LinearColor(c[0], c[1], c[2], 1)


def make_mi(name, parent):
    path = f'{DEST}/Materials/{name}'
    mi = eas.load_asset(path) if eas.does_asset_exist(path) else tools.create_asset(
        name, f'{DEST}/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', eas.load_asset(parent))
    return mi


def clean(s):
    return re.sub(r'[^A-Za-z0-9]', '_', s)


ue_mat = {}
for bname, info in scene['materials'].items():
    spec = MATS.get(bname)
    mname = 'MI_St_' + clean(bname)
    if spec is None and info['kind'] == 'emissive':
        spec = ('emis',)
    if spec is None:
        spec = ('world', 'Concrete006', 300, 0.3, 0.2, (0.7, 0.72, 0.76), 0)
    if spec[0] == 'mi':
        ue_mat[bname] = eas.load_asset(spec[1])
        continue
    if spec[0] == 'world':
        _, fam, tile, wet, puddle, tint, glow = spec
        mi = make_mi(mname, '/Game/Env/Materials/M_WorldPBR')
        for param, suffix in (('BaseColorTex', 'diff'), ('NormalTex', 'nor_gl'), ('ARMTex', 'arm'), ('EmissiveTex', 'emis')):
            p = f'{DEST}/Textures/T_{fam}_{suffix}'
            if eas.does_asset_exist(p):
                mel.set_material_instance_texture_parameter_value(mi, param, eas.load_asset(p))
        mel.set_material_instance_scalar_parameter_value(mi, 'TileSize', float(tile))
        mel.set_material_instance_scalar_parameter_value(mi, 'Wetness', wet)
        mel.set_material_instance_scalar_parameter_value(mi, 'PuddleAmount', puddle)
        mel.set_material_instance_scalar_parameter_value(mi, 'EmissiveStrength', float(glow))
        mel.set_material_instance_vector_parameter_value(mi, 'Tint', lin(tint))
    else:
        mi = make_mi(mname, '/Game/Robot/Materials/M_RobotMaster')
        if spec[0] == 'emis':
            e = info['emission']
            mel.set_material_instance_vector_parameter_value(mi, 'BaseColor', lin((0, 0, 0)))
            mel.set_material_instance_vector_parameter_value(mi, 'Emissive', lin(e[:3]))
            mel.set_material_instance_scalar_parameter_value(mi, 'EmissiveStrength', e[3] * EMIS_GAIN)
            mel.set_material_instance_scalar_parameter_value(mi, 'Roughness', 0.4)
        else:
            _, col, rough, metal = spec
            mel.set_material_instance_vector_parameter_value(mi, 'BaseColor', lin(col))
            mel.set_material_instance_scalar_parameter_value(mi, 'Roughness', rough)
            mel.set_material_instance_scalar_parameter_value(mi, 'Metallic', metal)
    eas.save_loaded_asset(mi)
    ue_mat[bname] = mi
default_mat = ue_mat.get('3x MT Concrete 2')

# ---------------------------------------------------------------- 网格
if not eas.does_directory_exist(f'{DEST}/Meshes') or not eas.list_assets(f'{DEST}/Meshes', recursive=False):
    ui = unreal.FbxImportUI()
    ui.set_editor_property('import_mesh', True)
    ui.set_editor_property('import_as_skeletal', False)
    ui.set_editor_property('import_materials', False)
    ui.set_editor_property('import_textures', False)
    ui.set_editor_property('import_animations', False)
    ui.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_STATIC_MESH)
    smd = ui.static_mesh_import_data
    smd.set_editor_property('combine_meshes', False)
    smd.set_editor_property('auto_generate_collision', False)
    smd.set_editor_property('generate_lightmap_u_vs', False)
    tools.import_asset_tasks([task(f'{SRC}/scene.fbx', f'{DEST}/Meshes', options=ui)])
meshes = {}
for path in eas.list_assets(f'{DEST}/Meshes', recursive=False, include_folder=False):
    a = eas.load_asset(path)
    if isinstance(a, unreal.StaticMesh):
        meshes[a.get_name()] = a
unreal.log(f'[street] 网格资源 {len(meshes)} 个，样例 {sorted(meshes)[:3]}')
for a in meshes.values():
    nan = a.get_editor_property('nanite_settings')
    bs = a.get_editor_property('body_setup')
    if not nan.enabled or (bs and bs.get_editor_property('collision_trace_flag') != unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE):
        nan.enabled = True
        a.set_editor_property('nanite_settings', nan)
        if bs:
            bs.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        eas.save_loaded_asset(a)


def find_mesh(name):
    if name in meshes:
        return meshes[name]
    hits = [v for k, v in meshes.items() if k.endswith(name)]
    return hits[0] if hits else None


# ---------------------------------------------------------------- 地图
unreal.EditorLoadingAndSavingUtils.load_map(SRC_MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
for a in sub.get_all_level_actors():
    cls = a.get_class().get_name()
    if not any(k in cls for k in ('LightingDirector', 'LightingEnvironmentRig', 'PlayerStart', 'WorldSettings')) and 'Brush' not in cls:
        sub.destroy_actor(a)
world.get_world_settings().set_editor_property('default_game_mode', unreal.load_class(None, '/Script/RobotSlice.MechGameMode'))

road = [o for o in scene['objects'] if '3x MT Asphalt' in o['materials']]
GROUND = road[0]['max'][2] if road else 0.0


def V(x, y, z):
    """Blender 坐标（米）→ UE（厘米）：Y 轴反向，路面挪到 0"""
    return unreal.Vector(x * 100.0, -y * 100.0, (z - GROUND) * 100.0)


placed = missing = 0
for o in scene['objects']:
    mesh = find_mesh(o['name'])
    if not mesh:
        missing += 1
        continue
    a = sub.spawn_actor_from_class(unreal.StaticMeshActor, V(*o['loc']), unreal.Rotator(pitch=0, yaw=0, roll=0))
    a.tags = [TAG]
    a.set_actor_label(o['name'])
    a.set_folder_path(f'Street/{o["collection"] or "Misc"}')
    c = a.static_mesh_component
    c.set_static_mesh(mesh)
    c.set_editor_property('mobility', unreal.ComponentMobility.STATIC)
    slots = [str(s) for s in mesh.get_editor_property('static_materials')]
    names = [clean(str(s.get_editor_property('material_slot_name'))) for s in mesh.get_editor_property('static_materials')]
    for i, sname in enumerate(names):
        hit = next((b for b in o['materials'] if b and clean(b) == sname), None)
        if hit is None and i < len(o['materials']):
            hit = o['materials'][i]
        c.set_material(i, ue_mat.get(hit, default_mat) if hit else default_mat)
    if o['collection'] == 'Neon Text':
        c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        c.set_editor_property('cast_shadow', False)
    placed += 1
    if placed <= 3:
        b = a.get_actor_bounds(False)
        unreal.log(f'[street] 核对 {o["name"]}: Blender 包围 {[round(v, 1) for v in o["min"]]}~{[round(v, 1) for v in o["max"]]} '
                   f'UE 中心 {b[0]} 半径 {b[1]}')

lights = 0
for L in scene['lights']:
    d = unreal.Vector(L['dir'][0], -L['dir'][1], L['dir'][2])
    rot = unreal.MathLibrary.make_rot_from_x(d)
    kind = {'POINT': unreal.PointLight, 'SPOT': unreal.SpotLight, 'AREA': unreal.RectLight}.get(L['type'])
    if not kind:
        continue
    a = sub.spawn_actor_from_class(kind, V(*L['loc']), rot)
    a.tags = [TAG]
    a.set_actor_label('L_' + clean(L['name']))
    a.set_folder_path('Street/Lights')
    lc = a.get_component_by_class(unreal.LocalLightComponent)
    lc.set_light_color(lin(L['color']))
    lc.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
    power = L['energy'] * LIGHT_GAIN
    if L['type'] == 'AREA':
        lc.set_intensity(min(L['energy'] * 0.3, 400.0))
        lc.set_editor_property('source_width', max(L['size'][0] * 100, 20.0))
        lc.set_editor_property('source_height', max(L['size'][1] * 100, 20.0))
        lc.set_attenuation_radius(3000.0)
    elif L['type'] == 'SPOT':
        lc.set_intensity(min(power, 12000.0))
        outer = math.degrees(L['spot_size']) / 2
        lc.set_editor_property('outer_cone_angle', min(outer, 80.0))
        lc.set_editor_property('inner_cone_angle', min(outer, 80.0) * (1 - L['spot_blend']))
        lc.set_attenuation_radius(8000.0 if L['loc'][2] - GROUND > 20 else 2500.0)
    else:
        lc.set_intensity(min(power, 5000.0))
        lc.set_attenuation_radius(3000.0)
        lc.set_editor_property('source_radius', L.get('radius', 0.1) * 100)
    lights += 1

# 出生点：路的一头，朝街道深处（Blender +Y → UE -Y）
if road:
    r = road[0]
    cx = (r['min'][0] + r['max'][0]) / 2
    for a in sub.get_all_level_actors():
        if isinstance(a, unreal.PlayerStart):
            a.set_actor_location(V(cx, r['min'][1] + 12, GROUND + 1.6), False, False)
            a.set_actor_rotation(unreal.Rotator(pitch=0, yaw=-90, roll=0), False)

# ---------------------------------------------------------------- 调色后期（同小巷）
ppv = sub.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0), unreal.Rotator(pitch=0, yaw=0, roll=0))
ppv.tags = [TAG]
ppv.set_actor_label('PP_CinematicGrade')
ppv.set_folder_path('Street/Lights')
ppv.set_editor_property('unbound', True)
ppv.set_editor_property('priority', 10.0)
pp = ppv.get_editor_property('settings')
for key, value in (('bloom_intensity', 0.85), ('scene_fringe_intensity', 0.35), ('vignette_intensity', 0.45),
                   ('film_grain_intensity', 0.22), ('lens_flare_intensity', 0.25),
                   ('color_saturation', unreal.Vector4(1.0, 1.0, 1.0, 1.08)),
                   ('color_contrast', unreal.Vector4(1.0, 1.0, 1.0, 1.12)),
                   ('color_gain_shadows', unreal.Vector4(0.86, 0.97, 1.1, 1.0)),
                   ('color_gain_highlights', unreal.Vector4(1.08, 0.98, 1.0, 1.0)),
                   ('color_offset_shadows', unreal.Vector4(0.0, 0.004, 0.012, 0.0))):
    pp.set_editor_property('override_' + key, True)
    pp.set_editor_property(key, value)
ppv.set_editor_property('settings', pp)

unreal.EditorLoadingAndSavingUtils.save_map(world, DST_MAP)
unreal.log(f'[street] 已另存 {DST_MAP}：摆放 {placed} 个物体（缺网格 {missing}），灯 {lights} 盏，路面高 {GROUND:.2f} 米已挪到 0；'
           f'路范围 Blender {[round(v, 1) for v in road[0]["min"]]}~{[round(v, 1) for v in road[0]["max"]]}' if road else '')
