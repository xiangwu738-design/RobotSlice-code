"""苏联板楼：冬季包网格陈列 + 四种雪地贴图对比（只为挑素材；存成 /Game/Env/Soviet/Test/L_SvGallery，不进游戏）。
灯光沿用 L_Soviet：加载后删掉楼 / 小品，另存一张。截图：ue_soviet_shots.py + SV_SHOTS_MAP=/Game/Env/Soviet/Test/L_SvGallery。
"""
import unreal

eas = unreal.EditorAssetLibrary
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
P = unreal.MaterialProperty
S = unreal.MaterialSamplerType
DST = '/Game/Env/Soviet/Test/L_SvGallery'
W = '/Game/ANGRY_MESH/NaturePack_WinterEnvironment'
log = lambda *a: unreal.log('[gallery] ' + ' '.join(str(x) for x in a))
out = open('D:/RobotSlice/Inbox/_k62/gallery.txt', 'w', encoding='utf-8')


def p(*a):
    out.write(' '.join(str(x) for x in a) + '\n')


# ---------------------------------------------------------------- 贴图信息 + 雪地测试材质
def sampler(t):
    cs = t.get_editor_property('compression_settings')
    sr = t.get_editor_property('srgb')
    if cs == unreal.TextureCompressionSettings.TC_NORMALMAP:
        return S.SAMPLERTYPE_NORMAL
    if cs == unreal.TextureCompressionSettings.TC_MASKS:
        return S.SAMPLERTYPE_MASKS
    if cs == unreal.TextureCompressionSettings.TC_GRAYSCALE or cs == unreal.TextureCompressionSettings.TC_ALPHA:
        return S.SAMPLERTYPE_GRAYSCALE if sr else S.SAMPLERTYPE_LINEAR_GRAYSCALE
    return S.SAMPLERTYPE_COLOR if sr else S.SAMPLERTYPE_LINEAR_COLOR


path = '/Game/Env/Soviet/Test/M_SnowTest'
m = eas.load_asset(path) if eas.does_asset_exist(path) else tools.create_asset('M_SnowTest', '/Game/Env/Soviet/Test', unreal.Material, unreal.MaterialFactoryNew())
mel.delete_all_material_expressions(m)
wp = mel.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -900, 0)
mk = mel.create_material_expression(m, unreal.MaterialExpressionComponentMask, -750, 0)
mk.set_editor_property('r', True)
mk.set_editor_property('g', True)
mk.set_editor_property('b', False)
mel.connect_material_expressions(wp, '', mk, '')
tile = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -750, 120)
tile.set_editor_property('parameter_name', 'Tile')
tile.set_editor_property('default_value', 500.0)
dv = mel.create_material_expression(m, unreal.MaterialExpressionDivide, -600, 0)
mel.connect_material_expressions(mk, '', dv, 'A')
mel.connect_material_expressions(tile, '', dv, 'B')
nodes = {}
for i, (pn, tex) in enumerate((('A', f'{W}/Textures/Landscape/T_Snow_01_A'), ('N', f'{W}/Textures/Landscape/T_Snow_01_N'), ('R', f'{W}/Textures/Landscape/T_Snow_01_R'))):
    t = eas.load_asset(tex)
    n = mel.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -400, i * 250)
    n.set_editor_property('parameter_name', pn)
    n.set_editor_property('texture', t)
    n.set_editor_property('sampler_type', sampler(t))
    mel.connect_material_expressions(dv, '', n, 'UVs')
    nodes[pn] = n
mel.connect_material_property(nodes['A'], 'RGB', P.MP_BASE_COLOR)
mel.connect_material_property(nodes['N'], 'RGB', P.MP_NORMAL)
mel.connect_material_property(nodes['R'], 'R', P.MP_ROUGHNESS)
mel.recompile_material(m)
eas.save_loaded_asset(m)
for k in range(1, 5):
    for s in ('A', 'N', 'R', 'H'):
        t = eas.load_asset(f'{W}/Textures/Landscape/T_Snow_0{k}_{s}')
        p(t.get_name(), t.blueprint_get_size_x(), t.get_editor_property('compression_settings'), t.get_editor_property('srgb'))

# ---------------------------------------------------------------- 关卡
unreal.EditorLoadingAndSavingUtils.load_map('/Game/Maps/L_Soviet')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
for a in sub.get_all_level_actors():
    fp = str(a.get_folder_path())
    if any(fp.endswith(x) for x in ('Buildings', 'Far', 'Props', 'Winter', 'Bounds')):
        sub.destroy_actor(a)
cube = eas.load_asset('/Engine/BasicShapes/Cube')


def place(mesh, x, y, yaw=0.0, label=None, scale=1.0):
    a = sub.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x * 100, -y * 100, 0), unreal.Rotator(roll=0, pitch=0, yaw=-yaw))
    a.set_actor_label(label or mesh.get_name())
    a.set_folder_path('Gallery')
    a.static_mesh_component.set_static_mesh(mesh)
    a.static_mesh_component.set_editor_property('mobility', unreal.ComponentMobility.STATIC)
    a.static_mesh_component.set_collision_profile_name('NoCollision')
    return a


def row(names, y, step, sub_dir):
    x = -((len(names) - 1) * step) / 2
    for n in names:
        mesh = eas.load_asset(f'{W}/Meshes/{sub_dir}/{n}')
        if mesh:
            place(mesh, x, y)
            p(sub_dir, n, 'x', x, 'y', y)
        else:
            p('缺', sub_dir, n)
        x += step


row([f'SM_Tree_A_0{i}' for i in range(1, 7)], 80, 16, 'Trees')
row([f'SM_Tree_B_0{i}' for i in range(1, 7)], 60, 16, 'Trees')
row([f'SM_Tree_C_0{i}' for i in range(1, 8)], 40, 16, 'Trees')
row([f'SM_Bush_A_0{i}' for i in range(1, 8)], 24, 11, 'Trees')
row(['SM_Rock_01', 'SM_Rock_11', 'SM_Rock_12', 'SM_Rock_18', 'SM_Rock_Group_01', 'SM_Fence_A_01', 'SM_Fence_B_01', 'SM_Fence_C_01'][:5], 10, 10, 'Rocks')
row(['SM_Fence_A_01', 'SM_Fence_A_03', 'SM_Fence_B_01', 'SM_Fence_B_03', 'SM_Fence_C_01', 'SM_Fence_C_03', 'SM_Debris_01', 'SM_Debris_03', 'SM_Debris_Small_02', 'SM_Log_01', 'SM_Log_03', 'SM_Plank_Group_A_01', 'SM_Plank_Group_B_01', 'SM_Plank_Group_C_01', 'SM_Root_01', 'SM_Stump_01'], -4, 6, 'Props')
row([f'SM_Grass_A_0{i}' for i in range(1, 7)] + [f'SM_Grass_B_0{i}' for i in range(1, 7)] + [f'SM_Grass_C_0{i}' for i in range(1, 7)], -14, 3, 'Grasses')
# 四种雪地：各一块 40×40 米，紧贴地面
for k in range(1, 5):
    mi_path = f'/Game/Env/Soviet/Test/MI_SnowTest{k}'
    mi = eas.load_asset(mi_path) if eas.does_asset_exist(mi_path) else tools.create_asset(f'MI_SnowTest{k}', '/Game/Env/Soviet/Test', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', m)
    for pn, s in (('A', 'A'), ('N', 'N'), ('R', 'R')):
        mel.set_material_instance_texture_parameter_value(mi, pn, eas.load_asset(f'{W}/Textures/Landscape/T_Snow_0{k}_{s}'))
    eas.save_loaded_asset(mi)
    a = place(cube, -100 + (k - 1) * 45, -50)
    a.set_actor_scale3d(unreal.Vector(40, 40, 0.02))
    a.set_actor_location(unreal.Vector((-100 + (k - 1) * 45) * 100, 50 * 100, 0.5), False, False)
    a.static_mesh_component.set_material(0, mi)
unreal.EditorLoadingAndSavingUtils.save_map(world, DST)
out.close()
log('已存', DST)
