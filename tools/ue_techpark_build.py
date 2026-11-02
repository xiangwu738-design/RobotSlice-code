"""按 k051_layout.py 的布局表搭 /Game/Maps/L_TechPark（相位电子园区死斗图）。先跑 ue_techpark_import.py 把资源导进来。
- 底图：L_LightingBase（只有天空灯光那套），删掉别的 actor
- 模块：每件按布局表摆 SM_HTS_<模块>_Solid / _Deco / _Holo（Deco、Holo 关碰撞，Holo 不投影）；远景 SM_TPB_C<编号>（无碰撞）
- 地面：引擎方块压扁成 700 × 600 米、顶面在 0，M_WorldPBR 浅灰大方砖
- 隐形墙：四周 + 头顶，对象类型 Vehicle、只挡 Pawn（子弹、AI 视线、镜头都不受影响），游戏里隐藏
- 出生点：TargetPoint「TeamSpawn0 / TeamSpawn1」（团队淘汰两队出生区中心，在基地圆台上）、PlayerStart「DMSpawn」12 个（死斗重生点候选）
- 场地标记：TargetPoint「TechParkArena」带标签 MechArena、HalfX= / HalfY= / Ceiling=（厘米）——GameMode::ComputeArena 读它
- 调色后期（阴天冷调，白色压高光）
- 最后跑 ue_techpark_dress.py（10-31 外观整修：地面、远景、后期、光照调参都在那边，只动外观）
坐标：布局表是 Blender 坐标（米，y 朝北），UE = (x, -y, z) × 100，朝向取反。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_techpark_build.py" -AllowCommandletRendering -RenderOffscreen
"""
import json

import unreal

LAYOUT = 'D:/RobotSlice/Inbox/_k051/layout.json'
DEST = '/Game/Env/TechPark'
SRC_MAP = '/Game/Maps/L_LightingBase'
DST_MAP = '/Game/Maps/L_TechPark'
TAG = 'TechPark'
eas = unreal.EditorAssetLibrary
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
L = json.load(open(LAYOUT, encoding='utf-8'))


def V(x, y, z):
    return unreal.Vector(x * 100.0, -y * 100.0, z * 100.0)


def key_name(it):
    k = it['piece']
    if it.get('skip'):
        k += '_No' + ''.join(it['skip'])
    if it.get('only'):
        k += '_Only' + ''.join(it['only'])
    return k


mesh_cache = {}


def mesh(path):
    if path not in mesh_cache:
        mesh_cache[path] = eas.load_asset(path) if eas.does_asset_exist(path) else None
    return mesh_cache[path]


unreal.EditorLoadingAndSavingUtils.load_map(SRC_MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
for a in sub.get_all_level_actors():
    cls = a.get_class().get_name()
    if not any(k in cls for k in ('LightingDirector', 'LightingEnvironmentRig', 'WorldSettings')) and 'Brush' not in cls:
        sub.destroy_actor(a)
world.get_world_settings().set_editor_property('default_game_mode', unreal.load_class(None, '/Script/RobotSlice.MechGameMode'))


def place(m, loc, yaw, scale, label, folder, cls):
    a = sub.spawn_actor_from_class(unreal.StaticMeshActor, loc, unreal.Rotator(pitch=0, yaw=yaw, roll=0))
    a.tags = [TAG]
    a.set_actor_label(label)
    a.set_folder_path(folder)
    a.set_actor_scale3d(scale)
    c = a.static_mesh_component
    c.set_static_mesh(m)
    c.set_editor_property('mobility', unreal.ComponentMobility.STATIC)
    if cls != 'Solid':
        c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        c.set_editor_property('can_character_step_up_on', unreal.CanBeCharacterBase.ECB_NO)
    if cls == 'Holo':
        c.set_editor_property('cast_shadow', False)
        c.set_editor_property('affect_distance_field_lighting', False)
        c.set_editor_property('affect_dynamic_indirect_lighting', False)
    return a


placed = missing = 0
for n, it in enumerate(L['items']):
    x, y, z = it['loc']
    loc = V(x, y, z)
    yaw = -it['yaw']
    sx, sy, sz = it['scale']
    scale = unreal.Vector(sx, sy, sz)
    if it['piece'].startswith('BRAND_'):
        m = mesh(f'{DEST}/Brand/SM_TP_{it["piece"][6:]}')
        if not m:
            missing += 1
            continue
        a = place(m, loc, yaw, scale, f'BR_{n:03d}_{it["piece"]}', 'TechPark/Brand', 'Deco')
        a.static_mesh_component.set_editor_property('cast_shadow', False)
        placed += 1
        continue
    if it['piece'].startswith('TPB_'):
        m = mesh(f'{DEST}/Backdrop/SM_{it["piece"]}')
        if not m:
            missing += 1
            continue
        place(m, loc, yaw, scale, f'BD_{n:03d}_{it["piece"]}', 'TechPark/Backdrop', 'Backdrop')
        placed += 1
        continue
    k = key_name(it)
    found = False
    for cls in ('Solid', 'Deco', 'Holo'):
        m = mesh(f'{DEST}/Meshes/SM_HTS_{k}_{cls}')
        if m:
            place(m, loc, yaw, scale, f'{n:03d}_{k}_{cls}', f'TechPark/{it.get("tag") or "Props"}', cls)
            found = True
    placed += found
    missing += not found
unreal.log(f'[techpark] 摆了 {placed} 件，缺网格 {missing}')

# ---------------------------------------------------------------- 地面
cube = eas.load_asset('/Engine/BasicShapes/Cube')
ground_mi_path = f'{DEST}/Materials/MI_TP_Ground'
mel = unreal.MaterialEditingLibrary
if eas.does_asset_exist(ground_mi_path):
    gmi = eas.load_asset(ground_mi_path)
else:
    gmi = unreal.AssetToolsHelpers.get_asset_tools().create_asset('MI_TP_Ground', f'{DEST}/Materials', unreal.MaterialInstanceConstant,
                                                                  unreal.MaterialInstanceConstantFactoryNew())
gmi.set_editor_property('parent', eas.load_asset('/Game/Env/Materials/M_WorldPBR'))
# 粗糙度借混凝土的 ARM：白方砖原图粗糙度 0.17，整片广场都要 Lumen 反射追踪（又贵又像打了蜡），换成 0.49 的哑光
for param, t in (('BaseColorTex', 'T_Tiles002_diff'), ('NormalTex', 'T_Tiles002_nor_gl'), ('ARMTex', 'T_Concrete006_arm')):
    mel.set_material_instance_texture_parameter_value(gmi, param, eas.load_asset(f'/Game/Env/Street/Textures/{t}'))
mel.set_material_instance_scalar_parameter_value(gmi, 'TileSize', 1000.0)   # 4×4 块一张图 → 2.5 米一块砖
mel.set_material_instance_scalar_parameter_value(gmi, 'Wetness', 0.0)    # 湿度会按噪声出深色水渍，企业广场要干净
mel.set_material_instance_scalar_parameter_value(gmi, 'PuddleAmount', -1.0)   # 0 时噪声偶尔超过 1 还会冒出深色水洼，给负数彻底关掉
mel.set_material_instance_scalar_parameter_value(gmi, 'EmissiveStrength', 0.0)
mel.set_material_instance_vector_parameter_value(gmi, 'Tint', unreal.LinearColor(0.44, 0.455, 0.48, 1))   # 比白色模块暗一档，读得出层次
eas.save_loaded_asset(gmi)
g = sub.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -50), unreal.Rotator(0, 0, 0))
g.tags = [TAG]
g.set_actor_label('Ground')
g.set_folder_path('TechPark')
g.set_actor_scale3d(unreal.Vector(700.0, 600.0, 1.0))
g.static_mesh_component.set_static_mesh(cube)
g.static_mesh_component.set_material(0, gmi)
g.static_mesh_component.set_editor_property('mobility', unreal.ComponentMobility.STATIC)

# ---------------------------------------------------------------- 隐形墙（只挡机体）
bx, by = L['bound']
ceil = L['ceiling']
walls = [((bx + 1, 0, ceil / 2 - 3), (2, by * 2 + 4, ceil + 6)), ((-bx - 1, 0, ceil / 2 - 3), (2, by * 2 + 4, ceil + 6)),
         ((0, by + 1, ceil / 2 - 3), (bx * 2 + 4, 2, ceil + 6)), ((0, -by - 1, ceil / 2 - 3), (bx * 2 + 4, 2, ceil + 6)),
         ((0, 0, ceil + 1), (bx * 2 + 4, by * 2 + 4, 2))]
for i, (c, s) in enumerate(walls):
    w = sub.spawn_actor_from_class(unreal.StaticMeshActor, V(*c), unreal.Rotator(0, 0, 0))
    w.tags = [TAG, 'ArenaWall']
    w.set_actor_label(f'ArenaWall_{i}')
    w.set_folder_path('TechPark/Walls')
    w.set_actor_scale3d(unreal.Vector(*s))
    sm = w.static_mesh_component
    sm.set_static_mesh(cube)
    sm.set_editor_property('mobility', unreal.ComponentMobility.STATIC)
    sm.set_editor_property('cast_shadow', False)
    sm.set_editor_property('affect_distance_field_lighting', False)
    sm.set_collision_profile_name('Custom')
    sm.set_collision_object_type(unreal.CollisionChannel.ECC_VEHICLE)
    sm.set_collision_response_to_all_channels(unreal.CollisionResponseType.ECR_IGNORE)
    sm.set_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN, unreal.CollisionResponseType.ECR_BLOCK)
    sm.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
    w.set_actor_hidden_in_game(True)

# ---------------------------------------------------------------- 出生点 + 场地标记（标签约定见 Source/RobotSlice/MechMaps.h）
# 团队淘汰：TeamSpawn0 / TeamSpawn1 各一个点 = 出生区中心（朝向 = 出生朝向，一队几台沿左右 7 米排开）
for team in (0, 1):
    pts = L['spawns'][f'team{team}']
    x = sum(p[0] for p in pts[:3]) / 3
    y = sum(p[1] for p in pts[:3]) / 3
    z, yaw = pts[0][2], pts[0][3]
    t = sub.spawn_actor_from_class(unreal.TargetPoint, V(x, y, z), unreal.Rotator(pitch=0, yaw=-yaw, roll=0))
    t.tags = [TAG, f'TeamSpawn{team}']
    t.set_actor_label(f'TeamSpawn{team}')
    t.set_folder_path('TechPark/Spawns')
# 死斗：DMSpawn 重生点候选（PlayerStart，第一个也当编辑器 / 查看模式的默认出生点）
for i, (x, y, z, yaw) in enumerate(L['spawns']['dm']):
    p = sub.spawn_actor_from_class(unreal.PlayerStart, V(x, y, z), unreal.Rotator(pitch=0, yaw=-yaw, roll=0))
    p.tags = [TAG, 'DMSpawn']
    p.set_actor_label(f'DMSpawn_{i:02d}')
    p.set_folder_path('TechPark/Spawns')
m = sub.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
m.tags = [TAG, 'MechArena', f'HalfX={bx * 100:.0f}', f'HalfY={by * 100:.0f}', f'Ceiling={ceil * 100:.0f}']
m.set_actor_label('TechParkArena')
m.set_folder_path('TechPark')

# ---------------------------------------------------------------- 调色后期：阴天冷调，白色压高光
ppv = sub.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
ppv.tags = [TAG]
ppv.set_actor_label('PP_TechPark')
ppv.set_folder_path('TechPark')
ppv.set_editor_property('unbound', True)
ppv.set_editor_property('priority', 10.0)
pp = ppv.get_editor_property('settings')
for key, value in (('bloom_intensity', 0.5), ('scene_fringe_intensity', 0.2), ('vignette_intensity', 0.32),
                   ('film_grain_intensity', 0.06), ('auto_exposure_bias', 0.35),
                   ('color_saturation', unreal.Vector4(1.0, 1.0, 1.0, 0.88)),
                   ('color_contrast', unreal.Vector4(1.0, 1.0, 1.0, 1.16)),
                   ('color_gain', unreal.Vector4(0.95, 0.99, 1.05, 1.0)),          # 整体偏冷（阴天原本发灰发黄）
                   ('color_gain_shadows', unreal.Vector4(0.9, 0.97, 1.08, 1.0)),
                   ('color_gain_highlights', unreal.Vector4(0.98, 1.0, 1.02, 0.96)),
                   ('color_offset_shadows', unreal.Vector4(0.0, 0.002, 0.007, 0.0))):
    pp.set_editor_property('override_' + key, True)
    pp.set_editor_property(key, value)
ppv.set_editor_property('settings', pp)

# 截图机位（ue_techpark_shots.py 运行时挪它）
cam = sub.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(0, 0, 3000), unreal.Rotator(pitch=-30, yaw=0, roll=0))
cam.tags = [TAG]
cam.set_actor_label('ShotCam')
cam.set_folder_path('TechPark')
cam.get_editor_property('root_component').set_mobility(unreal.ComponentMobility.MOVABLE)

unreal.EditorLoadingAndSavingUtils.save_map(world, DST_MAP)
unreal.log(f'[techpark] 已另存 {DST_MAP}：模块 {placed} 件，死斗出生点 {len(L["spawns"]["dm"])} 个')

# 10-31 外观整修（地面材质、远景楼层 / 外圈楼 / 楼顶标识、后期、光照导演逐地图调参）在这张图上再加一层，重搭地图时一起做（单独跑也行）
exec(compile(open('D:/RobotSlice/tools/ue_techpark_dress.py', encoding='utf-8').read(), 'ue_techpark_dress.py', 'exec'), {'__name__': '__main__'})
