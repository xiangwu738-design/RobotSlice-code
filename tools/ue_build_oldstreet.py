"""搭 /Game/Maps/L_OldStreet（老旧街道，无限循环的娱乐图）：在 L_LightingBase（只有天空灯光）上按 Inbox/_oldstreet/layout.json 摆，整张重建。第一阶段只有场景和画面，循环代码下一阶段做。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_build_oldstreet.py" -AllowCommandletRendering -RenderOffscreen -unattended -nosplash

三层（都是实例化，资源只有一份）：
- 全精度 k = -1, 0, +1：地面、路缘、护桩、道具、树、街面楼。地面和楼带碰撞，机体和镜头在接缝两边都有东西挡。
- 低模 k = ±2 .. ±(1+LOW_COPIES)：只有地面和楼，没碰撞。
- 远景楼（每个周期一组，隐形墙外面的街区，横路两头一直有楼），没碰撞。
最远（±(1.5+LOW_COPIES) 个周期）贴街景图，靠雾盖住。
边界：Y = ±40 米、头顶 40 米的隐形墙（只挡 Pawn），标签 OldStreetWall / OldStreetCeiling；OldStreetBounds 点存周期和边界数字（PeriodX= SeamX= HalfY= CeilZ=，厘米）。
这一阶段没有循环代码，全精度层两头放 OldStreetEndWall（X = ±149 米）防止飞出去，下一阶段穿接缝之后碰不到。
"""
import json
import os

import unreal

L = json.load(open('D:/RobotSlice/Inbox/_oldstreet/layout.json', encoding='utf-8'))
SRC_MAP = '/Game/Maps/L_LightingBase'
DST_MAP = '/Game/Maps/L_OldStreet'
MD = '/Game/Env/OldStreet/Meshes'
TAG = 'OldStreet'
P = L['PERIOD']
TREES_LOW = int(os.environ.get('OS_TREES_LOW', '2'))     # 低模层前几份也摆树（见报告：穿接缝时树不会在 100–200 米外突然冒出来）
HOUR_NOTE = '时间天气在 GameMode 里按地图名设，下一阶段加；测试用 -MechHour=16.3 -MechWeather=Haze'
eas = unreal.EditorAssetLibrary
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
log = lambda *a: unreal.log('[oldstreet] ' + ' '.join(str(x) for x in a))
counts = {}


def mesh(name):
    a = eas.load_asset(f'{MD}/{name}')
    assert a, f'缺网格 {name}'
    return a


def xf(x, y, z=0.0, yaw=0.0, scale=(1, 1, 1)):
    return unreal.Transform(unreal.Vector(x * 100, y * 100, z * 100), unreal.Rotator(0, 0, yaw), unreal.Vector(*scale))


def tagged(a, folder, label, extra=()):
    a.tags = [TAG] + list(extra)
    a.set_folder_path(f'OldStreet/{folder}')
    a.set_actor_label(label)
    return a


def ism(label, msh, transforms, folder, collide=False, shadow=True, extra=()):
    a = sub.spawn_actor_from_class(unreal.Actor, unreal.Vector(0, 0, 0))
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    root = sds.k2_gather_subobject_data_for_instance(a)[0]
    params = unreal.AddNewSubobjectParams(parent_handle=root, new_class=unreal.InstancedStaticMeshComponent, blueprint_context=None)
    h, fail = sds.add_new_subobject(params)
    if fail and str(fail):
        raise RuntimeError(str(fail))
    comp = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h))
    comp.set_static_mesh(msh)
    comp.set_editor_property('mobility', unreal.ComponentMobility.STATIC)
    if collide:
        comp.set_collision_profile_name('BlockAll')
    else:
        comp.set_collision_profile_name('NoCollision')
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        comp.set_editor_property('can_character_step_up_on', unreal.CanBeCharacterBase.ECB_NO)
    if not shadow:
        comp.set_editor_property('cast_shadow', False)
    comp.add_instances(transforms, False, True)
    tagged(a, folder, label, extra)
    counts[label] = comp.get_instance_count()
    return a, comp


unreal.EditorLoadingAndSavingUtils.load_map(SRC_MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
FULL = list(range(-L['FULL_COPIES'], L['FULL_COPIES'] + 1))
LOW = [k for k in range(-L['FULL_COPIES'] - L['LOW_COPIES'], L['FULL_COPIES'] + L['LOW_COPIES'] + 1) if k not in FULL]
ALL = FULL + LOW

# ---------------------------------------------------------------- 全精度层
ism('Full_Ground', mesh('SM_OS_Ground'), [xf(k * P, 0) for k in FULL], 'Full', collide=True, extra=['OldStreetGround'])
for name in ('SM_OS_Curb', 'SM_OS_Bollards', 'SM_OS_Props'):
    ism('Full_' + name[6:], mesh(name), [xf(k * P, 0) for k in FULL], 'Full')
TREE = mesh('SM_OS_Tree')
def tree_flags(comp):
    # 树不进 Lumen（树冠是几万片遮罩叶子，距离场和表面缓存都没意义，还会把天光全挡掉，街上更黑）；直接光的影子照常（虚拟阴影）
    on = os.environ.get('OS_TREE_NOLUMEN', '1') != '1'
    comp.set_editor_property('affect_distance_field_lighting', on)
    comp.set_editor_property('affect_dynamic_indirect_lighting', on)


tree_flags(ism('Full_Tree', TREE, [xf(t['X'] + k * P, t['Y'], t['Z']) for k in FULL for t in L['trees']], 'Full')[1])
for t_, info in L['types'].items():
    rows = [b for b in L['street'] if b['type'] == t_]
    # B 是一圈立面 + 盖上去的屋顶，四角有缝：不用网格碰撞，下面单独摆隐藏方块
    ism(f'Full_Bld_{t_}', mesh(f'SM_OS_Bld_{t_}'), [xf(b['X'] + k * P, b['Y'], b['Z'], b['yaw']) for k in FULL for b in rows], 'Full', collide=t_ != 'B', extra=['OldStreetBuilding'])

# 树干碰撞：隐藏圆柱（只到树冠下面）。树本身不碰撞，叶子和枝不挡机体和子弹
CYL = eas.load_asset('/Engine/BasicShapes/Cylinder')
tr = L['trunk_r'] * L['S']
a, comp = ism('Full_TrunkCollide', CYL, [xf(t['X'] + k * P, t['Y'], 4.0, 0, (2 * tr, 2 * tr, 8.0)) for k in FULL for t in L['trees']], 'Full', collide=True, shadow=False, extra=['OldStreetTrunk'])
comp.set_editor_property('hidden_in_game', True)
comp.set_editor_property('visible', False)
comp.set_editor_property('affect_distance_field_lighting', False)
comp.set_editor_property('affect_dynamic_indirect_lighting', False)

_S = L['S']
_b = L['types']['B']
a, comp = ism('Full_BldB_Collide', eas.load_asset('/Engine/BasicShapes/Cube'),
              [xf(b['X'] + k * P, b['Y'], b['Z'] + _S * _b['h'] / 2, b['yaw'], (_S * _b['size'][1], _S * _b['size'][0], _S * _b['h'])) for k in FULL for b in L['street'] if b['type'] == 'B'],
              'Full', collide=True, shadow=False, extra=['OldStreetBuilding'])
comp.set_editor_property('hidden_in_game', True)
comp.set_editor_property('visible', False)
comp.set_editor_property('affect_distance_field_lighting', False)
comp.set_editor_property('affect_dynamic_indirect_lighting', False)

# ---------------------------------------------------------------- 低模层
ism('Low_Ground', mesh('SM_OS_Ground'), [xf(k * P, 0) for k in LOW], 'Low')
ism('Low_Curb', mesh('SM_OS_Curb'), [xf(k * P, 0) for k in LOW], 'Low')    # 路缘要有：人行道比路面高几厘米，没路缘石边上一条黑缝，远处能看见
for t_, info in L['types'].items():
    rows = [b for b in L['street'] if b['type'] == t_]
    ism(f'Low_Bld_{t_}', mesh(f'SM_OS_Bld_{t_}'), [xf(b['X'] + k * P, b['Y'], b['Z'], b['yaw']) for k in LOW for b in rows], 'Low')
if TREES_LOW > 0:
    near_low = [k for k in LOW if abs(k) <= L['FULL_COPIES'] + TREES_LOW]
    tree_flags(ism('Low_Tree', TREE, [xf(t['X'] + k * P, t['Y'], t['Z']) for k in near_low for t in L['trees']], 'Low')[1])

# ---------------------------------------------------------------- 远景楼（隐形墙外面）
for t_, info in L['types'].items():
    rows = [b for b in L['town'] if b['type'] == t_]
    ism(f'Town_Bld_{t_}', mesh(f'SM_OS_Bld_{t_}'), [xf(b['X'] + k * P, b['Y'], b['Z'], b['yaw']) for k in ALL for b in rows], 'Town')

# 最外圈的大地面（远景楼外面露出来的地方）：大方块压扁，只有底色，靠雾盖
PLANE = eas.load_asset('/Engine/BasicShapes/Plane')
far = sub.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -30), unreal.Rotator())
far.set_actor_scale3d(unreal.Vector(400, 400, 1))     # 底面 100 米 × 400 = 40 公里
c = far.static_mesh_component
c.set_static_mesh(PLANE)
c.set_editor_property('mobility', unreal.ComponentMobility.STATIC)
c.set_collision_profile_name('NoCollision')
c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
c.set_editor_property('cast_shadow', False)
c.set_material(0, eas.load_asset('/Game/Env/OldStreet/Materials/MI_OS_FarGround'))
tagged(far, 'Town', 'FarGround')

# ---------------------------------------------------------------- 边界
CUBE = eas.load_asset('/Engine/BasicShapes/Cube')
HY = L['ARENA_HALF_Y']
CEIL = L['CEILING']
SPAN = L['END_WALL_X'] + 6.0
WALLS = [('Wall_Right', (0, HY + 1, CEIL / 2), (2 * SPAN, 2, CEIL + 10), 'OldStreetWall'), ('Wall_Left', (0, -HY - 1, CEIL / 2), (2 * SPAN, 2, CEIL + 10), 'OldStreetWall'),
         ('Ceiling', (0, 0, CEIL + 1), (2 * SPAN, 2 * HY + 4, 2), 'OldStreetCeiling'),
         ('EndWall_Fwd', (L['END_WALL_X'] + 1, 0, CEIL / 2), (2, 2 * HY + 4, CEIL + 10), 'OldStreetEndWall'), ('EndWall_Back', (-L['END_WALL_X'] - 1, 0, CEIL / 2), (2, 2 * HY + 4, CEIL + 10), 'OldStreetEndWall')]
for name, cc, s, tag in WALLS:
    a = sub.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(cc[0] * 100, cc[1] * 100, cc[2] * 100), unreal.Rotator())
    a.set_actor_scale3d(unreal.Vector(*s))
    comp = a.static_mesh_component
    comp.set_static_mesh(CUBE)
    comp.set_editor_property('mobility', unreal.ComponentMobility.STATIC)
    comp.set_editor_property('cast_shadow', False)
    a.set_actor_hidden_in_game(True)
    comp.set_editor_property('visible', False)
    comp.set_collision_profile_name('Custom')       # 先设 Custom 再改对象类型 / 响应，不然存盘读回来按 BlockAll（城区图的坑）
    comp.set_collision_object_type(unreal.CollisionChannel.ECC_VEHICLE)
    comp.set_collision_response_to_all_channels(unreal.CollisionResponseType.ECR_IGNORE)
    comp.set_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN, unreal.CollisionResponseType.ECR_BLOCK)
    comp.set_editor_property('affect_distance_field_lighting', False)
    comp.set_editor_property('affect_dynamic_indirect_lighting', False)
    tagged(a, 'Bounds', name, [tag])

tp = sub.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(0, 0, 0), unreal.Rotator())
tp.tags = ['MechArena', f'HalfX={P / 2 * 100:.0f}', f'HalfY={HY * 100:.0f}', TAG]
tp.set_folder_path('OldStreet/Bounds')
tp.set_actor_label('MechArena')
tp = sub.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(0, 0, 0), unreal.Rotator())
tp.tags = ['OldStreetBounds', f'PeriodX={P * 100:.0f}', f'SeamX={P / 2 * 100:.0f}', f'HalfY={HY * 100:.0f}', f'CeilZ={CEIL * 100:.0f}', TAG]
tp.set_folder_path('OldStreet/Bounds')
tp.set_actor_label('OldStreetBounds')

# ---------------------------------------------------------------- 出生点（约定见 Source/RobotSlice/MechMaps.h）
for i, s_ in enumerate(L['team_spawn']):
    t = sub.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(s_['X'] * 100, s_['Y'] * 100, 100), unreal.Rotator(0, 0, s_['yaw']))
    tagged(t, 'Spawns', f'TeamSpawn{i}', [f'TeamSpawn{i}'])
for i, (x, y) in enumerate(L['dm_spawn']):
    t = sub.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(x * 100, y * 100, 120), unreal.Rotator())
    tagged(t, 'Spawns', f'DMSpawn{i + 1}', ['DMSpawn'])
    t.set_editor_property('player_start_tag', 'DMSpawn')
# 基础图自带的 PlayerStart 挪到路口
for a in sub.get_all_level_actors():
    if a.get_class().get_name() == 'PlayerStart' and TAG not in [str(t) for t in a.tags]:
        a.set_actor_location(unreal.Vector(-2500, 0, 120), False, False)
        a.set_actor_rotation(unreal.Rotator(0, 0, 0), False)

# ---------------------------------------------------------------- 光照导演的逐地图调参
TUNE = json.load(open('D:/RobotSlice/tools/oldstreet_look.json', encoding='utf-8'))
dirs = [a for a in sub.get_all_level_actors() if 'LightingDirector' in a.get_class().get_name()]
for d in dirs:
    prof = None
    try:
        prof = d.get_editor_property('time_controller').get_editor_property('profile')
    except Exception as e:
        log('读不到日光剖面，用默认', e)
    src = prof or unreal.get_default_object(unreal.LightingDayProfile)
    t = d.get_editor_property('tuning_settings')
    for k in ('latitude', 'longitude', 'utc_offset'):
        t.set_editor_property(k, float(src.get_editor_property(k)))
    import datetime
    dt = datetime.date(int(src.get_editor_property('calendar_year')), 1, 1) + datetime.timedelta(days=int(src.get_editor_property('day_of_year')) - 1)
    t.set_editor_property('calendar_year', float(dt.year))
    t.set_editor_property('calendar_month', float(dt.month))
    t.set_editor_property('calendar_day', float(dt.day))
    t.set_editor_property('real_moon', 1.0 if src.get_editor_property('real_moon') else 0.0)
    for k, v in TUNE['tuning'].items():
        t.set_editor_property(k, v)
    d.set_editor_property('tuning_settings', t)
log('光照导演', len(dirs), '个，已设逐地图调参。', HOUR_NOTE)

# ---------------------------------------------------------------- 后期：A 老胶片午后（优先级 200，压过光照插件的 100）
ppv = sub.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0), unreal.Rotator())
tagged(ppv, 'Look', 'PP_OldFilm')
ppv.set_editor_property('unbound', True)
ppv.set_editor_property('priority', 200.0)
pp = ppv.get_editor_property('settings')
for key, value in TUNE['post'].items():
    if isinstance(value, list):
        value = unreal.Vector4(*value)
    pp.set_editor_property('override_' + key, True)
    pp.set_editor_property(key, value)
ppv.set_editor_property('settings', pp)

# 截图机位
cam = sub.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(0, 0, 2000), unreal.Rotator())
cam.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
tagged(cam, 'Look', 'ShotCam', ['ShotCam'])

# ---------------------------------------------------------------- 灯光架上插件不每帧改的属性：天空大气（偏暖、发白的午后天）。见 oldstreet_look.json
RIGS = [a for a in sub.get_all_level_actors() if 'LightingEnvironmentRig' in a.get_class().get_name()]
for r in RIGS:
    atm = r.get_editor_property('atmosphere')
    for k, v in TUNE.get('atmosphere', {}).items():
        if isinstance(v, list):
            v = unreal.LinearColor(*v)
        atm.set_editor_property(k, v)
    log('天空大气已设', list(TUNE.get('atmosphere', {}).keys()))

# ---------------------------------------------------------------- 街景贴片：两头最远处，靠雾盖住。图是游戏内截的（tools/ue_oldstreet_shots.py 的 backdrop 机位 + ue_oldstreet_backdrop.py 导入）
if eas.does_asset_exist('/Game/Env/OldStreet/Meshes/SM_OS_Quad'):
    dist = (L['FULL_COPIES'] + L['LOW_COPIES'] + 0.5) * P + 2.0
    BD = L['backdrop']
    QUAD = eas.load_asset('/Game/Env/OldStreet/Meshes/SM_OS_Quad')
    for name, sx, yaw, mi in (('Backdrop_Fwd', 1, 0.0, 'MI_OS_Backdrop_Fwd'), ('Backdrop_Back', -1, 180.0, 'MI_OS_Backdrop_Back')):
        b = sub.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(sx * dist * 100, 0, BD['zc'] * 100), unreal.Rotator(0, 0, yaw))
        b.set_actor_scale3d(unreal.Vector(1.0, BD['w'] / 100.0, BD['h'] / 100.0))
        comp = b.static_mesh_component
        comp.set_static_mesh(QUAD)
        comp.set_editor_property('mobility', unreal.ComponentMobility.STATIC)
        comp.set_collision_profile_name('NoCollision')
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        comp.set_editor_property('cast_shadow', False)
        comp.set_editor_property('affect_distance_field_lighting', False)
        comp.set_editor_property('affect_dynamic_indirect_lighting', False)
        comp.set_material(0, eas.load_asset(f'/Game/Env/OldStreet/Materials/{mi}'))
        tagged(b, 'Look', name)
    log('街景贴片已摆，距离', dist)

unreal.EditorLoadingAndSavingUtils.save_map(world, DST_MAP)
log('已存', DST_MAP, counts)
