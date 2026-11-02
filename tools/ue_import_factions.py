"""在 RobotSlice 工程里跑：把阵营机体（三家企业 × 轻 / 中 / 重，lookdev/rig/factions/<key>.fbx）导入游戏。
流程照 tools/ue_import_robot.py：老 FBX 导入器（Interchange 并骨头时会卡死）、骨架用现有的 SK_Mannequin（九台骨头和 v3 完全一样，
模板动画直接能用）、导完按 FBX 里的原名改回槽名、显式存骨架。和 ue_import_robot.py 的区别：不改 MI_Robot_* 的参数（只挂现有的），
维克托三台的主漆槽挂物体空间折线迷彩 MI_Robot_PaintCamo（没有就先建 M_RobotCamo）。

  UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_import_factions.py NEC_M NEC_H ..." -AllowCommandletRendering -RenderOffscreen -unattended
  参数：台名（NEC_L / NEC_M / ... / VHI_H），或 all（除 NEC_L 外的八台），或 camo（只建迷彩材质）。
  NEC_L = 现行 v3：覆盖导入 /Game/Robot/Meshes/SKM_Robot_v3（修了素材件的贴图 UV、面数 10 万 → 7.2 万）；其余导到 /Game/Robot/Factions/SKM_<key>。
日志里搜 [factions]：每台的骨头数、槽名、包围盒；最后一行是汇总。
"""
import sys

import unreal

FBX_DIR = 'D:/RobotSlice/lookdev/rig/factions'
DEST = '/Game/Robot/Factions'
MAT_DIR = '/Game/Robot/Materials'
SKELETON = '/Game/Characters/Mannequins/Meshes/SK_Mannequin'
PHYSICS = '/Game/Characters/Mannequins/Rigs/PA_Mannequin'
KEYS = ['NEC_L', 'NEC_M', 'NEC_H', 'VHI_L', 'VHI_M', 'VHI_H', 'PHASE_L', 'PHASE_M', 'PHASE_H']
SLOTS = ['MI_Robot_Frame', 'MI_Robot_Paint', 'MI_Robot_PaintDark', 'MI_Robot_Accent', 'MI_Robot_Stencil', 'MI_Robot_Nozzle',
         'MI_Robot_Plastic', 'MI_Robot_Lens', 'MI_Robot_Glow', 'MI_Robot_Rubber', 'MI_Kit_Hydraulics', 'MI_Kit_Pipes']

tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary


def log(msg):
    unreal.log(f'[factions] {msg}')


def lin(hexstr):
    h = hexstr.lstrip('#')
    c = [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    return [x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c]


# ---------------------------------------------------------------- 物体空间三色折线迷彩（维克托重工）
# 不用 UV：按绑定姿势的网格坐标（PreSkinnedPosition，厘米，左 +X、前 +Y、上 +Z）切两层斜着的格子，格子边再按三角波折成锯齿，
# 每格按哈希取色：第一层取到的涂暗漆（CamoDark），第二层取到的涂一个灰色（主漆的亮度 × CamoLight），其余是主漆（BaseColor）。
# 两层格子方向差约 40 度，叠起来是一块块带斜边、折线边的多边形色块（城市灰折线迷彩）；动作时花纹贴着装甲走（用的是蒙皮前的位置）。
# 涂装系统改主漆 / 副漆时 AMechCharacter::ApplyPaint 同时设 BaseColor 和 CamoDark，迷彩跟着变。
# 自定义节点代码里不能有中文（材质编译会失败），注释写在这里。
CAMO_HLSL = r'''
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
float lum = dot(A, float3(0.2126, 0.7152, 0.0722));
float3 g = saturate(float3(lum, lum, lum) * Light + 0.012);
float3 col = A;
if (h1 < DarkShare) col = B;
if (h2 < GrayShare) col = g;
return lerp(A, col, Amount);
'''


def make_camo():
    """M_RobotCamo（母材质，参数同 M_RobotMaster + 迷彩）+ MI_Robot_PaintCamo（默认色同 MI_Robot_Paint / PaintDark）。已有就直接用"""
    mi_path = f'{MAT_DIR}/MI_Robot_PaintCamo'
    if eal.does_asset_exist(mi_path):
        return unreal.load_asset(mi_path)
    path = f'{MAT_DIR}/M_RobotCamo'
    if eal.does_asset_exist(path):
        m = unreal.load_asset(path)
    else:
        m = tools.create_asset('M_RobotCamo', MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
        P = unreal.MaterialProperty
        y = [-700]

        def node(cls, x=-900, **props):
            e = mel.create_material_expression(m, cls, x, y[0])
            y[0] += 110
            for k, v in props.items():
                e.set_editor_property(k, v)
            return e

        def sp(name, v):
            return node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=v)

        def vp(name, c):
            return node(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=c)

        pos = node(unreal.MaterialExpressionPreSkinnedPosition, x=-1300)
        interp = node(unreal.MaterialExpressionVertexInterpolator, x=-1100)  # 蒙皮前位置只能在顶点着色器里取，插值到像素着色器
        mel.connect_material_expressions(pos, '', interp, '')
        base = vp('BaseColor', unreal.LinearColor(*lin('#5a636c'), 1))
        dark = vp('CamoDark', unreal.LinearColor(*lin('#383f46'), 1))
        names = ['P', 'A', 'B', 'Light', 'Scale', 'DarkShare', 'GrayShare', 'Amount']
        ins_nodes = [interp, base, dark, sp('CamoLight', 1.55), sp('CamoScale', 21.0), sp('CamoDarkShare', 0.40), sp('CamoGrayShare', 0.27),
                     sp('CamoAmount', 1.0)]
        custom = mel.create_material_expression(m, unreal.MaterialExpressionCustom, -500, -300)
        custom.set_editor_property('code', CAMO_HLSL)
        custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        custom.set_editor_property('description', 'SplinterCamo')
        ins = []
        for n in names:
            ci = unreal.CustomInput()
            ci.set_editor_property('input_name', n)
            ins.append(ci)
        custom.set_editor_property('inputs', ins)
        for n, e in zip(names, ins_nodes):
            mel.connect_material_expressions(e, '', custom, n)
        metal = sp('Metallic', 0.0)
        rough = sp('Roughness', 0.5)
        var = sp('RoughnessVariation', 0.12)
        noise = node(unreal.MaterialExpressionNoise, scale=0.08, levels=4, output_min=-1.0, output_max=1.0)
        mul = node(unreal.MaterialExpressionMultiply, x=-600)
        add = node(unreal.MaterialExpressionAdd, x=-450)
        mel.connect_material_expressions(noise, '', mul, 'A')
        mel.connect_material_expressions(var, '', mul, 'B')
        mel.connect_material_expressions(rough, '', add, 'A')
        mel.connect_material_expressions(mul, '', add, 'B')
        emis = vp('Emissive', unreal.LinearColor(0, 0, 0, 1))
        estr = sp('EmissiveStrength', 0.0)
        emul = node(unreal.MaterialExpressionMultiply, x=-600)
        mel.connect_material_expressions(emis, '', emul, 'A')
        mel.connect_material_expressions(estr, '', emul, 'B')
        mel.connect_material_property(custom, '', P.MP_BASE_COLOR)
        mel.connect_material_property(metal, '', P.MP_METALLIC)
        mel.connect_material_property(add, '', P.MP_ROUGHNESS)
        mel.connect_material_property(emul, '', P.MP_EMISSIVE_COLOR)
        m.set_editor_property('used_with_skeletal_mesh', True)
        mel.recompile_material(m)
        eal.save_loaded_asset(m, False)
        log('建了迷彩母材质 M_RobotCamo')
    mi = tools.create_asset('MI_Robot_PaintCamo', MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', m)
    mel.set_material_instance_vector_parameter_value(mi, 'BaseColor', unreal.LinearColor(*lin('#5a636c'), 1))
    mel.set_material_instance_vector_parameter_value(mi, 'CamoDark', unreal.LinearColor(*lin('#383f46'), 1))
    mel.set_material_instance_scalar_parameter_value(mi, 'Roughness', 0.5)
    eal.save_loaded_asset(mi, False)
    log('建了 MI_Robot_PaintCamo')
    return mi


# ---------------------------------------------------------------- 导入一台
def import_one(key):
    fbx = f'{FBX_DIR}/{key}.fbx'
    if key == 'NEC_L':
        dest_dir, dest_name = '/Game/Robot/Meshes', 'SKM_Robot_v3'  # 现行 v3：覆盖重导（同一套建模的游戏版）
    else:
        dest_dir, dest_name = DEST, f'SKM_{key}'
    for cvar in ('Interchange.FeatureFlags.Import.FBX', 'r.Interchange.FeatureFlags.Import.FBX'):
        unreal.SystemLibrary.execute_console_command(None, f'{cvar} 0')
    task = unreal.AssetImportTask()
    task.filename = fbx
    task.destination_path = dest_dir
    task.destination_name = dest_name
    task.automated = True
    task.replace_existing = True
    task.save = True
    ui = unreal.FbxImportUI()
    ui.import_mesh = True
    ui.import_as_skeletal = True
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    ui.skeleton = unreal.load_asset(SKELETON)
    ui.import_animations = False
    ui.import_materials = False
    ui.import_textures = False
    ui.create_physics_asset = False
    ui.skeletal_mesh_import_data.set_editor_property('import_morph_targets', False)
    ui.skeletal_mesh_import_data.set_editor_property('normal_import_method', unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    task.options = ui
    tools.import_asset_tasks([task])
    path = f'{dest_dir}/{dest_name}'
    mesh = unreal.load_asset(path)
    if mesh is None:
        unreal.log_error(f'[factions] {key} 导入失败：没有生成 {path}')
        return None
    camo = make_camo() if key.startswith('VHI') else None
    mats = list(mesh.get_editor_property('materials'))
    missing = []
    for i, sm in enumerate(mats):
        # 覆盖导入时老导入器会沿用旧网格的槽名，按 FBX 里的原名改回
        name = str(sm.get_editor_property('imported_material_slot_name')) or str(sm.get_editor_property('material_slot_name'))
        sm.set_editor_property('material_slot_name', name)
        want = f'{MAT_DIR}/{name}'
        if camo and name == 'MI_Robot_Paint':
            sm.set_editor_property('material_interface', camo)
        elif eal.does_asset_exist(want):
            sm.set_editor_property('material_interface', unreal.load_asset(want))
        else:
            missing.append(name)
        mats[i] = sm
    mesh.set_editor_property('materials', mats)
    pa = unreal.load_asset(PHYSICS)
    if pa:
        mesh.set_editor_property('physics_asset', pa)
    eal.save_loaded_asset(mesh, False)
    skeleton = mesh.get_editor_property('skeleton')
    saved = eal.save_loaded_asset(skeleton, False)
    names = [str(m.get_editor_property('material_slot_name')) for m in mats]
    b = mesh.get_bounds()
    ok_slots = names == SLOTS
    verts = -1
    try:
        verts = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem).get_num_verts(mesh, 0)
    except Exception:
        pass
    # 骨头数 / 骨头顺序和 v3 是否一致在游戏里核对（-MechFrameLineup 开头每台打一行 [lineup] 骨头），这里 Python 拿不到网格的参考骨架
    log(f'{key} → {mesh.get_path_name()} 骨架={skeleton.get_name()}（存 {saved}） 顶点={verts} '
        f'槽={len(names)} {"同名同序" if ok_slots else "不一致：" + str(names)} 缺材质={missing} 包围盒半径={b.sphere_radius:.1f} '
        f'盒={b.box_extent} 主漆槽={"迷彩" if camo else "MI_Robot_Paint"}')
    return mesh, verts, ok_slots


args = [a for a in sys.argv[1:] if a]
if not args:
    args = ['all']
want = []
for a in args:
    if a == 'all':
        want += [k for k in KEYS if k != 'NEC_L']
    elif a == 'camo':
        make_camo()
    elif a in KEYS:
        want.append(a)
    else:
        unreal.log_error(f'[factions] 不认识的参数 {a}')
summary = []
for key in want:
    r = import_one(key)
    summary.append(f'{key}:{"失败" if r is None else ("顶点%d %s" % (r[1], "槽OK" if r[2] else "槽不一致"))}')
log('汇总 ' + '，'.join(summary))
