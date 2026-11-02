"""石柱山水（L_Pillars）的资源导入 → /Game/Env/Pillars。可重复跑。
先跑 tools/blender_pillars_export.py（FBX 写到 Inbox/_f25/export）。贴图全是原包的 / 工程里现成的，不手搓（11-02 用户：缺素材列出来，别自己画）。
细节贴图借 /Game/Env/Canyon/Textures（Poly Haven，只读引用），草借 /Game/Env/Dreamfield/Textures/T_DF_Lawn_*。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_pillars_import.py [tex] [mat] [mesh]" -AllowCommandletRendering -RenderOffscreen -unattended -nosplash
"""
import json
import re
import sys

import unreal

SRC = 'D:/RobotSlice/Inbox/_f25/export'
TEX = 'D:/RobotSlice/Inbox/_f25/textures'
DEST = '/Game/Env/Pillars'
CANYON = '/Game/Env/Canyon/Textures'
DF = '/Game/Env/Dreamfield/Textures'
STEPS = [a for a in sys.argv[1:] if a in ('tex', 'mat', 'mesh', 'mdf')] or ['tex', 'mat', 'mesh', 'mdf']
ONLY = [a[5:] for a in sys.argv[1:] if a.startswith('only=')]
WATERTEX = '/Water/Textures/Normals'  # Water 插件自带的无缝水面法线（只读引用，不改不存）
eas = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
P = unreal.MaterialProperty
log = lambda *a: unreal.log('[pillars] ' + ' '.join(str(x) for x in a))


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


def tex_import(path, name, kind='color', max_size=0):
    tools.import_asset_tasks([task(path, f'{DEST}/Textures', name)])
    p = f'{DEST}/Textures/{name}'
    t = eas.load_asset(p)
    if not t:
        unreal.log_warning('[pillars] 缺贴图 ' + p)
        return
    C = unreal.TextureCompressionSettings
    if kind == 'normal':
        t.set_editor_property('compression_settings', C.TC_NORMALMAP)
        t.set_editor_property('srgb', False)
        t.set_editor_property('flip_green_channel', True)  # 原包是 OpenGL 法线
    elif kind == 'normal_dx':
        t.set_editor_property('compression_settings', C.TC_NORMALMAP)
        t.set_editor_property('srgb', False)
    elif kind == 'mask':
        t.set_editor_property('compression_settings', C.TC_MASKS)
        t.set_editor_property('srgb', False)
    elif kind == 'gray':
        t.set_editor_property('compression_settings', C.TC_GRAYSCALE)
        t.set_editor_property('srgb', False)
    elif kind == 'bed':
        t.set_editor_property('compression_settings', C.TC_BC7)
        t.set_editor_property('srgb', True)
    if max_size:
        t.set_editor_property('max_texture_size', max_size)
    eas.save_loaded_asset(t)
    log('贴图', name, t.blueprint_get_size_x(), t.blueprint_get_size_y())


if 'tex' in STEPS:
    for key, suf in (('A', ''), ('B', '1'), ('C', '2')):
        tex_import(f'{TEX}/Rock_102_diffuse_deligh{suf}_opt.jpg', f'T_PL_Rock{key}_C')
        tex_import(f'{TEX}/Rock_102_diffuse_delight_normal{suf}.png', f'T_PL_Rock{key}_N', 'normal')
        tex_import(f'{TEX}/Rock_102_diffuse_delight_occlusion{suf}_opt.jpg', f'T_PL_Rock{key}_AO', 'gray', 2048)
        tex_import(f'{TEX}/Rock_102_roughness{suf}_opt.jpg', f'T_PL_Rock{key}_R', 'gray', 2048)
    tex_import(f'{TEX}/Colormap_0.png', 'T_PL_Terrain_C')
    tex_import(f'{TEX}/Normal Map_0.png', 'T_PL_Terrain_N', 'normal')
    tex_import(f'{TEX}/Moss_color.jpg', 'T_PL_Moss_C')
    tex_import(f'{TEX}/Moss_normal.jpg', 'T_PL_Moss_N', 'normal')


def hlsl(name):
    lines = open(f'D:/RobotSlice/tools/shaders/{name}', encoding='utf-8').read().splitlines()
    code = '\n'.join(re.sub('//.*$', '', ln) for ln in lines)
    assert all(ord(ch) < 128 for ch in code), name
    return code


def new_material(name):
    path = f'{DEST}/Materials/{name}'
    if eas.does_asset_exist(path):
        m = eas.load_asset(path)
        mel.delete_all_material_expressions(m)
    else:
        m = tools.create_asset(name, f'{DEST}/Materials', unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('used_with_nanite', True)
    m.set_editor_property('used_with_instanced_static_meshes', True)
    return m


def node(m, cls, x, y):
    return mel.create_material_expression(m, cls, x, y)


def custom(m, code, names, desc, out_type, x, y):
    c = node(m, unreal.MaterialExpressionCustom, x, y)
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
    s = node(m, unreal.MaterialExpressionScalarParameter, x, y)
    s.set_editor_property('parameter_name', name)
    s.set_editor_property('default_value', v)
    return s


def vec(m, name, v, x, y):
    s = node(m, unreal.MaterialExpressionVectorParameter, x, y)
    s.set_editor_property('parameter_name', name)
    s.set_editor_property('default_value', unreal.LinearColor(v[0], v[1], v[2], 1.0))
    return s


def link(a, out, b, inp):
    if not mel.connect_material_expressions(a, out, b, inp):
        unreal.log_warning(f'[pillars] 连不上 {a.get_name()}.{out} → {b.get_name()}.{inp}')


def finish(m):
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    log('材质', m.get_path_name())


def make_mi(name, parent, scalars=None, vectors=None, textures=None):
    path = f'{DEST}/Materials/{name}'
    mi = eas.load_asset(path) if eas.does_asset_exist(path) else tools.create_asset(
        name, f'{DEST}/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', parent)
    mel.clear_all_material_instance_parameters(mi)
    for k, v in (scalars or {}).items():
        mel.set_material_instance_scalar_parameter_value(mi, k, v)
    for k, v in (vectors or {}).items():
        mel.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(v[0], v[1], v[2], 1.0))
    for k, v in (textures or {}).items():
        mel.set_material_instance_texture_parameter_value(mi, k, eas.load_asset(v))
    eas.save_loaded_asset(mi)
    return mi


F2 = unreal.CustomMaterialOutputType.CMOT_FLOAT2
F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
UV_TOP = 'return WP.xy / Tile;'
UV_SIDE = 'return (abs(N.x) > abs(N.y) ? WP.yz : WP.xz) / Tile;'


def sample(m, tex_path, x, y, normal=False, param=None):
    if param:
        t = node(m, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        t.set_editor_property('parameter_name', param)
    else:
        t = node(m, unreal.MaterialExpressionTextureSample, x, y)
    tx = unreal.load_asset(tex_path) if tex_path.startswith('/Water/') else eas.load_asset(tex_path)  # 命令行模式资产表不扫插件内容，直接按路径加载
    if not tx:
        unreal.log_warning('[pillars] 缺 ' + tex_path)
    else:
        t.set_editor_property('texture', tx)
    if normal:
        t.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    elif tx and tx.get_editor_property('compression_settings') == unreal.TextureCompressionSettings.TC_MASKS:
        t.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    elif tx and tx.get_editor_property('compression_settings') == unreal.TextureCompressionSettings.TC_GRAYSCALE:
        t.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    return t


def projected(m, wp, nn, tile_param, tex_path, y, normal=False):
    """同一张贴图的朝上 / 朝侧面两个投影（比三向投影省一次采样，朝侧面按法线在 X、Y 两轴里挑一个）。返回 (top, side)"""
    ut = custom(m, UV_TOP, ['WP', 'Tile'], 'UVTop', F2, -1500, y)
    us = custom(m, UV_SIDE, ['WP', 'N', 'Tile'], 'UVSide', F2, -1500, y + 120)
    for c in (ut, us):
        link(wp, '', c, 'WP')
        link(tile_param, '', c, 'Tile')
    link(nn, '', us, 'N')
    a = sample(m, tex_path, -1200, y, normal)
    b = sample(m, tex_path, -1200, y + 180, normal)
    link(ut, '', a, 'UVs')
    link(us, '', b, 'UVs')
    return a, b


def connect_outputs(m, code, names, sources, x=-300):
    """同一段 HLSL 算底色 / 法线 / 粗糙度（Mode 0 / 1 / 2），sources = {输入名: (节点, 输出名)}"""
    outs = {}
    for mode, desc, yy in ((0.0, 'Base', 0), (1.0, 'Normal', 900), (2.0, 'Rough', 1800)):
        c_ = custom(m, code, names + ['Mode'], desc, F3, x, yy)
        mc = node(m, unreal.MaterialExpressionConstant, x - 300, yy + 100)
        mc.set_editor_property('r', mode)
        link(mc, '', c_, 'Mode')
        for k in names:
            nd, out = sources[k]
            link(nd, out, c_, k)
        outs[desc] = c_
    mel.connect_material_property(outs['Base'], '', P.MP_BASE_COLOR)
    mel.connect_material_property(outs['Normal'], '', P.MP_NORMAL)
    rm = node(m, unreal.MaterialExpressionComponentMask, x + 300, 1800)
    for ch, on in (('r', True), ('g', False), ('b', False), ('a', False)):
        rm.set_editor_property(ch, on)
    link(outs['Rough'], '', rm, '')
    mel.connect_material_property(rm, '', P.MP_ROUGHNESS)


ROCK_S = {'Gamma': 1.0, 'Saturation': 0.72, 'AOStrength': 0.75, 'DetailMean': 0.42, 'DetailColor': 0.75, 'RoughScale': 1.0, 'BaseNScale': 1.0, 'BaseNMix': 1.0,
          'DetailN': 0.55, 'MidN': 0.3, 'IvyWeight': 1.05, 'TopWeight': 0.7, 'MossSoft': 0.22, 'MossN': 0.9,
          'TileF': 420.0, 'TileM': 2300.0, 'TileS': 380.0, 'TileNoise': 5200.0}
ROCK_V = {'Tint': (0.6, 0.6, 0.57), 'MossTint': (0.40, 0.70, 0.27)}
TERR_S = {'RockFrom': 0.28, 'RockTo': 0.62, 'GrassMix': 0.8, 'LawnMean': 0.28, 'BaseScale': 0.85, 'GrassN': 0.6, 'RockN': 0.7, 'TileG': 620.0, 'TileR': 520.0, 'TileNoise': 6100.0}
TERR_V = {'RockTint': (0.10, 0.105, 0.09)}
WATER_S = {'W1': 0.2, 'W2': 0.15, 'W3': 0.09, 'Rough': 0.08, 'RoughFar': 0.3, 'Spec': 0.45, 'SpecNear': 0.15, 'NearD': 2600.0, 'FarD': 9000.0, 'FoamW': 95.0, 'TileA': 2600.0, 'TileB': 7000.0, 'TileC': 24000.0, 'TileNoise': 42000.0, 'TileFoam': 900.0}
WATER_V = {'Deep': (0.004, 0.024, 0.05), 'Mid': (0.006, 0.058, 0.07), 'Shallow': (0.018, 0.125, 0.1), 'FoamColor': (0.5, 0.56, 0.55)}

if 'mat' in STEPS:
    # ---------------------------------------------------------------- 岩石
    m = new_material('M_PL_Rock')
    m.set_editor_property('tangent_space_normal', False)
    wp = node(m, unreal.MaterialExpressionWorldPosition, -1900, 0)
    nn = node(m, unreal.MaterialExpressionVertexNormalWS, -1900, 100)
    cam = node(m, unreal.MaterialExpressionCameraPositionWS, -1900, 200)
    vc = node(m, unreal.MaterialExpressionVertexColor, -1900, 300)
    pr = {}
    y = 500
    for k, v in ROCK_V.items():
        pr[k] = vec(m, k, v, -1900, y)
        y += 90
    for k, v in ROCK_S.items():
        pr[k] = scalar(m, k, v, -1900, y)
        y += 90
    base_c = sample(m, f'{DEST}/Textures/T_PL_RockA_C', -1200, -700, param='BaseC')
    base_n = sample(m, f'{DEST}/Textures/T_PL_RockA_N', -1200, -500, True, param='BaseN')
    base_ao = sample(m, f'{DEST}/Textures/T_PL_RockA_AO', -1200, -300, param='BaseAO')
    base_ro = sample(m, f'{DEST}/Textures/T_PL_RockA_R', -1200, -100, param='BaseR')
    tr = node(m, unreal.MaterialExpressionTransform, -900, -500)
    tr.set_editor_property('transform_source_type', unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_TANGENT)
    tr.set_editor_property('transform_type', unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    link(base_n, 'RGB', tr, '')
    dc_t, dc_s = projected(m, wp, nn, pr['TileF'], f'{CANYON}/T_rock_boulder_cracked_diff', 0)
    dn_t, dn_s = projected(m, wp, nn, pr['TileF'], f'{CANYON}/T_rock_boulder_cracked_nor_gl', 400, True)
    mn_t, mn_s = projected(m, wp, nn, pr['TileM'], f'{CANYON}/T_cliff_side_nor_gl', 800, True)
    sc_t, sc_s = projected(m, wp, nn, pr['TileS'], f'{DEST}/Textures/T_PL_Moss_C', 1200)
    sn_t, sn_s = projected(m, wp, nn, pr['TileS'], f'{DEST}/Textures/T_PL_Moss_N', 1600, True)
    un = custom(m, UV_TOP, ['WP', 'Tile'], 'UVNoise', F2, -1500, 2000)
    link(wp, '', un, 'WP')
    link(pr['TileNoise'], '', un, 'Tile')
    noise = sample(m, f'{CANYON}/T_Canyon_Noise', -1200, 2000)
    link(un, '', noise, 'UVs')
    names = ['WP', 'CamPos', 'N', 'BaseN', 'C', 'AO', 'Ro', 'V', 'DC_T', 'DC_S', 'DN_T', 'DN_S', 'MN_T', 'MN_S', 'SC_T', 'SC_S', 'SN_T', 'SN_S', 'Noise',
             'Tint', 'MossTint', 'Gamma', 'Saturation', 'AOStrength', 'DetailMean', 'DetailColor', 'RoughScale', 'BaseNScale', 'BaseNMix', 'DetailN', 'MidN',
             'IvyWeight', 'TopWeight', 'MossSoft', 'MossN']
    src = {'WP': (wp, ''), 'CamPos': (cam, ''), 'N': (nn, ''), 'BaseN': (tr, ''), 'C': (base_c, 'RGB'), 'AO': (base_ao, 'R'), 'Ro': (base_ro, 'R'), 'V': (vc, ''),
           'DC_T': (dc_t, 'RGB'), 'DC_S': (dc_s, 'RGB'), 'DN_T': (dn_t, 'RGB'), 'DN_S': (dn_s, 'RGB'), 'MN_T': (mn_t, 'RGB'), 'MN_S': (mn_s, 'RGB'),
           'SC_T': (sc_t, 'RGB'), 'SC_S': (sc_s, 'RGB'), 'SN_T': (sn_t, 'RGB'), 'SN_S': (sn_s, 'RGB'), 'Noise': (noise, 'RGB')}
    for k in names[19:]:
        src[k] = (pr[k], '')
    connect_outputs(m, hlsl('pillars_rock.hlsl'), names, src)
    finish(m)
    for key in 'ABC':
        make_mi(f'MI_PL_Rock{key}', m, textures={'BaseC': f'{DEST}/Textures/T_PL_Rock{key}_C', 'BaseN': f'{DEST}/Textures/T_PL_Rock{key}_N', 'BaseAO': f'{DEST}/Textures/T_PL_Rock{key}_AO', 'BaseR': f'{DEST}/Textures/T_PL_Rock{key}_R'})

    # ---------------------------------------------------------------- 地形
    m = new_material('M_PL_Terrain')
    m.set_editor_property('tangent_space_normal', False)
    wp = node(m, unreal.MaterialExpressionWorldPosition, -1900, 0)
    nn = node(m, unreal.MaterialExpressionVertexNormalWS, -1900, 100)
    cam = node(m, unreal.MaterialExpressionCameraPositionWS, -1900, 200)
    pr = {}
    y = 400
    for k, v in TERR_V.items():
        pr[k] = vec(m, k, v, -1900, y)
        y += 90
    for k, v in TERR_S.items():
        pr[k] = scalar(m, k, v, -1900, y)
        y += 90
    base_c = sample(m, f'{DEST}/Textures/T_PL_Terrain_C', -1200, -500)
    base_n = sample(m, f'{DEST}/Textures/T_PL_Terrain_N', -1200, -300, True)
    tr = node(m, unreal.MaterialExpressionTransform, -900, -300)
    tr.set_editor_property('transform_source_type', unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_TANGENT)
    tr.set_editor_property('transform_type', unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    link(base_n, 'RGB', tr, '')
    ug = custom(m, UV_TOP, ['WP', 'Tile'], 'UVGrass', F2, -1500, 0)
    link(wp, '', ug, 'WP')
    link(pr['TileG'], '', ug, 'Tile')
    gc = sample(m, f'{DF}/T_DF_Lawn_C', -1200, 0)
    gn = sample(m, f'{DF}/T_DF_Lawn_N', -1200, 180, True)
    link(ug, '', gc, 'UVs')
    link(ug, '', gn, 'UVs')
    rc_t, rc_s = projected(m, wp, nn, pr['TileR'], f'{CANYON}/T_rock_boulder_cracked_diff', 400)
    rn_t, rn_s = projected(m, wp, nn, pr['TileR'], f'{CANYON}/T_rock_boulder_cracked_nor_gl', 800, True)
    un = custom(m, UV_TOP, ['WP', 'Tile'], 'UVNoise', F2, -1500, 1200)
    link(wp, '', un, 'WP')
    link(pr['TileNoise'], '', un, 'Tile')
    noise = sample(m, f'{CANYON}/T_Canyon_Noise', -1200, 1200)
    link(un, '', noise, 'UVs')
    names = ['WP', 'CamPos', 'N', 'BaseN', 'C', 'GC', 'GN', 'RC_T', 'RC_S', 'RN_T', 'RN_S', 'Noise', 'RockTint', 'RockFrom', 'RockTo', 'GrassMix', 'LawnMean', 'BaseScale', 'GrassN', 'RockN']
    src = {'WP': (wp, ''), 'CamPos': (cam, ''), 'N': (nn, ''), 'BaseN': (tr, ''), 'C': (base_c, 'RGB'), 'GC': (gc, 'RGB'), 'GN': (gn, 'RGB'), 'RC_T': (rc_t, 'RGB'), 'RC_S': (rc_s, 'RGB'),
           'RN_T': (rn_t, 'RGB'), 'RN_S': (rn_s, 'RGB'), 'Noise': (noise, 'RGB')}
    for k in names[12:]:
        src[k] = (pr[k], '')
    connect_outputs(m, hlsl('pillars_terrain.hlsl'), names, src)
    finish(m)
    make_mi('MI_PL_Terrain', m)

    # ---------------------------------------------------------------- 水面：不透明受光（太阳影子、Lumen 反射都是现成的）。法线、白沫是 Water 插件自带的贴图，离岸远近用全局距离场（DistanceToNearestSurface，引擎现成的），不烤也不画贴图
    # 单层水（Single Layer Water）试过，当时一片白，后来查到是平面放大 8 万倍的问题（ue_build_pillars.py），没再回头试
    m = new_material('M_PL_Water')
    m.set_editor_property('used_with_nanite', False)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    m.set_editor_property('tangent_space_normal', True)
    wp = node(m, unreal.MaterialExpressionWorldPosition, -1900, 0)
    cam = node(m, unreal.MaterialExpressionCameraPositionWS, -1900, 100)
    tm = node(m, unreal.MaterialExpressionTime, -1900, 200)
    dist = node(m, unreal.MaterialExpressionDistanceToNearestSurface, -1900, 300)
    pr = {}
    y = 400
    for k, v in WATER_V.items():
        pr[k] = vec(m, k, v, -1900, y)
        y += 90
    for k, v in WATER_S.items():
        pr[k] = scalar(m, k, v, -1900, y)
        y += 90
    ns = []
    for i, (tile, dirx, diry) in enumerate((('TileA', 0.03, 0.019), ('TileB', -0.012, 0.009), ('TileC', 0.004, -0.0035))):
        c_ = custom(m, f'return WP.xy / Tile + float2({dirx}, {diry}) * T;', ['WP', 'Tile', 'T'], f'WaterUV{i}', F2, -1500, i * 220)
        link(wp, '', c_, 'WP')
        link(pr[tile], '', c_, 'Tile')
        link(tm, '', c_, 'T')
        s_ = sample(m, f'{WATERTEX}/T_Water_TilingNormal_Waves_02', -1200, i * 220, True)
        link(c_, '', s_, 'UVs')
        ns.append(s_)
    un = custom(m, UV_TOP, ['WP', 'Tile'], 'UVNoise', F2, -1500, 700)
    link(wp, '', un, 'WP')
    link(pr['TileNoise'], '', un, 'Tile')
    noise = sample(m, f'{CANYON}/T_Canyon_Noise', -1200, 700)
    link(un, '', noise, 'UVs')
    uf = custom(m, 'return WP.xy / Tile + float2(0.02, 0.012) * T;', ['WP', 'Tile', 'T'], 'UVFoam', F2, -1500, 950)
    link(wp, '', uf, 'WP')
    link(pr['TileFoam'], '', uf, 'Tile')
    link(tm, '', uf, 'T')
    foam = sample(m, '/Water/Textures/Foam/T_WaterFlow_01_Foam_Tiled', -1200, 950)
    link(uf, '', foam, 'UVs')
    names = ['WP', 'CamPos', 'Time', 'Dist', 'N1', 'N2', 'N3', 'Noise', 'Foam', 'W1', 'W2', 'W3', 'Deep', 'Mid', 'Shallow', 'FoamColor', 'NearD', 'FarD', 'FoamW', 'Rough', 'RoughFar']
    src = {'WP': (wp, ''), 'CamPos': (cam, ''), 'Time': (tm, ''), 'Dist': (dist, ''), 'N1': (ns[0], 'RGB'), 'N2': (ns[1], 'RGB'), 'N3': (ns[2], 'RGB'), 'Noise': (noise, 'RGB'), 'Foam': (foam, 'RGB')}
    for k in names[9:]:
        src[k] = (pr[k], '')
    connect_outputs(m, hlsl('pillars_water.hlsl'), names, src, x=0)
    # 高光：离岸近的地方压低（Lumen 反射在石柱脚、地形边上会打中距离场变黑，看着像一圈脏东西），水面颜色多露出来一点
    spc = custom(m, 'return lerp(SpecNear, Spec, smoothstep(0.0, 1.0, saturate(max(Dist, 0.0) / NearD * 1.4)));', ['Dist', 'SpecNear', 'Spec', 'NearD'], 'WaterSpec', unreal.CustomMaterialOutputType.CMOT_FLOAT1, 0, 2400)
    link(dist, '', spc, 'Dist')
    link(pr['SpecNear'], '', spc, 'SpecNear')
    link(pr['Spec'], '', spc, 'Spec')
    link(pr['NearD'], '', spc, 'NearD')
    mel.connect_material_property(spc, '', P.MP_SPECULAR)
    finish(m)
    make_mi('MI_PL_Water', m)


def import_fbx(fbx, dest, name):
    ui = unreal.FbxImportUI()
    ui.set_editor_property('import_mesh', True)
    ui.set_editor_property('import_as_skeletal', False)
    ui.set_editor_property('import_materials', False)
    ui.set_editor_property('import_textures', False)
    ui.set_editor_property('import_animations', False)
    ui.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_STATIC_MESH)
    smd = ui.static_mesh_import_data
    smd.set_editor_property('combine_meshes', True)
    smd.set_editor_property('auto_generate_collision', False)
    smd.set_editor_property('generate_lightmap_u_vs', False)
    smd.set_editor_property('build_nanite', True)
    smd.set_editor_property('vertex_color_import_option', unreal.VertexColorImportOption.REPLACE)
    tools.import_asset_tasks([task(fbx, dest, name=name, options=ui)])


if 'mesh' in STEPS:
    MD = f'{DEST}/Meshes'
    meta = json.load(open(f'{SRC}/pillars.json', encoding='utf-8'))
    MI = {k: eas.load_asset(f'{DEST}/Materials/{v}') for k, v in (('PL_RockA', 'MI_PL_RockA'), ('PL_RockB', 'MI_PL_RockB'), ('PL_RockC', 'MI_PL_RockC'), ('PL_Terrain', 'MI_PL_Terrain'))}
    for name in sorted(meta['meshes']):
        if 'Bush' in name:
            continue
        if ONLY and not any(o in name for o in ONLY):
            continue
        import_fbx(f'{SRC}/{name}.fbx', MD, name)
        a = eas.load_asset(f'{MD}/{name}')
        if not a:
            unreal.log_warning(f'[pillars] 缺网格 {name}')
            continue
        nan = a.get_editor_property('nanite_settings')
        if not nan.enabled:
            nan.enabled = True
            a.set_editor_property('nanite_settings', nan)
        bs = a.get_editor_property('body_setup')
        if bs:
            bs.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        sm = list(a.get_editor_property('static_materials'))
        slots = []
        for i, s in enumerate(sm):
            sn = str(s.get_editor_property('material_slot_name'))
            mi = MI.get(sn) or MI.get(sn.split('.')[0])
            if mi is None:
                # 槽名没认出来：岩石默认 A
                mi = MI['PL_Terrain'] if 'Terrain' in name else MI['PL_RockA']
                unreal.log_warning(f'[pillars] {name} 槽 {sn} 没对上')
            s.set_editor_property('material_interface', mi)
            sm[i] = s
            slots.append(sn)
        a.set_editor_property('static_materials', sm)
        eas.save_loaded_asset(a)
        log('网格', name, 'Nanite', a.get_editor_property('nanite_settings').enabled, '槽', slots)
if 'mdf' in STEPS:
    # 地形是敞口的薄片：距离场按实心算的话，片子下面（水面在里面）全算「在实体内部」，水面的 Lumen 反射一块块发黑。按双面算就没有内部了
    ses = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.EditorStaticMeshLibrary  # 命令行模式拿不到子系统，走旧接口
    for i in range(4):
        a = eas.load_asset(f'{DEST}/Meshes/SM_PL_Terrain_{i}')
        bs = ses.get_lod_build_settings(a, 0)
        bs.set_editor_property('generate_distance_field_as_if_two_sided', True)
        ses.set_lod_build_settings(a, 0, bs)
        eas.save_loaded_asset(a)
        log('地形距离场改双面', a.get_name())
log('完成', STEPS)
