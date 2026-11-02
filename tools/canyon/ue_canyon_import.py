"""峡谷（夺芯）：导入贴图、地形网格（Blender 出的 FBX），建材质，全部放在 /Game/Env/Canyon。可重复跑（覆盖）。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/canyon/ue_canyon_import.py" -AllowCommandletRendering -RenderOffscreen -unattended
环境变量 CANYON_SKIP_MESH=1 只重建材质。"""
import os
import unreal

for cvar in ('Interchange.FeatureFlags.Import.FBX', 'r.Interchange.FeatureFlags.Import.FBX'):
    unreal.SystemLibrary.execute_console_command(None, f'{cvar} 0')

SRC = 'D:/RobotSlice/SourceAssets/canyon'
DEST = '/Game/Env/Canyon'
eas = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
P = unreal.MaterialProperty
S = unreal.MaterialSamplerType
log = lambda *a: unreal.log('[canyon] ' + ' '.join(str(x) for x in a))


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
PH = {'cliff_side': ('diff', 'nor_gl', 'arm'), 'rock_boulder_cracked': ('diff', 'nor_gl', 'arm'),
      'gravelly_sand': ('diff', 'nor_gl', 'arm'), 'sandy_gravel_02': ('diff', 'nor_gl', 'arm'),
      'low_tide_rocks': ('diff', 'nor_gl', 'arm'), 'rocks_ground_06': ('diff', 'nor_gl', 'arm')}
TEX = f'{DEST}/Textures'
tasks = []
for tid, maps in PH.items():
    for mp in maps:
        name = f'T_{tid}_{mp}'
        if not eas.does_asset_exist(f'{TEX}/{name}'):
            tasks.append(task(f'{SRC}/textures/{tid}/{tid}_{mp}_2k.jpg', TEX, name))
for g in ('T_Canyon_Noise', 'T_Canyon_Strata', 'T_Canyon_WaterN'):
    tasks.append(task(f'{SRC}/textures/_gen/{g}.png', TEX, g))
if tasks:
    tools.import_asset_tasks(tasks)
for path in eas.list_assets(TEX, recursive=False, include_folder=False):
    t = eas.load_asset(path)
    n = path.split('/')[-1].split('.')[0]
    if n.endswith('_nor_gl') or n == 'T_Canyon_WaterN':
        t.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
        t.set_editor_property('srgb', False)
        t.set_editor_property('flip_green_channel', n.endswith('_nor_gl'))   # Poly Haven 是 OpenGL 法线
    elif n.endswith('_arm') or n == 'T_Canyon_Noise':
        t.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_MASKS)
        t.set_editor_property('srgb', False)
    elif n == 'T_Canyon_Strata':
        t.set_editor_property('address_x', unreal.TextureAddress.TA_CLAMP)
        t.set_editor_property('address_y', unreal.TextureAddress.TA_CLAMP)
        t.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    eas.save_loaded_asset(t)
log('贴图', len(eas.list_assets(TEX, recursive=False, include_folder=False)))


def T(name):
    return eas.load_asset(f'{TEX}/{name}')


# ---------------------------------------------------------------- 材质节点小工具
class G:
    def __init__(self, path):
        folder, name = path.rsplit('/', 1)
        if eas.does_asset_exist(path):
            self.m = eas.load_asset(path)
            mel.delete_all_material_expressions(self.m)
            log('清空旧节点后剩', mel.get_num_material_expressions(self.m))
        else:
            self.m = tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
        self.k = 0

    def n(self, cls, **props):
        self.k += 1
        node = mel.create_material_expression(self.m, cls, -400 - 260 * (self.k // 40), (self.k % 40) * 110 - 2000)
        for k, v in props.items():
            node.set_editor_property(k, v)
        return node

    def link(self, a, b, inp, out=''):
        ok = mel.connect_material_expressions(a, out, b, inp)
        assert ok, f'连线失败 {a.get_name()}.{out} -> {b.get_name()}.{inp}'

    def op(self, cls, a, b, ao='', bo=''):
        a = self.c(a) if isinstance(a, (int, float)) else a
        b = self.c(b) if isinstance(b, (int, float)) else b
        n = self.n(cls)
        self.link(a, n, 'A', ao)
        self.link(b, n, 'B', bo)
        return n

    def mul(self, a, b, ao='', bo=''):
        return self.op(unreal.MaterialExpressionMultiply, a, b, ao, bo)

    def add(self, a, b, ao='', bo=''):
        return self.op(unreal.MaterialExpressionAdd, a, b, ao, bo)

    def sub(self, a, b, ao='', bo=''):
        return self.op(unreal.MaterialExpressionSubtract, a, b, ao, bo)

    def div(self, a, b, ao='', bo=''):
        return self.op(unreal.MaterialExpressionDivide, a, b, ao, bo)

    def append(self, a, b, ao='', bo=''):
        return self.op(unreal.MaterialExpressionAppendVector, a, b, ao, bo)

    def dot(self, a, b):
        return self.op(unreal.MaterialExpressionDotProduct, a, b)

    def c(self, v):
        return self.n(unreal.MaterialExpressionConstant, r=float(v))

    def c3(self, r, g, b):
        return self.n(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(r, g, b, 1))

    def scalar(self, name, v):
        return self.n(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=float(v))

    def vec(self, name, rgb):
        return self.n(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1))

    def mask(self, a, ch, out=''):
        n = self.n(unreal.MaterialExpressionComponentMask, r='r' in ch, g='g' in ch, b='b' in ch, a='a' in ch)
        self.link(a, n, '', out)
        return n

    def one(self, cls, a, out=''):
        n = self.n(cls)
        self.link(a, n, '', out)
        return n

    def abs(self, a):
        return self.one(unreal.MaterialExpressionAbs, a)

    def sat(self, a):
        return self.one(unreal.MaterialExpressionSaturate, a)

    def inv(self, a):
        return self.one(unreal.MaterialExpressionOneMinus, a)

    def normalize(self, a):
        n = self.n(unreal.MaterialExpressionNormalize)
        self.link(a, n, 'VectorInput')
        return n

    def lerp(self, a, b, alpha, ao='', bo=''):
        a = self.c(a) if isinstance(a, (int, float)) else a
        b = self.c(b) if isinstance(b, (int, float)) else b
        n = self.n(unreal.MaterialExpressionLinearInterpolate)
        self.link(a, n, 'A', ao)
        self.link(b, n, 'B', bo)
        alpha = self.c(alpha) if isinstance(alpha, (int, float)) else alpha
        self.link(alpha, n, 'Alpha')
        return n

    def smooth(self, lo, hi, v):
        t = self.sat(self.mul(self.sub(v, lo), 1.0 / (hi - lo)))
        return self.mul(self.mul(t, t), self.sub(3.0, self.mul(t, 2.0)))

    def tex(self, texture, uv, stype, clamp=False, param=None):
        cls = unreal.MaterialExpressionTextureSampleParameter2D if param else unreal.MaterialExpressionTextureSample
        props = dict(texture=texture, sampler_type=stype,
                     sampler_source=unreal.SamplerSourceMode.SSM_CLAMP_WORLD_GROUP_SETTINGS if clamp else unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
        if param:
            props['parameter_name'] = param
        node = self.n(cls, **props)
        if uv is not None:
            self.link(uv, node, 'UVs')
        return node

    def out(self, node, prop, o=''):
        mel.connect_material_property(node, o, prop)

    def finish(self, tangent_normal):
        self.m.set_editor_property('tangent_space_normal', tangent_normal)
        for flag in ('used_with_nanite', 'used_with_instanced_static_meshes'):
            try:
                self.m.set_editor_property(flag, True)
            except Exception as e:
                log('用途标记', flag, e)
        mel.recompile_material(self.m)
        eas.save_loaded_asset(self.m)
        log('材质', self.m.get_path_name(), '节点', mel.get_num_material_expressions(self.m))
        return self.m


def triplanar_weights(g, N):
    a = g.abs(N)
    a2 = g.mul(a, a)
    w = g.mul(a2, a2)
    w = g.div(w, g.dot(w, g.c3(1, 1, 1)))
    return g.mask(w, 'r'), g.mask(w, 'g'), g.mask(w, 'b')


def world_uvs(g, WP, tile):
    x, y, z = g.mask(WP, 'r'), g.mask(WP, 'g'), g.mask(WP, 'b')
    nz = g.mul(z, -1.0)
    uvx = g.div(g.append(y, nz), tile)
    uvy = g.div(g.append(x, nz), tile)
    uvz = g.div(g.append(x, y), tile)
    return uvx, uvy, uvz


def tri_sample(g, texture, stype, uvs, ws, out='RGB'):
    sx = g.tex(texture, uvs[0], stype); sy = g.tex(texture, uvs[1], stype); sz = g.tex(texture, uvs[2], stype)
    return g.add(g.add(g.mul(sx, ws[0], out), g.mul(sy, ws[1], out)), g.mul(sz, ws[2], out)), (sx, sy, sz)


def tri_normal(g, N, samples, ws, strength):
    """UDN 三平面法线：N + Σ w·(切线法线摆到对应平面)"""
    sx, sy, sz = samples
    zero = g.c(0.0)
    vx = g.append(g.append(zero, g.mask(sx, 'r', 'RGB')), g.mask(sx, 'g', 'RGB'))
    vy = g.append(g.append(g.mask(sy, 'r', 'RGB'), zero), g.mask(sy, 'g', 'RGB'))
    vz = g.append(g.append(g.mask(sz, 'r', 'RGB'), g.mul(g.mask(sz, 'g', 'RGB'), -1.0)), zero)
    d = g.add(g.add(g.mul(vx, ws[0]), g.mul(vy, ws[1])), g.mul(vz, ws[2]))
    return g.add(N, g.mul(d, strength))


def top_normal(g, N, s, strength):
    v = g.append(g.append(g.mask(s, 'r'), g.mul(g.mask(s, 'g'), -1.0)), g.c(0.0))
    return g.add(N, g.mul(v, strength))


# ---------------------------------------------------------------- 地形母材质：三平面岩壁 + 顶面沙砾 / 台地土石 + 岩层色带 + 流痕 + 河岸湿
def build_terrain_material():
    g = G(f'{DEST}/Materials/M_Canyon_Terrain')
    WP = g.n(unreal.MaterialExpressionWorldPosition)
    N = g.n(unreal.MaterialExpressionVertexNormalWS)
    ws = triplanar_weights(g, N)
    x, y, z = g.mask(WP, 'r'), g.mask(WP, 'g'), g.mask(WP, 'b')
    ay = g.abs(y)
    Nz = g.mask(N, 'b')
    # 噪声
    noise = g.tex(T('T_Canyon_Noise'), g.div(g.append(x, y), g.scalar('MacroTile', 5200.0)), S.SAMPLERTYPE_MASKS)
    nR, nG = g.mask(noise, 'r', 'RGB'), g.mask(noise, 'g', 'RGB')
    streak_uv = g.append(g.div(g.add(x, y), 2600.0), g.div(z, 11000.0))
    streak = g.mask(g.tex(T('T_Canyon_Noise'), streak_uv, S.SAMPLERTYPE_MASKS), 'b', 'RGB')
    # 岩壁（cliff_side，三平面）
    ruv = world_uvs(g, WP, g.scalar('RockTile', 850.0))
    rockD, _ = tri_sample(g, T('T_cliff_side_diff'), S.SAMPLERTYPE_COLOR, ruv, ws)
    rockA, _ = tri_sample(g, T('T_cliff_side_arm'), S.SAMPLERTYPE_MASKS, ruv, ws)
    _, rockNs = tri_sample(g, T('T_cliff_side_nor_gl'), S.SAMPLERTYPE_NORMAL, ruv, ws)
    rockN = tri_normal(g, N, rockNs, ws, g.scalar('RockNormal', 1.0))
    # 大尺度第二层岩（rock_boulder_cracked，三平面，大铺贴）破重复
    buv = world_uvs(g, WP, g.scalar('Rock2Tile', 2300.0))
    rock2D, _ = tri_sample(g, T('T_rock_boulder_cracked_diff'), S.SAMPLERTYPE_COLOR, buv, ws)
    rockD = g.lerp(rockD, rock2D, g.mul(g.smooth(0.42, 0.72, nG), g.scalar('Rock2Mix', 0.45)))
    # 岩层色带（按高度）
    sv = g.sub(1.0, g.div(g.add(g.add(z, 500.0), g.mul(g.sub(nG, 0.5), 160.0)), 7000.0))
    strata = g.tex(T('T_Canyon_Strata'), g.append(g.c(0.5), sv), S.SAMPLERTYPE_COLOR, clamp=True)
    rockD = g.mul(rockD, g.lerp(1.0, g.mul(strata, 1.33), g.scalar('StrataAmount', 0.7), bo=''))
    # 流痕（沙漠漆）
    varn = g.mul(g.mul(streak, g.scalar('Varnish', 0.55)), g.smooth(150.0, 700.0, z))
    rockD = g.mul(rockD, g.lerp(1.0, 0.45, varn))
    rockD = g.mul(rockD, g.vec('RockTint', (1.0, 0.96, 0.92)))
    # 顶面：谷底沙砾 / 河岸卵石 / 栈道与台地沙土 / 台地光岩
    def top_set(tid, tile):
        uv = g.div(g.append(x, y), g.scalar(f'{tid}_Tile', tile))
        return (g.tex(T(f'T_{tid}_diff'), uv, S.SAMPLERTYPE_COLOR), g.tex(T(f'T_{tid}_arm'), uv, S.SAMPLERTYPE_MASKS),
                g.tex(T(f'T_{tid}_nor_gl'), uv, S.SAMPLERTYPE_NORMAL))
    s1 = top_set('gravelly_sand', 420.0)
    s2 = top_set('sandy_gravel_02', 560.0)
    s3 = top_set('low_tide_rocks', 300.0)
    s4 = top_set('rock_boulder_cracked', 650.0)
    bank = g.sub(1.0, g.smooth(520.0, 980.0, ay))
    wallbase = g.mul(g.smooth(1380.0, 1760.0, ay), g.smooth(0.45, 0.7, nR))
    m_floor = g.sat(g.add(bank, wallbase))
    m_plat_rock = g.smooth(0.46, 0.64, g.add(nR, g.mul(g.sub(nG, 0.5), 0.3)))
    zfloor = g.sub(1.0, g.smooth(150.0, 320.0, z))
    zplat = g.smooth(1500.0, 1900.0, z)
    m_ledge_peb = g.smooth(0.55, 0.75, nG)

    def mix(i, o='RGB'):
        floor_ = g.lerp(s1[i], s3[i], m_floor, o, o)
        ledge_ = g.lerp(s2[i], s3[i], g.mul(m_ledge_peb, 0.6), o, o)
        plat_ = g.lerp(s2[i], s4[i], m_plat_rock, o, o)
        return g.lerp(g.lerp(ledge_, floor_, zfloor), plat_, zplat)
    gD = g.mul(mix(0), g.vec('GroundTint', (1.0, 0.93, 0.86)))
    gA = mix(1)
    gNt = mix(2)
    gN = top_normal(g, N, gNt, g.scalar('GroundNormal', 1.0))
    # 坡度混合
    flat = g.smooth(0.58, 0.86, g.add(Nz, g.mul(g.sub(nG, 0.5), 0.35)))
    base = g.lerp(rockD, gD, flat)
    macro = g.lerp(0.8, 1.12, nR)
    base = g.mul(base, macro)
    wet = g.mul(bank, g.sub(1.0, g.smooth(8.0, 70.0, z)))
    base = g.mul(base, g.lerp(1.0, 0.6, wet))
    rough = g.lerp(g.mask(rockA, 'g'), g.mask(gA, 'g'), flat)
    rough = g.lerp(rough, 0.32, wet)
    ao = g.lerp(g.mask(rockA, 'r'), g.mask(gA, 'r'), flat)
    nrm = g.normalize(g.lerp(rockN, gN, flat))
    g.out(base, P.MP_BASE_COLOR)
    g.out(rough, P.MP_ROUGHNESS)
    g.out(ao, P.MP_AMBIENT_OCCLUSION)
    g.out(nrm, P.MP_NORMAL)
    g.out(g.c(0.0), P.MP_METALLIC)
    return g.finish(False)


# ---------------------------------------------------------------- 浅河水面（不透明：河床卵石 + 动态细波 + 低粗糙度反射）
def build_water_material():
    g = G(f'{DEST}/Materials/M_Canyon_Water')
    WP = g.n(unreal.MaterialExpressionWorldPosition)
    xy = g.mask(WP, 'rg')
    bed = g.tex(T('T_low_tide_rocks_diff'), g.div(xy, 300.0), S.SAMPLERTYPE_COLOR)
    noise = g.tex(T('T_Canyon_Noise'), g.div(xy, 2500.0), S.SAMPLERTYPE_MASKS)
    tint = g.vec('WaterTint', (0.42, 0.48, 0.44))
    base = g.mul(g.mul(bed, tint, 'RGB'), g.lerp(0.75, 1.1, g.mask(noise, 'r', 'RGB')))
    t = g.n(unreal.MaterialExpressionTime)
    p1 = g.n(unreal.MaterialExpressionPanner, speed_x=0.035, speed_y=0.012)
    g.link(g.div(xy, 260.0), p1, 'Coordinate'); g.link(t, p1, 'Time')
    p2 = g.n(unreal.MaterialExpressionPanner, speed_x=0.022, speed_y=-0.018)
    g.link(g.div(xy, 620.0), p2, 'Coordinate'); g.link(t, p2, 'Time')
    n1 = g.tex(T('T_Canyon_WaterN'), p1, S.SAMPLERTYPE_NORMAL)
    n2 = g.tex(T('T_Canyon_WaterN'), p2, S.SAMPLERTYPE_NORMAL)
    nb = g.tex(T('T_low_tide_rocks_nor_gl'), g.div(xy, 300.0), S.SAMPLERTYPE_NORMAL)
    nsum = g.add(g.add(g.mask(n1, 'rg', 'RGB'), g.mask(n2, 'rg', 'RGB')), g.mul(g.mask(nb, 'rg', 'RGB'), 0.25))
    nrm = g.normalize(g.append(g.mul(nsum, g.scalar('RippleStrength', 0.35)), g.c(1.0)))
    g.out(base, P.MP_BASE_COLOR)
    g.out(g.scalar('Roughness', 0.05), P.MP_ROUGHNESS)
    g.out(g.c(0.6), P.MP_SPECULAR)
    g.out(nrm, P.MP_NORMAL)
    return g.finish(True)


# ---------------------------------------------------------------- 石头道具：沿用模型自己的法线，颜色用和崖壁一样的岩石三平面 + 色带
def build_prop_material():
    g = G(f'{DEST}/Materials/M_Canyon_Prop')
    WP = g.n(unreal.MaterialExpressionWorldPosition)
    N = g.n(unreal.MaterialExpressionVertexNormalWS)
    ws = triplanar_weights(g, N)
    z = g.mask(WP, 'b')
    uv = world_uvs(g, WP, g.scalar('PropTile', 380.0))
    d1, _ = tri_sample(g, T('T_rock_boulder_cracked_diff'), S.SAMPLERTYPE_COLOR, uv, ws)
    uv2 = world_uvs(g, WP, g.scalar('PropTile2', 700.0))
    d2, _ = tri_sample(g, T('T_cliff_side_diff'), S.SAMPLERTYPE_COLOR, uv2, ws)
    noise = g.tex(T('T_Canyon_Noise'), g.div(g.mask(WP, 'rg'), 1800.0), S.SAMPLERTYPE_MASKS)
    d = g.lerp(d1, d2, g.mul(g.mask(noise, 'g', 'RGB'), g.scalar('CliffMix', 0.6)))
    sv = g.sub(1.0, g.div(g.add(z, 500.0), 7000.0))
    strata = g.tex(T('T_Canyon_Strata'), g.append(g.c(0.5), sv), S.SAMPLERTYPE_COLOR, clamp=True)
    d = g.mul(g.mul(d, g.lerp(1.0, g.mul(strata, 1.33), 0.5)), g.vec('Tint', (0.95, 0.9, 0.86)))
    top = g.smooth(0.6, 0.95, g.mask(N, 'b'))
    sand = g.tex(T('T_gravelly_sand_diff'), g.div(g.mask(WP, 'rg'), 300.0), S.SAMPLERTYPE_COLOR)
    d = g.lerp(d, sand, g.mul(top, g.scalar('DustOnTop', 0.35)), '', 'RGB')
    nt = g.tex(T('T_rock_boulder_cracked_nor_gl'), None, S.SAMPLERTYPE_NORMAL, param='PropNormal')
    g.out(d, P.MP_BASE_COLOR)
    g.out(nt, P.MP_NORMAL, 'RGB')
    g.out(g.scalar('Roughness', 0.82), P.MP_ROUGHNESS)
    return g.finish(True)


def make_mi(name, parent, scalars=None, vectors=None, textures=None):
    path = f'{DEST}/Materials/{name}'
    mi = eas.load_asset(path) if eas.does_asset_exist(path) else tools.create_asset(
        name, f'{DEST}/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', parent)
    for k, v in (scalars or {}).items():
        mel.set_material_instance_scalar_parameter_value(mi, k, v)
    for k, v in (vectors or {}).items():
        mel.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(v[0], v[1], v[2], 1))
    for k, v in (textures or {}).items():
        mel.set_material_instance_texture_parameter_value(mi, k, eas.load_asset(v))
    eas.save_loaded_asset(mi)
    return mi


m_terrain = build_terrain_material()
m_water = build_water_material()
m_prop = build_prop_material()
mi_terrain = make_mi('MI_Canyon_Terrain', m_terrain)
mi_far = make_mi('MI_Canyon_Far', m_terrain, scalars={'RockTile': 1600.0, 'MacroTile': 12000.0})
mi_water = make_mi('MI_Canyon_Water', m_water)
STONES = '/Game/Environments/AndreyPestryakov/Environment_Pack_01/Stones/Textures'
mi_sbig = make_mi('MI_Canyon_StoneBig', m_prop, textures={'PropNormal': f'{STONES}/SS-StonesV1Big-N'})
mi_smid = make_mi('MI_Canyon_StoneMiddle', m_prop, textures={'PropNormal': f'{STONES}/SS-StonesV1Middle-N'})

# ---------------------------------------------------------------- 网格
MESH = f'{DEST}/Meshes'
if os.environ.get('CANYON_SKIP_MESH') != '1':
    def fbx_ui(nanite):
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
        smd.set_editor_property('normal_import_method', unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
        try:
            smd.set_editor_property('build_nanite', nanite)
        except Exception as e:
            log('build_nanite 设不了', e)
        return ui
    names = sorted(f[:-4] for f in os.listdir(f'{SRC}/meshes') if f.endswith('.fbx'))
    tasks = [task(f'{SRC}/meshes/{n}.fbx', MESH, n, fbx_ui(not (n.startswith('SM_Canyon_Col') or n.startswith('SM_Canyon_Water')))) for n in names]
    tools.import_asset_tasks(tasks)
for path in eas.list_assets(MESH, recursive=False, include_folder=False):
    a = eas.load_asset(path)
    if not isinstance(a, unreal.StaticMesh):
        continue
    n = a.get_name()
    is_col = n.startswith('SM_Canyon_Col') or n == 'SM_Canyon_WaterCol'
    nan = a.get_editor_property('nanite_settings')
    nan.enabled = not is_col and n != 'SM_Canyon_Water'
    a.set_editor_property('nanite_settings', nan)
    bs = a.get_editor_property('body_setup')
    if bs:
        bs.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE
                               if (is_col or n == 'SM_Canyon_Far') else unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
    mats = a.get_editor_property('static_materials')
    for s in mats:
        s.set_editor_property('material_interface', mi_water if n == 'SM_Canyon_Water' else (mi_far if n == 'SM_Canyon_Far' else mi_terrain))
    a.set_editor_property('static_materials', mats)
    eas.save_loaded_asset(a)
    b = a.get_bounding_box()
    log(f'网格 {n}: 三角 {a.get_num_triangles(0)} Nanite {nan.enabled} 范围 X {b.min.x:.0f}~{b.max.x:.0f} Y {b.min.y:.0f}~{b.max.y:.0f} Z {b.min.z:.0f}~{b.max.z:.0f}')

# ---------------------------------------------------------------- 石头：复制一份到 /Game/Env/Canyon/Props（开复杂碰撞，不动原素材包）
SRC_STONES = '/Game/Environments/AndreyPestryakov/Environment_Pack_01/Stones/Meshes'
PROPS = f'{DEST}/Props'
for path in eas.list_assets(SRC_STONES, recursive=False, include_folder=False):
    nm = path.split('/')[-1].split('.')[0]
    if 'FoliageType' in nm or not nm.startswith('SM_Stones'):
        continue
    dst = f'{PROPS}/{nm}'
    if not eas.does_asset_exist(dst):
        eas.duplicate_asset(path, dst)
    a = eas.load_asset(dst)
    bs = a.get_editor_property('body_setup')
    if bs:
        bs.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    mats = a.get_editor_property('static_materials')
    for s in mats:
        s.set_editor_property('material_interface', mi_sbig if 'Big' in nm else mi_smid)
    a.set_editor_property('static_materials', mats)
    eas.save_loaded_asset(a)
    b = a.get_bounding_box()
    log(f'石头 {nm}: 尺寸 {(b.max.x - b.min.x):.0f} x {(b.max.y - b.min.y):.0f} x {(b.max.z - b.min.z):.0f} 底 {b.min.z:.0f} 中心 {(b.max.x + b.min.x) / 2:.0f},{(b.max.y + b.min.y) / 2:.0f} Nanite {a.get_editor_property("nanite_settings").enabled}')
log('完成')
