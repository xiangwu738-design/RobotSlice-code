"""相位电子园区（K051 高科技街道）资源导入：贴图、母材质、材质实例、网格 → /Game/Env/TechPark。
输入是 k051_export.py + k051_textures.py 的产物（Inbox/_k051/export：meshes.fbx、export.json、ue_tex/）。
- 母材质 /Game/Env/Materials/M_KitMR（金属度 / 粗糙度流程：底色 × Tint、法线、ORM 打包图、自发光 × 颜色 × 强度）
         /Game/Env/Materials/M_KitHolo（全息片：叠加、无光照、双面、轻微扫描线闪烁，不投影；holo=dither 换成遮罩 + 抖动透明）
- 白色件底色压到 0.86（阴天下白漆不会发白糊掉，再亮就和天空糊成一片）；原包灰金属 / 亮金属改成浅灰半哑光（金属度 0.9 在阴天里反射一片暗）；黄色金属件压饱和度（白塔的主色是白 + 冷青，黄只留一点警示）；
  LightYellow 灯带改成冷青色
- 网格：Solid 开 Nanite + 复杂碰撞；Deco 开 Nanite、关碰撞；Holo 不开 Nanite（半透明不支持）、关碰撞
重复跑只补没导过的（贴图 / 网格已存在就跳过；材质实例参数每次重设）。参数 meshes=force 强制重导网格。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_techpark_import.py" -AllowCommandletRendering -RenderOffscreen
"""
import json
import os
import sys

import unreal

SRC = 'D:/RobotSlice/Inbox/_k051/export'
DEST = '/Game/Env/TechPark'
FORCE_MESH = 'meshes=force' in sys.argv
REBUILD_MATS = 'mats=rebuild' in sys.argv   # 母材质节点删光重连（改了 build_mr / build_holo 之后用）
eas = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
data = json.load(open(f'{SRC}/export.json', encoding='utf-8'))
P = unreal.MaterialProperty


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
        n = t.destination_name
        if n.endswith('_N'):
            tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
            tex.set_editor_property('srgb', False)
        elif n.endswith('_ORM'):
            tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_MASKS)
            tex.set_editor_property('srgb', False)
        eas.save_loaded_asset(tex)
unreal.log(f'[techpark] 贴图新导入 {len(tasks)} 张')


# ---------------------------------------------------------------- 母材质
def build_material(path, builder):
    folder, name = path.rsplit('/', 1)
    if eas.does_asset_exist(path):
        if not REBUILD_MATS:
            return eas.load_asset(path)
        m = eas.load_asset(path)
        mel.delete_all_material_expressions(m)
        builder(m)
        mel.recompile_material(m)
        eas.save_loaded_asset(m)
        return m
    m = tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    builder(m)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    return m


def node(m, cls, x, y, **props):
    n = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        n.set_editor_property(k, v)
    return n


def link(a, out, b, inp):
    assert mel.connect_material_expressions(a, out, b, inp), f'连线失败 {a.get_name()}.{out} -> {b.get_name()}.{inp}'


S = unreal.MaterialSamplerType


def tex_param(m, name, sampler, default, x, y):
    return node(m, unreal.MaterialExpressionTextureSampleParameter2D, x, y, parameter_name=name,
                texture=eas.load_asset(default), sampler_type=sampler)


def day_glow(m, src, x, y):
    """自发光 × lerp(1, EyeAdaptationInverse, DayGlow)：DayGlow=1 时按屏幕亮度给（白天曝光 EV100≈12–14，
    不乘的话灯带、全息屏在阴天里几乎看不见；和 ue_fx_daylight.py 给特效补亮是一个思路）"""
    inv = node(m, unreal.MaterialExpressionEyeAdaptationInverse, x - 450, y + 150)
    one = node(m, unreal.MaterialExpressionConstant, x - 450, y + 250, r=1.0)
    dg = node(m, unreal.MaterialExpressionScalarParameter, x - 450, y + 330, parameter_name='DayGlow', default_value=1.0)
    lp = node(m, unreal.MaterialExpressionLinearInterpolate, x - 250, y + 200)
    link(one, '', lp, 'A')
    link(inv, '', lp, 'B')
    link(dg, '', lp, 'Alpha')
    out = node(m, unreal.MaterialExpressionMultiply, x - 80, y)
    link(src, '', out, 'A')
    link(lp, '', out, 'B')
    return out


def build_mr(m):
    base = tex_param(m, 'BaseTex', S.SAMPLERTYPE_COLOR, '/Engine/EngineMaterials/DefaultDiffuse', -900, -400)
    tint = node(m, unreal.MaterialExpressionVectorParameter, -900, -600, parameter_name='Tint', default_value=unreal.LinearColor(1, 1, 1, 1))
    # 10-31 贴图起伏减弱：lerp(TexAvg, 贴图, TexAmount)，默认 1 = 原样（灰金属的贴图有大块斜向明暗，提成白色后很脏）
    tavg = node(m, unreal.MaterialExpressionScalarParameter, -900, -760, parameter_name='TexAvg', default_value=0.5)
    tamt = node(m, unreal.MaterialExpressionScalarParameter, -900, -700, parameter_name='TexAmount', default_value=1.0)
    flat = node(m, unreal.MaterialExpressionLinearInterpolate, -700, -450)
    link(tavg, '', flat, 'A')
    link(base, 'RGB', flat, 'B')
    link(tamt, '', flat, 'Alpha')
    col = node(m, unreal.MaterialExpressionMultiply, -500, -450)
    link(flat, '', col, 'A')
    link(tint, '', col, 'B')
    # 去饱和（黄色件压色）：lerp(原色, 灰度, Desaturate)
    lum = node(m, unreal.MaterialExpressionConstant3Vector, -500, -650, constant=unreal.LinearColor(0.3, 0.59, 0.11, 1))
    gray = node(m, unreal.MaterialExpressionDotProduct, -330, -620)
    link(col, '', gray, 'A')
    link(lum, '', gray, 'B')
    sat = node(m, unreal.MaterialExpressionScalarParameter, -500, -300, parameter_name='Desaturate', default_value=0.0)
    desat = node(m, unreal.MaterialExpressionLinearInterpolate, -150, -500)
    link(col, '', desat, 'A')
    link(gray, '', desat, 'B')
    link(sat, '', desat, 'Alpha')
    # 10-31 按朝向 / 高度分色：灰金属 MetalGrey 是 K051 的主材质（桥面、坡道、楼体立面、柱子全是它，按面积占八成多，
    # tools/ue_k051_mat_area.py 统计），一种颜色只能整张图一起灰。改成：竖直面 × SideMul（白色板）、朝上且低于 DeckTopZ（桥面、坡道）× TopLowMul
    # （石墨色路面）、朝上且更高（楼顶）× TopHighMul、朝下 × BottomMul。四个默认都是 1 = 不变，别的材质实例不受影响
    wn = node(m, unreal.MaterialExpressionVertexNormalWS, -900, -1300)
    nz = node(m, unreal.MaterialExpressionComponentMask, -750, -1300, r=False, g=False, b=True, a=False)
    link(wn, '', nz, '')
    up0 = node(m, unreal.MaterialExpressionSubtract, -600, -1350, const_b=0.6)
    link(nz, '', up0, 'A')
    up1 = node(m, unreal.MaterialExpressionMultiply, -480, -1350, const_b=5.0)
    link(up0, '', up1, 'A')
    up = node(m, unreal.MaterialExpressionSaturate, -360, -1350)
    link(up1, '', up, '')
    dn0 = node(m, unreal.MaterialExpressionAdd, -600, -1250, const_b=0.6)
    link(nz, '', dn0, 'A')
    dn1 = node(m, unreal.MaterialExpressionMultiply, -480, -1250, const_b=-5.0)
    link(dn0, '', dn1, 'A')
    dn = node(m, unreal.MaterialExpressionSaturate, -360, -1250)
    link(dn1, '', dn, '')
    wp = node(m, unreal.MaterialExpressionWorldPosition, -900, -1150)
    wz = node(m, unreal.MaterialExpressionComponentMask, -750, -1150, r=False, g=False, b=True, a=False)
    link(wp, '', wz, '')
    dz = node(m, unreal.MaterialExpressionScalarParameter, -750, -1050, parameter_name='DeckTopZ', default_value=800.0)
    lo0 = node(m, unreal.MaterialExpressionSubtract, -600, -1100)
    link(dz, '', lo0, 'A')
    link(wz, '', lo0, 'B')
    lo1 = node(m, unreal.MaterialExpressionMultiply, -480, -1100, const_b=0.01)
    link(lo0, '', lo1, 'A')
    low = node(m, unreal.MaterialExpressionSaturate, -360, -1100)
    link(lo1, '', low, '')
    one = lambda nm, y: node(m, unreal.MaterialExpressionVectorParameter, -360, y, parameter_name=nm, default_value=unreal.LinearColor(1, 1, 1, 1))
    side_m, high_m, low_m, bot_m = one('SideMul', -1600), one('TopHighMul', -1520), one('TopLowMul', -1440), one('BottomMul', -1000)
    topm = node(m, unreal.MaterialExpressionLinearInterpolate, -200, -1480)
    link(high_m, '', topm, 'A')
    link(low_m, '', topm, 'B')
    link(low, '', topm, 'Alpha')
    ud = node(m, unreal.MaterialExpressionAdd, -200, -1300)
    link(up, '', ud, 'A')
    link(dn, '', ud, 'B')
    sidew = node(m, unreal.MaterialExpressionOneMinus, -80, -1300)
    link(ud, '', sidew, '')
    t1 = node(m, unreal.MaterialExpressionMultiply, 40, -1600)
    link(side_m, '', t1, 'A')
    link(sidew, '', t1, 'B')
    t2 = node(m, unreal.MaterialExpressionMultiply, 40, -1480)
    link(topm, '', t2, 'A')
    link(up, '', t2, 'B')
    t3 = node(m, unreal.MaterialExpressionMultiply, 40, -1000)
    link(bot_m, '', t3, 'A')
    link(dn, '', t3, 'B')
    s12 = node(m, unreal.MaterialExpressionAdd, 160, -1540)
    link(t1, '', s12, 'A')
    link(t2, '', s12, 'B')
    mul = node(m, unreal.MaterialExpressionAdd, 280, -1300)
    link(s12, '', mul, 'A')
    link(t3, '', mul, 'B')
    bc = node(m, unreal.MaterialExpressionMultiply, 400, -500)
    link(desat, '', bc, 'A')
    link(mul, '', bc, 'B')
    mel.connect_material_property(bc, '', P.MP_BASE_COLOR)
    nor = tex_param(m, 'NormalTex', S.SAMPLERTYPE_NORMAL, '/Game/Env/Textures/T_FlatNormal', -900, 0)
    mel.connect_material_property(nor, 'RGB', P.MP_NORMAL)
    orm = tex_param(m, 'ORMTex', S.SAMPLERTYPE_MASKS, f'{DEST}/Textures/T_HTS_MetalWhite_ORM', -900, 300)  # 默认图也得是遮罩类型
    rs = node(m, unreal.MaterialExpressionScalarParameter, -700, 520, parameter_name='RoughScale', default_value=1.0)
    rmul = node(m, unreal.MaterialExpressionMultiply, -500, 320)
    link(orm, 'G', rmul, 'A')
    link(rs, '', rmul, 'B')
    rclamp = node(m, unreal.MaterialExpressionClamp, -330, 320)
    link(rmul, '', rclamp, '')
    mel.connect_material_property(rclamp, '', P.MP_ROUGHNESS)
    ms = node(m, unreal.MaterialExpressionScalarParameter, -700, 620, parameter_name='MetalScale', default_value=1.0)
    mmul = node(m, unreal.MaterialExpressionMultiply, -500, 450)
    link(orm, 'B', mmul, 'A')
    link(ms, '', mmul, 'B')
    mel.connect_material_property(mmul, '', P.MP_METALLIC)
    emis = tex_param(m, 'EmisTex', S.SAMPLERTYPE_COLOR, '/Engine/EngineResources/Black', -900, 800)
    ec = node(m, unreal.MaterialExpressionVectorParameter, -900, 1000, parameter_name='EmisColor', default_value=unreal.LinearColor(1, 1, 1, 1))
    es = node(m, unreal.MaterialExpressionScalarParameter, -700, 1100, parameter_name='EmisStrength', default_value=0.0)
    e1 = node(m, unreal.MaterialExpressionMultiply, -500, 850)
    link(emis, 'RGB', e1, 'A')
    link(ec, '', e1, 'B')
    e2 = node(m, unreal.MaterialExpressionMultiply, -330, 900)
    link(e1, '', e2, 'A')
    link(es, '', e2, 'B')
    mel.connect_material_property(day_glow(m, e2, -100, 900), '', P.MP_EMISSIVE_COLOR)
    m.set_editor_property('used_with_nanite', True)
    m.set_editor_property('used_with_instanced_static_meshes', True)


HOLO_DITHER = 'holo=dither' in sys.argv   # 试过的另一种：遮罩 + 抖动透明（省半透明叠画，但暂停 / 慢动时是一片噪点，默认不用）


def build_holo(m):
    # 默认叠加半透明（看着最像全息）。性能：-MechPerf 1600×900 关掉全部半透明 60 → 76 帧，全息片是其中一部分（特效也是半透明）。
    # 按亮度硬裁的遮罩版只剩零星蓝点、阈值低了又是一整块实心蓝；抖动版（holo=dither）静止画面像噪点
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED if HOLO_DITHER else unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided', True)
    m.set_editor_property('opacity_mask_clip_value', 0.5)
    emis = tex_param(m, 'EmisTex', S.SAMPLERTYPE_COLOR, '/Engine/EngineResources/Black', -900, 0)
    if HOLO_DITHER:
        lum = node(m, unreal.MaterialExpressionConstant3Vector, -900, -200, constant=unreal.LinearColor(0.3, 0.59, 0.11, 1))
        dot = node(m, unreal.MaterialExpressionDotProduct, -700, -150)
        link(emis, 'RGB', dot, 'A')
        link(lum, '', dot, 'B')
        mg = node(m, unreal.MaterialExpressionScalarParameter, -700, -280, parameter_name='MaskGain', default_value=2.5)
        mm = node(m, unreal.MaterialExpressionMultiply, -550, -180)
        link(dot, '', mm, 'A')
        link(mg, '', mm, 'B')
        msat = node(m, unreal.MaterialExpressionSaturate, -420, -180)
        link(mm, '', msat, '')
        # 抖动透明：按亮度给覆盖率，TSR 把抖动点抹成半透明的样子（只按阈值硬裁时全息图只剩零星蓝点）
        dither = node(m, unreal.MaterialExpressionMaterialFunctionCall, -280, -180)
        dither.set_editor_property('material_function', unreal.load_object(None, '/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA.DitherTemporalAA'))
        if not mel.connect_material_expressions(msat, '', dither, 'Alpha Threshold'):
            link(msat, '', dither, 'AlphaThreshold')
        mel.connect_material_property(dither, '', P.MP_OPACITY_MASK)
    ec = node(m, unreal.MaterialExpressionVectorParameter, -900, 250, parameter_name='EmisColor', default_value=unreal.LinearColor(0.55, 0.9, 1.0, 1))
    es = node(m, unreal.MaterialExpressionScalarParameter, -900, 400, parameter_name='EmisStrength', default_value=3.0)
    e1 = node(m, unreal.MaterialExpressionMultiply, -600, 50)
    link(emis, 'RGB', e1, 'A')
    link(ec, '', e1, 'B')
    e2 = node(m, unreal.MaterialExpressionMultiply, -420, 100)
    link(e1, '', e2, 'A')
    link(es, '', e2, 'B')
    # 扫描线：世界 Z 方向的细条纹随时间往上走，亮度 0.75–1
    wp = node(m, unreal.MaterialExpressionWorldPosition, -1100, 600)
    wz = node(m, unreal.MaterialExpressionComponentMask, -950, 600, r=False, g=False, b=True, a=False)
    link(wp, '', wz, '')
    t = node(m, unreal.MaterialExpressionTime, -1100, 750)
    ts = node(m, unreal.MaterialExpressionMultiply, -950, 750, const_b=60.0)
    link(t, '', ts, 'A')
    zs = node(m, unreal.MaterialExpressionMultiply, -800, 600, const_b=0.12)
    link(wz, '', zs, 'A')
    add = node(m, unreal.MaterialExpressionAdd, -650, 650)
    link(zs, '', add, 'A')
    link(ts, '', add, 'B')
    sn = node(m, unreal.MaterialExpressionSine, -500, 650)
    link(add, '', sn, '')
    sc = node(m, unreal.MaterialExpressionMultiply, -380, 650, const_b=0.125)
    link(sn, '', sc, 'A')
    off = node(m, unreal.MaterialExpressionAdd, -260, 650, const_b=0.875)
    link(sc, '', off, 'A')
    e3 = node(m, unreal.MaterialExpressionMultiply, -200, 200)
    link(e2, '', e3, 'A')
    link(off, '', e3, 'B')
    mel.connect_material_property(day_glow(m, e3, 0, 200), '', P.MP_EMISSIVE_COLOR)


MR = build_material('/Game/Env/Materials/M_KitMR', build_mr)
HOLO = build_material('/Game/Env/Materials/M_KitHolo', build_holo)

# ---------------------------------------------------------------- 材质实例
# 每个 Blender 材质的调整：(Tint, 去饱和, 粗糙度倍数, 自发光颜色, 自发光强度)
TUNE = {
    # 自发光强度按屏幕亮度给（DayGlow=1，见 day_glow）：1 左右就是一条亮线，再大就开始泛光
    # 10-31 外观整修（相位园区）：改成晴天有直射光；灰金属按朝向分成白色立面 / 石墨色路面（见下 FACING），白板配深色路面才有层次
    'MetalWhite': ((0.84, 0.85, 0.87), 0.0, 1.0, None, 0, 1.0),     # 基地台台面是它，0.9 时晴天整片顶白
    'PlasticWhite': ((0.9, 0.91, 0.93), 0.0, 1.5, None, 0, 1.0),    # 原粗糙度 0.33，Lumen 反射要追踪（< 0.4），大面白塑料压成 0.5
    # 原包的灰 / 亮金属金属度 0.9+，阴天里反射一片暗，桥面和楼体发黑 → 阴天时改成浅灰半哑光漆面（亮金属 0.88 / 金属度 0.35，灰金属 ×2.05 / 0.12）
    # 晴天：亮金属做成冷银色缎面（金属度 0.75，反射蓝天）；灰金属底色 1、金属度 0.15（漆面），颜色交给 FACING 按朝向乘
    'MetalLight': ((0.8, 0.82, 0.85), 0.0, 2.8, None, 0, 0.75),
    'MetalGrey': ((1.0, 1.0, 1.0), 0.0, 1.4, None, 0, 0.15),
    'MetalDarkGrey': ((0.85, 0.87, 0.9), 0.0, 1.2, None, 0, 0.5),
    'MetalYellow': ((0.9, 0.88, 0.85), 0.55, 1.4, None, 0, 1.0),
    'RubberGray': ((1, 1, 1), 0.0, 1.0, None, 0, 1.0),
    'GreenHedge': ((0.75, 0.8, 0.75), 0.25, 1.0, None, 0, 1.0),
    # 灯带用相位电子企业色 #3CC8DC（线性 0.045/0.578/0.716，按最大分量归一）；原来 (0.45, 0.85, 1.0) 偏淡蓝白，强度 1.6 / 1.3 在晴天曝光下太弱
    'LightBlue': ((0.7, 0.8, 0.85), 0.0, 1.0, (0.063, 0.807, 1.0), 2.6, 1.0),
    'LightYellow': ((0.7, 0.8, 0.85), 1.0, 1.0, (0.063, 0.807, 1.0), 2.2, 1.0),   # 黄灯改冷青：底色去饱和，自发光按亮度乘青色
}
# 10-31 按朝向分色（M_KitMR 的 SideMul / TopLowMul / TopHighMul / BottomMul，见 build_mr）。灰金属贴图线性均值约 0.19：
# 立面 ×3.3 ≈ 0.63 冷白（白色切角板）、桥面 / 坡道 ×0.95 ≈ 0.18 石墨色、楼顶 ×2.6 ≈ 0.5 浅灰、朝下 ×0.7；贴图起伏减半（斜向大块明暗提白后很脏）
FACING = {
    'MetalGrey': {'SideMul': (3.3, 3.36, 3.48), 'TopLowMul': (0.95, 0.98, 1.05), 'TopHighMul': (2.6, 2.65, 2.75), 'BottomMul': (0.7, 0.72, 0.76),
                  'TexAvg': 0.19, 'TexAmount': 0.5, 'DeckTopZ': 800.0},
}
# 全息：(自发光强度, 遮罩增益)。覆盖率 = 亮度 × 增益，再抖动（DitherTemporalAA）：硬阈值 0.2×2.5 整块底板都留下、一片实心蓝，0.5 只剩零星蓝点
HOLO_TUNE = {'HoloInfoA': (0.8, 1.6), 'HoloElementsAtlas': (0.8, 1.6), 'HoloFillA': (0.6, 1.6), 'HoloLines': (1.0, 2.0), 'HoloPlanet': (0.9, 1.6)}


def make_mi(name, parent):
    path = f'{DEST}/Materials/{name}'
    mi = eas.load_asset(path) if eas.does_asset_exist(path) else tools.create_asset(
        name, f'{DEST}/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', parent)
    return mi


def tex(short, suffix):
    p = f'{DEST}/Textures/T_HTS_{short}_{suffix}'
    return eas.load_asset(p) if eas.does_asset_exist(p) else None


mats = {}
for full, info in data['materials'].items():
    short = full.replace('KB3D_HTS_', '')
    if short in HOLO_TUNE:
        mi = make_mi(f'MI_HTS_{short}', HOLO)
        e = tex(short, 'E')
        if e:
            mel.set_material_instance_texture_parameter_value(mi, 'EmisTex', e)
        mel.set_material_instance_scalar_parameter_value(mi, 'EmisStrength', HOLO_TUNE[short][0])
        mel.set_material_instance_scalar_parameter_value(mi, 'MaskGain', HOLO_TUNE[short][1])
        mel.set_material_instance_vector_parameter_value(mi, 'EmisColor', unreal.LinearColor(0.42, 0.88, 1.0, 1))   # 10-31：(0.62, 0.9, 1.0) → 往企业青色靠一点
    else:
        mi = make_mi(f'MI_HTS_{short}', MR)
        for param, suf in (('BaseTex', 'D'), ('NormalTex', 'N'), ('ORMTex', 'ORM'), ('EmisTex', 'E')):
            t = tex(short, suf)
            if t:
                mel.set_material_instance_texture_parameter_value(mi, param, t)
        tint, desat, rough, ecol, estr, metal = TUNE.get(short, ((1, 1, 1), 0.0, 1.0, None, 0, 1.0))
        mel.set_material_instance_scalar_parameter_value(mi, 'MetalScale', metal)
        if short == 'GlassBlue':   # 没有贴图：深青色镜面玻璃（不透明，省）；粗糙度借白漆的 ORM 乘小倍数
            tint, rough = (0.02, 0.05, 0.065), 0.2
            mel.set_material_instance_scalar_parameter_value(mi, 'MetalScale', 0.0)
            mel.set_material_instance_texture_parameter_value(mi, 'BaseTex', eas.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
        mel.set_material_instance_vector_parameter_value(mi, 'Tint', unreal.LinearColor(*tint, 1))
        mel.set_material_instance_scalar_parameter_value(mi, 'Desaturate', desat)
        mel.set_material_instance_scalar_parameter_value(mi, 'RoughScale', rough)
        if ecol:
            mel.set_material_instance_vector_parameter_value(mi, 'EmisColor', unreal.LinearColor(*ecol, 1))
            mel.set_material_instance_scalar_parameter_value(mi, 'EmisStrength', estr)
        for k, v in FACING.get(short, {}).items():
            if isinstance(v, tuple):
                mel.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(*v, 1))
            else:
                mel.set_material_instance_scalar_parameter_value(mi, k, v)
    eas.save_loaded_asset(mi)
    mats[full] = mi
unreal.log(f'[techpark] 材质实例 {len(mats)} 个')

# ---------------------------------------------------------------- 网格
have = eas.does_directory_exist(f'{DEST}/Meshes') and len(eas.list_assets(f'{DEST}/Meshes', recursive=False)) >= len(data['meshes'])
if FORCE_MESH or not have:
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
    tools.import_asset_tasks([task(f'{SRC}/meshes.fbx', f'{DEST}/Meshes', options=ui)])

info = {m['name']: m for m in data['meshes']}
fixed = 0
for path in eas.list_assets(f'{DEST}/Meshes', recursive=False, include_folder=False):
    a = eas.load_asset(path)
    if not isinstance(a, unreal.StaticMesh):
        continue
    meta = info.get(a.get_name())
    if not meta:
        continue
    changed = False
    # 槽名 = Blender 材质名 → 挂材质实例（要用 set_material；改 static_materials 数组再设回去不生效，槽还是 WorldGridMaterial）
    for i, s in enumerate(a.get_editor_property('static_materials')):
        slot = str(s.get_editor_property('material_slot_name'))
        mi = mats.get(slot) or next((v for k, v in mats.items() if k.replace('KB3D_HTS_', '') in slot), None)
        if mi and s.get_editor_property('material_interface') != mi:
            a.set_material(i, mi)
            changed = True
    nan = a.get_editor_property('nanite_settings')
    want_nanite = meta['class'] != 'Holo'
    if nan.enabled != want_nanite:
        nan.enabled = want_nanite
        a.set_editor_property('nanite_settings', nan)
        changed = True
    bs = a.get_editor_property('body_setup')
    if bs:
        flag = unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE if meta['class'] == 'Solid' else unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX
        if bs.get_editor_property('collision_trace_flag') != flag:
            bs.set_editor_property('collision_trace_flag', flag)
            changed = True
    if changed:
        eas.save_loaded_asset(a)
        fixed += 1
unreal.log(f'[techpark] 网格 {len(info)} 个（改了设置 {fixed} 个）')

# ---------------------------------------------------------------- 远景高楼（K005，k051_backdrop_export.py）
BD_SRC = 'D:/RobotSlice/Inbox/_k051/backdrop'
BD = f'{DEST}/Backdrop'
bd_info = json.load(open(f'{BD_SRC}/backdrop.json', encoding='utf-8'))
if FORCE_MESH or not eas.does_directory_exist(BD) or len(eas.list_assets(BD, recursive=False)) < len(bd_info):
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
    tools.import_asset_tasks([task(f'{BD_SRC}/backdrop.fbx', BD, options=ui)])
white = eas.load_asset('/Engine/EngineResources/WhiteSquareTexture')
bd_mats = {}
for name, tint, rough in (('Panel', (0.66, 0.68, 0.71), 0.7), ('Dark', (0.05, 0.055, 0.06), 0.9)):
    mi = make_mi(f'MI_TP_Backdrop{name}', MR)
    mel.set_material_instance_texture_parameter_value(mi, 'BaseTex', white)
    mel.set_material_instance_vector_parameter_value(mi, 'Tint', unreal.LinearColor(*tint, 1))
    mel.set_material_instance_scalar_parameter_value(mi, 'RoughScale', rough)
    mel.set_material_instance_scalar_parameter_value(mi, 'MetalScale', 0.0)
    eas.save_loaded_asset(mi)
    bd_mats[name] = mi
bd_mats['Glass'] = mats.get('KB3D_HTS_GlassBlue')
for path in eas.list_assets(BD, recursive=False, include_folder=False):
    a = eas.load_asset(path)
    if not isinstance(a, unreal.StaticMesh):
        continue
    for i, s in enumerate(a.get_editor_property('static_materials')):
        slot = str(s.get_editor_property('material_slot_name'))
        for k, mi in bd_mats.items():
            if k in slot and mi:
                a.set_material(i, mi)
    bs = a.get_editor_property('body_setup')
    if bs:
        bs.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
    eas.save_loaded_asset(a)
unreal.log(f'[techpark] 远景高楼 {len(bd_info)} 栋')

# ---------------------------------------------------------------- 标识（k051_brand_export.py：PHASE 占位标识、CONSENT 文字牌）
BR_SRC = 'D:/RobotSlice/Inbox/_k051/brand/brand.fbx'
BR = f'{DEST}/Brand'
if os.path.exists(BR_SRC) and (FORCE_MESH or not eas.does_asset_exist(f'{BR}/SM_TP_LogoPHASE')):
    ui = unreal.FbxImportUI()
    ui.set_editor_property('import_mesh', True)
    ui.set_editor_property('import_as_skeletal', False)
    ui.set_editor_property('import_materials', False)
    ui.set_editor_property('import_textures', False)
    ui.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_STATIC_MESH)
    smd = ui.static_mesh_import_data
    smd.set_editor_property('combine_meshes', False)
    smd.set_editor_property('auto_generate_collision', False)
    smd.set_editor_property('build_nanite', False)
    tools.import_asset_tasks([task(BR_SRC, BR, options=ui)])
# 白色发光（DayGlow 按屏幕亮度给，阴天下也是亮的）；CONSENT 偏暖一点和相位电子的冷青区分
for name, glow, strength in (('LogoPHASE', (0.78, 0.95, 1.0), 2.0), ('SignCONSENT', (1.0, 0.93, 0.82), 1.3)):   # 10-31 晴天：1.4 / 1.1 → 2.0 / 1.3
    mi = make_mi(f'MI_TP_{name}', MR)
    mel.set_material_instance_texture_parameter_value(mi, 'BaseTex', white)
    mel.set_material_instance_texture_parameter_value(mi, 'EmisTex', white)
    mel.set_material_instance_vector_parameter_value(mi, 'Tint', unreal.LinearColor(0.9, 0.92, 0.95, 1))
    mel.set_material_instance_vector_parameter_value(mi, 'EmisColor', unreal.LinearColor(*glow, 1))
    mel.set_material_instance_scalar_parameter_value(mi, 'EmisStrength', strength)
    mel.set_material_instance_scalar_parameter_value(mi, 'MetalScale', 0.0)
    eas.save_loaded_asset(mi)
    p = f'{BR}/SM_TP_{name}'
    if eas.does_asset_exist(p):
        a = eas.load_asset(p)
        for i in range(len(a.get_editor_property('static_materials'))):
            a.set_material(i, mi)
        bs = a.get_editor_property('body_setup')
        if bs:
            bs.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
        eas.save_loaded_asset(a)
unreal.log('[techpark] 标识 PHASE / CONSENT')
