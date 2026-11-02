"""远景高楼材质 M_Skyline：深色楼体 + 按世界坐标生成的一格格窗户（随机亮灭、冷暖），一栋楼只要一个方块。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="ue_skyline_material.py" -AllowCommandletRendering -RenderOffscreen
"""
import unreal

eas = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
P = unreal.MaterialProperty
PATH = '/Game/Env/Materials/M_Skyline'

if eas.does_asset_exist(PATH):
    m = eas.load_asset(PATH)
    mel.delete_all_material_expressions(m)
else:
    m = tools.create_asset('M_Skyline', '/Game/Env/Materials', unreal.Material, unreal.MaterialFactoryNew())

CODE = r'''
float3 p = WP / 100.0;
float3 n = abs(N);
// 顶面不开窗
if (n.z > 0.6) return float3(0, 0, 0);
float2 uv = (n.x > n.y) ? p.yz : p.xz;
float2 cellSize = float2(CellW, CellH);
float2 cell = floor(uv / cellSize);
float2 f = frac(uv / cellSize);
float win = step(0.22, f.x) * step(f.x, 0.78) * step(0.32, f.y) * step(f.y, 0.72);
float r = frac(sin(dot(cell, float2(12.9898, 78.233))) * 43758.5453);
float lit = step(1.0 - LitRatio, r);
float warm = step(0.72, frac(r * 7.13));
float3 cool = float3(0.55, 0.72, 1.0);
float3 amber = float3(1.0, 0.72, 0.42);
float flick = 0.55 + 0.75 * frac(r * 3.71);
return win * lit * lerp(cool, amber, warm) * flick;
'''


def node(cls, x, y, **props):
    e = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def scalar(name, value, x, y):
    return node(unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)


custom = node(unreal.MaterialExpressionCustom, -500, 0, code=CODE,
              output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3, description='SkylineWindows')
inputs = []
for name in ('WP', 'N', 'CellW', 'CellH', 'LitRatio'):
    ci = unreal.CustomInput()
    ci.set_editor_property('input_name', name)
    inputs.append(ci)
custom.set_editor_property('inputs', inputs)
wp = node(unreal.MaterialExpressionWorldPosition, -800, -150)
nrm = node(unreal.MaterialExpressionVertexNormalWS, -800, -50)
for src, pin in ((wp, 'WP'), (nrm, 'N'), (scalar('CellW', 2.4, -800, 50), 'CellW'),
                 (scalar('CellH', 3.3, -800, 150), 'CellH'), (scalar('LitRatio', 0.24, -800, 250), 'LitRatio')):
    assert mel.connect_material_expressions(src, '', custom, pin), pin
strength = scalar('WindowStrength', 2.6, -500, 250)
emis = node(unreal.MaterialExpressionMultiply, -250, 100)
mel.connect_material_expressions(custom, '', emis, 'A')
mel.connect_material_expressions(strength, '', emis, 'B')
body = node(unreal.MaterialExpressionVectorParameter, -500, -300, parameter_name='BodyColor',
            default_value=unreal.LinearColor(0.006, 0.007, 0.010, 1))
mel.connect_material_property(body, '', P.MP_BASE_COLOR)
mel.connect_material_property(emis, '', P.MP_EMISSIVE_COLOR)
rough = node(unreal.MaterialExpressionConstant, -250, -150, r=0.55)
mel.connect_material_property(rough, '', P.MP_ROUGHNESS)
mel.recompile_material(m)
eas.save_loaded_asset(m)
unreal.log('[sky] M_Skyline 完成')
