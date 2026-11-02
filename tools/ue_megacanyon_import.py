"""原版峡谷图（L_MegaCanyon）的资源导入 → /Game/Env/MegaCanyon（和以前的 /Game/Env/Canyon 是两回事）。可重复跑。
先跑 Blender 侧 tools/blender_megacanyon_export.py（FBX 写到 Inbox/_megacanyon/export）。
面板贴图借用梦核旷野图导好的 /Game/Env/Dreamfield/Textures/T_DW_Panel_C / _N（原包 Texture.jpg，只读引用，不改）；
行星、云的材质实例挂在梦核旷野的 M_DF_Planet / M_DF_Cloud 下面（只建新实例，不改母材质），面板和天空是这张图自己的母材质，
L_Dreamfield 用到的东西一个都不动。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_megacanyon_import.py [tex] [mat] [mesh]" -AllowCommandletRendering -RenderOffscreen -unattended
"""
import re
import sys

import unreal

SRC = 'D:/RobotSlice/Inbox/_megacanyon/export'
SA = 'D:/RobotSlice/SourceAssets/megacanyon'
DEST = '/Game/Env/MegaCanyon'
DF = '/Game/Env/Dreamfield'
STEPS = [a for a in sys.argv[1:] if a in ('tex', 'mat', 'mesh')] or ['tex', 'mat', 'mesh']
eas = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
P = unreal.MaterialProperty
log = lambda *a: unreal.log('[megacanyon] ' + ' '.join(str(x) for x in a))


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


if 'tex' in STEPS:
    tools.import_asset_tasks([task(f'{SA}/Texture_Planet_03.jpg', f'{DEST}/Textures', 'T_MC_Moon')])


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
    for flag in ('used_with_nanite', 'used_with_instanced_static_meshes'):
        m.set_editor_property(flag, True)
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
        unreal.log_warning(f'[megacanyon] 连不上 {a.get_name()}.{out} → {b.get_name()}.{inp}')


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


# 原包墙材质：Texture.jpg 当底色 + 凹凸，墙 5 米一格、甲板 10 米一格，往 XZ 平面平铺（PlanarXZ = 1）
PANEL = {'Tile': 500.0, 'Var': 0.0, 'CavityDark': 0.35, 'FadeH': 100000.0, 'BumpScale': 2.4, 'GlowDensity': 0.0, 'UnderDark': 1.0,
         'PlanarXZ': 1.0, 'Rough': 0.55, 'Spec': 0.5}
PANEL_V = {'Tint': (0.86, 0.88, 0.92), 'GlowColor': (3.0, 2.6, 2.2)}
SUN_TO = (-0.724, -0.405, 0.558)        # UE 里指向太阳的方向（原包太阳光线 Blender (0.724, -0.405, -0.558) → UE (x, -y, z) 取反）

if 'mat' in STEPS:
    m = new_material('M_MC_Panel')
    m.set_editor_property('tangent_space_normal', False)
    wp = node(m, unreal.MaterialExpressionWorldPosition, -1500, 0)
    nn = node(m, unreal.MaterialExpressionVertexNormalWS, -1500, 100)
    pn = ['Tile', 'Tint', 'Var', 'CavityDark', 'FadeH', 'BumpScale', 'GlowColor', 'GlowDensity', 'UnderDark', 'PlanarXZ']
    params = {}
    y = 300
    for k in pn:
        params[k] = vec(m, k, PANEL_V[k], -1500, y) if k in PANEL_V else scalar(m, k, PANEL[k], -1500, y)
        y += 90
    cu = custom(m, hlsl('megacanyon_panel_uv.hlsl'), ['WP', 'N', 'Tile', 'PlanarXZ'], 'PanelUV', unreal.CustomMaterialOutputType.CMOT_FLOAT2, -1150, 0)
    link(wp, '', cu, 'WP')
    link(nn, '', cu, 'N')
    link(params['Tile'], '', cu, 'Tile')
    link(params['PlanarXZ'], '', cu, 'PlanarXZ')
    tc = node(m, unreal.MaterialExpressionTextureSampleParameter2D, -850, -150)
    tc.set_editor_property('parameter_name', 'PanelColor')
    tc.set_editor_property('texture', eas.load_asset(f'{DF}/Textures/T_DW_Panel_C'))
    tn = node(m, unreal.MaterialExpressionTextureSampleParameter2D, -850, 150)
    tn.set_editor_property('parameter_name', 'PanelNormal')
    tn.set_editor_property('texture', eas.load_asset(f'{DF}/Textures/T_DW_Panel_N'))
    tn.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    link(cu, '', tc, 'UVs')
    link(cu, '', tn, 'UVs')
    code = hlsl('megacanyon_panel.hlsl')
    names = ['WP', 'N', 'C', 'NT', 'Mode'] + pn
    outs = {}
    for mode, desc, yy in ((0.0, 'PanelBase', 0), (1.0, 'PanelGlow', 300), (2.0, 'PanelNormal', 600)):
        c_ = custom(m, code, names, desc, unreal.CustomMaterialOutputType.CMOT_FLOAT3, -400, yy)
        link(wp, '', c_, 'WP')
        link(nn, '', c_, 'N')
        link(tc, 'RGB', c_, 'C')
        link(tn, 'RGB', c_, 'NT')
        mc = node(m, unreal.MaterialExpressionConstant, -650, yy + 100)
        mc.set_editor_property('r', mode)
        link(mc, '', c_, 'Mode')
        for k in pn:
            link(params[k], '', c_, k)
        outs[desc] = c_
    mel.connect_material_property(outs['PanelBase'], '', P.MP_BASE_COLOR)
    mel.connect_material_property(outs['PanelGlow'], '', P.MP_EMISSIVE_COLOR)
    mel.connect_material_property(outs['PanelNormal'], '', P.MP_NORMAL)
    mel.connect_material_property(scalar(m, 'Rough', PANEL['Rough'], -400, 900), '', P.MP_ROUGHNESS)
    mel.connect_material_property(scalar(m, 'Spec', PANEL['Spec'], -400, 1000), '', P.MP_SPECULAR)
    finish(m)
    make_mi('MI_MC_Wall', m)
    make_mi('MI_MC_Deck', m, {'Tile': 1000.0})
    # 远景大平面的甲板顶：原包也是 10 米一格往 XZ 平面铺，顶面会拉成一条条横纹；这里改成三向投影的面板（远看是一格格的甲板）
    make_mi('MI_MC_Plane', m, {'Tile': 1000.0, 'PlanarXZ': 0.0, 'Var': 0.06})
    # 楼柱群背后的衬墙、远处补的沟壁：原包这里是空的（体积雾里看不见），压暗一点
    # 大块平面（衬墙、补的沟壁）用三向投影：往 XZ 平铺时朝沟的大平面只剩一条条横纹，透视下拉成放射状
    make_mi('MI_MC_Backing', m, {'CavityDark': 0.5, 'PlanarXZ': 0.0}, {'Tint': (0.5, 0.55, 0.64)})

    # 天空：竖向渐变（原包世界背景是纯色深石板蓝，地平线发白靠体积雾）
    m = new_material('M_MC_Sky')
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided', True)
    wp = node(m, unreal.MaterialExpressionWorldPosition, -900, 0)
    cam = node(m, unreal.MaterialExpressionCameraPositionWS, -900, 100)
    sp = ['Zenith', 'Horizon', 'Below', 'Curve']
    sv = {'Zenith': (0.07, 0.15, 0.48), 'Horizon': (0.9, 1.15, 1.6), 'Below': (0.9, 1.1, 1.5)}
    cs = custom(m, hlsl('megacanyon_sky.hlsl'), ['WP', 'CamPos'] + sp, 'Sky', unreal.CustomMaterialOutputType.CMOT_FLOAT3, -400, 0)
    link(wp, '', cs, 'WP')
    link(cam, '', cs, 'CamPos')
    y = 200
    for k in sp:
        link(vec(m, k, sv[k], -900, y) if k in sv else scalar(m, k, 0.45, -900, y), '', cs, k)
        y += 90
    mel.connect_material_property(cs, '', P.MP_EMISSIVE_COLOR)
    finish(m)
    make_mi('MI_MC_Sky', m)

    # 行星（原包 Scene_03 的行星材质贴图节点是空的 → Cycles 里显示成品红色，渲染图 1、3 的粉紫行星就是这么来的；
    # 渲染图 2 和概念图是带花纹的粉红行星）→ 用峡谷包的 Texture_Planet_02 花纹染粉紫，受光方向对着原包太阳
    make_mi('MI_MC_Planet', eas.load_asset(f'{DF}/Materials/M_DF_Planet'), {'Glow': 2.2, 'Veil': 0.3},
            {'Tint': (1.0, 0.55, 0.85), 'RimColor': (1.2, 0.7, 1.3), 'Lit': SUN_TO, 'VeilColor': (1.0, 1.0, 1.5)})
    make_mi('MI_MC_Moon', eas.load_asset(f'{DF}/Materials/M_DF_Planet'), {'Glow': 2.2, 'Veil': 0.1},
            {'Tint': (1.0, 0.8, 0.6), 'RimColor': (0.4, 0.3, 0.3), 'Lit': SUN_TO}, {'Tex': f'{DEST}/Textures/T_MC_Moon'})
    make_mi('MI_MC_Cloud', eas.load_asset(f'{DF}/Materials/M_DF_Cloud'), {'SelfLight': 0.35}, {'Color': (0.8, 0.82, 0.88)})


def import_fbx(fbx, dest, name):
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
    smd.set_editor_property('build_nanite', True)
    tools.import_asset_tasks([task(fbx, dest, name=name, options=ui)])


if 'mesh' in STEPS:
    MD = f'{DEST}/Meshes'
    MESHES = {'SM_MC_Walls': ('mc_walls.fbx', 'MI_MC_Wall'), 'SM_MC_DeckTile': ('mc_decktile.fbx', 'MI_MC_Deck'),
              'SM_MC_Plane': ('mc_plane.fbx', 'MI_MC_Plane'), 'SM_MC_Clouds': ('mc_clouds.fbx', 'MI_MC_Cloud'),
              'SM_MC_Planet': ('mc_planet.fbx', 'MI_MC_Planet'), 'SM_MC_Moon': ('mc_moon.fbx', 'MI_MC_Moon'),
              'SM_MC_Bits': ('mc_bits.fbx', 'MI_MC_Wall')}
    for name, (fbx, _) in MESHES.items():
        import_fbx(f'{SRC}/{fbx}', MD, name)
    for name, (_, mi_name) in MESHES.items():
        a = eas.load_asset(f'{MD}/{name}')
        if not a:
            unreal.log_warning(f'[megacanyon] 缺网格 {name}')
            continue
        nan = a.get_editor_property('nanite_settings')
        if not nan.enabled:
            nan.enabled = True
            a.set_editor_property('nanite_settings', nan)
        bs = a.get_editor_property('body_setup')
        if bs:   # 全部不碰撞：碰撞是搭图时另摆的方块
            bs.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
        mi = eas.load_asset(f'{DEST}/Materials/{mi_name}')
        sm = list(a.get_editor_property('static_materials'))
        for i, s in enumerate(sm):
            s.set_editor_property('material_interface', mi)
            sm[i] = s
        a.set_editor_property('static_materials', sm)
        eas.save_loaded_asset(a)
        log('网格', name, 'Nanite', a.get_editor_property('nanite_settings').enabled)
log('完成', STEPS)
