"""苏联板楼图 L_Soviet 的资源导入：贴图、母材质（带顶面积雪）、材质实例、K62 楼、小品。资源都在 /Game/Env/Soviet。
前面几步：k62_export.py → k059_pack_textures.py；soviet_props_export.py → soviet_props_pack.py；soviet_plan.py（地面遮罩）
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_soviet_import.py 步骤..." -AllowCommandletRendering -RenderOffscreen
步骤：tex mat k62:SM,MD（或 k62:SM_A,MD_F）k62:LG props fix。一次别太多（UE 锁一次占 10 分钟以内）。
"""
import glob
import json
import os
import re
import sys

import unreal

STEPS = [a for a in sys.argv[1:] if not a.startswith('-')]
K62 = 'D:/RobotSlice/Inbox/_k62/export'
PROPS = 'D:/RobotSlice/Inbox/_soviet_props/export'
DEST = '/Game/Env/Soviet'
eas = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
P = unreal.MaterialProperty
S = unreal.MaterialSamplerType
FLAT_NORMAL = '/Game/Env/Textures/T_FlatNormal'
# 天空：Poly Haven kloofendal_misty_morning_puresky（薄雾漫射天，CC0）。cloudy06_EXR 其实是蓝天 + 大太阳，做不了阴天
SKY_EXR = 'D:/RobotSlice/Inbox/科幻峡谷blender源文件/Files/3D/kloofendal_misty_morning_puresky_4k.exr'


def log(msg):
    unreal.log(f'[soviet] {msg}')


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


# ---------------------------------------------------------------- 贴图
if 'sky' in STEPS:
    t = task(SKY_EXR, f'{DEST}/Textures', 'T_SkyMisty')
    tools.import_asset_tasks([t])
    tex = eas.load_asset(f'{DEST}/Textures/T_SkyMisty')
    tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_HDR_COMPRESSED)
    tex.set_editor_property('srgb', False)
    tex.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_WORLD)
    eas.save_loaded_asset(tex)
    log('天空贴图已导入 ' + tex.get_class().get_name())
if 'tex' in STEPS:
    files = sorted(glob.glob(f'{K62}/ue_tex/*.png')) + sorted(glob.glob(f'{PROPS}/ue_tex/*.png')) + ['D:/RobotSlice/Inbox/_k62/T_SV_GroundMask.png', SKY_EXR]
    files = [f.replace(chr(92), '/') for f in files]
    tasks = []
    for f in files:
        stem = os.path.splitext(os.path.basename(f))[0]
        tasks.append(task(f, f'{DEST}/Textures', 'T_SkyMisty' if f == SKY_EXR else stem))
    tools.import_asset_tasks(tasks)
    n = 0
    for t in tasks:
        nm = t.destination_name
        tex = eas.load_asset(f'{DEST}/Textures/{nm}')
        if not tex:
            unreal.log_warning(f'[soviet] 贴图没导进来 {t.filename}')
            continue
        if nm.endswith('_N'):
            tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
            tex.set_editor_property('srgb', False)
            tex.set_editor_property('flip_green_channel', True)   # 这些包都是 OpenGL 法线
        elif nm.endswith('_ARM'):
            tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_MASKS)
            tex.set_editor_property('srgb', False)
        elif nm == 'T_SV_GroundMask':
            tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_MASKS)
            tex.set_editor_property('srgb', False)
            tex.set_editor_property('address_x', unreal.TextureAddress.TA_CLAMP)
            tex.set_editor_property('address_y', unreal.TextureAddress.TA_CLAMP)
        elif nm == 'T_SkyMisty':
            tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_HDR_COMPRESSED)
            tex.set_editor_property('srgb', False)
            tex.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)   # 经纬度投影接缝处导数突变，有 mip 会出一条缝
        tex.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_WORLD)
        eas.save_loaded_asset(tex)
        n += 1
    log(f'贴图 {n} 张')


if 'mask' in STEPS:   # 只重导地面遮罩（改布局后）
    t = task('D:/RobotSlice/Inbox/_k62/T_SV_GroundMask.png', f'{DEST}/Textures', 'T_SV_GroundMask')
    tools.import_asset_tasks([t])
    tex = eas.load_asset(f'{DEST}/Textures/T_SV_GroundMask')
    tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_MASKS)
    tex.set_editor_property('srgb', False)
    tex.set_editor_property('address_x', unreal.TextureAddress.TA_CLAMP)
    tex.set_editor_property('address_y', unreal.TextureAddress.TA_CLAMP)
    tex.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_WORLD)
    eas.save_loaded_asset(tex)
    log('地面遮罩已更新')


if 'cap' in STEPS:   # 贴图预算：冬季包 4K 的贴图限到 2K（机体视角下看不出来），天空 4K 限到 2K。8G 显存的贴图池只有 700 MB
    n = 0
    for path in eas.list_assets('/Game/ANGRY_MESH/NaturePack_WinterEnvironment/Textures', recursive=True, include_folder=False) + [f'{DEST}/Textures/T_SkyMisty']:
        ad = eas.find_asset_data(path)
        if str(ad.asset_class_path.asset_name) != 'Texture2D':
            continue
        dims = str(ad.get_tag_value('Dimensions') or '')
        try:
            w = max(int(v) for v in dims.split('x')[:2])
        except ValueError:
            continue
        if w <= 2048:
            continue
        t = eas.load_asset(path)
        t.set_editor_property('max_texture_size', 2048)
        eas.save_loaded_asset(t, False)
        n += 1
    log(f'限到 2K 的贴图 {n} 张')


# ---------------------------------------------------------------- 母材质
def new_material(name):
    path = f'{DEST}/Materials/{name}'
    if eas.does_asset_exist(path):
        m = eas.load_asset(path)
        mel.delete_all_material_expressions(m)
    else:
        m = tools.create_asset(name, f'{DEST}/Materials', unreal.Material, unreal.MaterialFactoryNew())
    return m


class G:
    """连线小工具"""

    def __init__(self, m):
        self.m = m

    def node(self, cls, x, y, **props):
        n = mel.create_material_expression(self.m, cls, x, y)
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    def link(self, a, out, b, inp):
        assert mel.connect_material_expressions(a, out, b, inp), f'连线失败 {out}->{inp}'

    def scalar(self, pname, v, x, y):
        return self.node(unreal.MaterialExpressionScalarParameter, x, y, parameter_name=pname, default_value=v)

    def const(self, v, x, y):
        return self.node(unreal.MaterialExpressionConstant, x, y, r=v)

    def mul(self, a, b, x, y, ao='', bo=''):
        n = self.node(unreal.MaterialExpressionMultiply, x, y)
        self.link(a, ao, n, 'A')
        self.link(b, bo, n, 'B')
        return n

    def add(self, a, b, x, y, ao='', bo=''):
        n = self.node(unreal.MaterialExpressionAdd, x, y)
        self.link(a, ao, n, 'A')
        self.link(b, bo, n, 'B')
        return n

    def sub(self, a, b, x, y, ao='', bo=''):
        n = self.node(unreal.MaterialExpressionSubtract, x, y)
        self.link(a, ao, n, 'A')
        self.link(b, bo, n, 'B')
        return n

    def sat(self, a, x, y, ao=''):
        n = self.node(unreal.MaterialExpressionSaturate, x, y)
        self.link(a, ao, n, '')
        return n

    def lerp(self, a, b, t, x, y, ao='', bo='', to=''):
        n = self.node(unreal.MaterialExpressionLinearInterpolate, x, y)
        self.link(a, ao, n, 'A')
        self.link(b, bo, n, 'B')
        self.link(t, to, n, 'Alpha')
        return n

    def mask(self, src, r, g, b, x, y, out=''):
        n = self.node(unreal.MaterialExpressionComponentMask, x, y, r=r, g=g, b=b, a=False)
        self.link(src, out, n, '')
        return n

    def tex(self, pname, path, stype, uv, x, y):
        n = self.node(unreal.MaterialExpressionTextureSampleParameter2D, x, y, parameter_name=pname, texture=eas.load_asset(path), sampler_type=stype)
        if uv is not None:
            self.link(uv, '', n, 'UVs')
        return n

    def vec3(self, r, g, b, x, y):
        return self.node(unreal.MaterialExpressionConstant3Vector, x, y, constant=unreal.LinearColor(r, g, b, 1))


def finish(m, masked=False):
    for flag in ('used_with_nanite', 'used_with_instanced_static_meshes', 'used_with_static_lighting'):
        try:
            m.set_editor_property(flag, True)
        except Exception as e:
            unreal.log_warning(f'[soviet] {flag}: {e}')
    if masked:
        m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
        m.set_editor_property('two_sided', True)
        m.set_editor_property('opacity_mask_clip_value', 0.4)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)


def build_pbr(name, masked, lit=False):
    """UV 贴图 PBR + 朝上的面积雪（顶面、窗台、阳台板）：雪用世界投影的 snow_02，只看顶点法线 Z，便宜"""
    m = new_material(name)
    g = G(m)
    white = f'{DEST}/Textures/T_SV_White' if eas.does_asset_exist(f'{DEST}/Textures/T_SV_White') else f'{DEST}/Textures/T_G_Snow02_D'
    col = g.tex('BaseColorTex', f'{DEST}/Textures/T_G_Snow02_D', S.SAMPLERTYPE_COLOR, None, -1300, -500)
    nor = g.tex('NormalTex', FLAT_NORMAL, S.SAMPLERTYPE_NORMAL, None, -1300, -100)
    arm = g.tex('ARMTex', f'{DEST}/Textures/T_G_Snow02_ARM', S.SAMPLERTYPE_MASKS, None, -1300, 200)
    black = next(p for p in ('/Engine/EngineResources/Black', '/Engine/EngineMaterials/BlackSquareTexture', '/Engine/EngineResources/DefaultTexture') if eas.does_asset_exist(p))
    emt = g.tex('EmissiveTex', black, S.SAMPLERTYPE_COLOR, None, -1300, 650)   # 默认贴图的采样类型要对得上，不然母材质编译报错、整栋楼变默认灰格子
    tint = g.node(unreal.MaterialExpressionVectorParameter, -1000, -650, parameter_name='Tint', default_value=unreal.LinearColor(1, 1, 1, 1))
    usetex = g.scalar('UseBaseTex', 1.0, -1000, -760)
    base0 = g.lerp(g.const(1.0, -850, -700), col, usetex, -800, -560, bo='RGB')
    base = g.mul(base0, tint, -650, -520)
    # 粗糙度 / 金属度 = ARM × Mul + Add
    outs = {}
    for i, (pname, ch, mul_, add_) in enumerate((('Rough', 'G', 1.0, 0.0), ('Metal', 'B', 1.0, 0.0))):
        a = g.mul(arm, g.scalar(pname + 'Mul', mul_, -1000, 220 + i * 240), -820, 220 + i * 240, ao=ch)
        b = g.add(a, g.scalar(pname + 'Add', add_, -1000, 300 + i * 240), -680, 220 + i * 240)
        outs[pname] = g.sat(b, -560, 220 + i * 240)
    em = g.mul(emt, g.scalar('EmissiveStrength', 0.0, -1000, 720), -700, 680, ao='RGB')
    # ---- 积雪
    wp = g.node(unreal.MaterialExpressionWorldPosition, -1700, -1200)
    uv = g.node(unreal.MaterialExpressionDivide, -1500, -1200)
    g.link(g.mask(wp, True, True, False, -1600, -1200), '', uv, 'A')
    g.link(g.scalar('SnowTile', 400.0, -1700, -1090), '', uv, 'B')
    snow = g.tex('SnowTex', '/Game/ANGRY_MESH/NaturePack_WinterEnvironment/Textures/Landscape/T_Snow_01_A', S.SAMPLERTYPE_COLOR, uv, -1300, -1250)
    nrm = g.node(unreal.MaterialExpressionVertexNormalWS, -1300, -950)
    nz = g.mask(nrm, False, False, True, -1150, -950)
    d = g.sub(nz, g.scalar('SnowThreshold', 0.55, -1150, -860), -1000, -950)
    mk = g.mul(d, g.scalar('SnowSharp', 6.0, -1000, -860), -860, -950)
    # 让积雪边缘带点破碎：雪贴图的亮度加进遮罩
    brk = g.mul(mk, g.mul(snow, g.const(1.6, -900, -1100), -750, -1150, ao='G'), -650, -1000)
    amt = g.scalar('SnowAmount', 1.0, -650, -860)
    sm = g.sat(g.mul(brk, amt, -500, -950), -380, -950)
    snowc = g.mul(snow, g.vec3(0.82, 0.86, 0.92, -500, -1120), -360, -1120, ao='RGB')
    mel.connect_material_property(g.lerp(base, snowc, sm, -200, -700), '', P.MP_BASE_COLOR)
    mel.connect_material_property(g.lerp(nor, g.vec3(0, 0, 1, -400, -200), sm, -200, -200, ao='RGB'), '', P.MP_NORMAL)
    mel.connect_material_property(g.lerp(outs['Rough'], g.const(0.82, -400, 220), sm, -200, 220), '', P.MP_ROUGHNESS)
    mel.connect_material_property(g.lerp(outs['Metal'], g.const(0.0, -400, 460), sm, -200, 460), '', P.MP_METALLIC)
    mel.connect_material_property(arm, 'R', P.MP_AMBIENT_OCCLUSION)
    if lit:
        # 亮窗：按世界坐标分格子（立面方向 x+y、竖直 z）取哈希，一小部分格子亮暖光；不用贴图
        cu = g.node(unreal.MaterialExpressionCustom, -700, 900, code='float2 c = floor(float2((P.x + P.y) / Cell.x, P.z / Cell.y));\nfloat h = frac(sin(dot(c, float2(12.9898, 78.233))) * 43758.5453);\nreturn h > Cut ? 0.55 + 0.45 * frac(h * 17.3) : 0.0;',
                    output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        ci = []
        for nm in ('P', 'Cell', 'Cut'):
            c = unreal.CustomInput()
            c.set_editor_property('input_name', nm)
            ci.append(c)
        cu.set_editor_property('inputs', ci)
        g.link(g.node(unreal.MaterialExpressionWorldPosition, -950, 880), '', cu, 'P')
        g.link(g.node(unreal.MaterialExpressionConstant2Vector, -950, 960, r=310.0, g=350.0), '', cu, 'Cell')
        g.link(g.scalar('LitCut', 0.885, -950, 1040), '', cu, 'Cut')
        warm = g.node(unreal.MaterialExpressionVectorParameter, -700, 1050, parameter_name='LitColor', default_value=unreal.LinearColor(1.0, 0.62, 0.32, 1))
        lit_e = g.mul(g.mul(cu, warm, -500, 950), g.scalar('LitStrength', 1.3, -500, 1060), -350, 980)
        mel.connect_material_property(g.add(em, lit_e, -200, 800), '', P.MP_EMISSIVE_COLOR)
    else:
        mel.connect_material_property(em, '', P.MP_EMISSIVE_COLOR)
    if masked:
        mel.connect_material_property(col, 'A', P.MP_OPACITY_MASK)
    finish(m, masked)
    return m


def build_ground():
    """地面（第二版）：冬季包的雪 T_Snow_01（底）+ T_Snow_04（露草的雪，遮罩 G 通道 × 大尺度噪声决定哪里露草）+ 带雪沥青（遮罩 R 通道）。
    Poly Haven 的 snow_02 有一道道深色裂纹，大面积铺开很假，只留给路边。路面遮罩给 0.56 时噪声决定哪里露沥青，车辙给 1.0，所以路上是压实的雪 + 黑车辙"""
    m = new_material('M_SV_Ground')
    g = G(m)
    W = '/Game/ANGRY_MESH/NaturePack_WinterEnvironment/Textures/Landscape'
    LC = S.SAMPLERTYPE_LINEAR_COLOR
    wp = g.node(unreal.MaterialExpressionWorldPosition, -2400, 0)
    xy = g.mask(wp, True, True, False, -2250, 0)

    def uv_of(pname, v, x, y):
        n = g.node(unreal.MaterialExpressionDivide, x, y)
        g.link(xy, '', n, 'A')
        g.link(g.scalar(pname, v, x - 200, y + 60), '', n, 'B')
        return n

    uv_s = uv_of('SnowTile', 900.0, -1800, -1000)
    uv_g = uv_of('GrassTile', 800.0, -1800, -300)
    uv_a = uv_of('RoadTile', 650.0, -1800, 300)
    uv_n = uv_of('NoiseTile', 1700.0, -1800, 800)
    uv_f = uv_of('MacroTile', 9000.0, -1800, 1100)
    half = g.scalar('MaskHalf', 24000.0, -2000, 1500)
    uv_m1 = g.node(unreal.MaterialExpressionDivide, -1600, 1460)
    g.link(g.add(xy, half, -1760, 1460), '', uv_m1, 'A')
    g.link(g.mul(half, g.const(2.0, -1800, 1600), -1720, 1560), '', uv_m1, 'B')
    sD = g.tex('SnowD', f'{W}/T_Snow_01_A', S.SAMPLERTYPE_COLOR, uv_s, -1500, -1150)
    sN = g.tex('SnowN', f'{W}/T_Snow_01_N', S.SAMPLERTYPE_NORMAL, uv_s, -1500, -900)
    sR = g.tex('SnowR', f'{W}/T_Snow_01_R', LC, uv_s, -1500, -650)
    gD = g.tex('GrassD', f'{W}/T_Snow_04_A', S.SAMPLERTYPE_COLOR, uv_g, -1500, -400)
    gN = g.tex('GrassN', f'{W}/T_Snow_04_N', S.SAMPLERTYPE_NORMAL, uv_g, -1500, -150)
    gR = g.tex('GrassR', f'{W}/T_Snow_04_R', LC, uv_g, -1500, 100)
    aD = g.tex('RoadD', f'{DEST}/Textures/T_G_AsphaltSnow_D', S.SAMPLERTYPE_COLOR, uv_a, -1500, 350)
    aN = g.tex('RoadN', f'{DEST}/Textures/T_G_AsphaltSnow_N', S.SAMPLERTYPE_NORMAL, uv_a, -1500, 600)
    aA = g.tex('RoadA', f'{DEST}/Textures/T_G_AsphaltSnow_ARM', S.SAMPLERTYPE_MASKS, uv_a, -1500, 850)
    noi = g.tex('NoiseD', f'{DEST}/Textures/T_G_SnowField_D', S.SAMPLERTYPE_COLOR, uv_n, -1500, 1100)
    fD = g.tex('MacroD', f'{DEST}/Textures/T_G_SnowField_D', S.SAMPLERTYPE_COLOR, uv_f, -1500, 1350)
    mk = g.tex('RoadMask', f'{DEST}/Textures/T_SV_GroundMask', S.SAMPLERTYPE_MASKS, uv_m1, -1500, 1600)
    # 路面：t = saturate((R + (噪声 - 中值) × 强度 - 0.5) × 陡度)
    n0 = g.mul(g.sub(noi, g.scalar('NoiseMid', 0.25, -1250, 1180), -1120, 1120, ao='G'), g.scalar('EdgeNoise', 1.2, -1120, 1220), -980, 1120)
    t0 = g.sub(g.add(mk, n0, -820, 1400, ao='R'), g.const(0.5, -820, 1500), -680, 1420)
    t = g.sat(g.mul(t0, g.scalar('EdgeSharp', 4.0, -680, 1520), -540, 1440), -420, 1440)
    # 露草：G × 大尺度噪声
    t1 = g.sub(g.add(mk, n0, -820, 1650, ao='G'), g.scalar('GrassCut', 0.55, -820, 1760), -680, 1680)
    tg = g.sat(g.mul(t1, g.scalar('GrassSharp', 5.0, -680, 1780), -540, 1700), -420, 1700)
    macro = g.lerp(g.scalar('MacroLow', 0.9, -1000, 1000), g.const(1.06, -1000, 1080), fD, -850, 1020, to='G')
    tint = g.node(unreal.MaterialExpressionVectorParameter, -1100, -1250, parameter_name='SnowTint', default_value=unreal.LinearColor(0.93, 0.96, 1.0, 1))
    snow_col = g.mul(g.mul(sD, tint, -950, -1150, ao='RGB'), macro, -800, -1100)
    grass_col = g.mul(gD, g.node(unreal.MaterialExpressionVectorParameter, -1100, -500, parameter_name='GrassTint', default_value=unreal.LinearColor(0.82, 0.84, 0.88, 1)), -950, -420, ao='RGB')
    # 沥青去掉紫红杂色
    de = g.node(unreal.MaterialExpressionDesaturation, -1150, 350)
    if not mel.connect_material_expressions(aD, 'RGB', de, ''):
        g.link(aD, 'RGB', de, 'Input')
    g.link(g.scalar('RoadDesat', 0.85, -1250, 460), '', de, 'Fraction')
    road_col = g.mul(de, g.node(unreal.MaterialExpressionVectorParameter, -1100, 500, parameter_name='RoadTint', default_value=unreal.LinearColor(0.72, 0.75, 0.82, 1)), -950, 400)
    c0 = g.lerp(snow_col, grass_col, tg, -500, -900)
    n1 = g.lerp(sN, gN, tg, -500, -500, ao='RGB', bo='RGB')
    r1 = g.lerp(sR, gR, tg, -500, -100, ao='R', bo='R')
    mel.connect_material_property(g.lerp(c0, road_col, t, -200, -700), '', P.MP_BASE_COLOR)
    mel.connect_material_property(g.lerp(n1, aN, t, -200, -300, bo='RGB'), '', P.MP_NORMAL)
    mel.connect_material_property(g.lerp(r1, aA, t, -200, 100, bo='G'), '', P.MP_ROUGHNESS)
    mel.connect_material_property(g.const(0.0, -200, 400), '', P.MP_METALLIC)
    finish(m)
    return m


def build_sky():
    """天空球：无光照，按视线方向经纬度采样 T_SkyMisty（薄雾漫射天 HDRI）；不用建模云。Tint 压亮度（HDRI 本身亮度高）"""
    m = new_material('M_SV_Sky')
    g = G(m)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided', True)
    cam = g.node(unreal.MaterialExpressionCameraVectorWS, -900, 0)
    cu = g.node(unreal.MaterialExpressionCustom, -700, 0, code='float3 d = -normalize(V);\nfloat u = atan2(d.y, d.x) / 6.28318530718 + 0.5 + Yaw;\nfloat v = acos(clamp(d.z, -1.0, 1.0)) / 3.14159265359;\nreturn float2(u, v);',
                 output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT2)
    ci = []
    for nm in ('V', 'Yaw'):
        c = unreal.CustomInput()
        c.set_editor_property('input_name', nm)
        ci.append(c)
    cu.set_editor_property('inputs', ci)
    g.link(cam, '', cu, 'V')
    g.link(g.scalar('SkyYaw', 0.0, -900, 120), '', cu, 'Yaw')
    tx = g.tex('SkyTex', f'{DEST}/Textures/T_SkyMisty', S.SAMPLERTYPE_LINEAR_COLOR, cu, -450, 0)
    tint = g.node(unreal.MaterialExpressionVectorParameter, -300, 200, parameter_name='SkyTint', default_value=unreal.LinearColor(1, 1, 1, 1))
    mel.connect_material_property(g.mul(tx, tint, -120, 60, ao='RGB'), '', P.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    return m


def build_snow():
    """雪花：半透明无光照。顶点里算循环和朝向（见 tools/soviet_snowbox.py），像素里用天气包的 T_SnowFlake + 距离淡出"""
    m = new_material('M_SV_Snow')
    g = G(m)
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided', True)
    wp = g.node(unreal.MaterialExpressionWorldPosition, -1400, 0)
    cam = g.node(unreal.MaterialExpressionCameraPositionWS, -1400, 80)
    tm = g.node(unreal.MaterialExpressionTime, -1400, 160)
    uv0 = g.node(unreal.MaterialExpressionTextureCoordinate, -1400, 240, coordinate_index=0)
    uv1 = g.node(unreal.MaterialExpressionTextureCoordinate, -1400, 320, coordinate_index=1)
    right = g.node(unreal.MaterialExpressionTransform, -1200, 400, transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_VIEW, transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    g.link(g.vec3(1, 0, 0, -1400, 400), '', right, '')
    up = g.node(unreal.MaterialExpressionTransform, -1200, 500, transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_VIEW, transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    g.link(g.vec3(0, 1, 0, -1400, 500), '', up, '')
    code = """float3 corner0 = float3((UV.x - 0.5) * Q, (UV.y - 0.5) * Q, 0.0);
float3 pv = round((P - corner0) / Q) * Q;
float sp = 0.7 + 0.6 * R.x;
float ph = R.y * 6.2831853;
float3 moved = pv + float3(Wind.x * sp, Wind.y * sp, -Fall * sp) * T;
moved.x += sin(T * 0.9 + ph) * Sway;
moved.y += cos(T * 0.7 + ph * 1.7) * Sway;
float3 rel = frac((moved - Cam) / Box + 0.5) * Box - 0.5 * Box;
float s = Size * (0.6 + 0.8 * R.y);
float3 corner = (UV.x - 0.5) * s * Right + (0.5 - UV.y) * s * Up;
return Cam + rel + corner - P;"""
    cu = g.node(unreal.MaterialExpressionCustom, -800, 100, code=code, output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    ci = []
    for nm in ('P', 'Cam', 'T', 'UV', 'R', 'Right', 'Up', 'Q', 'Box', 'Wind', 'Fall', 'Sway', 'Size'):
        c = unreal.CustomInput()
        c.set_editor_property('input_name', nm)
        ci.append(c)
    cu.set_editor_property('inputs', ci)
    g.link(wp, '', cu, 'P')
    g.link(cam, '', cu, 'Cam')
    g.link(tm, '', cu, 'T')
    g.link(uv0, '', cu, 'UV')
    g.link(uv1, '', cu, 'R')
    g.link(right, '', cu, 'Right')
    g.link(up, '', cu, 'Up')
    g.link(g.scalar('MeshScale', 1.0, -1200, 620), '', cu, 'Q')
    box = g.node(unreal.MaterialExpressionVectorParameter, -1200, 700, parameter_name='Box', default_value=unreal.LinearColor(7000, 7000, 3500, 1))
    g.link(g.mask(box, True, True, True, -1000, 700), '', cu, 'Box')
    wind = g.node(unreal.MaterialExpressionVectorParameter, -1200, 820, parameter_name='Wind', default_value=unreal.LinearColor(55, 22, 0, 1))
    g.link(g.mask(wind, True, True, False, -1000, 820), '', cu, 'Wind')
    g.link(g.scalar('Fall', 125.0, -1000, 900), '', cu, 'Fall')
    g.link(g.scalar('Sway', 22.0, -1000, 980), '', cu, 'Sway')
    g.link(g.scalar('FlakeSize', 4.5, -1000, 1060), '', cu, 'Size')
    mel.connect_material_property(cu, '', P.MP_WORLD_POSITION_OFFSET)
    t0 = eas.load_asset('/Game/VFXWeatherPack/Textures/T_SnowFlake')
    cs = t0.get_editor_property('compression_settings')
    stype = S.SAMPLERTYPE_COLOR if t0.get_editor_property('srgb') else S.SAMPLERTYPE_LINEAR_COLOR
    if cs == unreal.TextureCompressionSettings.TC_GRAYSCALE or cs == unreal.TextureCompressionSettings.TC_ALPHA:
        stype = S.SAMPLERTYPE_GRAYSCALE if t0.get_editor_property('srgb') else S.SAMPLERTYPE_LINEAR_GRAYSCALE
    elif cs == unreal.TextureCompressionSettings.TC_MASKS:
        stype = S.SAMPLERTYPE_MASKS
    log(f'T_SnowFlake 压缩 {cs} sRGB {t0.get_editor_property("srgb")} {t0.blueprint_get_size_x()}')
    tex = g.node(unreal.MaterialExpressionTextureSample, -700, 600, texture=t0, sampler_type=stype)
    # T_SnowFlake 是 2×2 的图集（整张贴上去就是四团雪挤在一起）：每片按随机数挑一格
    cell = g.node(unreal.MaterialExpressionCustom, -900, 600, code='return UV * 0.5 + floor(frac(R * 7.31) * 2.0) * 0.5;', output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT2)
    ci2 = []
    for nm in ('UV', 'R'):
        c = unreal.CustomInput()
        c.set_editor_property('input_name', nm)
        ci2.append(c)
    cell.set_editor_property('inputs', ci2)
    g.link(uv0, '', cell, 'UV')
    g.link(uv1, '', cell, 'R')
    g.link(cell, '', tex, 'UVs')
    # 距离淡出：盒子边上（片在那里循环）完全透明，贴脸 1 米内也淡掉
    dep = g.node(unreal.MaterialExpressionPixelDepth, -700, 860)
    far = g.scalar('FadeFar', 3300.0, -700, 940)
    near = g.scalar('FadeNear', 150.0, -700, 1020)
    f1 = g.node(unreal.MaterialExpressionDivide, -520, 880)
    g.link(g.sub(far, dep, -600, 880), '', f1, 'A')
    g.link(g.mul(far, g.const(0.45, -680, 1100), -600, 980), '', f1, 'B')
    f2 = g.node(unreal.MaterialExpressionDivide, -520, 1020)
    g.link(g.sub(dep, near, -600, 1050), '', f2, 'A')
    g.link(near, '', f2, 'B')
    fade = g.mul(g.sat(f1, -400, 880), g.sat(f2, -400, 1020), -280, 950)
    a0 = g.mul(tex, tex, -450, 650, ao='R', bo='A')
    op = g.sat(g.mul(g.mul(a0, fade, -220, 700), g.scalar('Strength', 0.85, -220, 800), -100, 720), 40, 720)
    mel.connect_material_property(op, '', P.MP_OPACITY)
    col = g.node(unreal.MaterialExpressionVectorParameter, -220, 560, parameter_name='FlakeColor', default_value=unreal.LinearColor(1.05, 1.08, 1.14, 1))
    mel.connect_material_property(g.mask(col, True, True, True, -60, 560), '', P.MP_EMISSIVE_COLOR)
    for flag in ('b_responsive_aa', 'responsive_aa'):
        try:
            m.set_editor_property(flag, True)
            log('雪花材质设了 ' + flag)
        except Exception:
            pass
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    return m


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


if 'mat' in STEPS:
    master = build_pbr('M_SV_PBR', False)
    masked = build_pbr('M_SV_PBR_Masked', True)
    glass_m = build_pbr('M_SV_PBR_Glass', False, lit=True)
    build_ground()
    build_sky()
    snow_m = build_snow()
    # 近处一层（70 米盒子）+ 远处一层（网格放大 3.4 倍 = 240 米盒子，雪片大一些、淡一些，长街远处也有雪）
    sn_near = make_mi('MI_SV_SnowNear', snow_m)
    eas.save_loaded_asset(sn_near)
    sn_far = make_mi('MI_SV_SnowFar', snow_m)
    setp(sn_far, 's', 'MeshScale', 3.4)
    mel.set_material_instance_vector_parameter_value(sn_far, 'Box', unreal.LinearColor(23800, 23800, 7000, 1))
    setp(sn_far, 's', 'FlakeSize', 11.0)
    setp(sn_far, 's', 'FadeFar', 11500.0)
    setp(sn_far, 's', 'FadeNear', 1800.0)
    setp(sn_far, 's', 'Strength', 0.55)
    eas.save_loaded_asset(sn_far)
    n = 0
    tables = [json.load(open(f'{K62}/materials_ue.json', encoding='utf-8')), json.load(open(f'{PROPS}/materials_ue.json', encoding='utf-8'))]
    for table in tables:
        for mname, e in table.items():
            is_glass = bool(re.search(r'KB3D_SVB_Glass', mname))
            mi = make_mi('MI_SV_' + clean(mname), glass_m if is_glass else masked if e['masked'] else master)
            tex = e['tex']
            if 'D' in tex:
                setp(mi, 't', 'BaseColorTex', tex['D'])
            else:
                setp(mi, 's', 'UseBaseTex', 0.0)
                setp(mi, 'v', 'Tint', e['color'])
            if 'N' in tex:
                setp(mi, 't', 'NormalTex', tex['N'])
            if 'ARM' in tex:
                setp(mi, 't', 'ARMTex', tex['ARM'])
                setp(mi, 's', 'MetalMul', 0.45)      # 阴天里金属度 0.9 以上的东西只反射灰天空、发黑（K059 踩过）
            else:
                setp(mi, 's', 'RoughMul', 0.0)
                setp(mi, 's', 'RoughAdd', e['rough_v'])
                setp(mi, 's', 'MetalMul', 0.0)
                setp(mi, 's', 'MetalAdd', e['metal_v'])
            if 'E' in tex:
                setp(mi, 't', 'EmissiveTex', tex['E'])
                setp(mi, 's', 'EmissiveStrength', 2.5)
            if is_glass:      # 原包玻璃底色偏浅（Blender 里靠折射），不透明材质下一片薄荷色：压暗、低粗糙度反射天空
                setp(mi, 'v', 'Tint', (0.2, 0.235, 0.27))
                setp(mi, 's', 'RoughMul', 0.3)
                setp(mi, 's', 'RoughAdd', 0.1)
                setp(mi, 's', 'SnowAmount', 0.0)
            if re.search(r'KB3D_SVB_(MetalPaintedWhite|PlasticWhite)$', mname):   # 卫星锅这些白色件阴天里太亮，一个个白圆盘
                setp(mi, 'v', 'Tint', (0.5, 0.52, 0.55))
            if re.search(r'KB3D_SVB_(Metal|Plastic|AirCon|Galvanized|Polycarbonate|Rubber|Lamps|StreetAtlas)', mname):   # 细件只在接近水平的面上积雪
                setp(mi, 's', 'SnowThreshold', 0.86)
            eas.save_loaded_asset(mi)
            n += 1
    gr = make_mi('MI_SV_Ground', eas.load_asset(f'{DEST}/Materials/M_SV_Ground'))
    eas.save_loaded_asset(gr)
    sky = make_mi('MI_SV_Sky', eas.load_asset(f'{DEST}/Materials/M_SV_Sky'))
    eas.save_loaded_asset(sky)
    log(f'材质实例 {n} 个 + 地面 + 天空')


# ---------------------------------------------------------------- 网格
def import_fbx(fbx, dest):
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
    tools.import_asset_tasks([task(fbx, dest, options=ui)])


SAVE_FAILED, STILL_GRAY = [], []


def setup_mesh(path):
    a = eas.load_asset(path)
    if not isinstance(a, unreal.StaticMesh):
        return 0
    name = a.get_name()
    bs = a.get_editor_property('body_setup')
    if bs:   # 碰撞都用网格本身；Deco 在摆放时把组件设成不碰撞
        bs.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    sm = list(a.get_editor_property('static_materials'))
    for i, s in enumerate(sm):
        slot = str(s.get_editor_property('material_slot_name'))
        hit = None
        for cand in (slot, slot.split('.')[0]):
            p = f'{DEST}/Materials/MI_SV_{clean(cand)}'
            if eas.does_asset_exist(p):
                hit = eas.load_asset(p)
                break
        if hit:
            s.set_editor_property('material_interface', hit)
            sm[i] = s
        else:
            unreal.log_warning(f'[soviet] {name} 槽 {slot} 没找到材质')
    try:   # 只换材质槽，不通知（通知会让网格重建一遍渲染数据）
        a.set_editor_property('static_materials', sm, unreal.PropertyAccessChangeNotifyMode.NEVER)
    except TypeError:
        a.set_editor_property('static_materials', sm)
    still = [str(s.get_editor_property('material_slot_name')) for s in a.get_editor_property('static_materials')
             if not s.get_editor_property('material_interface') or s.get_editor_property('material_interface').get_path_name().startswith('/Engine/')]
    if still:
        STILL_GRAY.append(name)
    a.modify()
    if not eas.save_loaded_asset(a, False):
        SAVE_FAILED.append(name)
    return 1


for st in STEPS:
    if st.startswith('k62:'):
        keys = st[4:].split(',')
        fbxs = []
        for f in sorted(glob.glob(f'{K62}/fbx/SV_*.fbx')):
            base = os.path.basename(f)[3:-4]
            if any(base == k or base.startswith(k + '_') for k in keys):
                fbxs.append((base, f.replace(chr(92), '/')))
        for base, f in fbxs:
            import_fbx(f, f'{DEST}/Buildings')
            for nm in (f'SM_SV_{base}_Solid', f'SM_SV_{base}_Deco'):
                if eas.does_asset_exist(f'{DEST}/Buildings/{nm}'):
                    setup_mesh(f'{DEST}/Buildings/{nm}')
            log(f'楼 {base} 导入完')
    if st == 'props':
        import_fbx(f'{K62}/fbx/SV_props.fbx'.replace(chr(92), '/'), f'{DEST}/Props')
        for f in sorted(glob.glob(f'{PROPS}/fbx/*.fbx')):
            import_fbx(f.replace(chr(92), '/'), f'{DEST}/Props')
        n = 0
        for path in eas.list_assets(f'{DEST}/Props', recursive=False, include_folder=False):
            n += setup_mesh(path)
        log(f'小品 {n} 个')
    if st == 'snowbox':
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
        smd.set_editor_property('build_nanite', False)
        smd.set_editor_property('remove_degenerates', False)
        tools.import_asset_tasks([task('D:/RobotSlice/Inbox/_k62/snowbox.fbx', f'{DEST}/Props', 'SM_SV_SnowBox', options=ui)])
        log('雪花载体网格已导入 ' + str(eas.does_asset_exist(f'{DEST}/Props/SM_SV_SnowBox')))
    if st == 'fix':
        n = 0
        for d in ('Buildings', 'Props'):
            for path in eas.list_assets(f'{DEST}/{d}', recursive=False, include_folder=False):
                n += setup_mesh(path)
        log(f'网格设置 {n} 个')
log(f'步骤完成 {STEPS}；默认材质 {STILL_GRAY[:8]}，存盘失败 {SAVE_FAILED[:8]}')
