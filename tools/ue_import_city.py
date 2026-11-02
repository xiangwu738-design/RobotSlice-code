"""城市图（K059 街道小品 + K005 楼）素材导入 RobotSlice：贴图、母材质、材质实例、网格（Nanite、碰撞、按槽挂材质）。
前面几步：blender_k059_export.py（k059 / k005）→ k059_pack_textures.py → blender_k059_basic.py → k059_make_signs.py
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_import_city.py [tex] [mat] [mesh] [fix]" -AllowCommandletRendering -RenderOffscreen
不带参数 = 全做；mesh 最慢（两个 FBX 一共约 1200 万面，建 Nanite）。可以分段跑，每段存盘。
资源都在 /Game/Env/City 下：Textures / Materials / Meshes（街道小品、道具、基本形）/ Buildings（K005 楼）。
"""
import json
import os
import sys

import unreal

STEPS = [a for a in sys.argv[1:] if not a.startswith('-')] or ['tex', 'mat', 'mesh', 'fix']
SRC = 'D:/RobotSlice/Inbox/_k059'
SIGNS = 'D:/RobotSlice/SourceAssets/city_signs'
DEST = '/Game/Env/City'
eas = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
P = unreal.MaterialProperty
S = unreal.MaterialSamplerType
FLAT_NORMAL = '/Game/Env/Textures/T_FlatNormal'
BLACK = next(p for p in ('/Engine/EngineResources/Black', '/Engine/EngineMaterials/BlackSquareTexture',
                         '/Engine/EngineResources/DefaultTexture') if eas.does_asset_exist(p))


def log(msg):
    unreal.log(f'[city] {msg}')


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


mats_k059 = json.load(open(f'{SRC}/export_k059/materials_ue.json', encoding='utf-8'))
mats_k005 = json.load(open(f'{SRC}/export_k005/materials_ue.json', encoding='utf-8'))

# ---------------------------------------------------------------- 贴图
if 'tex' in STEPS:
    # 纯白小图（纯色材质的底色贴图默认值）
    white = 'D:/RobotSlice/SourceAssets/common/T_City_White.png'
    if not os.path.exists(white):
        import struct
        import zlib
        raw = b''.join(b'\x00' + b'\xff\xff\xff' * 4 for _ in range(4))
        png = b'\x89PNG\r\n\x1a\n'
        for tag, data in ((b'IHDR', struct.pack('>IIBBBBB', 4, 4, 8, 2, 0, 0, 0)), (b'IDAT', zlib.compress(raw)), (b'IEND', b'')):
            png += struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)
        open(white, 'wb').write(png)
    files = [white]
    for sub in ('export_k059', 'export_k005'):
        d = f'{SRC}/{sub}/ue_tex'
        files += [f'{d}/{f}' for f in sorted(os.listdir(d))]
    files += [f'{SIGNS}/{f}' for f in sorted(os.listdir(SIGNS))]
    tasks = []
    for f in files:
        stem = os.path.splitext(os.path.basename(f))[0]
        name = stem if stem.startswith('T_') else 'T_Sign_' + stem
        tasks.append(task(f, f'{DEST}/Textures', name))
    tools.import_asset_tasks(tasks)
    n = 0
    for t in tasks:
        tex = eas.load_asset(f'{DEST}/Textures/{t.destination_name}')
        if not tex:
            unreal.log_warning(f'[city] 贴图没导进来 {t.filename}')
            continue
        nm = t.destination_name
        if nm.endswith('_N'):
            tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
            tex.set_editor_property('srgb', False)
            tex.set_editor_property('flip_green_channel', True)   # 包里是 OpenGL 法线
        elif nm.endswith('_ARM') or nm.endswith('_ARMW'):
            tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_MASKS)
            tex.set_editor_property('srgb', False)
        tex.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_WORLD)
        eas.save_loaded_asset(tex)
        n += 1
    log(f'贴图 {n} 张')


# ---------------------------------------------------------------- 母材质（UV 贴图）：M_CityPBR / M_CityPBR_Masked
def build_master(name, masked):
    path = f'{DEST}/Materials/{name}'
    if eas.does_asset_exist(path) and not os.environ.get('CITY_REBUILD_MASTER'):
        return eas.load_asset(path)      # 已经有了就不重建（重建会让所有材质实例重编着色器）
    if eas.does_asset_exist(path):
        m = eas.load_asset(path)
        mel.delete_all_material_expressions(m)
    else:
        m = tools.create_asset(name, f'{DEST}/Materials', unreal.Material, unreal.MaterialFactoryNew())

    def node(cls, x, y, **props):
        n = mel.create_material_expression(m, cls, x, y)
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    def link(a, out, b, inp):
        assert mel.connect_material_expressions(a, out, b, inp), f'连线失败 {out}->{inp}'

    def scalar(pname, v, x, y):
        return node(unreal.MaterialExpressionScalarParameter, x, y, parameter_name=pname, default_value=v)

    arm_default = eas.load_asset(f'{DEST}/Textures/T_KB3D_CTS_SteelRoughGray_ARM')
    col = node(unreal.MaterialExpressionTextureSampleParameter2D, -900, -400, parameter_name='BaseColorTex',
               texture=eas.load_asset(f'{DEST}/Textures/T_City_White'), sampler_type=S.SAMPLERTYPE_COLOR)
    nor = node(unreal.MaterialExpressionTextureSampleParameter2D, -900, 0, parameter_name='NormalTex',
               texture=eas.load_asset(FLAT_NORMAL), sampler_type=S.SAMPLERTYPE_NORMAL)
    arm = node(unreal.MaterialExpressionTextureSampleParameter2D, -900, 300, parameter_name='ARMTex',
               texture=arm_default, sampler_type=S.SAMPLERTYPE_MASKS)
    emt = node(unreal.MaterialExpressionTextureSampleParameter2D, -900, 700, parameter_name='EmissiveTex',
               texture=eas.load_asset(BLACK), sampler_type=S.SAMPLERTYPE_COLOR)
    tint = node(unreal.MaterialExpressionVectorParameter, -600, -550, parameter_name='Tint', default_value=unreal.LinearColor(1, 1, 1, 1))
    base = node(unreal.MaterialExpressionMultiply, -400, -420)
    link(col, 'RGB', base, 'A')
    link(tint, '', base, 'B')
    # 粗糙度 = ARM.G × RoughMul + RoughAdd；金属度同理（纯色材质把 Mul 设 0、Add 设值）
    outs = {}
    for i, (pname, ch, mul, add) in enumerate((('Rough', 'G', 1.0, 0.0), ('Metal', 'B', 1.0, 0.0))):
        mu = scalar(pname + 'Mul', mul, -650, 250 + i * 250)
        ad = scalar(pname + 'Add', add, -650, 330 + i * 250)
        a = node(unreal.MaterialExpressionMultiply, -450, 250 + i * 250)
        link(arm, ch, a, 'A')
        link(mu, '', a, 'B')
        b = node(unreal.MaterialExpressionAdd, -300, 250 + i * 250)
        link(a, '', b, 'A')
        link(ad, '', b, 'B')
        s_ = node(unreal.MaterialExpressionSaturate, -180, 250 + i * 250)
        link(b, '', s_, '')
        outs[pname] = s_
    es = scalar('EmissiveStrength', 0.0, -650, 800)
    em = node(unreal.MaterialExpressionMultiply, -400, 750)
    link(emt, 'RGB', em, 'A')
    link(es, '', em, 'B')
    mel.connect_material_property(base, '', P.MP_BASE_COLOR)
    mel.connect_material_property(nor, 'RGB', P.MP_NORMAL)
    mel.connect_material_property(outs['Rough'], '', P.MP_ROUGHNESS)
    mel.connect_material_property(outs['Metal'], '', P.MP_METALLIC)
    mel.connect_material_property(arm, 'R', P.MP_AMBIENT_OCCLUSION)
    mel.connect_material_property(em, '', P.MP_EMISSIVE_COLOR)
    if masked:
        mel.connect_material_property(col, 'A', P.MP_OPACITY_MASK)
        m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
        m.set_editor_property('two_sided', True)
        m.set_editor_property('opacity_mask_clip_value', 0.4)
    for flag in ('used_with_nanite', 'used_with_instanced_static_meshes'):
        try:
            m.set_editor_property(flag, True)
        except Exception as e:
            unreal.log_warning(f'[city] {flag}: {e}')
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


def clean(s):
    import re
    return re.sub(r'[^A-Za-z0-9]', '_', s).strip('_')


# 冷调：K005 的红砖 / 褐石压一点饱和（整张图偏冷灰，砖不要太暖）
K005_TINT = {'Brick_CleanBrown': (0.82, 0.8, 0.8), 'BrickOld': (0.85, 0.83, 0.83), 'BrickOldRedPainted': (0.8, 0.78, 0.8),
             'Brownstone': (0.85, 0.84, 0.85), 'BrickCleanInvert': (0.9, 0.9, 0.92), 'WoodBeamRed': (0.8, 0.8, 0.8)}

if 'mat' in STEPS:
    master = build_master('M_CityPBR', False)
    masked = build_master('M_CityPBR_Masked', True)
    n = 0
    for src, table in (('k059', mats_k059), ('k005', mats_k005)):
        for mname, e in table.items():
            mi = make_mi('MI_City_' + clean(mname), masked if e['masked'] else master)
            tex = e['tex']
            if 'D' in tex:
                setp(mi, 't', 'BaseColorTex', tex['D'])
            else:
                setp(mi, 'v', 'Tint', e['color'])
            if 'N' in tex:
                setp(mi, 't', 'NormalTex', tex['N'])
            if 'ARM' in tex:
                setp(mi, 't', 'ARMTex', tex['ARM'])
                if src == 'k005' and e['metal_v'] == 0:
                    setp(mi, 's', 'MetalMul', 0.0)
                elif src == 'k059':
                    setp(mi, 's', 'MetalMul', 0.45)   # 阴天里金属度 0.9 以上的道具只反射灰天空、发黑
            else:
                setp(mi, 's', 'RoughMul', 0.0)
                setp(mi, 's', 'RoughAdd', e['rough_v'])
                setp(mi, 's', 'MetalMul', 0.0)
                setp(mi, 's', 'MetalAdd', e['metal_v'])
            if 'E' in tex:
                setp(mi, 't', 'EmissiveTex', tex['E'])
                setp(mi, 's', 'EmissiveStrength', 2.0)
            base = mname.split('.')[0]
            if base in K005_TINT:
                setp(mi, 'v', 'Tint', K005_TINT[base])
            if 'Glass' in mname:
                setp(mi, 's', 'RoughMul', 0.3)
            eas.save_loaded_asset(mi)
            n += 1
    # 牌子：底色 + 自发光都用同一张图（屏幕亮，纸面不亮）
    for name, glow in (('odds_board', 4.0), ('event_ad', 0.6), ('sponsor_ad', 0.4), ('evac_notice', 0.0)):
        mi = make_mi('MI_City_Sign_' + name, master)
        setp(mi, 't', 'BaseColorTex', 'T_Sign_' + name)
        setp(mi, 't', 'EmissiveTex', 'T_Sign_' + name)
        setp(mi, 's', 'EmissiveStrength', glow)
        setp(mi, 's', 'RoughMul', 0.0)
        setp(mi, 's', 'RoughAdd', 0.35 if glow > 1 else 0.7)
        setp(mi, 's', 'MetalMul', 0.0)
        eas.save_loaded_asset(mi)
    # 世界投影（M_WorldPBR）：路面、人行道、广场、工地、混凝土、钢、集装箱……（贴图名, 铺贴厘米, 湿度, 积水, 色调）
    W = '/Game/Env/Textures'
    # 积水一律 -1（给 0 时噪声偶尔超过 1 会冒出水渍；给正值时阴天反射天空，路面一块块发白像积雪——第一轮成图就是这样）
    WORLD = {
        'asphalt': ('KB3D_CTS_AsphaltA', 600, 0.12, -1.0, (0.92, 0.94, 0.98)),
        'sidewalk': ('KB3D_CTS_StreetTiles', 240, 0.05, -1.0, (0.88, 0.92, 0.98)),
        'plaza': ('KB3D_CTS_StreetTiles', 360, 0.05, -1.0, (0.95, 0.99, 1.05)),
        'gravel': ('KB3D_CTS_SoilGravel', 500, 0.05, -1.0, (0.85, 0.85, 0.85)),
        # 混凝土：Poly Haven 的两种太暗、发褐，用 K005 的浅色混凝土，色调往冷灰压（原图偏米黄）
        'concrete': ('k5:ConcreteMix', 500, 0.0, -1.0, (0.8, 0.86, 0.95)),
        'concrete_dark': ('k5:ConcreteMixDark', 450, 0.0, -1.0, (0.72, 0.78, 0.86)),
        'lot': ('k5:ConcreteMix', 600, 0.05, -1.0, (0.68, 0.73, 0.8)),
        'steel': ('KB3D_CTS_SteelRoughGray', 250, 0.0, -1.0, (0.85, 0.88, 0.92)),
        'steel_dark': ('KB3D_CTS_MetalPaintGrayA', 250, 0.0, -1.0, (0.35, 0.37, 0.4)),
        'container': ('pv:container_side', 300, 0.0, -1.0, (0.55, 0.75, 1.0)),
        'container_b': ('pv:container_side', 300, 0.0, -1.0, (1.0, 0.55, 0.35)),
        'train': ('KB3D_CTS_MetalPaintWhite', 300, 0.0, -1.0, (0.85, 0.87, 0.9)),
    }
    # K005 混凝土没有粗糙度图：给一张纯色 ARM（AO 1、粗糙 0.85、金属 0）
    flat_arm = 'D:/RobotSlice/SourceAssets/common/T_City_ARM_Concrete.png'
    if not eas.does_asset_exist(f'{DEST}/Textures/T_City_ARM_Concrete'):
        if not os.path.exists(flat_arm):
            import struct
            import zlib
            raw = bytes.fromhex('00' + 'ffd900' * 4) * 4      # 4×4：每行开头一个过滤字节，像素 (255, 217, 0)
            png = bytes.fromhex('89504e470d0a1a0a')
            for tag, data in ((b'IHDR', struct.pack('>IIBBBBB', 4, 4, 8, 2, 0, 0, 0)), (b'IDAT', zlib.compress(raw)), (b'IEND', b'')):
                png += struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)
            open(flat_arm, 'wb').write(png)
        tools.import_asset_tasks([task(flat_arm, f'{DEST}/Textures', 'T_City_ARM_Concrete')])
        t = eas.load_asset(f'{DEST}/Textures/T_City_ARM_Concrete')
        t.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_MASKS)
        t.set_editor_property('srgb', False)
        eas.save_loaded_asset(t)
    # 2048×1493 这种边长不是 4 的倍数的贴图压不成 BC，按未压缩存（一张 15 MB）→ 拉成 2 的幂
    for nm in ('T_Brownstone_D', 'T_Brownstone_N', 'T_Brownstone_ARM'):
        t = eas.load_asset(f'{DEST}/Textures/{nm}')
        if t and t.get_editor_property('power_of_two_mode') != unreal.TexturePowerOfTwoSetting.STRETCH_TO_POWER_OF_TWO:
            t.set_editor_property('power_of_two_mode', unreal.TexturePowerOfTwoSetting.STRETCH_TO_POWER_OF_TWO)
            eas.save_loaded_asset(t)
    wpbr = eas.load_asset('/Game/Env/Materials/M_WorldPBR')
    for key, (tid, tile, wet, pud, tint) in WORLD.items():
        mi = make_mi('MI_CityW_' + key, wpbr)
        if tid.startswith('k5:'):
            fam = tid[3:]
            for param, path in (('BaseColorTex', f'T_{fam}_D'), ('NormalTex', f'T_{fam}_N'), ('ARMTex', 'T_City_ARM_Concrete')):
                mel.set_material_instance_texture_parameter_value(mi, param, eas.load_asset(f'{DEST}/Textures/{path}'))
        elif tid.startswith('pv:'):
            fam = tid[3:]
            for param, suf in (('BaseColorTex', 'diff'), ('NormalTex', 'nor_gl'), ('ARMTex', 'arm')):
                mel.set_material_instance_texture_parameter_value(mi, param, eas.load_asset(f'{W}/{fam}/T_{fam}_{suf}_2k'))
        else:
            for param, suf in (('BaseColorTex', 'D'), ('NormalTex', 'N'), ('ARMTex', 'ARM')):
                t = f'{DEST}/Textures/T_{tid}_{suf}'
                if suf == 'ARM' and eas.does_asset_exist(t + 'W'):
                    t += 'W'      # 金属度压过的版本（大面金属阴天发黑）
                mel.set_material_instance_texture_parameter_value(mi, param, eas.load_asset(t))
        mel.set_material_instance_scalar_parameter_value(mi, 'TileSize', float(tile))
        mel.set_material_instance_scalar_parameter_value(mi, 'Wetness', wet)
        mel.set_material_instance_scalar_parameter_value(mi, 'PuddleAmount', pud)
        mel.set_material_instance_vector_parameter_value(mi, 'Tint', unreal.LinearColor(*tint, 1))
        eas.save_loaded_asset(mi)
    # 纯色：标线、玻璃
    for key, col, rough, metal in (('paint', (0.62, 0.62, 0.6), 0.65, 0.0), ('paint_y', (0.55, 0.42, 0.08), 0.65, 0.0),
                                   ('glass', (0.06, 0.08, 0.1), 0.06, 0.6), ('car', (0.22, 0.24, 0.27), 0.7, 0.0)):
        mi = make_mi('MI_CityF_' + key, master)
        setp(mi, 'v', 'Tint', col)
        setp(mi, 's', 'RoughMul', 0.0)
        setp(mi, 's', 'RoughAdd', rough)
        setp(mi, 's', 'MetalMul', 0.0)
        setp(mi, 's', 'MetalAdd', metal)
        eas.save_loaded_asset(mi)
    log(f'母材质 2 个、贴图材质实例 {n} 个、世界投影 {len(WORLD)} 个')

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


def setup_meshes(dest, meta, table):
    info = {m['name']: m for m in meta}
    n = 0
    for path in eas.list_assets(dest, recursive=False, include_folder=False):
        a = eas.load_asset(path)
        if not isinstance(a, unreal.StaticMesh):
            continue
        name = a.get_name()
        nan = a.get_editor_property('nanite_settings')
        if not nan.enabled:      # 导入时已经建了 Nanite；别再触发一遍重建
            nan.enabled = True
            a.set_editor_property('nanite_settings', nan)
        bs = a.get_editor_property('body_setup')
        if bs:   # 碰撞都用网格本身；小道具在摆放时把组件设成不碰撞（ue_build_city.py 按 meshes.json 尺寸判断）
            bs.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        # 10-31 查到的「资产上的材质存不住」真正原因：unreal.Array 里的结构体按值取出，原来 for s in sm: s.set_editor_property(...)
        # 改的是副本，写回去的还是原来那一组（全是引擎灰格子）——不是「不通知」的问题，存盘本身是成功的。改完要放回新数组再写
        sm = list(a.get_editor_property('static_materials'))
        for i, s in enumerate(sm):
            slot = str(s.get_editor_property('material_slot_name'))
            hit = None
            for cand in (slot, slot.split('.')[0]):
                p = f'{DEST}/Materials/MI_City_{clean(cand)}'
                if eas.does_asset_exist(p):
                    hit = eas.load_asset(p)
                    break
            if not hit and slot in SLOT_FALLBACK:   # 程序方块 / 牌子面片：摆放时总会覆盖，资产上给个像样的默认，别是引擎灰格子
                hit = eas.load_asset(f'{DEST}/Materials/{SLOT_FALLBACK[slot]}')
            if hit:
                s.set_editor_property('material_interface', hit)
                sm[i] = s
            else:
                unreal.log_warning(f'[city] {name} 槽 {slot} 没找到材质')
        try:   # 只换材质槽，不通知（通知会让 1200 万面的网格重建一遍渲染数据）
            a.set_editor_property('static_materials', sm, unreal.PropertyAccessChangeNotifyMode.NEVER)
        except TypeError:
            a.set_editor_property('static_materials', sm)
        still = [str(s.get_editor_property('material_slot_name')) for s in a.get_editor_property('static_materials')
                 if not s.get_editor_property('material_interface') or s.get_editor_property('material_interface').get_path_name().startswith('/Engine/')]
        if still:
            unreal.log_warning(f'[city] {name} 写完还是默认材质的槽：{still}')
            STILL_GRAY.append(name)
        a.modify()
        # 10-31：存盘结果要看——文件被别的进程（开着的游戏 / 编辑器）占着时这里返回 False，材质就只在内存里，下次打开又是灰格子
        if not eas.save_loaded_asset(a, False):
            SAVE_FAILED.append(name)
            unreal.log_warning(f'[city] {name} 存盘失败（文件被占用？先关掉开着这个工程的游戏 / 编辑器再跑 fix）')
        n += 1
    return n


SLOT_FALLBACK = {'M_Box': 'MI_CityW_concrete', 'M_Sign': 'MI_CityF_paint'}
SAVE_FAILED = []
STILL_GRAY = []


if 'mesh' in STEPS and 'fix' not in STEPS:
    # 重新导入网格（不导材质）会把所有槽冲回引擎灰格子（10-31 早上那次就只跑了 tex mat mesh），导网格必须接着 fix，这里自动补上
    STEPS.append('fix')

if 'mesh' in STEPS:
    import_fbx(f'{SRC}/export_basic/basic.fbx', f'{DEST}/Meshes')
    import_fbx(f'{SRC}/export_k059/k059.fbx', f'{DEST}/Meshes')
    log('K059 网格导入完')
    import_fbx(f'{SRC}/export_k005/k005.fbx', f'{DEST}/Buildings')
    log('K005 楼导入完')

if 'fix' in STEPS:
    meta = json.load(open(f'{SRC}/export_k059/meshes.json', encoding='utf-8'))['meshes']
    meta += json.load(open(f'{SRC}/export_k005/meshes.json', encoding='utf-8'))['meshes']
    n1 = setup_meshes(f'{DEST}/Meshes', meta, mats_k059)
    n2 = setup_meshes(f'{DEST}/Buildings', meta, mats_k005)
    for nm in ('SM_City_Box', 'SM_City_SignQuad'):
        a = eas.load_asset(f'{DEST}/Meshes/{nm}')
        if a:
            bs = a.get_editor_property('body_setup')
            if bs:
                bs.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
            eas.save_loaded_asset(a)
    log(f'网格设置完：街道小品 / 道具 {n1}，楼 {n2}；写完还有默认材质的 {len(STILL_GRAY)} 个 {STILL_GRAY[:8]}，存盘失败 {len(SAVE_FAILED)} 个 {SAVE_FAILED[:8]}')
    if SAVE_FAILED:
        unreal.log_warning(f'[city] 有 {len(SAVE_FAILED)} 个网格没存进去，跑 tools/ue_city_mat_scan.py 看资产上的槽')
