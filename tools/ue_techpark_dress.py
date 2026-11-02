"""相位电子园区（L_TechPark）外观整修（10-31）：只动外观，不动碰撞、布局、出生点、路点。
起因：用户说整张图偏灰、偏平，要一眼看出是相位电子（PHASE）的园区——白色切角板配深色缝 / 金属 / 玻璃、少量青色灯带、地面标线、看得出投影和明暗、远景不空。
在 ue_techpark_build.py 搭好的地图上加一层（可重复跑：先删带 TPDress 标签的 actor 再加，地图里别的东西不碰）：
- 地面换成 M_TP_Paving：深石墨色大方砖（Tiles002 的纹理压暗）+ 大尺度明暗变化 + 场地内每 24 米一道细青色嵌线（企业园区的「电路板」网格，远处淡出防闪）
  + 远处按距离淡成平均色（大片方砖在远处会出摩尔纹）。场外再铺一块 6 公里的同材质无碰撞平面（原来 350 米外是空的，露出地平线下的蓝色）
- 远景高楼的白面板换成 M_TP_Facade：按世界高度画楼层（深色玻璃带 + 白色窗间墙 + 竖向窗框），看得出尺度；PEC 总部塔保持原样（斜交网格不适合画楼层）
- 外圈再摆一圈远景楼（350–950 米，无碰撞、不投影），雾里一层层淡出去
- 几栋面向场地的远景楼楼顶挂青色 PHASE 标识（企业园区的「地盘」感）
- 后期：优先级 200 的后期体积（光照插件自己的后期体积是 100，会盖掉优先级 10 的 PP_TechPark 里的对比度 / 饱和度 / 曝光补偿，只有色彩增益生效）
- 光照：地图里 LightingDirector 的 TuningSettings（逐地图）——云改成高层薄云、雾加浓一点做空气透视；时间 / 天气在 AMechGameMode::StartPlay 里（15 点 Clear）
参数：mats=rebuild 重连两个母材质的节点；nosave 不存地图（试参数）
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_techpark_dress.py" -AllowCommandletRendering -RenderOffscreen
"""
import json
import math
import random
import sys

import unreal

DEST = '/Game/Env/TechPark'
MAP = '/Game/Maps/L_TechPark'
TAG = 'TPDress'
REBUILD = 'mats=rebuild' in sys.argv
eas = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
P = unreal.MaterialProperty
S = unreal.MaterialSamplerType
log = lambda *a: unreal.log('[tpdress] ' + ' '.join(str(x) for x in a))

# 相位电子企业色 #3CC8DC（sRGB）→ 线性 (0.045, 0.578, 0.716)，自发光按最大分量归一
PHASE_CYAN = (0.063, 0.807, 1.0)


def V(x, y, z):
    """布局表坐标（Blender 米，y 朝北）→ UE 厘米"""
    return unreal.Vector(x * 100.0, -y * 100.0, z * 100.0)


# ================================================================ 材质
def node(m, cls, x, y, **props):
    n = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        n.set_editor_property(k, v)
    return n


def link(a, out, b, inp):
    assert mel.connect_material_expressions(a, out, b, inp), f'连线失败 {a.get_name()}.{out} -> {b.get_name()}.{inp}'


def scalar(m, name, value, x, y):
    return node(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)


def vec(m, name, rgb, x, y):
    return node(m, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(*rgb, 1))


def mask(m, src, r, g, b, x, y, out=''):
    n = node(m, unreal.MaterialExpressionComponentMask, x, y, r=r, g=g, b=b, a=False)
    link(src, out, n, '')
    return n


def op(m, cls, a, b, x, y, **props):
    """二元运算；b 是数字时当常量"""
    n = node(m, cls, x, y, **props)
    link(a[0], a[1], n, 'A')
    if isinstance(b, (int, float)):
        n.set_editor_property('const_b', float(b))
    else:
        link(b[0], b[1], n, 'B')
    return n


def unary(m, cls, a, x, y):
    n = node(m, cls, x, y)
    link(a[0], a[1], n, '')
    return n


def lerp(m, a, b, alpha, x, y):
    n = node(m, unreal.MaterialExpressionLinearInterpolate, x, y)
    for src, pin in ((a, 'A'), (b, 'B'), (alpha, 'Alpha')):
        if isinstance(src, (int, float)):
            n.set_editor_property({'A': 'const_a', 'B': 'const_b', 'Alpha': 'const_alpha'}[pin], float(src))
        else:
            link(src[0], src[1], n, pin)
    return n


def day_glow(m, src, x, y):
    """自发光 × lerp(1, EyeAdaptationInverse, DayGlow)，和 ue_techpark_import.py 的 day_glow 一样：白天曝光下灯带才看得见"""
    inv = node(m, unreal.MaterialExpressionEyeAdaptationInverse, x - 450, y + 150)
    dg = scalar(m, 'DayGlow', 1.0, x - 450, y + 330)
    lp = lerp(m, 1.0, (inv, ''), (dg, ''), x - 250, y + 200)
    return op(m, unreal.MaterialExpressionMultiply, (src, ''), (lp, ''), x - 80, y)


def grid_line(m, coord_abs, coord_rel, grid, width, x, y):
    """世界坐标某一轴上每 grid 厘米一条宽 width 厘米的线，按像素覆盖率抗锯齿（线比一个像素还细时按比例变淡，不闪）
    coord_abs：绝对世界坐标（算离最近一条线的距离）；coord_rel：相机相对坐标（求 DDX/DDY，绝对坐标是大世界双精度，求导不稳）"""
    t = op(m, unreal.MaterialExpressionDivide, coord_abs, grid, x, y)
    t = op(m, unreal.MaterialExpressionAdd, (t, ''), 0.5, x + 150, y)
    t = unary(m, unreal.MaterialExpressionFrac, (t, ''), x + 300, y)
    t = op(m, unreal.MaterialExpressionSubtract, (t, ''), 0.5, x + 450, y)
    t = unary(m, unreal.MaterialExpressionAbs, (t, ''), x + 600, y)
    d = op(m, unreal.MaterialExpressionMultiply, (t, ''), grid, x + 750, y)          # 离最近一条线的距离（厘米）
    ddx = unary(m, unreal.MaterialExpressionDDX, coord_rel, x, y + 120)
    ddy = unary(m, unreal.MaterialExpressionDDY, coord_rel, x, y + 220)
    fw = op(m, unreal.MaterialExpressionAdd, (unary(m, unreal.MaterialExpressionAbs, (ddx, ''), x + 150, y + 120), ''),
            (unary(m, unreal.MaterialExpressionAbs, (ddy, ''), x + 150, y + 220), ''), x + 300, y + 160)
    fw = op(m, unreal.MaterialExpressionMax, (fw, ''), 0.05, x + 450, y + 160)       # 一个像素多少厘米
    # 覆盖率 ≈ saturate((w/2 + fw/2 - d) / fw) × saturate(w / fw)（width 是参数节点）
    half = op(m, unreal.MaterialExpressionMultiply, (fw, ''), 0.5, x + 600, y + 160)
    hw = op(m, unreal.MaterialExpressionMultiply, width, 0.5, x + 600, y + 260)
    edge = op(m, unreal.MaterialExpressionAdd, (half, ''), (hw, ''), x + 750, y + 160)
    cov = op(m, unreal.MaterialExpressionSubtract, (edge, ''), (d, ''), x + 900, y + 100)
    cov = op(m, unreal.MaterialExpressionDivide, (cov, ''), (fw, ''), x + 1050, y + 100)
    cov = unary(m, unreal.MaterialExpressionSaturate, (cov, ''), x + 1200, y + 100)
    thin = op(m, unreal.MaterialExpressionDivide, width, (fw, ''), x + 900, y + 260)
    thin = unary(m, unreal.MaterialExpressionSaturate, (thin, ''), x + 1050, y + 260)
    return op(m, unreal.MaterialExpressionMultiply, (cov, ''), (thin, ''), x + 1350, y + 150)


def build_paving(m):
    """地面：深石墨色方砖 + 场地内青色嵌线网格 + 远处淡成平均色"""
    wpos = node(m, unreal.MaterialExpressionWorldPosition, -3000, 0)
    rel = node(m, unreal.MaterialExpressionWorldPosition, -3000, 900)
    E = unreal.WorldPositionIncludedOffsets
    rel.set_editor_property('world_position_shader_offset', getattr(E, 'WPT_CAMERA_RELATIVE', None) or getattr(E, 'WPT_CAMERA_RELATIVE_WORLD_POSITION'))
    tile = scalar(m, 'TileSize', 1000.0, -3000, 150)
    p = op(m, unreal.MaterialExpressionDivide, (wpos, ''), (tile, ''), -2800, 50)
    uv = mask(m, p, True, True, False, -2600, 50)

    def tex(name, sampler, default, x, y, uvs):
        t = node(m, unreal.MaterialExpressionTextureSampleParameter2D, x, y, parameter_name=name,
                 texture=eas.load_asset(default), sampler_type=sampler)
        link(uvs, '', t, 'UVs')
        return t

    col = tex('BaseTex', S.SAMPLERTYPE_COLOR, '/Game/Env/Street/Textures/T_Tiles002_diff', -2300, -500, uv)
    nor = tex('NormalTex', S.SAMPLERTYPE_NORMAL, '/Game/Env/Street/Textures/T_Tiles002_nor_gl', -2300, -100, uv)
    arm = tex('ARMTex', S.SAMPLERTYPE_MASKS, '/Game/Env/Street/Textures/T_Concrete006_arm', -2300, 300, uv)
    # 大尺度明暗（几十米一块，像石材批次不同）：同一张 ARM 图放大很多倍采样，AO 通道 + 粗糙度通道混一下
    msz = scalar(m, 'MacroSize', 5300.0, -3000, 500)
    mp = op(m, unreal.MaterialExpressionDivide, (wpos, ''), (msz, ''), -2800, 500)
    muv = mask(m, mp, True, True, False, -2600, 500)
    macro_t = tex('MacroTex', S.SAMPLERTYPE_MASKS, '/Game/Env/Street/Textures/T_Concrete006_arm', -2300, 650, muv)
    mamt = scalar(m, 'MacroAmount', 0.35, -2300, 900)
    mg = op(m, unreal.MaterialExpressionSubtract, (macro_t, 'G'), 0.5, -2000, 650)
    mg = op(m, unreal.MaterialExpressionMultiply, (mg, ''), (mamt, ''), -1850, 650)
    macro = op(m, unreal.MaterialExpressionAdd, (mg, ''), 1.0, -1700, 650)
    tint = vec(m, 'Tint', (0.2, 0.21, 0.23), -2300, -700)
    c = op(m, unreal.MaterialExpressionMultiply, (col, 'RGB'), (tint, ''), -1900, -550)
    c = op(m, unreal.MaterialExpressionMultiply, (c, ''), (macro, ''), -1700, -500)
    # 按像素深度淡成平均色 / 平法线（远处大片方砖的摩尔纹）
    depth = node(m, unreal.MaterialExpressionPixelDepth, -2300, 1100)
    fs = scalar(m, 'FadeStart', 6000.0, -2300, 1200)
    fl = scalar(m, 'FadeLength', 18000.0, -2300, 1300)
    fd = op(m, unreal.MaterialExpressionSubtract, (depth, ''), (fs, ''), -2100, 1150)
    fd = op(m, unreal.MaterialExpressionDivide, (fd, ''), (fl, ''), -1950, 1150)
    fade = unary(m, unreal.MaterialExpressionSaturate, (fd, ''), -1800, 1150)
    avg = scalar(m, 'AvgAlbedo', 0.72, -2000, -750)
    avgc = op(m, unreal.MaterialExpressionMultiply, (tint, ''), (avg, ''), -1700, -750)
    avgc = op(m, unreal.MaterialExpressionMultiply, (avgc, ''), (macro, ''), -1550, -750)
    base = lerp(m, (c, ''), (avgc, ''), (fade, ''), -1400, -550)
    flat = node(m, unreal.MaterialExpressionConstant3Vector, -1800, -150, constant=unreal.LinearColor(0, 0, 1, 1))
    nrm = lerp(m, (nor, 'RGB'), (flat, ''), (fade, ''), -1400, -150)
    rs = scalar(m, 'RoughScale', 1.0, -2000, 350)
    r = op(m, unreal.MaterialExpressionMultiply, (arm, 'G'), (rs, ''), -1800, 300)
    rmin = scalar(m, 'RoughMin', 0.42, -2000, 450)   # Lumen 反射只追踪粗糙度 0.4 以下的，大片地面压在上面
    r = op(m, unreal.MaterialExpressionMax, (r, ''), (rmin, ''), -1650, 300)
    r = unary(m, unreal.MaterialExpressionSaturate, (r, ''), -1500, 300)

    # ---- 青色嵌线：场地内每 Grid 厘米一条，宽 LineWidth，离场地边 / 镜头远了淡掉
    grid = scalar(m, 'Grid', 2400.0, -3000, 1500)
    lw = scalar(m, 'LineWidth', 7.0, -3000, 1600)
    ax = mask(m, wpos, True, False, False, -2800, 1500)
    ay = mask(m, wpos, False, True, False, -2800, 1900)
    rx = mask(m, rel, True, False, False, -2800, 1600)
    ry = mask(m, rel, False, True, False, -2800, 2000)
    lx = grid_line(m, (ax, ''), (rx, ''), (grid, ''), (lw, ''), -2600, 1500)
    ly = grid_line(m, (ay, ''), (ry, ''), (grid, ''), (lw, ''), -2600, 1900)
    line = op(m, unreal.MaterialExpressionMax, (lx, ''), (ly, ''), -1000, 1700)
    # 场地范围（ArenaHalf = 半宽 / 半高，厘米）外不画
    ah = vec(m, 'ArenaHalf', (11200.0, 8000.0, 0.0), -1300, 2200)
    ahx = mask(m, ah, True, False, False, -1150, 2200)
    ahy = mask(m, ah, False, True, False, -1150, 2300)
    inx = op(m, unreal.MaterialExpressionSubtract, (ahx, ''), (unary(m, unreal.MaterialExpressionAbs, (ax, ''), -1150, 2100), ''), -1000, 2150)
    iny = op(m, unreal.MaterialExpressionSubtract, (ahy, ''), (unary(m, unreal.MaterialExpressionAbs, (ay, ''), -1150, 2400), ''), -1000, 2300)
    inside = op(m, unreal.MaterialExpressionMin, (inx, ''), (iny, ''), -850, 2200)
    inside = op(m, unreal.MaterialExpressionMultiply, (inside, ''), 0.01, -700, 2200)
    inside = unary(m, unreal.MaterialExpressionSaturate, (inside, ''), -550, 2200)
    lfs = scalar(m, 'LineFadeStart', 9000.0, -1300, 2500)
    lfl = scalar(m, 'LineFadeLength', 9000.0, -1300, 2600)
    lfd = op(m, unreal.MaterialExpressionSubtract, (depth, ''), (lfs, ''), -1150, 2500)
    lfd = op(m, unreal.MaterialExpressionDivide, (lfd, ''), (lfl, ''), -1000, 2500)
    lfade = unary(m, unreal.MaterialExpressionOneMinus, (unary(m, unreal.MaterialExpressionSaturate, (lfd, ''), -850, 2500), ''), -700, 2500)
    line = op(m, unreal.MaterialExpressionMultiply, (line, ''), (inside, ''), -800, 1700)
    line = op(m, unreal.MaterialExpressionMultiply, (line, ''), (lfade, ''), -650, 1700)
    # ---- 场外远地面的「城区街块」（BlockAmount 默认 0 = 不画；只有 MI_TP_FarGround 开）：每 BlockSize 一个街块，
    # 块内按块随机明暗（像一片片楼顶），块边一圈深色街道。高空镜头里远景楼脚下不再是一整块平板（第 3 轮远处的楼像飘在空地上）
    bsz = scalar(m, 'BlockSize', 6000.0, -3000, 2800)
    bamt = scalar(m, 'BlockAmount', 0.0, -3000, 2900)
    bq = op(m, unreal.MaterialExpressionDivide, (wpos, ''), (bsz, ''), -2800, 2800)
    bq = mask(m, bq, True, True, False, -2650, 2800)
    bcell = unary(m, unreal.MaterialExpressionFloor, (bq, ''), -2500, 2750)
    bfr = unary(m, unreal.MaterialExpressionFrac, (bq, ''), -2500, 2900)
    # 每个格子一个随机数：frac(sin(dot(格子, (12.9898, 78.233))) × 43758.5)（第 5 轮用贴图低 mip 取随机，各块几乎一样亮，整片像方格纸）
    hk = node(m, unreal.MaterialExpressionConstant2Vector, -2350, 2650, r=12.9898, g=78.233)
    hd = op(m, unreal.MaterialExpressionDotProduct, (bcell, ''), (hk, ''), -2200, 2700)
    hs = unary(m, unreal.MaterialExpressionSine, (hd, ''), -2080, 2700)
    hs = op(m, unreal.MaterialExpressionMultiply, (hs, ''), 43758.5, -1960, 2700)
    hrnd = unary(m, unreal.MaterialExpressionFrac, (hs, ''), -1840, 2700)
    fx = mask(m, bfr, True, False, False, -2350, 2900)
    fy = mask(m, bfr, False, True, False, -2350, 3000)
    ex = op(m, unreal.MaterialExpressionMin, (fx, ''), (unary(m, unreal.MaterialExpressionOneMinus, (fx, ''), -2200, 2950), ''), -2050, 2900)
    ey = op(m, unreal.MaterialExpressionMin, (fy, ''), (unary(m, unreal.MaterialExpressionOneMinus, (fy, ''), -2200, 3050), ''), -2050, 3000)
    ee = op(m, unreal.MaterialExpressionMin, (ex, ''), (ey, ''), -1900, 2950)
    blk = op(m, unreal.MaterialExpressionSubtract, (ee, ''), 0.1, -1750, 2950)
    blk = op(m, unreal.MaterialExpressionMultiply, (blk, ''), 30.0, -1600, 2950)
    blk = unary(m, unreal.MaterialExpressionSaturate, (blk, ''), -1450, 2950)        # 1 = 楼顶，0 = 街道
    roof = lerp(m, 0.75, 2.2, (hrnd, ''), -1550, 2700)
    roofc = op(m, unreal.MaterialExpressionMultiply, (tint, ''), (roof, ''), -1400, 2700)
    streetc = op(m, unreal.MaterialExpressionMultiply, (tint, ''), 0.7, -1400, 2850)   # 街道只比楼顶暗一点（0.4 时一道道黑线，像网格纸）
    city = lerp(m, (streetc, ''), (roofc, ''), (blk, ''), -1250, 2800)
    base = lerp(m, (base, ''), (city, ''), (bamt, ''), -1150, -600)
    lcol = vec(m, 'LineBase', (0.03, 0.035, 0.04), -1000, -350)
    base = lerp(m, (base, ''), (lcol, ''), (line, ''), -1100, -500)
    mel.connect_material_property(base, '', P.MP_BASE_COLOR)
    mel.connect_material_property(nrm, '', P.MP_NORMAL)
    rl = lerp(m, (r, ''), 0.35, (line, ''), -1300, 300)
    mel.connect_material_property(rl, '', P.MP_ROUGHNESS)
    ec = vec(m, 'LineColor', PHASE_CYAN, -800, 1900)
    es = scalar(m, 'LineGlow', 2.0, -800, 2000)
    e = op(m, unreal.MaterialExpressionMultiply, (ec, ''), (es, ''), -600, 1900)
    e = op(m, unreal.MaterialExpressionMultiply, (e, ''), (line, ''), -450, 1850)
    mel.connect_material_property(day_glow(m, e, -100, 1850), '', P.MP_EMISSIVE_COLOR)


def build_facade(m):
    """远景楼白面板：按世界高度画楼层（深色玻璃带 + 白色窗间墙）+ 竖向窗框，只画在竖直面上"""
    wpos = node(m, unreal.MaterialExpressionWorldPosition, -2400, 0)
    nrm = node(m, unreal.MaterialExpressionVertexNormalWS, -2400, 400)
    an = unary(m, unreal.MaterialExpressionAbs, (nrm, ''), -2200, 400)
    nx = mask(m, an, True, False, False, -2000, 350)
    ny = mask(m, an, False, True, False, -2000, 450)
    nz = mask(m, an, False, False, True, -2000, 550)
    vert = op(m, unreal.MaterialExpressionSubtract, (nz, ''), 0.45, -1850, 550)
    vert = op(m, unreal.MaterialExpressionMultiply, (vert, ''), -5.0, -1700, 550)
    vert = unary(m, unreal.MaterialExpressionSaturate, (vert, ''), -1550, 550)    # 竖直面 1，顶面 0
    wz = mask(m, wpos, False, False, True, -2200, 0)
    fh = scalar(m, 'FloorHeight', 420.0, -2200, 100)
    t = op(m, unreal.MaterialExpressionDivide, (wz, ''), (fh, ''), -2000, 0)
    t = unary(m, unreal.MaterialExpressionFrac, (t, ''), -1850, 0)
    gf = scalar(m, 'SpandrelFrac', 0.38, -2000, 150)   # 每层下面这一段是白色窗间墙，上面是玻璃
    band = op(m, unreal.MaterialExpressionSubtract, (t, ''), (gf, ''), -1700, 50)
    band = op(m, unreal.MaterialExpressionMultiply, (band, ''), 60.0, -1550, 50)
    band = unary(m, unreal.MaterialExpressionSaturate, (band, ''), -1400, 50)
    wx = mask(m, wpos, True, False, False, -2200, 200)
    wy = mask(m, wpos, False, True, False, -2200, 280)
    h = node(m, unreal.MaterialExpressionIf, -1850, 250)   # 朝 X 的面沿 Y 排窗框，朝 Y 的面沿 X
    link(nx, '', h, 'A')
    link(ny, '', h, 'B')
    link(wy, '', h, 'A > B')
    link(wx, '', h, 'A == B')
    link(wx, '', h, 'A < B')
    mw = scalar(m, 'MullionSpacing', 180.0, -2000, 330)
    mt = op(m, unreal.MaterialExpressionDivide, (h, ''), (mw, ''), -1700, 250)
    mt = unary(m, unreal.MaterialExpressionFrac, (mt, ''), -1550, 250)
    mf = scalar(m, 'MullionFrac', 0.1, -1700, 350)
    mull = op(m, unreal.MaterialExpressionSubtract, (mt, ''), (mf, ''), -1400, 250)
    mull = op(m, unreal.MaterialExpressionMultiply, (mull, ''), 40.0, -1250, 250)
    mull = unary(m, unreal.MaterialExpressionSaturate, (mull, ''), -1100, 250)     # 0 = 窗框
    glass = op(m, unreal.MaterialExpressionMultiply, (band, ''), (mull, ''), -1000, 100)
    glass = op(m, unreal.MaterialExpressionMultiply, (glass, ''), (vert, ''), -850, 100)
    panel = vec(m, 'PanelColor', (0.62, 0.64, 0.67), -1000, -300)
    gcol = vec(m, 'GlassColor', (0.018, 0.035, 0.045), -1000, -150)
    mel.connect_material_property(lerp(m, (panel, ''), (gcol, ''), (glass, ''), -600, -200), '', P.MP_BASE_COLOR)
    pr = scalar(m, 'PanelRough', 0.62, -1000, 400)
    gr = scalar(m, 'GlassRough', 0.12, -1000, 500)
    mel.connect_material_property(lerp(m, (pr, ''), (gr, ''), (glass, ''), -600, 400), '', P.MP_ROUGHNESS)
    gm = scalar(m, 'GlassMetal', 0.4, -1000, 600)   # 玻璃带一点金属度：远处反射天空，冷色
    mel.connect_material_property(lerp(m, 0.0, (gm, ''), (glass, ''), -600, 600), '', P.MP_METALLIC)
    m.set_editor_property('used_with_nanite', True)
    m.set_editor_property('used_with_instanced_static_meshes', True)


def build_material(path, builder):
    folder, name = path.rsplit('/', 1)
    if eas.does_asset_exist(path):
        m = eas.load_asset(path)
        if not REBUILD:
            return m
        mel.delete_all_material_expressions(m)
    else:
        m = tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    builder(m)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    log('母材质', path, '已重建')
    return m


def make_mi(name, parent, scalars=(), vectors=(), textures=()):
    path = f'{DEST}/Materials/{name}'
    mi = eas.load_asset(path) if eas.does_asset_exist(path) else tools.create_asset(
        name, f'{DEST}/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', parent)
    for k, v in scalars:
        mel.set_material_instance_scalar_parameter_value(mi, k, float(v))
    for k, v in vectors:
        mel.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(*v, 1))
    for k, v in textures:
        mel.set_material_instance_texture_parameter_value(mi, k, eas.load_asset(v))
    eas.save_loaded_asset(mi)
    return mi


M_PAVE = build_material(f'{DEST}/Materials/M_TP_Paving', build_paving)
M_FACADE = build_material(f'{DEST}/Materials/M_TP_Facade', build_facade)
MR = eas.load_asset('/Game/Env/Materials/M_KitMR')
WHITE = '/Engine/EngineResources/WhiteSquareTexture'

# 地面：深石墨色（原来 0.44 的浅灰方砖 + 阴天 = 整片灰）。嵌线 24 米一道，对齐 x=0 / y=0 两条主轴
PAVE = make_mi('MI_TP_Paving', M_PAVE,
               scalars=(('TileSize', 1000.0), ('MacroSize', 5300.0), ('MacroAmount', 0.35), ('RoughScale', 1.0), ('RoughMin', 0.45),
                        ('FadeStart', 6000.0), ('FadeLength', 18000.0), ('AvgAlbedo', 0.72),
                        ('Grid', 2400.0), ('LineWidth', 7.0), ('LineGlow', 1.6), ('LineFadeStart', 9000.0), ('LineFadeLength', 9000.0)),
               vectors=(('Tint', (0.16, 0.168, 0.185)), ('LineColor', PHASE_CYAN), ('LineBase', (0.03, 0.035, 0.04)),
                        ('ArenaHalf', (11200.0, 8000.0, 0.0))))
FACADE = make_mi('MI_TP_Facade', M_FACADE,
                 scalars=(('FloorHeight', 420.0), ('SpandrelFrac', 0.3), ('MullionSpacing', 180.0), ('MullionFrac', 0.1),
                          ('PanelRough', 0.62), ('GlassRough', 0.35), ('GlassMetal', 0.0)),
                 # 玻璃带是中灰蓝、半哑光：深色玻璃（第 1 / 2 轮 0.02–0.1）和镜面玻璃（第 3 轮，高处看反射的是深色地面）整栋楼都是黑白斑马纹，太花、抢眼
                 vectors=(('PanelColor', (0.52, 0.54, 0.57)), ('GlassColor', (0.16, 0.2, 0.25))))
LOGO_CYAN = make_mi('MI_TP_LogoCyan', MR,
                    scalars=(('MetalScale', 0.0), ('EmisStrength', 2.2), ('RoughScale', 1.0)),
                    vectors=(('Tint', (0.85, 0.88, 0.9)), ('EmisColor', PHASE_CYAN)),
                    textures=(('BaseTex', WHITE), ('EmisTex', WHITE)))
STRIP_CYAN = make_mi('MI_TP_StripCyan', MR,
                     scalars=(('MetalScale', 0.0), ('EmisStrength', 2.0), ('RoughScale', 1.0)),
                     vectors=(('Tint', (0.6, 0.8, 0.85)), ('EmisColor', PHASE_CYAN)),
                     textures=(('BaseTex', WHITE), ('EmisTex', WHITE)))
log('材质实例 MI_TP_Paving / MI_TP_Facade / MI_TP_LogoCyan / MI_TP_StripCyan')

# ================================================================ 地图
unreal.EditorLoadingAndSavingUtils.load_map(MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
actors = sub.get_all_level_actors()
old = [a for a in actors if TAG in [str(t) for t in a.tags]]
for a in old:
    sub.destroy_actor(a)
actors = [a for a in sub.get_all_level_actors()]
log('删掉上次加的', len(old), '个')


def spawn(cls, loc, rot=None, label='', folder='TechPark/Dress'):
    a = sub.spawn_actor_from_class(cls, loc, rot or unreal.Rotator(0, 0, 0))
    a.tags = [TAG]
    a.set_actor_label(label)
    a.set_folder_path(folder)
    return a


def deco_mesh(m, loc, rot, scale, label, folder, shadow=False):
    a = spawn(unreal.StaticMeshActor, loc, rot, label, folder)
    a.set_actor_scale3d(scale)
    c = a.static_mesh_component
    c.set_static_mesh(m)
    c.set_editor_property('mobility', unreal.ComponentMobility.STATIC)
    c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    c.set_editor_property('can_character_step_up_on', unreal.CanBeCharacterBase.ECB_NO)
    c.set_editor_property('cast_shadow', shadow)
    if not shadow:
        c.set_editor_property('affect_distance_field_lighting', False)
    return a


# ---- 地面换材质（Ground 是 ue_techpark_build.py 摆的那块 700×600 米方块，碰撞不动）
ground = [a for a in actors if a.get_actor_label() == 'Ground']
for g in ground:
    g.static_mesh_component.set_material(0, PAVE)
log('地面换成 MI_TP_Paving', len(ground))

# ---- 场外远地面：6 公里无碰撞平面，比 Ground 顶面低 3 厘米（Ground 范围内被盖住）
plane = eas.load_asset('/Engine/BasicShapes/Plane')
# 第 2 轮 6 公里：高空镜头里平面边缘外还露出一条深蓝（大气的「地面」），加到 30 公里；场外用暗一点、大块斑驳的版本（远看像成片城区，不是一块平板）
FAR = make_mi('MI_TP_FarGround', M_PAVE,
              scalars=(('TileSize', 1000.0), ('MacroSize', 26000.0), ('MacroAmount', 0.9), ('RoughScale', 1.0), ('RoughMin', 0.5),
                       ('FadeStart', 3000.0), ('FadeLength', 12000.0), ('AvgAlbedo', 0.72), ('LineGlow', 0.0),
                       ('BlockSize', 6000.0), ('BlockAmount', 1.0)),
              vectors=(('Tint', (0.12, 0.128, 0.14)), ('ArenaHalf', (0.0, 0.0, 0.0))))
far = deco_mesh(plane, unreal.Vector(0, 0, -3), unreal.Rotator(0, 0, 0), unreal.Vector(30000, 30000, 1), 'FarGround', 'TechPark/Dress')
far.static_mesh_component.set_material(0, FAR)
far.static_mesh_component.set_editor_property('affect_dynamic_indirect_lighting', False)

# ---- 远景楼：白面板换楼层材质（逐 actor 覆盖，不改网格资源）；PEC 总部塔 C30 不换
L = json.load(open('D:/RobotSlice/Inbox/_k051/layout.json', encoding='utf-8'))


def facade_override(a):
    c = a.static_mesh_component
    sm = c.get_editor_property("static_mesh")
    n = 0
    for i, s in enumerate(sm.get_editor_property('static_materials')):
        if 'Panel' in str(s.get_editor_property('material_slot_name')):
            c.set_material(i, FACADE)
            n += 1
    return n


bd = [a for a in actors if a.get_actor_label().startswith('BD_')]
nf = sum(facade_override(a) for a in bd if 'TPB_C30' not in a.get_actor_label())
log('远景楼', len(bd), '栋，换楼层材质的槽', nf)

# ---- 外圈远景楼：350–950 米一圈，固定种子；离已有远景楼 / 彼此 75 米以上；总部塔正后方那一扇区不摆高楼（让它的剪影单独立着）
rng = random.Random(1031)
kinds = ['C10', 'C14', 'C17', 'C18', 'C26', 'C27', 'C28', 'C31', 'C32', 'C33', 'C35', 'C36']
taken = [(it['loc'][0], it['loc'][1]) for it in L['items'] if it.get('tag') == 'backdrop']
hq_ang = math.atan2(215.0, -20.0)
ring = 0
tries = 0
while ring < 34 and tries < 4000:
    tries += 1
    ang = rng.uniform(-math.pi, math.pi)
    rad = 350.0 + rng.random() ** 0.8 * 600.0
    x, y = rad * math.cos(ang), rad * math.sin(ang)
    if abs(x) < 180 and abs(y) < 130:   # 别进到已有那圈里
        continue
    if any((x - tx) ** 2 + (y - ty) ** 2 < 75.0 ** 2 for tx, ty in taken):
        continue
    d_ang = abs((ang - hq_ang + math.pi) % (2 * math.pi) - math.pi)
    k = rng.choice(kinds)
    sc = 1.5 + rng.random() * 0.6 + (rad - 350.0) / 600.0 * 0.9   # 越远越高，能从近处楼顶上冒出来
    if d_ang < math.radians(14) and k in ('C26', 'C27', 'C31', 'C33', 'C35'):
        continue
    m = eas.load_asset(f'{DEST}/Backdrop/SM_TPB_{k}')
    a = deco_mesh(m, V(x, y, 0), unreal.Rotator(roll=0, pitch=0, yaw=-rng.choice(range(0, 180, 15))), unreal.Vector(sc, sc, sc),
                  f'RING_{ring:02d}_{k}', 'TechPark/Dress/Ring')
    facade_override(a)
    taken.append((x, y))
    ring += 1
log('外圈远景楼', ring, '栋')

# ---- 外圈之间的矮楼（第 2 轮高空镜头里外圈楼一栋栋立在空地上，像飘着）：300–900 米再撒一层 25–50 米高的楼，彼此 45 米以上
low = 0
tries = 0
while low < 60 and tries < 6000:
    tries += 1
    ang = rng.uniform(-math.pi, math.pi)
    rad = 290.0 + rng.random() * 620.0
    x, y = rad * math.cos(ang), rad * math.sin(ang)
    if abs(x) < 170 and abs(y) < 125:
        continue
    if any((x - tx) ** 2 + (y - ty) ** 2 < 45.0 ** 2 for tx, ty in taken):
        continue
    k = rng.choice(['C10', 'C14', 'C17', 'C18', 'C28', 'C36'])
    sc = (0.9 + rng.random() * 0.5) if k != 'C36' else 0.8
    m = eas.load_asset(f'{DEST}/Backdrop/SM_TPB_{k}')
    a = deco_mesh(m, V(x, y, 0), unreal.Rotator(roll=0, pitch=0, yaw=-rng.choice(range(0, 180, 15))), unreal.Vector(sc, sc, sc),
                  f'LOW_{low:02d}_{k}', 'TechPark/Dress/Ring')
    facade_override(a)
    taken.append((x, y))
    low += 1
log('外圈矮楼', low, '栋')

# ---- 面向场地的几栋远景楼：楼顶挂青色 PHASE 标识 + 一圈青色灯带（只挑朝向是 0 / 90 度的，包围盒贴着立面）
logo = eas.load_asset(f'{DEST}/Brand/SM_TP_LogoPHASE')
cube = eas.load_asset('/Engine/BasicShapes/Cube')
signs = 0
for a in bd:
    lab = a.get_actor_label()
    if not any(k in lab for k in ('TPB_C28', 'TPB_C17')):
        continue
    org, ext = a.get_actor_bounds(False)
    yaw = a.get_actor_rotation().yaw % 90.0
    if min(yaw, 90.0 - yaw) > 1.0:
        continue
    loc = a.get_actor_location()
    # 朝场地中心那一面：|x| 大的楼挂在朝 x=0 的面，否则挂在朝 y=0 的面。标识网格正面朝 UE +Y
    if abs(loc.x) > abs(loc.y):
        face_x = org.x - ext.x if loc.x > 0 else org.x + ext.x
        rot = unreal.Rotator(roll=0, pitch=0, yaw=90.0 if loc.x > 0 else -90.0)   # +Y 转到 -X / +X
        w = ext.y * 2.0
        pos = unreal.Vector(face_x + (-30 if loc.x > 0 else 30), org.y, 0)
    else:
        face_y = org.y + ext.y if loc.y < 0 else org.y - ext.y      # UE +Y = 南；北边的楼（y<0）挂南面
        rot = unreal.Rotator(roll=0, pitch=0, yaw=0.0 if loc.y < 0 else 180.0)
        w = ext.x * 2.0
        pos = unreal.Vector(org.x, face_y + (30 if loc.y < 0 else -30), 0)
    top = org.z + ext.z
    s = w * 0.62 / 100.0
    pos.z = top - ext.z * 0.16 - s * 100.0 * 0.12
    lg = deco_mesh(logo, pos, rot, unreal.Vector(s, s, s), f'SIGN_{lab}', 'TechPark/Dress/Signs')
    lg.static_mesh_component.set_material(0, LOGO_CYAN)
    # 楼顶下面一圈灯带（4 条薄方块，贴包围盒外 15 厘米；1.2 米高——300 米外一个像素约 0.4 米，再细就看不见了）
    zz = top - 300.0
    for i, (cx, cy, sx, sy) in enumerate(((org.x, org.y - ext.y - 15, ext.x * 2 / 100 + 0.3, 0.3), (org.x, org.y + ext.y + 15, ext.x * 2 / 100 + 0.3, 0.3),
                                          (org.x - ext.x - 15, org.y, 0.3, ext.y * 2 / 100 + 0.3), (org.x + ext.x + 15, org.y, 0.3, ext.y * 2 / 100 + 0.3))):
        st = deco_mesh(cube, unreal.Vector(cx, cy, zz), unreal.Rotator(0, 0, 0), unreal.Vector(sx, sy, 1.2), f'STRIP_{lab}_{i}', 'TechPark/Dress/Signs')
        st.static_mesh_component.set_material(0, STRIP_CYAN)
    signs += 1
log('楼顶标识', signs, '处')

# ---- 后期（优先级 200，压过光照插件的 100）
ppv = spawn(unreal.PostProcessVolume, unreal.Vector(0, 0, 0), None, 'PP_TechParkDress', 'TechPark/Dress')
ppv.set_editor_property('unbound', True)
ppv.set_editor_property('priority', 200.0)
pp = ppv.get_editor_property('settings')
for key, value in (('white_temp', 5800.0),                                   # 白平衡往冷里拉一点（太阳 5500K 偏暖）
                   ('color_saturation', unreal.Vector4(0.97, 0.99, 1.02, 0.9)),
                   ('color_contrast', unreal.Vector4(1.0, 1.0, 1.0, 1.08)),
                   ('color_gain_shadows', unreal.Vector4(0.97, 0.99, 1.03, 1.0)),   # 阴影偏一点青蓝（第 1 轮 0.92/0.98/1.08 背光面发藏青）
                   ('color_gain_highlights', unreal.Vector4(1.0, 1.0, 1.0, 1.0)),
                   ('color_gain', unreal.Vector4(0.98, 1.0, 1.02, 1.0)),
                   ('color_saturation_shadows', unreal.Vector4(1.0, 1.0, 1.0, 0.8)),   # 背光面被蓝天照成一片藏青（第 4 轮），阴影去一点饱和
                   ('color_offset_shadows', unreal.Vector4(0.0, 0.0, 0.0, 0.0)),
                   ('bloom_intensity', 0.35), ('auto_exposure_bias', -0.6),           # 晴天白色大面会顶到全白（插件的白天曝光上限 EV 13.4 偏亮），压 0.6 档
                   ('local_exposure_highlight_contrast_scale', 0.8), ('local_exposure_shadow_contrast_scale', 0.9),
                   ('vignette_intensity', 0.3), ('film_grain_intensity', 0.04), ('scene_fringe_intensity', 0.15)):
    pp.set_editor_property('override_' + key, True)
    pp.set_editor_property(key, value)
ppv.set_editor_property('settings', pp)

# ---- 光照导演的逐地图调参（TuningSettings 是 actor 实例属性，存在地图里）
dirs = [a for a in actors if 'LightingDirector' in a.get_class().get_name()]
for d in dirs:
    t = d.get_editor_property('tuning_settings')
    # 太阳方位：插件按真实天文算（北纬 30.8°、9 月 12 日），15 点太阳在方位 248°（西南偏西）、高 38°；地图的北向偏角 NorthYaw 默认 -75，
    # 太阳几乎正对 -X（正西），从南边看过来的镜头（总览、开场、中央往北）看到的南立面只有掠射光、和阴天一样平。
    # 改成 -123：太阳转到世界方位 125°（西南偏南），南立面和西立面受光、东 / 北立面背光，影子朝东北斜着铺过广场。其余天文参数照抄原日光剖面
    prof = None
    try:
        prof = d.get_editor_property('time_controller').get_editor_property('profile')
    except Exception as e:
        log('读不到日光剖面，用默认', e)
    src = prof or unreal.get_default_object(unreal.LightingDayProfile)
    t = d.get_editor_property('tuning_settings')
    for k in ('latitude', 'longitude', 'utc_offset'):
        t.set_editor_property(k, float(src.get_editor_property(k)))
    doy = int(src.get_editor_property('day_of_year'))
    yr = int(src.get_editor_property('calendar_year'))
    import datetime
    dt = datetime.date(yr, 1, 1) + datetime.timedelta(days=doy - 1)
    t.set_editor_property('calendar_year', float(dt.year))
    t.set_editor_property('calendar_month', float(dt.month))
    t.set_editor_property('calendar_day', float(dt.day))
    t.set_editor_property('real_moon', 1.0 if src.get_editor_property('real_moon') else 0.0)
    log('日光剖面', '自定义' if prof else '默认', '纬度', src.get_editor_property('latitude'), '原 NorthYaw', src.get_editor_property('north_yaw'), '日期', dt)
    d.set_editor_property('tuning_settings', t)
    for k, v in (('override_astronomy', True), ('north_yaw', -123.0),
                 ('sun_multiplier', 0.75),   # 82k 勒克斯的太阳配插件白天 EV 13 上限，白色大面全顶白；压到 0.75，影子照样清楚
                 ('sky_multiplier', 1.3),    # 背光面太黑（第 4 轮），天光提 30%
                 ('override_clouds', True), ('cloud_coverage', 0.62), ('cloud_density', 0.5), ('cloud_bottom_km', 2.0),
                 ('cloud_depth_km', 2.0), ('cloud_shape_scale', 1.3), ('cloud_shadow_strength', 0.35),   # 覆盖率 0.4 时一朵云都没有（插件里 lerp(-0.8, 0.5, 覆盖率)）
                 ('override_weather', True), ('fog_density', 0.009), ('fog_height_falloff', 0.2),   # 0.012 / 0.1 时地平线一片白墙；0.006 时远处平地一直清楚到地平线
                 ('rain_intensity', 0.0), ('wetness', 0.0), ('wind_strength', 0.2), ('wind_direction', 45.0), ('lightning_rate', 0.0)):
        t.set_editor_property(k, v)
    d.set_editor_property('tuning_settings', t)
log('光照导演', len(dirs), '个，已设逐地图调参')

if 'nosave' not in sys.argv:
    unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)
    log('已保存', MAP)
