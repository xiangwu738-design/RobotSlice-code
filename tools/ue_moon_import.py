"""月球基地素材导入 /Game/Env/Moon：贴图、材质、K65 网格、岩石、远景地形。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_moon_import.py [tex] [mat] [mesh] [fix] [rocks] [far]" -AllowCommandletRendering -RenderOffscreen -unattended -nosplash
不带参数 = 全做。mesh 最慢（650 万面建 Nanite），可以分段跑，每段存盘。前面几步：blender_k65_export.py → k059_pack_textures.py；moon_soil_textures.py；blender_moon_far.py
母材质：K65 借城区图的 M_CityPBR / M_CityPBR_Masked（带 Nanite / 实例化用途标记、参数齐全，着色器已经编好了）；月壤、岩石、星空、地球、玻璃是这里自己的。
"""
import json
import os
import re
import sys

import unreal

STEPS = [a for a in sys.argv[1:] if not a.startswith('-')] or ['tex', 'mat', 'mesh', 'fix', 'rocks', 'far']
K65 = 'D:/RobotSlice/Inbox/_k65/export'
SOIL = 'D:/RobotSlice/Inbox/_moon_nasa/work/soil'
STAR = 'D:/RobotSlice/Inbox/_moon_nasa/starmap/starmap_2020_8k.exr'
FAR = 'D:/RobotSlice/Inbox/_moon_nasa/work/far/far.fbx'
PH = 'D:/RobotSlice/Inbox/_polyhaven/models'
ROCKS = ['boulder_01', 'namaqualand_boulder_02', 'namaqualand_boulder_03']
DEST = '/Game/Env/Moon'
CITY = '/Game/Env/City/Materials'
eas = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
P = unreal.MaterialProperty
S = unreal.MaterialSamplerType
TC = unreal.TextureCompressionSettings
FLAT_NORMAL = '/Game/Env/Textures/T_FlatNormal'


def log(msg):
    unreal.log(f'[moon] {msg}')


def clean(s):
    return re.sub(r'[^A-Za-z0-9]', '_', s).strip('_')


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


mats_k65 = json.load(open(f'{K65}/materials_ue.json', encoding='utf-8'))

# ---------------------------------------------------------------- 贴图
if 'macro' in STEPS:
    tools.import_asset_tasks([task(f'{SOIL}/{f}.png', f'{DEST}/Textures', f) for f in ('T_Moon_NearMacro', 'T_Moon_FarMacro')])
    for f in ('T_Moon_NearMacro', 'T_Moon_FarMacro'):
        tex = eas.load_asset(f'{DEST}/Textures/{f}')
        tex.set_editor_property('compression_settings', TC.TC_MASKS)
        tex.set_editor_property('srgb', False)
        tex.set_editor_property('address_x', unreal.TextureAddress.TA_CLAMP)
        tex.set_editor_property('address_y', unreal.TextureAddress.TA_CLAMP)
        eas.save_loaded_asset(tex)
    log('明暗图导入')

if 'soil' in STEPS:
    names = [os.path.splitext(f)[0] for f in sorted(os.listdir(SOIL))]
    tools.import_asset_tasks([task(f'{SOIL}/{n}.png', f'{DEST}/Textures', n) for n in names])
    for nm in names:
        tex = eas.load_asset(f'{DEST}/Textures/{nm}')
        tex.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_WORLD)
        if nm.endswith('Macro'):
            tex.set_editor_property('compression_settings', TC.TC_MASKS)
            tex.set_editor_property('srgb', False)
            tex.set_editor_property('address_x', unreal.TextureAddress.TA_CLAMP)
            tex.set_editor_property('address_y', unreal.TextureAddress.TA_CLAMP)
        elif nm.endswith('_N'):
            tex.set_editor_property('compression_settings', TC.TC_NORMALMAP)
            tex.set_editor_property('srgb', False)
            tex.set_editor_property('flip_green_channel', True)
        elif nm.endswith('_R'):
            tex.set_editor_property('compression_settings', TC.TC_MASKS)
            tex.set_editor_property('srgb', False)
        # 世界投影的贴图：流送系统按网格 UV 估算要多大的 mip，算出来偏小，近处地面发糊 → 这几张常驻（一共几十 MB）
        if nm.endswith('_D') or nm.endswith('_N'):
            tex.set_editor_property('never_stream', True)
        eas.save_loaded_asset(tex)
    for r in ROCKS:
        for suf in ('D', 'N'):
            tex = eas.load_asset(f'{DEST}/Textures/T_Rock_{r}_{suf}')
            tex.set_editor_property('never_stream', True)
            eas.save_loaded_asset(tex)
    log(f'月壤贴图 {len(names)}')

if 'tex' in STEPS:
    tasks = []
    for f in sorted(os.listdir(f'{K65}/ue_tex')):
        tasks.append(task(f'{K65}/ue_tex/{f}', f'{DEST}/Textures', os.path.splitext(f)[0]))
    for f in sorted(os.listdir(SOIL)):
        tasks.append(task(f'{SOIL}/{f}', f'{DEST}/Textures', os.path.splitext(f)[0]))
    tasks.append(task(STAR, f'{DEST}/Textures', 'T_Moon_Starmap'))
    for r in ROCKS:
        tasks.append(task(f'{PH}/{r}/textures/{r}_diff_2k.jpg', f'{DEST}/Textures', f'T_Rock_{r}_D'))
        tasks.append(task(f'{PH}/{r}/textures/{r}_nor_gl_2k.exr', f'{DEST}/Textures', f'T_Rock_{r}_N'))
    tools.import_asset_tasks(tasks)
    n = 0
    for t in tasks:
        nm = t.destination_name
        tex = eas.load_asset(f'{DEST}/Textures/{nm}')
        if not tex:
            unreal.log_warning(f'[moon] 贴图没导进来 {t.filename}')
            continue
        tex.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_WORLD)
        if nm.endswith('Macro'):
            tex.set_editor_property('compression_settings', TC.TC_MASKS)
            tex.set_editor_property('srgb', False)
            tex.set_editor_property('address_x', unreal.TextureAddress.TA_CLAMP)
            tex.set_editor_property('address_y', unreal.TextureAddress.TA_CLAMP)
        elif nm.endswith('_N'):
            tex.set_editor_property('compression_settings', TC.TC_NORMALMAP)
            tex.set_editor_property('srgb', False)
            tex.set_editor_property('flip_green_channel', True)   # 都是 OpenGL 法线
        elif nm.endswith('_ARM') or nm.endswith('_R'):
            tex.set_editor_property('compression_settings', TC.TC_MASKS)
            tex.set_editor_property('srgb', False)
        elif nm == 'T_Moon_Starmap':
            tex.set_editor_property('compression_settings', TC.TC_HDR_COMPRESSED)
            tex.set_editor_property('srgb', False)
            tex.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_SKYBOX)
            tex.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
            tex.set_editor_property('never_stream', True)
        eas.save_loaded_asset(tex)
        n += 1
    log(f'贴图 {n} 张')


# ---------------------------------------------------------------- 材质工具
class G:
    """母材质节点小工具"""

    def __init__(self, name, **flags):
        path = f'{DEST}/Materials/{name}'
        if eas.does_asset_exist(path):
            self.m = eas.load_asset(path)
            mel.delete_all_material_expressions(self.m)
        else:
            self.m = tools.create_asset(name, f'{DEST}/Materials', unreal.Material, unreal.MaterialFactoryNew())
        for k, v in flags.items():
            self.m.set_editor_property(k, v)
        self.y = 0

    def node(self, cls, x=-600, y=None, **props):
        self.y += 120
        n = mel.create_material_expression(self.m, cls, x, self.y if y is None else y)
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    def link(self, a, out, b, inp):
        assert mel.connect_material_expressions(a, out, b, inp), f'连线失败 {out}->{inp}'

    def scalar(self, name, v):
        return self.node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=v)

    def vec(self, name, v):
        return self.node(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=unreal.LinearColor(*v, 1))

    def tex(self, name, path, st, coords=None):
        n = self.node(unreal.MaterialExpressionTextureSampleParameter2D, parameter_name=name, texture=eas.load_asset(path), sampler_type=st)
        if coords is not None:
            self.link(coords, '', n, 'UVs')
        return n

    def mul(self, a, b, ao='', bo=''):
        n = self.node(unreal.MaterialExpressionMultiply)
        self.link(a, ao, n, 'A')
        self.link(b, bo, n, 'B')
        return n

    def add(self, a, b, ao='', bo=''):
        n = self.node(unreal.MaterialExpressionAdd)
        self.link(a, ao, n, 'A')
        self.link(b, bo, n, 'B')
        return n

    def lerp(self, a, b, alpha):
        n = self.node(unreal.MaterialExpressionLinearInterpolate)
        self.link(a, '', n, 'A')
        self.link(b, '', n, 'B')
        self.link(alpha, '', n, 'Alpha')
        return n

    def custom(self, code, out_type, inputs):
        n = self.node(unreal.MaterialExpressionCustom)
        n.set_editor_property('code', code)
        n.set_editor_property('output_type', out_type)
        lst = []
        for nm in inputs:
            ci = unreal.CustomInput()
            ci.set_editor_property('input_name', nm)
            lst.append(ci)
        n.set_editor_property('inputs', lst)
        for nm, src in inputs.items():
            self.link(src, '', n, nm)
        return n

    def world_xy(self, tile_param, tile):
        wp = self.node(unreal.MaterialExpressionWorldPosition)
        mask = self.node(unreal.MaterialExpressionComponentMask, r=True, g=True, b=False, a=False)
        self.link(wp, '', mask, '')
        d = self.node(unreal.MaterialExpressionDivide)
        self.link(mask, '', d, 'A')
        self.link(self.scalar(tile_param, tile), '', d, 'B')
        return d

    def done(self):
        mel.recompile_material(self.m)
        eas.save_loaded_asset(self.m)
        return self.m


def make_mi(name, parent):
    path = f'{DEST}/Materials/{name}'
    mi = eas.load_asset(path) if eas.does_asset_exist(path) else tools.create_asset(
        name, f'{DEST}/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', parent)
    return mi


def setp(mi, kind, pname, v):
    if kind == 's':
        mel.set_material_instance_scalar_parameter_value(mi, pname, float(v))
    elif kind == 'v':
        mel.set_material_instance_vector_parameter_value(mi, pname, unreal.LinearColor(v[0], v[1], v[2], 1))
    else:
        mel.set_material_instance_texture_parameter_value(mi, pname, eas.load_asset(f'{DEST}/Textures/{v}'))


# ---------------------------------------------------------------- 材质
# K65：白板在硬太阳下压一点；金属度压低（真空里没有天空可反射，高金属度的东西全黑）
K65_TUNE = {
    'KB3D_LNB_PanelsWhiteA': {'tint': (0.82, 0.82, 0.82), 'metal': 0.15}, 'KB3D_LNB_PanelsWhiteB': {'tint': (0.82, 0.82, 0.82), 'metal': 0.15},
    'KB3D_LNB_PanelsWhiteC': {'tint': (0.82, 0.82, 0.82), 'metal': 0.15}, 'KB3D_LNB_ConcreteWhite': {'tint': (0.8, 0.8, 0.8)},
    'KB3D_LNB_MetalGray': {'metal': 0.3, 'tint': (1.15, 1.15, 1.15)}, 'KB3D_LNB_MetalGrayBright': {'metal': 0.35}, 'KB3D_LNB_MetalRough': {'metal': 0.3, 'tint': (1.1, 1.1, 1.1)},
    'KB3D_LNB_MetalShiny': {'metal': 0.45}, 'KB3D_LNB_PanelsMetal': {'metal': 0.3}, 'KB3D_LNB_PanelsDark': {'metal': 0.3, 'tint': (1.2, 1.2, 1.2)},
    'KB3D_LNB_SolarArrayTrim': {'metal': 0.4}, 'KB3D_LNB_Reflectors': {'metal': 0.5}, 'KB3D_LNB_GlassTinted': {'rough': 0.14, 'metal': 0.55, 'tint': (0.45, 0.5, 0.6)},
    'KB3D_LNB_PaintYellow': {'metal': 0.2}, 'KB3D_LNB_PaintRed': {'metal': 0.2}, 'KB3D_LNB_PaintBlue': {'metal': 0.2}, 'KB3D_LNB_PipesTrim': {'metal': 0.3},
}

if 'mat' in STEPS:
    master = eas.load_asset(f'{CITY}/M_CityPBR')
    masked = eas.load_asset(f'{CITY}/M_CityPBR_Masked')
    n = 0
    for mname, e in mats_k65.items():
        if 'GlassBlueTransparent' in mname:
            continue
        mi = make_mi('MI_Moon_' + clean(mname), masked if e['masked'] else master)
        tex = e['tex']
        tune = K65_TUNE.get(mname, {})
        if 'D' in tex:
            setp(mi, 't', 'BaseColorTex', tex['D'])
        else:
            setp(mi, 'v', 'Tint', e['color'])
        if 'N' in tex:
            setp(mi, 't', 'NormalTex', tex['N'])
        if 'ARM' in tex:
            setp(mi, 't', 'ARMTex', tex['ARM'])
            setp(mi, 's', 'MetalMul', tune.get('metal', 0.4))
        else:
            setp(mi, 's', 'RoughMul', 0.0)
            setp(mi, 's', 'RoughAdd', e['rough_v'])
            setp(mi, 's', 'MetalMul', 0.0)
            setp(mi, 's', 'MetalAdd', e['metal_v'])
        if 'rough' in tune:
            setp(mi, 's', 'RoughMul', 0.0)
            setp(mi, 's', 'RoughAdd', tune['rough'])
        if 'E' in tex:
            setp(mi, 't', 'EmissiveTex', tex['E'])
            setp(mi, 's', 'EmissiveStrength', 4.0)
        if 'tint' in tune:
            setp(mi, 'v', 'Tint', tune['tint'])
        eas.save_loaded_asset(mi)
        n += 1
    log(f'K65 材质实例 {n}')

    # 月壤：世界投影、四层（细颗粒、小尺度、中尺度、大尺度明暗图）。贴图都做过高通（moon_soil_textures.py），均值 0.45 sRGB ≈ 0.17 线性；每层转不同角度，避免一格一格对齐
    g = G('M_Moon_Ground')
    T = f'{DEST}/Textures'

    def rot_uv(tile_param, tile, deg):
        uv = g.world_xy(tile_param, tile)
        import math
        c, s_ = math.cos(math.radians(deg)), math.sin(math.radians(deg))
        return g.custom(f'return float2(UV.x * {c:.5f} - UV.y * {s_:.5f}, UV.x * {s_:.5f} + UV.y * {c:.5f});', unreal.CustomMaterialOutputType.CMOT_FLOAT2, {'UV': uv})

    uv_f, uv_a, uv_b, uv_c = rot_uv('TileF', 230.0, 17.0), rot_uv('TileA', 900.0, 0.0), rot_uv('TileB', 4300.0, 31.0), rot_uv('TileC', 23000.0, 67.0)
    df = g.tex('FineD', f'{T}/T_Moon_moon_dusted_01_D', S.SAMPLERTYPE_COLOR, uv_f)
    da = g.tex('DustD', f'{T}/T_Moon_moon_dusted_03_D', S.SAMPLERTYPE_COLOR, uv_a)
    db = g.tex('SoftD', f'{T}/T_Moon_moon_01_D', S.SAMPLERTYPE_COLOR, uv_b)
    dc = g.tex('MacroD', f'{T}/T_Moon_moon_meteor_01_D', S.SAMPLERTYPE_COLOR, uv_c)
    nf = g.tex('FineN', f'{T}/T_Moon_moon_dusted_01_N', S.SAMPLERTYPE_NORMAL, uv_f)
    na = g.tex('DustN', f'{T}/T_Moon_moon_dusted_03_N', S.SAMPLERTYPE_NORMAL, uv_a)
    nb = g.tex('SoftN', f'{T}/T_Moon_moon_01_N', S.SAMPLERTYPE_NORMAL, uv_b)
    # 大尺度明暗图（真实地形算的，见 moon_terrain_bake.py）：世界坐标按参数换算成 UV
    wp_ = g.node(unreal.MaterialExpressionWorldPosition)
    muv = g.custom('return (P.xy - float2(OX, OY)) / float2(SX, SY);', unreal.CustomMaterialOutputType.CMOT_FLOAT2,
                   {'P': wp_, 'OX': g.scalar('MacroX', -100800.0), 'OY': g.scalar('MacroY', -100800.0), 'SX': g.scalar('MacroW', 201600.0), 'SY': g.scalar('MacroH', 201600.0)})
    mac = g.tex('MacroMap', f'{T}/T_Moon_NearMacro', S.SAMPLERTYPE_MASKS, muv)
    col = g.custom('float3 c = A; c *= pow(F / 0.17, FAmt); c *= pow(B / 0.17, BAmt); c *= pow(C / 0.17, CAmt); float l = dot(c, float3(0.3, 0.59, 0.11)); c = lerp(c, l.xxx, Desat);'
                   ' float k = lerp(1.0, M.r * 2.0, MacroAmt) * lerp(1.0, 0.6 + M.b * 0.8, CavAmt); return c * k * Tint;', unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                   {'A': da, 'F': df, 'B': db, 'C': dc, 'M': mac, 'Desat': g.scalar('Desat', 0.93), 'Tint': g.vec('Tint', (1.42, 1.40, 1.36)), 'MacroAmt': g.scalar('MacroAmt', 1.0), 'CavAmt': g.scalar('CavAmt', 0.7),
                    'FAmt': g.scalar('FineAmt', 1.0), 'BAmt': g.scalar('SoftAmt', 1.0), 'CAmt': g.scalar('BigAmt', 1.0)})
    # 法线：三层相加（细颗粒、小尺度、中尺度），强度可调
    nor = g.custom('float3 a = lerp(float3(0,0,1), A, SA); float3 b = lerp(float3(0,0,1), B, SB); float3 f = lerp(float3(0,0,1), F, SF); return normalize(float3(a.xy + b.xy + f.xy, a.z * b.z * f.z));',
                   unreal.CustomMaterialOutputType.CMOT_FLOAT3, {'A': na, 'B': nb, 'F': nf, 'SA': g.scalar('DetailNormal', 1.1), 'SB': g.scalar('MidNormal', 1.4), 'SF': g.scalar('FineNormal', 0.9)})
    mel.connect_material_property(col, '', P.MP_BASE_COLOR)
    mel.connect_material_property(nor, '', P.MP_NORMAL)
    mel.connect_material_property(g.scalar('Rough', 0.95), '', P.MP_ROUGHNESS)
    mel.connect_material_property(g.scalar('Spec', 0.06), '', P.MP_SPECULAR)     # 月壤不往前散射（背光看不该发亮）
    g.m.set_editor_property('used_with_nanite', True)
    m_ground = g.done()
    eas.save_loaded_asset(make_mi('MI_Moon_Ground', m_ground))
    far_mi = make_mi('MI_Moon_FarGround', m_ground)
    setp(far_mi, 's', 'DetailNormal', 0.5)
    setp(far_mi, 's', 'FineNormal', 0.0)
    setp(far_mi, 's', 'FineAmt', 0.0)
    setp(far_mi, 's', 'TileA', 4000.0)
    setp(far_mi, 's', 'TileB', 21000.0)
    setp(far_mi, 's', 'TileC', 90000.0)
    setp(far_mi, 't', 'MacroMap', 'T_Moon_FarMacro')
    # 远景网格范围：far.npz 的 x0, y0（米），1507×2470 格 × 16 米
    setp(far_mi, 's', 'MacroX', -996400.0)
    setp(far_mi, 's', 'MacroY', -3321200.0)
    setp(far_mi, 's', 'MacroW', 1507 * 1600.0)
    setp(far_mi, 's', 'MacroH', 2470 * 1600.0)
    eas.save_loaded_asset(far_mi)
    fill_mi = make_mi('MI_Moon_FillGround', m_ground)
    setp(fill_mi, 's', 'DetailNormal', 0.5)
    setp(fill_mi, 's', 'FineNormal', 0.0)
    setp(fill_mi, 's', 'FineAmt', 0.0)
    setp(fill_mi, 's', 'TileA', 4000.0)
    setp(fill_mi, 's', 'TileB', 21000.0)
    setp(fill_mi, 's', 'TileC', 90000.0)
    setp(fill_mi, 's', 'MacroAmt', 0.0)
    setp(fill_mi, 's', 'CavAmt', 0.0)
    eas.save_loaded_asset(fill_mi)
    log('月壤材质')

    # 岩石：Poly Haven 底色去色相 + 法线，当月岩
    g = G('M_Moon_Rock')
    d_ = g.tex('BaseColorTex', f'{T}/T_Rock_boulder_01_D', S.SAMPLERTYPE_COLOR)
    n_ = g.tex('NormalTex', f'{T}/T_Rock_boulder_01_N', S.SAMPLERTYPE_NORMAL)
    col = g.custom('float l = dot(A, float3(0.3, 0.59, 0.11)); return lerp(A, l.xxx, Desat) * Tint;', unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                   {'A': d_, 'Desat': g.scalar('Desat', 0.92), 'Tint': g.vec('Tint', (0.9, 0.88, 0.85))})
    mel.connect_material_property(col, '', P.MP_BASE_COLOR)
    mel.connect_material_property(n_, 'RGB', P.MP_NORMAL)
    mel.connect_material_property(g.scalar('Rough', 0.9), '', P.MP_ROUGHNESS)
    mel.connect_material_property(g.scalar('Spec', 0.3), '', P.MP_SPECULAR)
    g.m.set_editor_property('used_with_nanite', True)
    g.m.set_editor_property('used_with_instanced_static_meshes', True)
    m_rock = g.done()
    for r in ROCKS:
        mi = make_mi(f'MI_Moon_Rock_{r}', m_rock)
        setp(mi, 't', 'BaseColorTex', f'T_Rock_{r}_D')
        setp(mi, 't', 'NormalTex', f'T_Rock_{r}_N')
        eas.save_loaded_asset(mi)

    # 星空：大球内壁（无光照），按世界方向取经纬图，没有 mip 所以没有接缝
    g = G('M_Moon_Sky', shading_model=unreal.MaterialShadingModel.MSM_UNLIT, two_sided=True, is_sky=True)
    wp = g.node(unreal.MaterialExpressionWorldPosition)
    uv = g.custom('float3 d = normalize(P); return float2(atan2(d.y, d.x) / 6.2831853 + 0.5, acos(clamp(d.z, -1.0, 1.0)) / 3.14159265);',
                  unreal.CustomMaterialOutputType.CMOT_FLOAT2, {'P': wp})
    star = g.tex('Stars', f'{T}/T_Moon_Starmap', S.SAMPLERTYPE_LINEAR_COLOR, uv)
    em = g.mul(star, g.scalar('Gain', 1.5), 'RGB', '')
    mel.connect_material_property(em, '', P.MP_EMISSIVE_COLOR)
    g.done()
    log('星空材质')

    # 地球：无光照球，按物体空间方向取经纬图；背日面暗、边缘一圈大气蓝光（数字都能调）。太阳方向是参数，建图时写入
    g = G('M_Moon_Earth', shading_model=unreal.MaterialShadingModel.MSM_UNLIT)
    n_ws = g.node(unreal.MaterialExpressionVertexNormalWS)
    tr = g.node(unreal.MaterialExpressionTransform, transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD,
                transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
    g.link(n_ws, '', tr, '')
    uv = g.custom('float3 d = normalize(N); return float2(atan2(-d.y, d.x) / 6.2831853 + 0.5, acos(clamp(d.z, -1.0, 1.0)) / 3.14159265);',
                  unreal.CustomMaterialOutputType.CMOT_FLOAT2, {'N': tr})   # -y：UE 是左手系，不取反东西方向是反的
    et = g.tex('EarthTex', f'{T}/T_Moon_Earth', S.SAMPLERTYPE_COLOR, uv)
    cam = g.node(unreal.MaterialExpressionCameraVectorWS)
    em = g.custom('float3 n = normalize(N); float3 l = normalize(L); float lit = saturate(dot(n, l) * 1.6 + 0.12); float rim = pow(1.0 - saturate(dot(n, normalize(V))), 3.5);'
                  ' float3 atm = float3(0.28, 0.5, 1.0) * rim * 0.9; float3 oc = T + float3(0.004, 0.012, 0.03); return (oc * lit + atm * saturate(lit * 2.0)) * Gain;',
                  unreal.CustomMaterialOutputType.CMOT_FLOAT3, {'N': n_ws, 'L': g.vec('SunDir', (-0.62, 0.72, -0.1)), 'V': cam, 'T': et, 'Gain': g.scalar('Gain', 5.5)})
    mel.connect_material_property(em, '', P.MP_EMISSIVE_COLOR)
    g.done()
    log('地球材质')

    # 穹顶玻璃：半透明无光照，中间很透、掠射角发蓝；太阳方向给一点高光。不开 Nanite
    g = G('M_Moon_Glass', shading_model=unreal.MaterialShadingModel.MSM_UNLIT, blend_mode=unreal.BlendMode.BLEND_TRANSLUCENT, two_sided=True)
    cam = g.node(unreal.MaterialExpressionCameraVectorWS)
    nw = g.node(unreal.MaterialExpressionPixelNormalWS)
    fres = g.custom('return pow(1.0 - saturate(abs(dot(normalize(N), normalize(V)))), 3.0);', unreal.CustomMaterialOutputType.CMOT_FLOAT1, {'N': nw, 'V': cam})
    em = g.custom('float3 r = reflect(-normalize(V), normalize(N)); float s = pow(saturate(dot(r, normalize(L))), 900.0) * 60.0 + pow(saturate(dot(r, normalize(L))), 40.0) * 0.35;'
                  ' return Tint * (0.25 + F * 2.0) + s;', unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                  {'V': cam, 'N': nw, 'L': g.vec('SunDir', (-0.45, 0.68, 0.5)), 'F': fres, 'Tint': g.vec('Tint', (0.05, 0.13, 0.3))})
    op = g.custom('return saturate(Base + F * 0.55);', unreal.CustomMaterialOutputType.CMOT_FLOAT1, {'Base': g.scalar('Opacity', 0.1), 'F': fres})
    mel.connect_material_property(em, '', P.MP_EMISSIVE_COLOR)
    mel.connect_material_property(op, '', P.MP_OPACITY)
    glass = g.done()
    eas.save_loaded_asset(make_mi('MI_Moon_KB3D_LNB_GlassBlueTransparent', glass))
    log('玻璃材质')


# ---------------------------------------------------------------- 网格
def import_fbx(fbx, dest, name=None, scale=1.0):
    ui = unreal.FbxImportUI()
    ui.set_editor_property('import_mesh', True)
    ui.set_editor_property('import_as_skeletal', False)
    ui.set_editor_property('import_materials', False)
    ui.set_editor_property('import_textures', False)
    ui.set_editor_property('import_animations', False)
    ui.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_STATIC_MESH)
    smd = ui.static_mesh_import_data
    smd.set_editor_property('combine_meshes', name is not None)
    smd.set_editor_property('auto_generate_collision', False)
    smd.set_editor_property('generate_lightmap_u_vs', False)
    smd.set_editor_property('build_nanite', True)
    if scale != 1.0:
        smd.set_editor_property('import_uniform_scale', scale)
    tools.import_asset_tasks([task(fbx, dest, name, ui)])


def setup_meshes():
    n, gray = 0, []
    for path in eas.list_assets(f'{DEST}/Meshes', recursive=False, include_folder=False):
        a = eas.load_asset(path)
        if not isinstance(a, unreal.StaticMesh) or not a.get_name().startswith('SM_K65_'):
            continue
        name = a.get_name()
        if name.endswith('_Glass'):         # 透明材质不能进 Nanite
            nan = a.get_editor_property('nanite_settings')
            if nan.enabled:
                nan.enabled = False
                a.set_editor_property('nanite_settings', nan)
        bs = a.get_editor_property('body_setup')
        if bs:
            bs.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        sm = list(a.get_editor_property('static_materials'))
        for i, s in enumerate(sm):
            slot = str(s.get_editor_property('material_slot_name'))
            hit = None
            for cand in (slot, slot.split('.')[0]):
                p = f'{DEST}/Materials/MI_Moon_{clean(cand)}'
                if eas.does_asset_exist(p):
                    hit = eas.load_asset(p)
                    break
            if hit:
                s.set_editor_property('material_interface', hit)
                sm[i] = s
            else:
                gray.append((name, slot))
        try:
            a.set_editor_property('static_materials', sm, unreal.PropertyAccessChangeNotifyMode.NEVER)
        except TypeError:
            a.set_editor_property('static_materials', sm)
        a.modify()
        eas.save_loaded_asset(a, False)
        n += 1
    log(f'K65 网格设置完 {n}；没配上材质的槽 {gray[:10]}')


if 'mesh' in STEPS:
    import_fbx(f'{K65}/k65.fbx', f'{DEST}/Meshes')
    log('K65 网格导入完')
    if 'fix' not in STEPS:
        STEPS.append('fix')

if 'fix' in STEPS:
    setup_meshes()

if 'rocks' in STEPS:
    for r in ROCKS:
        import_fbx(f'{PH}/{r}/{r}_2k.fbx', f'{DEST}/Rocks', f'SM_Rock_{r}')
        m = eas.load_asset(f'{DEST}/Rocks/SM_Rock_{r}')
        if not m:
            unreal.log_warning(f'[moon] 岩石没导进来 {r}')
            continue
        bs = m.get_editor_property('body_setup')
        if bs:
            bs.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        sm = list(m.get_editor_property('static_materials'))
        for i, s in enumerate(sm):
            s.set_editor_property('material_interface', eas.load_asset(f'{DEST}/Materials/MI_Moon_Rock_{r}'))
            sm[i] = s
        m.set_editor_property('static_materials', sm)
        b = m.get_bounding_box()
        log(f'岩石 {r} 尺寸 {b.max - b.min} 面 {m.get_num_triangles(0)}')
        eas.save_loaded_asset(m, False)

if 'fill' in STEPS:
    import_fbx('D:/RobotSlice/Inbox/_moon_nasa/work/far/fill.fbx', f'{DEST}/Meshes', 'SM_Moon_FarFill')
    m = eas.load_asset(f'{DEST}/Meshes/SM_Moon_FarFill')
    sm = list(m.get_editor_property('static_materials'))
    for i, s_ in enumerate(sm):
        s_.set_editor_property('material_interface', eas.load_asset(f'{DEST}/Materials/MI_Moon_FillGround'))
        sm[i] = s_
    m.set_editor_property('static_materials', sm)
    eas.save_loaded_asset(m, False)
    log(f'补网格导入 {m.get_bounding_box().min} {m.get_bounding_box().max}')

if 'far' in STEPS:
    import_fbx(FAR, f'{DEST}/Meshes', 'SM_Moon_FarTerrain')
    m = eas.load_asset(f'{DEST}/Meshes/SM_Moon_FarTerrain')
    sm = list(m.get_editor_property('static_materials'))
    for i, s in enumerate(sm):
        s.set_editor_property('material_interface', eas.load_asset(f'{DEST}/Materials/MI_Moon_FarGround'))
        sm[i] = s
    m.set_editor_property('static_materials', sm)
    eas.save_loaded_asset(m, False)
    b = m.get_bounding_box()
    log(f'远景地形 {b.min} ~ {b.max}')

log('完成 ' + ' '.join(STEPS))
