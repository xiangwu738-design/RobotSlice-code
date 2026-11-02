"""保卫战地图的氛围试验（10-31 用户：「先在复制的地图上做一版战场氛围（阴天、烟、焦土、残骸），出几张截图给我看」）。
把 L_Nature 复制成 /Game/Maps/L_Defense（L_Nature 本身不动，对战照常用），在复制品上：
- 阴天：太阳调暗、光变软变冷，天光提亮，大气去蓝加雾霾，雾加浓变灰
- 焦土：后期材质把绿色往枯黄偏、整体去饱和（M_PP_Battlefield）；地上撒焦黑的弹坑贴花（M_Scorch）
- 烟和火：残楼和机甲残骸上冒烟柱（AAAVFX Course_05 SmokePlume）、着火（CarFire）
- 残骸：几台倒地的机甲（阵营机体 + 武士刀包的死亡动作最后一帧），发光件换成暗色
- 不摆堡垒（用户 10-31：巨构防御工事是以后的方向，这张图只做氛围）
可以重复跑（复制品已存在就直接改；带 Defense 标签的东西先删掉再摆）。草的密度是运行时的控制台变量，截图脚本里设。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_build_defense.py" -AllowCommandletRendering -RenderOffscreen -unattended
"""
import math
import random
import re

import unreal

SRC = '/Game/Maps/L_Nature'
DST = '/Game/Maps/L_Defense'
DIR = '/Game/Env/Defense'
TAG = 'Defense'
eas = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
rng = random.Random(1031)
log = lambda *a: unreal.log('[defense] ' + ' '.join(str(x) for x in a))


def hlsl(name):
    lines = open(f'D:/RobotSlice/tools/shaders/{name}', encoding='utf-8').read().splitlines()
    code = '\n'.join(re.sub('//.*$', '', ln) for ln in lines)
    assert all(ord(ch) < 128 for ch in code), name
    return code


def custom(m, code, names, desc, out_type, x, y):
    c = mel.create_material_expression(m, unreal.MaterialExpressionCustom, x, y)
    c.set_editor_property('code', code)
    c.set_editor_property('output_type', out_type)
    c.set_editor_property('description', desc)
    ins = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property('input_name', n)
        ins.append(ci)
    c.set_editor_property('inputs', ins)
    return c


def scalar(m, name, v, x, y):
    s = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, x, y)
    s.set_editor_property('parameter_name', name)
    s.set_editor_property('default_value', v)
    return s


def find_custom(m, prop, desc):
    todo = [mel.get_material_property_input_node(m, prop)]
    while todo:
        e = todo.pop()
        if not e:
            continue
        if isinstance(e, unreal.MaterialExpressionCustom) and str(e.get_editor_property('description')) == desc:
            return e
        todo += mel.get_inputs_for_material_expression(m, e)
    return None


# ---------------------------------------------------------------- 材质
PP_PARAMS = {'Amount': 0.75, 'Desat': 0.3}


def pp_material():
    path = f'{DIR}/M_PP_Battlefield'
    if eas.does_asset_exist(path):
        m = eas.load_asset(path)
        c = find_custom(m, unreal.MaterialProperty.MP_EMISSIVE_COLOR, 'Battlefield')
        if c:
            c.set_editor_property('code', hlsl('pp_battlefield.hlsl'))
            for e in mel.get_inputs_for_material_expression(m, c):
                if isinstance(e, unreal.MaterialExpressionScalarParameter):
                    e.set_editor_property('default_value', PP_PARAMS.get(str(e.get_editor_property('parameter_name')), e.get_editor_property('default_value')))
            mel.recompile_material(m)
            eas.save_loaded_asset(m)
            return m
    m = tools.create_asset('M_PP_Battlefield', DIR, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('material_domain', unreal.MaterialDomain.MD_POST_PROCESS)
    m.set_editor_property('blendable_location', unreal.BlendableLocation.BL_SCENE_COLOR_BEFORE_DOF)
    st = mel.create_material_expression(m, unreal.MaterialExpressionSceneTexture, -900, 0)
    st.set_editor_property('scene_texture_id', unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    c = custom(m, hlsl('pp_battlefield.hlsl'), ['Scene', 'Amount', 'Desat'], 'Battlefield', unreal.CustomMaterialOutputType.CMOT_FLOAT3, -400, 0)
    assert mel.connect_material_expressions(st, 'Color', c, 'Scene')
    mel.connect_material_expressions(scalar(m, 'Amount', PP_PARAMS['Amount'], -900, 200), '', c, 'Amount')
    mel.connect_material_expressions(scalar(m, 'Desat', PP_PARAMS['Desat'], -900, 300), '', c, 'Desat')
    mel.connect_material_property(c, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    log('新建', path)
    return m


def scorch_material():
    path = f'{DIR}/M_Scorch'
    if eas.does_asset_exist(path):
        m = eas.load_asset(path)
        c = find_custom(m, unreal.MaterialProperty.MP_BASE_COLOR, 'Scorch')
        if c:
            m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
            m.set_editor_property('material_domain', unreal.MaterialDomain.MD_DEFERRED_DECAL)
            c.set_editor_property('code', hlsl('scorch_decal.hlsl'))
            mel.recompile_material(m)
            eas.save_loaded_asset(m)
            return m
    m = tools.create_asset('M_Scorch', DIR, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)   # 先设混合模式再设贴花域（反过来第一次编译会报「贴花只能半透明」）
    m.set_editor_property('material_domain', unreal.MaterialDomain.MD_DEFERRED_DECAL)
    uv = mel.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -900, 0)
    wp = mel.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -900, 120)
    c = custom(m, hlsl('scorch_decal.hlsl'), ['UV', 'WP'], 'Scorch', unreal.CustomMaterialOutputType.CMOT_FLOAT4, -500, 0)
    assert mel.connect_material_expressions(uv, '', c, 'UV')
    assert mel.connect_material_expressions(wp, '', c, 'WP')
    rgb = mel.create_material_expression(m, unreal.MaterialExpressionComponentMask, -200, -60)
    for k, v in (('r', True), ('g', True), ('b', True), ('a', False)):
        rgb.set_editor_property(k, v)
    al = mel.create_material_expression(m, unreal.MaterialExpressionComponentMask, -200, 80)
    for k, v in (('r', False), ('g', False), ('b', False), ('a', True)):
        al.set_editor_property(k, v)
    mel.connect_material_expressions(c, '', rgb, '')
    mel.connect_material_expressions(c, '', al, '')
    mel.connect_material_property(rgb, '', unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(al, '', unreal.MaterialProperty.MP_OPACITY)
    mel.connect_material_property(scalar(m, 'Roughness', 0.95, -200, 200), '', unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    log('新建', path)
    return m


pp = pp_material()
scorch = scorch_material()

# 云：复制一份示例图的云材质实例（共用的那份不动），覆盖率、密度加上去、开一点暴风云 → 阴天
CLOUD_SRC = '/Game/Environments/AndreyPestryakov/Environment_Pack_01/Landscape/OtherSettings/AP_m_SimpleVolumetricCloud_Inst'
CLOUD = f'{DIR}/MI_Cloud_Overcast'
if not eas.does_asset_exist(CLOUD):
    eas.duplicate_asset(CLOUD_SRC, CLOUD)
cloud_mi = eas.load_asset(CLOUD)
for k, v in (('Cloud_GlobalCoverage', 0.35), ('Cloud_GlobalDensity', 0.02), ('StormClouds', 0.5)):
    mel.set_material_instance_scalar_parameter_value(cloud_mi, k, v)
eas.save_loaded_asset(cloud_mi)

# ---------------------------------------------------------------- 地图
unreal.EditorLoadingAndSavingUtils.load_map(SRC)
_ps = next(a for a in sub.get_all_level_actors() if isinstance(a, unreal.PlayerStart))
ps_loc, ps_rot = _ps.get_actor_location(), _ps.get_actor_rotation()
if not eas.does_asset_exist(DST):
    assert eas.duplicate_asset(SRC, DST), '复制地图失败'
    log('复制', SRC, '→', DST)
unreal.EditorLoadingAndSavingUtils.load_map(DST)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
for a in sub.get_all_level_actors():
    if TAG in [str(t) for t in a.tags]:
        sub.destroy_actor(a)
acts = sub.get_all_level_actors()


def first(cls):
    return next((a for a in acts if isinstance(a, cls)), None)


def setp(obj, name, value):
    try:
        obj.set_editor_property(name, value)
    except Exception as e:
        log('设不了', obj.get_class().get_name(), name, e)


# 阴天
sun = first(unreal.DirectionalLight)
if sun:
    lc = sun.light_component
    setp(lc, 'intensity', 6.0)
    setp(lc, 'temperature', 7400.0)
    setp(lc, 'light_source_angle', 8.0)
    sun.set_actor_rotation(unreal.Rotator(roll=180, pitch=-38, yaw=-147.6), False)
sky = first(unreal.SkyLight)
if sky:
    setp(sky.light_component, 'intensity', 2.3)
    setp(sky.light_component, 'lower_hemisphere_color', unreal.LinearColor(0.09, 0.085, 0.08, 1))
atm = first(unreal.SkyAtmosphere)
if atm:
    ac = atm.get_component_by_class(unreal.SkyAtmosphereComponent)
    setp(ac, 'rayleigh_scattering_scale', 0.014)
    setp(ac, 'mie_scattering_scale', 0.014)
cl = first(unreal.VolumetricCloud)
if cl and cloud_mi:
    setp(cl.get_component_by_class(unreal.VolumetricCloudComponent), 'material', cloud_mi)
fog = first(unreal.ExponentialHeightFog)
if fog:
    fc = fog.component
    setp(fc, 'fog_density', 0.04)
    setp(fc, 'fog_height_falloff', 0.12)
    setp(fc, 'fog_inscattering_luminance', unreal.LinearColor(0.42, 0.41, 0.39, 1))
    setp(fc, 'start_distance', 900.0)
ppv = first(unreal.PostProcessVolume)
if ppv:
    s = ppv.get_editor_property('settings')
    for k, v in (('color_saturation', unreal.Vector4(0.85, 0.85, 0.85, 1)), ('color_contrast', unreal.Vector4(1.0, 1.0, 1.0, 1)),
                 ('white_temp', 6100.0), ('auto_exposure_bias', 8.3), ('vignette_intensity', 0.45)):
        setp(s, 'override_' + k, True)
        setp(s, k, v)
    setp(s, 'weighted_blendables', unreal.WeightedBlendables(array=[unreal.WeightedBlendable(weight=1.0, object=pp)]))
    ppv.set_editor_property('settings', s)


def ground(x, y):
    r = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, 200000), unreal.Vector(x, y, -200000),
                                               unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [], unreal.DrawDebugTrace.NONE, True)
    if isinstance(r, tuple):
        r = r[1] if r[0] else None
    if r is None:
        return None
    t = r.to_tuple()
    return t[4].z if t[0] else None


def tagged(a, folder):
    a.tags = [TAG]
    a.set_folder_path(f'Defense/{folder}')
    return a


def mesh_actor(mesh, loc, rot, scale, folder, mat=None):
    a = tagged(sub.spawn_actor_from_class(unreal.StaticMeshActor, loc, rot), folder)
    c = a.static_mesh_component
    c.set_static_mesh(mesh)
    if mat:
        for i in range(c.get_num_materials()):
            c.set_material(i, mat)
    a.set_actor_scale3d(unreal.Vector(scale, scale, scale) if isinstance(scale, (int, float)) else scale)
    return a


# 散布的中心：原来的出生点（湖东边草地）。用户 10-31：巨构防御工事是以后的方向，这张图上不摆堡垒，只做氛围
F = unreal.Vector(ps_loc.x, ps_loc.y, 0)
F.z = ground(F.x, F.y) or ps_loc.z

# 焦土弹坑贴花：堡垒外 30–230 米
placed = 0
for i in range(55):
    ang = rng.uniform(0, 2 * math.pi)
    d = rng.uniform(4000, 23000)
    x, y = F.x + math.cos(ang) * d, F.y + math.sin(ang) * d
    z = ground(x, y)
    if z is None:
        continue
    size = rng.uniform(1200, 3200)
    a = tagged(sub.spawn_actor_from_class(unreal.DecalActor, unreal.Vector(x, y, z), unreal.Rotator(pitch=-90, yaw=rng.uniform(0, 360), roll=0)), 'Scorch')
    dc = a.decal
    dc.set_decal_material(scorch)
    setp(dc, 'decal_size', unreal.Vector(600, size, size))
    placed += 1
log('焦土贴花', placed)

# 机甲残骸：倒地的阵营机体（死亡动作最后一帧），发光件换成暗色，身上着火冒烟
death = eas.load_asset('/Game/Robot/Anims/Katana/Death_InPlace')
dark = eas.load_asset('/Game/Robot/Materials/MI_Robot_Frame')
fire = eas.load_asset('/Game/AAAVFX/Course_05/Particles/Ex_05_CarFireMain_01')
smoke = eas.load_asset('/Game/AAAVFX/Course_05/Particles/Ex_02_SmokePlume')


def fx(system, loc, scale, folder):
    if not system:
        return None
    a = tagged(sub.spawn_actor_from_class(unreal.NiagaraActor, loc, unreal.Rotator()), folder)
    a.niagara_component.set_asset(system)
    a.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    return a


wrecks = 0
for key, ang, d in (('NEC_M', 230, 9000), ('VHI_H', 270, 12000), ('PHASE_M', 120, 10000), ('VHI_M', 320, 8000), ('NEC_H', 40, 14000)):
    skm = eas.load_asset(f'/Game/Robot/Factions/SKM_{key}')
    if not skm:
        continue
    x, y = F.x + math.cos(math.radians(ang)) * d, F.y + math.sin(math.radians(ang)) * d
    z = ground(x, y)
    if z is None:
        continue
    a = tagged(sub.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(x, y, z - 20), unreal.Rotator(yaw=rng.uniform(0, 360))), 'Wrecks')
    c = a.skeletal_mesh_component
    c.set_skinned_asset_and_update(skm)
    a.set_actor_scale3d(unreal.Vector(1.35, 1.35, 1.35))
    if death:
        c.set_editor_property('animation_mode', unreal.AnimationMode.ANIMATION_SINGLE_NODE)
        data = unreal.SingleAnimationPlayData()
        data.set_editor_property('anim_to_play', death)
        data.set_editor_property('saved_looping', False)
        data.set_editor_property('saved_playing', False)
        data.set_editor_property('saved_position', max(0.0, death.get_play_length() - 0.05))
        c.set_editor_property('animation_data', data)
    if dark:
        for i, nm in enumerate(c.get_material_slot_names()):
            if 'Glow' in str(nm) or 'Lens' in str(nm):
                c.set_material(i, dark)
    fx(fire, unreal.Vector(x, y, z + 80), 0.7, 'Wrecks')
    fx(smoke, unreal.Vector(x, y, z + 400), 3.0, 'Wrecks')
    dec = tagged(sub.spawn_actor_from_class(unreal.DecalActor, unreal.Vector(x, y, z), unreal.Rotator(pitch=-90, yaw=rng.uniform(0, 360), roll=0)), 'Wrecks')
    dec.decal.set_decal_material(scorch)
    setp(dec.decal, 'decal_size', unreal.Vector(600, 1300, 1300))
    wrecks += 1
log('机甲残骸', wrecks)

# 残楼冒烟：挑 6 栋离堡垒近的
ruins = [a for a in acts if 'Nature' in [str(t) for t in a.tags] and isinstance(a, unreal.StaticMeshActor)]
ruins.sort(key=lambda a: (a.get_actor_location() - F).length())
for a in ruins[:6]:
    p = a.get_actor_location()
    o, e = a.get_actor_bounds(False)
    fx(smoke, unreal.Vector(o.x, o.y, o.z + e.z * 0.6), 6.0, 'Smoke')
    fx(fire, unreal.Vector(o.x + rng.uniform(-600, 600), o.y + rng.uniform(-600, 600), (ground(o.x, o.y) or p.z) + 100), 1.1, 'Smoke')
log('残楼冒烟', min(6, len(ruins)))

# 出生点照原图（巨构灰盒那一版挪过，放回去）
ps = first(unreal.PlayerStart)
if ps:
    ps.set_actor_location(ps_loc, False, True)
    ps.set_actor_rotation(ps_rot, False)

unreal.EditorLoadingAndSavingUtils.save_map(world, DST)
log('已保存', DST)
