"""老旧街道的街景贴片：导入游戏内截的两张街景图（顺街往前 / 往后，见 ue_oldstreet_shots.py 的 backdrop 机位）和一个面片，建无光照半透明材质（上边缘淡出，天空用真天空）。可重复跑。
py -3 tools/oldstreet_backdrop_tex.py 之后：UnrealEditor-Cmd ... -run=pythonscript -script="D:/RobotSlice/tools/ue_oldstreet_backdrop.py" 。之后要重跑 ue_build_oldstreet.py 才摆进图里。
"""
import json

import unreal

SRC = 'D:/RobotSlice/Inbox/_oldstreet/export'
DEST = '/Game/Env/OldStreet'
L = json.load(open('D:/RobotSlice/Inbox/_oldstreet/layout.json', encoding='utf-8'))
BD = L['backdrop']
eas = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
P = unreal.MaterialProperty
log = lambda *a: unreal.log('[oldstreet] ' + ' '.join(str(x) for x in a))


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


tools.import_asset_tasks([task(f'{SRC}/backdrop/T_OS_Backdrop_Fwd.png', f'{DEST}/Textures'), task(f'{SRC}/backdrop/T_OS_Backdrop_Back.png', f'{DEST}/Textures')])
for n in ('T_OS_Backdrop_Fwd', 'T_OS_Backdrop_Back'):
    a = eas.load_asset(f'{DEST}/Textures/{n}')
    a.set_editor_property('srgb', True)
    a.set_editor_property('never_stream', True)
    eas.save_loaded_asset(a)

# 母材质
path = f'{DEST}/Materials/M_OS_Backdrop'
if eas.does_asset_exist(path):
    m = eas.load_asset(path)
    for _ in range(5):
        mel.delete_all_material_expressions(m)
        if mel.get_num_material_expressions(m) == 0:
            break
else:
    m = tools.create_asset('M_OS_Backdrop', f'{DEST}/Materials', unreal.Material, unreal.MaterialFactoryNew())


def node(cls, x, y, **props):
    n = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        n.set_editor_property(k, v)
    return n


def link(a, out, b, inp):
    assert mel.connect_material_expressions(a, out, b, inp), f'连线失败 {out}->{inp}'


def scalar(name, v, x, y):
    return node(unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=v)


d = node(unreal.MaterialExpressionTextureSampleParameter2D, -900, -200, parameter_name='BaseTex', texture=eas.load_asset(f'{DEST}/Textures/T_OS_Backdrop_Fwd'), sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
br = scalar('Bright', 0.85, -900, 80)
inv = node(unreal.MaterialExpressionEyeAdaptationInverse, -900, 160)
a = node(unreal.MaterialExpressionMultiply, -600, -180)
link(d, 'RGB', a, 'A')
link(br, '', a, 'B')
b = node(unreal.MaterialExpressionMultiply, -420, -120)
link(a, '', b, 'A')
link(inv, '', b, 'B')
mel.connect_material_property(b, '', P.MP_EMISSIVE_COLOR)
# 淡出：UV 的 V 从上到下 0..1。V ≤ FadeTop 全透明，V ≥ FadeBottom 不透明
tc = node(unreal.MaterialExpressionTextureCoordinate, -900, 300)
mask = node(unreal.MaterialExpressionComponentMask, -720, 300, r=False, g=True, b=False, a=False)
link(tc, '', mask, '')
top = scalar('FadeTop', BD['fade_top'], -720, 400)
bot = scalar('FadeBottom', BD['fade_bottom'], -720, 480)
s1 = node(unreal.MaterialExpressionSubtract, -520, 320)
link(mask, '', s1, 'A')
link(top, '', s1, 'B')
s2 = node(unreal.MaterialExpressionSubtract, -520, 440)
link(bot, '', s2, 'A')
link(top, '', s2, 'B')
dv = node(unreal.MaterialExpressionDivide, -360, 360)
link(s1, '', dv, 'A')
link(s2, '', dv, 'B')
sat = node(unreal.MaterialExpressionSaturate, -220, 360)
link(dv, '', sat, '')
mel.connect_material_property(sat, '', P.MP_OPACITY)
m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property('used_with_instanced_static_meshes', False)
mel.recompile_material(m)
eas.save_loaded_asset(m)

for name, tex in (('MI_OS_Backdrop_Fwd', 'T_OS_Backdrop_Fwd'), ('MI_OS_Backdrop_Back', 'T_OS_Backdrop_Back')):
    p = f'{DEST}/Materials/{name}'
    mi = eas.load_asset(p) if eas.does_asset_exist(p) else tools.create_asset(name, f'{DEST}/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, m)
    mel.set_material_instance_texture_parameter_value(mi, 'BaseTex', eas.load_asset(f'{DEST}/Textures/{tex}'))
    mel.set_material_instance_scalar_parameter_value(mi, 'Bright', BD.get('bright', 0.85))
    eas.save_loaded_asset(mi)

# 面片（不开 Nanite：半透明不能 Nanite）
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
smd.set_editor_property('build_nanite', False)
tools.import_asset_tasks([task(f'{SRC}/backdrop/SM_OS_Quad.fbx', f'{DEST}/Meshes', name='SM_OS_Quad', options=ui)])
log('街景贴片资源好了')
