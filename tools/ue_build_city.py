"""按 Inbox/_k059/plan.json 搭 /Game/Maps/L_City（城区死斗图）：在 L_LightingBase（只有天空灯光）上摆路面方块、K005 楼、
K059 小品 / 道具、广告牌、出生点、隐形边界墙和天花板、场地范围标记、调色后期。重跑会整张重建。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_build_city.py" -AllowCommandletRendering -RenderOffscreen
坐标：plan 是 Blender 的（米，Y 向北）→ UE (x, -y, z) × 100，yaw 取反。
"""
import json
import math

import unreal

PLAN = 'D:/RobotSlice/Inbox/_k059/plan.json'
META = ['D:/RobotSlice/Inbox/_k059/export_k059/meshes.json', 'D:/RobotSlice/Inbox/_k059/export_k005/meshes.json']
SRC_MAP = '/Game/Maps/L_LightingBase'
DST_MAP = '/Game/Maps/L_City'
DEST = '/Game/Env/City'
TAG = 'City'
eas = unreal.EditorAssetLibrary
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
plan = json.load(open(PLAN, encoding='utf-8'))
meta = {}
for p in META:
    for m in json.load(open(p, encoding='utf-8'))['meshes']:
        meta[m['name']] = m


def log(msg):
    unreal.log(f'[city] {msg}')


def V(x, y, z):
    return unreal.Vector(x * 100.0, -y * 100.0, z * 100.0)


def R(yaw):
    return unreal.Rotator(roll=0.0, pitch=0.0, yaw=-yaw)


_cache = {}


def asset(path):
    if path not in _cache:
        _cache[path] = eas.load_asset(path) if eas.does_asset_exist(path) else None
    return _cache[path]


import re as _re
_slot_cache = {}


def slot_materials(mesh):
    """网格材质槽名 → MI_City_<槽名>（ue_import_city.py 建的）；资源上的槽没挂上材质时，摆放时按槽覆盖"""
    key = mesh.get_path_name()
    if key not in _slot_cache:
        out = []
        for s in mesh.get_editor_property('static_materials'):
            slot = str(s.get_editor_property('material_slot_name'))
            mi = None
            for cand in (slot, slot.split('.')[0]):
                mi = asset(f'{DEST}/Materials/MI_City_' + _re.sub(r'[^A-Za-z0-9]', '_', cand).strip('_'))
                if mi:
                    break
            out.append(mi)
        _slot_cache[key] = out
    return _slot_cache[key]


def spawn_mesh(mesh, loc, yaw, scale=(1, 1, 1), folder='City', label=None, collide=True, mat=None, shadow=True):
    a = sub.spawn_actor_from_class(unreal.StaticMeshActor, loc, R(yaw))
    a.tags = [TAG]
    if label:
        a.set_actor_label(label)
    a.set_folder_path(folder)
    a.set_actor_scale3d(unreal.Vector(*scale))
    c = a.static_mesh_component
    c.set_static_mesh(mesh)
    c.set_editor_property('mobility', unreal.ComponentMobility.STATIC)
    if mat:
        for i in range(max(1, len(mesh.get_editor_property('static_materials')))):
            c.set_material(i, mat)
    elif mesh.get_path_name().startswith(DEST):
        for i, mi in enumerate(slot_materials(mesh)):
            if mi:
                c.set_material(i, mi)
    if not collide:
        c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        c.set_editor_property('can_character_step_up_on', unreal.CanBeCharacterBase.ECB_NO)
    if not shadow:
        c.set_editor_property('cast_shadow', False)
    return a


# ---------------------------------------------------------------- 底图：只留天空灯光
unreal.EditorLoadingAndSavingUtils.load_map(SRC_MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
starts = []
for a in sub.get_all_level_actors():
    cls = a.get_class().get_name()
    if isinstance(a, unreal.PlayerStart):
        starts.append(a)
        continue
    if not any(k in cls for k in ('LightingDirector', 'LightingEnvironmentRig', 'WorldSettings')) and 'Brush' not in cls:
        sub.destroy_actor(a)
world.get_world_settings().set_editor_property('default_game_mode', unreal.load_class(None, '/Script/RobotSlice.MechGameMode'))

BOX = asset(f'{DEST}/Meshes/SM_City_Box')
QUAD = asset(f'{DEST}/Meshes/SM_City_SignQuad')
assert BOX and QUAD, '先跑 ue_import_city.py'


def wmat(key):
    for p in (f'{DEST}/Materials/MI_CityW_{key}', f'{DEST}/Materials/MI_CityF_{key}'):
        m = asset(p)
        if m:
            return m
    unreal.log_warning(f'[city] 没有材质 {key}')
    return asset(f'{DEST}/Materials/MI_CityW_concrete')


counts = {}
team_pts = {}
for it in plan['items']:
    k = it['kind']
    counts[k] = counts.get(k, 0) + 1
    if k == 'box':
        x, y, z = it['c']
        key = it['mat']
        if it['name'].startswith('Lot_') and key == 'concrete':
            key = 'lot'      # 街区内地面：同一种混凝土，铺得大一点、暗一点
        folder = 'City/Ground' if it['name'].split('_')[0] in ('Ground', 'Sidewalk', 'Lot', 'Alley') else \
            'City/Marking' if not it['col'] else 'City/Structures'
        spawn_mesh(BOX, V(x, y, z), it['yaw'], it['s'], folder, it['name'], it['col'], wmat(key),
                   shadow=it['col'] and it['s'][2] > 0.05)
    elif k == 'bldg':
        mesh = asset(f'{DEST}/Buildings/SM_CB_C{it["id"]:02d}')
        if not mesh:
            unreal.log_warning(f'[city] 缺楼 C{it["id"]}')
            continue
        far = it.get('tag') == 'far'
        spawn_mesh(mesh, V(it['x'], it['y'], it['z']), it['yaw'], folder='City/Far' if far else 'City/Buildings',
                   label=f'Bldg_C{it["id"]:02d}', collide=not far, shadow=not far)
    elif k == 'vig':
        mesh = asset(f'{DEST}/Meshes/SM_CS_Vig{it["id"]:02d}')
        if mesh:
            spawn_mesh(mesh, V(it['x'], it['y'], it['z']), it['yaw'], folder='City/Street', label=f'Vig{it["id"]:02d}')
    elif k == 'prop':
        pid = it['id']
        sc = (it.get('scale', 1.0),) * 3
        if pid.startswith('/Game/'):
            kit = pid.rsplit('/', 1)[0] + '/StaticMeshes'
            for p in eas.list_assets(kit, recursive=False, include_folder=False):
                m = asset(p.split('.')[0])
                if isinstance(m, unreal.StaticMesh):
                    spawn_mesh(m, V(it['x'], it['y'], it['z']), it['yaw'], sc, 'City/Props', m.get_name())
            continue
        name = next((n for n in sorted(meta) if n.startswith('SM_CS_' + pid)), None)
        mesh = asset(f'{DEST}/Meshes/{name}') if name else None
        if not mesh:
            unreal.log_warning(f'[city] 缺道具 {pid}')
            continue
        m = meta[name]
        size = [m['max'][i] - m['min'][i] for i in range(3)]
        collide = max(size[0], size[1]) >= 1.0 and size[2] >= 0.9 and 'Tree' not in name and 'Lamp' not in name
        spawn_mesh(mesh, V(it['x'], it['y'], it['z']), it['yaw'], sc, 'City/Props', name[6:], collide)
    elif k == 'sign':
        yaw = math.radians(it['yaw'])
        back = (-math.sin(yaw), math.cos(yaw))       # 本地 +Y（牌子背面方向），Blender 坐标
        x, y = it['x'], it['y']
        mat = asset(f'{DEST}/Materials/MI_City_Sign_{it["img"]}')
        spawn_mesh(QUAD, V(x - back[0] * 0.03, y - back[1] * 0.03, it['z']), it['yaw'], (it['w'], 1, it['h']),
                   'City/Signs', 'Sign_' + it['img'], False, mat, shadow=False)
        if it.get('frame'):
            spawn_mesh(BOX, V(x + back[0] * 0.2, y + back[1] * 0.2, it['z'] + it['h'] / 2), it['yaw'],
                       (it['w'] + 0.4, 0.3, it['h'] + 0.4), 'City/Signs', 'SignFrame_' + it['img'], True, wmat('steel_dark'))
    elif k == 'spawn':
        if starts:
            a = starts.pop()
            a.set_actor_location(V(it['x'], it['y'], it['z'] + 1.5), False, False)
            a.set_actor_rotation(R(it['yaw']), False)
        else:
            a = sub.spawn_actor_from_class(unreal.PlayerStart, V(it['x'], it['y'], it['z'] + 1.5), R(it['yaw']))
        # 和 GameMode 的约定（模式代理 / K051 代理定的）：死斗点带 DMSpawn；两队出生中心是带 TeamSpawn0 / TeamSpawn1 的 actor
        a.tags = ['DMSpawn' if it['tag'] == 'DM' else it['tag'], TAG]
        a.set_folder_path('City/Spawns')
        a.set_actor_label(f'Spawn_{it["tag"]}_{counts[k]:02d}')
        if it['tag'] in ('Team0', 'Team1'):
            team_pts.setdefault(it['tag'], []).append(it)

for tag, pts in team_pts.items():
    cx = sum(p['x'] for p in pts) / len(pts)
    cy = sum(p['y'] for p in pts) / len(pts)
    t = sub.spawn_actor_from_class(unreal.TargetPoint, V(cx, cy, pts[0]['z'] + 0.2), R(pts[0]['yaw']))
    t.tags = ['TeamSpawn' + tag[-1], TAG]
    t.set_folder_path('City/Spawns')
    t.set_actor_label('TeamSpawn' + tag[-1])

# ---------------------------------------------------------------- 边界：四面隐形墙 + 天花板（只挡机体，不挡子弹 / 镜头 / AI 视线）
A = plan['arena']
hx, hy, ceil = A['half'][0], A['half'][1], A['ceiling']
walls = [('Wall_N', (0, hy + 1, ceil / 2), (2 * hx + 4, 2, ceil + 10)), ('Wall_S', (0, -hy - 1, ceil / 2), (2 * hx + 4, 2, ceil + 10)),
         ('Wall_E', (hx + 1, 0, ceil / 2), (2, 2 * hy + 4, ceil + 10)), ('Wall_W', (-hx - 1, 0, ceil / 2), (2, 2 * hy + 4, ceil + 10)),
         ('Ceiling', (0, 0, ceil + 1), (2 * hx + 4, 2 * hy + 4, 2))]
for name, c, s in walls:
    a = spawn_mesh(BOX, V(*c), 0, s, 'City/Bounds', name, True, wmat('steel'), shadow=False)
    a.set_actor_hidden_in_game(True)
    comp = a.static_mesh_component
    comp.set_editor_property('visible', False)
    comp.set_collision_profile_name('Custom')
    comp.set_collision_object_type(unreal.CollisionChannel.ECC_VEHICLE)
    comp.set_collision_response_to_all_channels(unreal.CollisionResponseType.ECR_IGNORE)
    comp.set_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN, unreal.CollisionResponseType.ECR_BLOCK)
    comp.set_editor_property('affect_distance_field_lighting', False)
    comp.set_editor_property('affect_dynamic_indirect_lighting', False)
    a.tags = [TAG, 'ArenaWall']
# 场地范围标记：GameMode::ComputeArena 找带 MechArena 标签的点，中心 = 它的位置，半宽读 HalfX= / HalfY= 标签（厘米）
# 小地图拍摄框、AI 回场范围、掉出地图判定（中心以下 30 米）都按它
t = sub.spawn_actor_from_class(unreal.TargetPoint, V(*A['center']), R(0))
t.tags = ['MechArena', f'HalfX={hx * 100:.0f}', f'HalfY={hy * 100:.0f}', TAG]
t.set_folder_path('City/Bounds')
t.set_actor_label('MechArena')

# ---------------------------------------------------------------- 调色后期：阴天、冷灰、低饱和，不发白
ppv = sub.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0), R(0))
ppv.tags = [TAG]
ppv.set_actor_label('PP_CityOvercast')
ppv.set_folder_path('City')
ppv.set_editor_property('unbound', True)
ppv.set_editor_property('priority', 10.0)
pp = ppv.get_editor_property('settings')
for key, value in (('bloom_intensity', 0.35), ('vignette_intensity', 0.35), ('film_grain_intensity', 0.08),
                   ('scene_fringe_intensity', 0.15), ('auto_exposure_bias', 0.3),
                   ('color_saturation', unreal.Vector4(0.92, 0.95, 1.0, 0.84)),
                   ('color_contrast', unreal.Vector4(1.0, 1.0, 1.0, 1.1)),
                   ('color_gamma', unreal.Vector4(1.0, 1.0, 1.0, 0.97)),
                   ('color_gain_shadows', unreal.Vector4(0.9, 0.97, 1.08, 1.0)),
                   ('color_gain_highlights', unreal.Vector4(0.98, 1.0, 1.03, 1.0)),
                   ('color_offset_shadows', unreal.Vector4(0.0, 0.002, 0.008, 0.0))):
    pp.set_editor_property('override_' + key, True)
    pp.set_editor_property(key, value)
ppv.set_editor_property('settings', pp)

# 截图机位（ue_city_shots.py 运行时挪它）
cam = sub.spawn_actor_from_class(unreal.CameraActor, V(0, 0, 20), R(0))
cam.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
cam.tags = [TAG, 'ShotCam']
cam.set_folder_path('City')
cam.set_actor_label('ShotCam')

unreal.EditorLoadingAndSavingUtils.save_map(world, DST_MAP)
log(f'已存 {DST_MAP}：{counts}')
