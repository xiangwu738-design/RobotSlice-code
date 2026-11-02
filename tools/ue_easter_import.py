"""在 RobotSlice 工程里跑（commandlet）：把彩蛋模型导进来（11-02）。可重复跑（覆盖导入；母材质已有就只换节点代码）。
  UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_easter_import.py [all|mat|drone|dog|anims]" -AllowCommandletRendering -RenderOffscreen -unattended
  不写参数 = all。输入是 Blender 脚本的输出 Inbox/_easter/export/（SM_VDrone_Body.fbx、SM_VDrone_Rotor.fbx、SK_PDog.fbx、A_PDog_*.fbx、*_build.json）。
资源（代码按这些路径加载）：
  /Game/Easter/Materials/M_EasterLivery  母材质：向量参数 PaintA / PaintB / Accent，标量 AccentGlow（代码改涂装就设这四个）；
      另有 FrameColor、PaintAmount、AccentAmount、PatternMix、PatternScale、PatternShare、GrimeAmt、GrimeTile、Metallic、Roughness、RoughVar（实例里调好，代码不用管）。
      做法同现有机体 / 肩部无人机材质：不用 UV（底子和组件库的 UV 密度乱），按蒙皮前位置做维克托那套折线迷彩（PatternMix 0 = 纯 PaintA）、
      借肩部无人机的脏污贴图 T_Drone_Grime_D 三向投影做明暗 / 粗糙度起伏；自发光 = Accent × AccentGlow × 顶点色 R（Blender 里只有灯的面 R=1）× 白天补偿。
  /Game/Easter/Materials/MI_Easter_{PDog,VDrone}_{Shell,Frame,Accent}  三个槽的实例（颜色取 tools/easter_dog.json / easter_drone.json 的 material）
  /Game/Easter/Drone/SM_VDrone_Body   静态网格，插槽 Rotor_FL/FR/BL/BR、Muzzle（按 VDrone_build.json 核对 / 补齐，旋转清零），简单碰撞（FBX 的 UCX 盒子，没有就补一个盒子），关 Nanite
  /Game/Easter/Drone/SM_VDrone_Rotor  静态网格，原点在桨毂，绕本地 Z 转，没有碰撞，关 Nanite
  /Game/Easter/Dog/SK_PDog（+ SK_PDog_Skeleton、SK_PDog_PhysicsAsset）骨骼网格，关 Nanite；A_PDog_Idle / Walk / Run / Sit（有 FBX 才有）
日志搜 [easter]；同样内容写到 Saved/easter_import.txt（发回来）。
"""
import json
import os
import shutil
import sys
import time

import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()).rstrip('/')
EXPORT = f'{PROJECT}/Inbox/_easter/export'
MAT_DIR = '/Game/Easter/Materials'
DOG_DIR = '/Game/Easter/Dog'
DRONE_DIR = '/Game/Easter/Drone'
MASTER = 'M_EasterLivery'
GRIME = ['/Game/Weapons/Textures/T_Drone_Grime_D', '/Game/Env/City/Textures/T_KB3D_CTS_MetalPaintGrayA_D']
BACKUP_ROOT = 'D:/归档'
SLOTS = ('Shell', 'Frame', 'Accent')
DOG_BONES = ['root', 'body', 'head', 'tail_antenna'] + [f'leg_{k}_{s}' for k in ('fl', 'fr', 'bl', 'br') for s in ('upper', 'lower')]
ANIMS = ['A_PDog_Idle', 'A_PDog_Walk', 'A_PDog_Run', 'A_PDog_Sit']

eas = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
REPORT, ERRORS = [], []


def log(msg):
    for line in str(msg).splitlines() or ['']:
        unreal.log(f'[easter] {line}')
    REPORT.append(msg)


def err(msg):
    for line in str(msg).splitlines() or ['']:
        unreal.log_error(f'[easter] {line}')  # 一行一行打：多行的话本地测试流程只收得到第一行
    REPORT.append('错误：' + msg)
    ERRORS.append(msg)


def srgb(h):
    h = h.lstrip('#')

    def f(v):
        v /= 255.0
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    return unreal.LinearColor(*[f(int(h[i:i + 2], 16)) for i in (0, 2, 4)], 1.0)


def read_json(path, default=None):
    if not os.path.exists(path):
        return default
    return json.load(open(path, encoding='utf-8'))


def backup():
    """第一次覆盖前把 Content/Easter 整个拷到 D:/归档/RobotSlice_彩蛋导入前_<日期>（已经有就不拷）"""
    src = f'{PROJECT}/Content/Easter'
    if not os.path.isdir(src) or not os.path.isdir(BACKUP_ROOT):
        return
    dst = f'{BACKUP_ROOT}/RobotSlice_彩蛋导入前_{time.strftime("%Y%m%d")}'
    if not os.path.exists(dst):
        shutil.copytree(src, dst)
        log(f'备份 {src} → {dst}')


def legacy_fbx():
    for cvar in ('Interchange.FeatureFlags.Import.FBX', 'r.Interchange.FeatureFlags.Import.FBX'):
        unreal.SystemLibrary.execute_console_command(None, f'{cvar} 0')


def task(filename, dest, name, options):
    t = unreal.AssetImportTask()
    t.filename = filename
    t.destination_path = dest
    t.destination_name = name
    t.automated = True
    t.replace_existing = True
    t.save = True
    t.options = options
    return t


def setp(obj, name, value):
    """设属性；这一版没有这个属性就记一笔不报错"""
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception as e:
        log(f'  （{type(obj).__name__} 设不了 {name}：{e}）')
        return False


# ---------------------------------------------------------------- 母材质
# 自定义节点代码里不能有中文（材质编译失败会退回默认材质）。说明：
# P = 蒙皮前位置（厘米，网格空间；静态网格就是本地位置），N = 蒙皮前法线。折线迷彩和 M_RobotCamo（维克托机体）同一套：两层斜格子、边按三角波折、按哈希取色。
# 底色 = lerp(FrameColor, 涂装, PaintAmount)，再 lerp 到 Accent（AccentAmount）；脏污贴图三向投影后的亮度 / 0.13 当明暗（贴图平均亮度约 0.13，同 drone_surface.hlsl）。
# 输出 float4：rgb 底色、a 粗糙度。
LIVERY_HLSL = r'''
float3 q = P / max(Scale, 1.0);
float3 r1 = float3(dot(q, float3(0.80, 0.00, 0.60)), dot(q, float3(-0.36, 0.80, 0.48)), dot(q, float3(-0.48, -0.60, 0.64)));
r1.y += 0.42 * abs(frac(r1.x * 0.85) - 0.5);
r1.z += 0.30 * abs(frac(r1.y * 1.30 + 0.25) - 0.5);
float3 c1 = floor(r1 * float3(0.55, 1.00, 1.60));
float h1 = frac(sin(dot(c1, float3(12.9898, 78.233, 37.719))) * 43758.5453);
float3 r2 = float3(dot(q, float3(0.60, 0.00, -0.80)), dot(q, float3(0.64, 0.60, 0.48)), dot(q, float3(0.48, -0.80, 0.36)));
r2.x += 0.38 * abs(frac(r2.y * 0.95 + 0.4) - 0.5);
float3 c2 = floor(r2 * float3(0.75, 1.35, 0.90) + 0.37);
float h2 = frac(sin(dot(c2, float3(39.346, 11.135, 83.155))) * 43758.5453);
float3 pa = A.rgb;
float3 pb = B.rgb;
float lum = dot(pa, float3(0.2126, 0.7152, 0.0722));
float3 gray = saturate(float3(lum, lum, lum) * 1.35 + 0.012);
float3 camo = pa;
if (h1 < Share) camo = pb;
if (h2 < Share * 0.6) camo = lerp(pa, gray, 0.6);
float3 paint = lerp(pa, camo, saturate(PatternMix));
float3 base = lerp(Frame.rgb, paint, saturate(PaintAmount));
base = lerp(base, Acc.rgb, saturate(AccentAmount));
float3 nn = normalize(N + 0.0001);
float3 w = pow(abs(nn), 4.0);
w /= max(w.x + w.y + w.z, 0.0001);
float3 t = Texture2DSample(Tex, TexSampler, P.zy * Tile).rgb * w.x + Texture2DSample(Tex, TexSampler, P.xz * Tile).rgb * w.y + Texture2DSample(Tex, TexSampler, P.xy * Tile).rgb * w.z;
float g = dot(t, float3(0.3, 0.59, 0.11)) / 0.13;
base *= lerp(1.0, clamp(g, 0.55, 1.6), saturate(GrimeAmt));
float rough = saturate(Rough + (1.0 - clamp(g, 0.0, 2.0)) * RoughVar);
return float4(base, rough);
'''
CUSTOM_DESC = 'EasterLivery'
VECTORS = {'PaintA': '#8b949c', 'PaintB': '#5a636c', 'Accent': '#3cc8dc', 'FrameColor': '#2e3236'}
SCALARS = {'AccentGlow': 8.0, 'PaintAmount': 1.0, 'AccentAmount': 0.0, 'PatternMix': 0.0, 'PatternScale': 24.0, 'PatternShare': 0.4,
           'GrimeAmt': 0.6, 'GrimeTile': 1.0 / 30.0, 'Metallic': 0.2, 'Roughness': 0.5, 'RoughVar': 0.15}


def grime_texture():
    for p in GRIME:
        if eas.does_asset_exist(p):
            return eas.load_asset(p), True
    log(f'没有脏污贴图 {GRIME}，用引擎默认贴图、GrimeAmt 默认 0')
    return eas.load_asset('/Engine/EngineResources/DefaultTexture'), False


def find_custom(m):
    todo = [mel.get_material_property_input_node(m, p) for p in (unreal.MaterialProperty.MP_BASE_COLOR, unreal.MaterialProperty.MP_ROUGHNESS)]
    seen = set()
    while todo:
        e = todo.pop()
        if not e or e.get_name() in seen:
            continue
        seen.add(e.get_name())
        if isinstance(e, unreal.MaterialExpressionCustom) and str(e.get_editor_property('description')) == CUSTOM_DESC:
            return e
        todo += mel.get_inputs_for_material_expression(m, e)
    return None


def make_master():
    path = f'{MAT_DIR}/{MASTER}'
    assert all(ord(ch) < 128 for ch in LIVERY_HLSL), '材质代码里有非 ASCII 字符'
    if eas.does_asset_exist(path):
        # 已有：只换节点代码（代码运行时会加载它，清空节点重建可能触发 !IsRooted 崩溃，同 ue_drone_materials.py）
        m = eas.load_asset(path)
        c = find_custom(m)
        if c:
            c.set_editor_property('code', LIVERY_HLSL)
            mel.recompile_material(m)
            eas.save_asset(path, only_if_is_dirty=False)
            log(f'母材质已有，更新了节点代码 {path}')
            return m
        log(f'{path} 里找不到 {CUSTOM_DESC} 节点，重建节点')
        mel.delete_all_material_expressions(m)
    else:
        m = tools.create_asset(MASTER, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    tex, has_grime = grime_texture()
    if not has_grime:
        SCALARS['GrimeAmt'] = 0.0
    y = [-1400]

    def node(cls, x=-1500, **props):
        e = mel.create_material_expression(m, cls, x, y[0])
        y[0] += 100
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    P = {}
    for n, h in VECTORS.items():
        P[n] = node(unreal.MaterialExpressionVectorParameter, parameter_name=n, default_value=srgb(h))
    for n, v in SCALARS.items():
        P[n] = node(unreal.MaterialExpressionScalarParameter, parameter_name=n, default_value=float(v))
    P['GrimeTex'] = node(unreal.MaterialExpressionTextureObjectParameter, parameter_name='GrimeTex', texture=tex)
    pos = node(unreal.MaterialExpressionPreSkinnedPosition, x=-1900)
    pos_i = node(unreal.MaterialExpressionVertexInterpolator, x=-1700)
    assert mel.connect_material_expressions(pos, '', pos_i, '')
    nor = node(unreal.MaterialExpressionPreSkinnedNormal, x=-1900)
    nor_i = node(unreal.MaterialExpressionVertexInterpolator, x=-1700)
    assert mel.connect_material_expressions(nor, '', nor_i, '')
    names = ['P', 'N', 'Tex', 'A', 'B', 'Acc', 'Frame', 'PaintAmount', 'AccentAmount', 'PatternMix', 'Scale', 'Share', 'GrimeAmt', 'Tile', 'Rough', 'RoughVar']
    src = [pos_i, nor_i, P['GrimeTex'], P['PaintA'], P['PaintB'], P['Accent'], P['FrameColor'], P['PaintAmount'], P['AccentAmount'], P['PatternMix'],
           P['PatternScale'], P['PatternShare'], P['GrimeAmt'], P['GrimeTile'], P['Roughness'], P['RoughVar']]
    c = mel.create_material_expression(m, unreal.MaterialExpressionCustom, -800, -300)
    c.set_editor_property('code', LIVERY_HLSL)
    c.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    c.set_editor_property('description', CUSTOM_DESC)
    ins = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property('input_name', n)
        ins.append(ci)
    c.set_editor_property('inputs', ins)
    for n, s in zip(names, src):
        assert mel.connect_material_expressions(s, '', c, n), f'接不上 {n}'

    def mask(chs, x, yy):
        k = mel.create_material_expression(m, unreal.MaterialExpressionComponentMask, x, yy)
        for ch in 'rgba':  # 新建时 R 默认勾着，四个都显式设
            k.set_editor_property(ch, ch in chs)
        assert mel.connect_material_expressions(c, '', k, '')
        return k

    rgb = mask('rgb', -500, -350)
    rough = mask('a', -500, -200)
    # 自发光：Accent × AccentGlow × 顶点色 R × 白天补偿 max(1, 曝光倒数×1.3)^0.85（同 ue_drone_materials.py / ue_fx_daylight.py）
    vc = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -1100, 400)
    eye = mel.create_material_expression(m, unreal.MaterialExpressionEyeAdaptationInverse, -1100, 550)
    k13 = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -1100, 650)
    k13.set_editor_property('r', 1.3)
    eyek = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -950, 570)
    assert mel.connect_material_expressions(eye, '', eyek, 'A') and mel.connect_material_expressions(k13, '', eyek, 'B')
    one = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -950, 670)
    one.set_editor_property('r', 1.0)
    mx = mel.create_material_expression(m, unreal.MaterialExpressionMax, -800, 590)
    assert mel.connect_material_expressions(eyek, '', mx, 'A') and mel.connect_material_expressions(one, '', mx, 'B')
    ex = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -800, 690)
    ex.set_editor_property('r', 0.85)
    day = mel.create_material_expression(m, unreal.MaterialExpressionPower, -650, 610)
    assert mel.connect_material_expressions(mx, '', day, 'Base') and mel.connect_material_expressions(ex, '', day, 'Exp')
    e1 = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -800, 400)
    assert mel.connect_material_expressions(P['Accent'], '', e1, 'A') and mel.connect_material_expressions(P['AccentGlow'], '', e1, 'B')
    e2 = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -650, 420)
    assert mel.connect_material_expressions(e1, '', e2, 'A') and mel.connect_material_expressions(vc, 'R', e2, 'B')
    e3 = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -500, 450)
    assert mel.connect_material_expressions(e2, '', e3, 'A') and mel.connect_material_expressions(day, '', e3, 'B')
    Prop = unreal.MaterialProperty
    assert mel.connect_material_property(rgb, '', Prop.MP_BASE_COLOR)
    assert mel.connect_material_property(rough, '', Prop.MP_ROUGHNESS)
    assert mel.connect_material_property(P['Metallic'], '', Prop.MP_METALLIC)
    assert mel.connect_material_property(e3, '', Prop.MP_EMISSIVE_COLOR)
    m.set_editor_property('used_with_skeletal_mesh', True)
    mel.recompile_material(m)
    eas.save_asset(path, only_if_is_dirty=False)
    log(f'建了母材质 {path}（脏污贴图 {tex.get_path_name()}）')
    return m


def instance(master, name, scalars, vectors):
    if not any(eas.does_asset_exist(p) for p in GRIME):
        scalars = dict(scalars, GrimeAmt=0.0)  # 没有脏污贴图时引擎默认贴图会把颜色整体提亮，关掉
    path = f'{MAT_DIR}/{name}'
    mi = eas.load_asset(path) if eas.does_asset_exist(path) else tools.create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', master)
    for k, v in scalars.items():
        mel.set_material_instance_scalar_parameter_value(mi, k, float(v))
    for k, v in vectors.items():
        mel.set_material_instance_vector_parameter_value(mi, k, srgb(v) if isinstance(v, str) else v)
    mel.update_material_instance(mi)
    eas.save_asset(path, only_if_is_dirty=False)
    return mi


def make_instances(master):
    """每种怪三个槽各一个实例；颜色取 tools/easter_<dog|drone>.json 的 material"""
    out = {}
    for tag, cfgname, pattern in (('PDog', 'easter_dog.json', 0.0), ('VDrone', 'easter_drone.json', 1.0)):
        mc = (read_json(f'{PROJECT}/tools/{cfgname}', {}) or {}).get('material', {})
        vec = {'PaintA': mc.get('PaintA', VECTORS['PaintA']), 'PaintB': mc.get('PaintB', VECTORS['PaintB']),
               'Accent': mc.get('Accent', VECTORS['Accent']), 'FrameColor': mc.get('Frame', VECTORS['FrameColor'])}
        glow = float(mc.get('AccentGlow', SCALARS['AccentGlow']))
        out[tag] = {
            # 外壳：跟涂装；维克托默认开折线迷彩（PatternMix 1），相位纯色
            'Shell': instance(master, f'MI_Easter_{tag}_Shell', {'PaintAmount': 1, 'AccentAmount': 0, 'PatternMix': pattern, 'Metallic': 0.25, 'Roughness': 0.48,
                                                                 'AccentGlow': glow}, vec),
            # 骨架件：深石墨灰金属，不跟 PaintA
            'Frame': instance(master, f'MI_Easter_{tag}_Frame', {'PaintAmount': 0, 'AccentAmount': 0, 'Metallic': 0.85, 'Roughness': 0.42, 'GrimeAmt': 0.8,
                                                                 'AccentGlow': glow}, vec),
            # 品牌色件 + 灯（顶点色 R=1 的面发光）
            'Accent': instance(master, f'MI_Easter_{tag}_Accent', {'PaintAmount': 0, 'AccentAmount': 1, 'Metallic': 0.3, 'Roughness': 0.4, 'GrimeAmt': 0.3,
                                                                   'AccentGlow': glow}, vec),
        }
        log(f'实例 MI_Easter_{tag}_Shell/Frame/Accent：PaintA {vec["PaintA"]} PaintB {vec["PaintB"]} Accent {vec["Accent"]} AccentGlow {glow}')
    return out


def load_instances():
    out = {}
    for tag in ('PDog', 'VDrone'):
        out[tag] = {s: eas.load_asset(f'{MAT_DIR}/MI_Easter_{tag}_{s}') for s in SLOTS if eas.does_asset_exist(f'{MAT_DIR}/MI_Easter_{tag}_{s}')}
    return out


# ---------------------------------------------------------------- 静态网格（无人机）
def static_ui():
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
    smd.set_editor_property('remove_degenerates', True)
    smd.set_editor_property('normal_import_method', unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    setp(smd, 'vertex_color_import_option', unreal.VertexColorImportOption.REPLACE)
    setp(smd, 'build_nanite', False)
    return ui


def slot_names_static(mesh):
    return [str(s.get_editor_property('material_slot_name')) for s in mesh.get_editor_property('static_materials')]


def assign_static(mesh, mats):
    names = slot_names_static(mesh)
    for i, n in enumerate(names):
        mi = mats.get(n)
        if mi:
            mesh.set_material(i, mi)  # 按索引写回（for s in sm 改的是拷贝，见 ue_drone_materials.py）
        else:
            err(f'{mesh.get_name()} 槽 {n} 不是 Shell/Frame/Accent，没挂材质')
    missing = [s for s in SLOTS if s not in names]
    if missing:
        log(f'  {mesh.get_name()} 没有槽 {missing}（Blender 里这个槽没有面，导入时丢了；代码按槽名找要能处理）')
    return names


def nanite_off(mesh):
    try:
        ns = mesh.get_editor_property('nanite_settings')
        if ns.get_editor_property('enabled'):
            ns.set_editor_property('enabled', False)
            mesh.set_editor_property('nanite_settings', ns)
            log(f'  {mesh.get_name()} 关了 Nanite')
        return True
    except Exception as e:
        log(f'  {mesh.get_name()} 读不到 nanite_settings（{e}），应该本来就没开')
        return False


def simple_collision_count(mesh):
    for lib in (lambda: unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem), lambda: unreal.EditorStaticMeshLibrary):
        try:
            o = lib()
            if o:
                return o.get_simple_collision_count(mesh), o
        except Exception:
            continue
    return -1, None


def import_drone(mats):
    rep = read_json(f'{EXPORT}/VDrone_build.json')
    if rep is None:
        err(f'没有 {EXPORT}/VDrone_build.json：先跑 Blender 的 blender_easter_drone.py')
    out = {}
    for name in ('SM_VDrone_Body', 'SM_VDrone_Rotor'):
        fbx = f'{EXPORT}/{name}.fbx'
        if not os.path.exists(fbx):
            err(f'没有 {fbx}')
            continue
        tools.import_asset_tasks([task(fbx, DRONE_DIR, name, static_ui())])
        path = f'{DRONE_DIR}/{name}'
        mesh = eas.load_asset(path)
        if not isinstance(mesh, unreal.StaticMesh):
            err(f'导入失败：没有生成静态网格 {path}')
            continue
        nanite_off(mesh)
        names = assign_static(mesh, mats)
        cnt, lib = simple_collision_count(mesh)
        bs = mesh.get_editor_property('body_setup')
        if name == 'SM_VDrone_Body':
            if cnt == 0 and lib:
                try:
                    lib.add_simple_collisions(mesh, unreal.ScriptCollisionShapeType.BOX)
                    cnt2, _ = simple_collision_count(mesh)
                    log(f'  FBX 里的 UCX 碰撞没进来，补了一个盒子（现在 {cnt2} 个）')
                except Exception as e:
                    err(f'补盒子碰撞失败：{e}')
            if bs:
                bs.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_DEFAULT)
            fix_sockets(mesh, (rep or {}).get('sockets', {}))
        else:
            if cnt and cnt > 0 and lib:
                try:
                    lib.remove_collisions(mesh)
                    log('  旋翼去掉了简单碰撞（旋翼不挡子弹，代码里组件也设成无碰撞）')
                except Exception as e:
                    log(f'  去旋翼碰撞失败：{e}')
        eas.save_loaded_asset(mesh, False)
        b = mesh.get_bounding_box()
        size = b.max - b.min
        log(f'{path}：槽 {names}，简单碰撞 {simple_collision_count(mesh)[0]} 个，尺寸 {size.x:.1f} × {size.y:.1f} × {size.z:.1f} 厘米，'
            f'范围 ({b.min.x:.1f}, {b.min.y:.1f}, {b.min.z:.1f})..({b.max.x:.1f}, {b.max.y:.1f}, {b.max.z:.1f})，'
            f'顶点 {mesh.get_num_vertices(0)}，插槽 {[str(s.get_editor_property("socket_name")) for s in mesh.get_editor_property("sockets")]}')
        out[name] = mesh
    if rep and 'SM_VDrone_Body' in out:
        want = rep.get('size_m')
        b = out['SM_VDrone_Body'].get_bounding_box()
        got = [(b.max.x - b.min.x) / 100, (b.max.y - b.min.y) / 100, (b.max.z - b.min.z) / 100]
        if want and any(abs(g - w) > 0.05 * max(w, 0.1) for g, w in zip(got, want)):
            err(f'机身尺寸 {got} 米和 Blender 报告 {want} 米对不上（单位 / 缩放不对？）')
        else:
            log(f'机身尺寸和 Blender 报告一致：{[round(g, 2) for g in got]} 米')
    return out


def fix_sockets(mesh, want):
    """插槽：FBX 的 SOCKET_ 空物体进来了就核对位置、把旋转清零；没进来就按 Blender 报告补"""
    have = {str(s.get_editor_property('socket_name')): s for s in mesh.get_editor_property('sockets')}
    for name, p in want.items():
        loc = unreal.Vector(*p)
        s = have.get(name)
        if s is None:
            s = unreal.StaticMeshSocket(mesh)
            s.set_editor_property('socket_name', name)
            s.set_editor_property('relative_location', loc)
            mesh.add_socket(s)
            log(f'  补了插槽 {name} @ {p}')
            continue
        cur = s.get_editor_property('relative_location')
        d = ((cur.x - loc.x) ** 2 + (cur.y - loc.y) ** 2 + (cur.z - loc.z) ** 2) ** 0.5
        rot = s.get_editor_property('relative_rotation')
        if d > 0.5:
            log(f'  插槽 {name} FBX 里在 ({cur.x:.1f}, {cur.y:.1f}, {cur.z:.1f})，Blender 报告 {p}，差 {d:.1f} 厘米 → 按报告改')
            s.set_editor_property('relative_location', loc)
        if abs(rot.roll) + abs(rot.pitch) + abs(rot.yaw) > 0.01:
            log(f'  插槽 {name} 旋转 ({rot.roll:.1f}, {rot.pitch:.1f}, {rot.yaw:.1f}) → 清零（Muzzle 朝 +X、旋翼绕 Z）')
            s.set_editor_property('relative_rotation', unreal.Rotator(0, 0, 0))
        s.set_editor_property('relative_scale', unreal.Vector(1, 1, 1))
    extra = [n for n in have if n not in want]
    if extra:
        log(f'  多出来的插槽（没动）：{extra}')
    missing = [n for n in ('Rotor_FL', 'Rotor_FR', 'Rotor_BL', 'Rotor_BR', 'Muzzle') if n not in want and n not in have]
    if missing:
        err(f'缺插槽 {missing}')


# ---------------------------------------------------------------- 骨骼网格（狗）
def import_dog(mats):
    fbx = f'{EXPORT}/SK_PDog.fbx'
    if not os.path.exists(fbx):
        err(f'没有 {fbx}：先跑 Blender 的 blender_easter_dog.py')
        return None
    skel_path = f'{DOG_DIR}/SK_PDog_Skeleton'
    skel = eas.load_asset(skel_path) if eas.does_asset_exist(skel_path) else None
    ui = unreal.FbxImportUI()
    ui.import_mesh = True
    ui.import_as_skeletal = True
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    if skel:
        ui.skeleton = skel  # 重导：沿用骨架（骨头名字是定死的，动作不用重导）
    ui.import_animations = False
    ui.import_materials = False
    ui.import_textures = False
    ui.create_physics_asset = not eas.does_asset_exist(f'{DOG_DIR}/SK_PDog_PhysicsAsset')
    sk = ui.skeletal_mesh_import_data
    sk.set_editor_property('import_morph_targets', False)
    sk.set_editor_property('normal_import_method', unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    setp(sk, 'vertex_color_import_option', unreal.VertexColorImportOption.REPLACE)
    tools.import_asset_tasks([task(fbx, DOG_DIR, 'SK_PDog', ui)])
    path = f'{DOG_DIR}/SK_PDog'
    mesh = eas.load_asset(path)
    if not isinstance(mesh, unreal.SkeletalMesh):
        err(f'导入失败：没有生成骨骼网格 {path}（看上面 LogFbx 的报错）')
        return None
    ms = list(mesh.get_editor_property('materials'))
    names = []
    for i, sm in enumerate(ms):
        n = str(sm.get_editor_property('imported_material_slot_name')) or str(sm.get_editor_property('material_slot_name'))
        sm.set_editor_property('material_slot_name', n)
        if n in mats:
            sm.set_editor_property('material_interface', mats[n])
        else:
            err(f'SK_PDog 槽 {n} 不是 Shell/Frame/Accent')
        ms[i] = sm
        names.append(n)
    mesh.set_editor_property('materials', ms)
    missing = [s for s in SLOTS if s not in names]
    if missing:
        log(f'  SK_PDog 没有槽 {missing}')
    nanite_off(mesh)
    pa_path = f'{DOG_DIR}/SK_PDog_PhysicsAsset'
    if eas.does_asset_exist(pa_path):
        mesh.set_editor_property('physics_asset', eas.load_asset(pa_path))
    eas.save_loaded_asset(mesh, False)
    skeleton = mesh.get_editor_property('skeleton')
    eas.save_loaded_asset(skeleton, False)
    b = mesh.get_bounds()
    log(f'{path}：骨架 {skeleton.get_path_name()}，槽 {names}，物理资产 {eas.does_asset_exist(pa_path)}，'
        f'包围盒半尺寸 ({b.box_extent.x:.1f}, {b.box_extent.y:.1f}, {b.box_extent.z:.1f}) 厘米、中心 ({b.origin.x:.1f}, {b.origin.y:.1f}, {b.origin.z:.1f})')
    check_bones(skeleton)
    rep = read_json(f'{EXPORT}/PDog_build.json')
    if rep:
        want = rep.get('size_m')
        got = [b.box_extent.x * 2 / 100, b.box_extent.y * 2 / 100, b.box_extent.z * 2 / 100]
        if want and any(abs(g - w) > 0.08 * max(w, 0.1) for g, w in zip(got, want)):
            err(f'SK_PDog 尺寸 {[round(g, 2) for g in got]} 米和 Blender 报告 {want} 米对不上（厘米制 / 缩放不对？README「机体资源流程」第 4 条）')
        else:
            log(f'SK_PDog 尺寸和 Blender 报告一致：{[round(g, 2) for g in got]} 米')
    return mesh


def check_bones(skeleton):
    """核对骨头名（API 在不同版本里不一样，拿不到就只记一笔）"""
    names = None
    try:
        pose = unreal.AnimPoseExtensions.get_reference_pose(skeleton)
        names = [str(n) for n in unreal.AnimPoseExtensions.get_bone_names(pose)]
        for n in names:
            t = unreal.AnimPoseExtensions.get_ref_bone_pose(pose, n, unreal.AnimPoseSpaces.WORLD)
            p = t.translation
            log(f'  骨头 {n} 绑定姿势 ({p.x:.1f}, {p.y:.1f}, {p.z:.1f})')
    except Exception as e:
        log(f'  读不到骨头列表（{e}），在编辑器里打开 SK_PDog_Skeleton 看')
    if names is not None:
        miss = [b for b in DOG_BONES if b not in names]
        if miss:
            err(f'SK_PDog 缺骨头 {miss}（现有 {names}）')
        else:
            log(f'  骨头齐全：{len(names)} 根 {names}')


def import_anims(mesh):
    skel = mesh.get_editor_property('skeleton') if mesh else None
    if not skel:
        p = f'{DOG_DIR}/SK_PDog_Skeleton'
        skel = eas.load_asset(p) if eas.does_asset_exist(p) else None
    if not skel:
        err('没有 SK_PDog_Skeleton，导不了动作')
        return
    done = []
    for name in ANIMS:
        fbx = f'{EXPORT}/{name}.fbx'
        if not os.path.exists(fbx):
            log(f'{name}：没有 FBX（底子没这个动作），代码走程序化步态')
            continue
        before = set(eas.list_assets(DOG_DIR, recursive=False, include_folder=False))
        ui = unreal.FbxImportUI()
        ui.import_mesh = False
        ui.import_as_skeletal = True
        ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_ANIMATION
        ui.skeleton = skel
        ui.import_animations = True
        ui.import_materials = False
        ui.import_textures = False
        ui.create_physics_asset = False
        ad = ui.anim_sequence_import_data
        setp(ad, 'animation_length', unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
        setp(ad, 'import_bone_tracks', True)
        setp(ad, 'remove_redundant_keys', False)
        tools.import_asset_tasks([task(fbx, DOG_DIR, name, ui)])
        path = f'{DOG_DIR}/{name}'
        if not eas.does_asset_exist(path):
            new = [p for p in eas.list_assets(DOG_DIR, recursive=False, include_folder=False) if p not in before]
            seqs = [p for p in new if isinstance(eas.load_asset(p), unreal.AnimSequence)]
            if seqs:
                eas.rename_asset(seqs[0], path)
                log(f'  {seqs[0]} 改名 → {path}')
        anim = eas.load_asset(path) if eas.does_asset_exist(path) else None
        if not isinstance(anim, unreal.AnimSequence):
            err(f'{name} 导入失败')
            continue
        setp(anim, 'enable_root_motion', False)
        eas.save_loaded_asset(anim, False)
        try:
            length = unreal.AnimationLibrary.get_sequence_length(anim)
        except Exception:
            length = -1
        log(f'{path}：{length:.2f} 秒')
        done.append(name)
    log(f'动作 {done or "无"}')


# ---------------------------------------------------------------- 主流程
def main():
    args = [a for a in sys.argv[1:] if a and not a.startswith('-')] or ['all']
    what = set(args)
    if 'all' in what:
        what = {'mat', 'drone', 'dog', 'anims'}
    log(f'工程 {PROJECT}，输入 {EXPORT}，要做 {sorted(what)}')
    for f in sorted(os.listdir(EXPORT)) if os.path.isdir(EXPORT) else []:
        log(f'  输入文件 {f} {os.path.getsize(os.path.join(EXPORT, f)) / 1e6:.2f} MB')
    backup()
    legacy_fbx()
    if 'mat' in what or not eas.does_asset_exist(f'{MAT_DIR}/{MASTER}'):
        master = make_master()
        insts = make_instances(master)
    else:
        insts = load_instances()
    mesh = None
    if 'drone' in what:
        import_drone(insts.get('VDrone', {}))
    if 'dog' in what:
        mesh = import_dog(insts.get('PDog', {}))
    if 'anims' in what:
        if mesh is None and eas.does_asset_exist(f'{DOG_DIR}/SK_PDog'):
            mesh = eas.load_asset(f'{DOG_DIR}/SK_PDog')
        import_anims(mesh)
    log(f'汇总：{"没有错误" if not ERRORS else str(len(ERRORS)) + " 个错误：" + "；".join(ERRORS)}')


try:
    main()
except Exception:
    import traceback
    err('脚本出错：\n' + traceback.format_exc())
finally:
    try:
        os.makedirs(f'{PROJECT}/Saved', exist_ok=True)
        open(f'{PROJECT}/Saved/easter_import.txt', 'w', encoding='utf-8').write('\n'.join(REPORT))
    except Exception:
        pass
