"""火山夺芯图 L_Volcano 的工程内整理（地图本身由草稿工程 D:/RobotSlice_MapLab 生成后拷进来，见 docs/火山地图评估）：
1. /Game/8KVolcanoLandscapePack（拷进来的火山包子集）整体搬到 /Game/Env/Volcano（引用自动修好）
2. 贴图限 2048（烟雾 1024），5 块石头开 Nanite，地形草消失距离 300 米，熔岩自发光 50 → 5
3. 地图里：近景地形关碰撞（碰撞全靠夺芯灰盒），加指数高度雾，太阳抬高，峡谷中心放 TargetPoint（Tag CoreArena，朝向 +X）
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_volcano_setup.py [步骤…]" -AllowCommandletRendering -RenderOffscreen -unattended
步骤：move tex boxrock rocks grass lava map（不写 = 全做）
"""
import sys

import unreal

args = [a for a in sys.argv[1:] if not a.startswith('-')]
STEPS = set(args) if args else {'move', 'tex', 'boxrock', 'rocks', 'grass', 'lava', 'map'}
SRC = '/Game/8KVolcanoLandscapePack'
DST = '/Game/Env/Volcano'
MAP = '/Game/Maps/L_Volcano'
EAL = unreal.EditorAssetLibrary
P = lambda *a: unreal.log('[volcano] ' + ' '.join(str(x) for x in a))

if 'move' in STEPS and EAL.does_directory_exist(SRC):
    ok = EAL.rename_directory(SRC, DST)
    P('搬目录', SRC, '→', DST, ok)
    EAL.save_directory(DST, only_if_is_dirty=True, recursive=True)

assets = EAL.list_assets(DST, recursive=True, include_folder=False) if EAL.does_directory_exist(DST) else []

if 'tex' in STEPS:
    n = 0
    for p in assets:
        d = EAL.find_asset_data(p)
        if str(d.asset_class_path.asset_name) != 'Texture2D':
            continue
        a = unreal.load_asset(p.split('.')[0])
        try:
            w = max(int(v) for v in str(d.get_tag_value('Dimensions') or '0x0').split('x')[:2])
        except ValueError:
            w = 0
        cap = 1024 if 'Smoke' in a.get_name() else 2048
        if w > cap and a.get_editor_property('max_texture_size') != cap:
            a.set_editor_property('max_texture_size', cap)
            EAL.save_loaded_asset(a, False)
            n += 1
            P('限尺寸', a.get_name(), w, '→', cap)
    P('贴图限尺寸', n, '张')

if 'boxrock' in STEPS:
    # 夺芯灰盒在“只留碰撞”模式下掩体、岩洞内壁用的世界投影岩石（M_WorldPBR 按法线挑投影轴）
    path = f'{DST}/MI_Volcano_BoxRock'
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mi = unreal.load_asset(path) if EAL.does_asset_exist(path) else tools.create_asset(
        'MI_Volcano_BoxRock', DST, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel = unreal.MaterialEditingLibrary
    mi.set_editor_property('parent', unreal.load_asset('/Game/Env/Materials/M_WorldPBR'))
    mel.set_material_instance_texture_parameter_value(mi, 'BaseColorTex', unreal.load_asset(f'{DST}/Materials/Cliff_Granite/Cliff_Granite_Albedo'))
    mel.set_material_instance_texture_parameter_value(mi, 'NormalTex', unreal.load_asset(f'{DST}/Materials/Cliff_Granite/Cliff_Granite_Normal'))
    arm = unreal.load_asset('/Game/Env/Textures/concrete_wall_006/T_concrete_wall_006_arm_2k')
    if arm:
        mel.set_material_instance_texture_parameter_value(mi, 'ARMTex', arm)
    mel.set_material_instance_scalar_parameter_value(mi, 'TileSize', 450.0)
    mel.set_material_instance_scalar_parameter_value(mi, 'Wetness', 0.0)
    mel.set_material_instance_scalar_parameter_value(mi, 'PuddleAmount', 0.0)
    mel.set_material_instance_vector_parameter_value(mi, 'Tint', unreal.LinearColor(1.6, 1.55, 1.5, 1))
    EAL.save_loaded_asset(mi, False)
    P('岩石方块材质', path)

if 'rocks' in STEPS:
    for i in range(1, 6):
        sm = unreal.load_asset(f'{DST}/Assets/Rock_{i}/Model/Rock_{i}')
        if sm is None:
            P('找不到石头', i)
            continue
        ns = sm.get_editor_property('nanite_settings')
        if not ns.enabled:
            ns.enabled = True
            sm.set_editor_property('nanite_settings', ns)
            EAL.save_loaded_asset(sm, False)
        P('石头 Nanite', sm.get_name(), sm.get_editor_property('nanite_settings').enabled)

if 'grass' in STEPS:
    for p in assets:
        g = unreal.load_asset(p.split('.')[0])
        if not isinstance(g, unreal.LandscapeGrassType):
            continue
        vs = g.get_editor_property('grass_varieties')
        for v in vs:
            ec = v.get_editor_property('end_cull_distance')
            ec.set_editor_property('default', 30000)
            v.set_editor_property('end_cull_distance', ec)
            sc = v.get_editor_property('start_cull_distance')
            sc.set_editor_property('default', 24000)
            v.set_editor_property('start_cull_distance', sc)
        g.set_editor_property('grass_varieties', vs)
        EAL.save_loaded_asset(g, False)
        P('地形草', g.get_name(), len(vs), '种，消失距离 300 米')

if 'lava' in STEPS:
    mi = unreal.load_asset(f'{DST}/Materials/Terrain/Terrain_MainMat_Inst')
    if mi:
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi, 'MoltenLava_EmissivePower', 5.0)
        EAL.save_loaded_asset(mi, False)
        P('熔岩自发光', unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(mi, 'MoltenLava_EmissivePower'))

if 'map' in STEPS:
    unreal.EditorLoadingAndSavingUtils.load_map(MAP)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = eas.get_all_level_actors()
    for a in actors:
        if isinstance(a, unreal.LandscapeProxy):
            lab = a.get_actor_label()
            if lab == 'Volcano_Near':
                # 碰撞全交给灰盒：近景地形的崖面会挡住岩洞口，所以关掉
                bi = a.get_editor_property('body_instance')
                bi.set_editor_property('collision_profile_name', 'NoCollision')
                bi.set_editor_property('collision_enabled', unreal.CollisionEnabled.NO_COLLISION)
                a.set_editor_property('body_instance', bi)
            P('地形', lab, a.get_actor_location(), a.get_actor_rotation().yaw, a.get_actor_scale3d(),
              a.get_editor_property('body_instance').get_editor_property('collision_profile_name'))
    if not any(isinstance(a, unreal.ExponentialHeightFog) for a in actors):
        fog = eas.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0))
        fc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
        fc.set_editor_property('fog_density', 0.02)
        fc.set_editor_property('fog_height_falloff', 0.08)
        fc.set_editor_property('start_distance', 6000.0)
        fc.set_editor_property('fog_inscattering_luminance', unreal.LinearColor(0.45, 0.42, 0.40, 1.0))
        fog.set_actor_label('Volcano_Fog')
        P('加高度雾')
    for a in actors:
        if isinstance(a, unreal.DirectionalLight):
            # 原关卡太阳贴着地平线（峡谷里一片黑），抬到 38°，从峡谷斜上方照进来
            a.set_actor_rotation(unreal.Rotator(roll=0, pitch=-38, yaw=-35), False)
            P('太阳', a.get_actor_rotation())
        if isinstance(a, unreal.SkyLight):
            sc = a.get_component_by_class(unreal.SkyLightComponent)
            sc.set_editor_property('real_time_capture', True)
            P('天光实时捕获')
    if not any('CoreArena' in [str(t) for t in a.tags] for a in actors):
        tp = eas.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
        tp.set_editor_property('tags', [unreal.Name('CoreArena')])
        tp.set_actor_label('CoreArena')
        P('加夺芯点', tp.get_actor_location(), tp.tags)
    ok = unreal.EditorLoadingAndSavingUtils.save_current_level()
    P('存地图', ok)
    if EAL.does_directory_exist(SRC):
        left = EAL.list_assets(SRC, recursive=True)
        reds = [p for p in left if 'ObjectRedirector' in str(EAL.find_asset_data(p).asset_class_path.asset_name)]
        if len(reds) == len(left):
            P('删旧目录里的重定向器', len(reds), EAL.delete_directory(SRC))
        else:
            P('旧目录里还有非重定向资源，不删', len(left) - len(reds))

if 'perf' in STEPS:
    # 帧率（10-30 实测 1080p 离屏自动对战：改前 55 帧，测试场 74 帧）：
    # 地形草（Nanite 石头）每帧在游戏线程生成实例，关掉省 3–5 毫秒；体积云 GPU 约 2.5 毫秒，去掉（高度雾代替）
    unreal.EditorLoadingAndSavingUtils.load_map(MAP)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in eas.get_all_level_actors():
        if isinstance(a, unreal.Landscape):
            a.set_editor_property('grass_enabled', False)
            P('地形草关闭', a.get_actor_label())
        if a.get_class().get_name() == 'VolumetricCloud':
            P('删体积云', a.get_actor_label())
            eas.destroy_actor(a)
    P('存地图', unreal.EditorLoadingAndSavingUtils.save_current_level())

if 'dim' in STEPS:
    # 熔岩调暗（10-30 用户：别把旁边掩体照成一片橙）：自发光 5 → 2.5；地图后期里单独加了辉光的话也压一半
    mi = unreal.load_asset(f'{DST}/Materials/Terrain/Terrain_MainMat_Inst')
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi, 'MoltenLava_EmissivePower', 2.5)
    EAL.save_loaded_asset(mi, False)
    P('熔岩自发光', unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(mi, 'MoltenLava_EmissivePower'))
    unreal.EditorLoadingAndSavingUtils.load_map(MAP)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    changed = False
    for a in eas.get_all_level_actors():
        if isinstance(a, unreal.PostProcessVolume):
            ps = a.get_editor_property('settings')
            info = {k: ps.get_editor_property(k) for k in ('override_bloom_intensity', 'bloom_intensity', 'override_bloom_threshold', 'bloom_threshold',
                                                         'override_auto_exposure_bias', 'auto_exposure_bias', 'override_lens_flare_intensity', 'lens_flare_intensity')}
            P('后期', a.get_actor_label(), 'unbound', a.get_editor_property('unbound'), info)
            if ps.get_editor_property('override_bloom_intensity'):
                ps.set_editor_property('bloom_intensity', ps.get_editor_property('bloom_intensity') * 0.5)
                a.set_editor_property('settings', ps)
                changed = True
                P('辉光压一半 →', a.get_editor_property('settings').get_editor_property('bloom_intensity'))
    if changed:
        P('存地图', unreal.EditorLoadingAndSavingUtils.save_current_level())
P('完成', sorted(STEPS))
