"""白天特效发黑的修正：特效材质（不受光、自发光）的亮度是按夜晚的曝光调的，白天自动曝光比夜里暗约 14 档，
叠加型的看不见、半透明型的只剩黑影。给这些材质的自发光乘上 max(1, 曝光倒数 × GAIN) ^ POWER：
夜里（曝光倒数约 0.76）是 1，样子不变；白天（约 13000）补几千倍，又不会亮得和夜里一样刺眼。
可以重复跑：已经加过的会跳过。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_fx_daylight.py" -AllowCommandletRendering -RenderOffscreen"""
import unreal

GAIN = 1.3
POWER = 0.85
TAG = 'FxDayBoost'
MATERIALS = [
    '/Game/AAAVFX/Course_00/Material/M_AdditiveDe_v01', '/Game/AAAVFX/Course_00/Material/M_AdditiveDe_v02',
    '/Game/AAAVFX/Course_00/Material/M_RadialUV_v01', '/Game/AAAVFX/Course_00/Material/M_Sequence_v01',
    '/Game/AAAVFX/Course_01/Material/M_AdditiveDe_v01', '/Game/AAAVFX/Course_01/Material/M_AdditiveDe_v02',
    '/Game/AAAVFX/Course_01/Material/M_Mesh_v01', '/Game/AAAVFX/Course_01/Material/M_SwordTrail_01',
    '/Game/AAAVFX/Course_04/Material/M_Additive_01', '/Game/AAAVFX/Course_04/Material/M_Additive_02',
    '/Game/AAAVFX/Course_04/Material/M_Additive_NOMB_01', '/Game/AAAVFX/Course_04/Material/M_RadialUV_v01',
    '/Game/AAAVFX/Course_04/Material/M_Smoke_01',
]
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
P = unreal.MaterialProperty.MP_EMISSIVE_COLOR


def node(mat, cls, x, y, desc=TAG):
    n = mel.create_material_expression(mat, cls, x, y)
    n.set_editor_property('desc', desc)
    return n


for path in MATERIALS:
    mat = unreal.load_asset(path)
    if not isinstance(mat, unreal.Material):
        unreal.log_warning(f'[fxday] 找不到 {path}')
        continue
    src = mel.get_material_property_input_node(mat, P)
    if src is None:
        unreal.log_warning(f'[fxday] {path} 自发光没接，跳过')
        continue
    if str(src.get_editor_property('desc')) == TAG:
        unreal.log(f'[fxday] {path} 已经加过（要改系数就先在编辑器里删掉 {TAG} 那几个节点再跑）')
        continue
    out_name = mel.get_material_property_input_node_output_name(mat, P)
    x, y = -500, 600
    inv = node(mat, unreal.MaterialExpressionEyeAdaptationInverse, x - 700, y)
    gain = node(mat, unreal.MaterialExpressionConstant, x - 700, y + 120)
    gain.set_editor_property('r', GAIN)
    mul = node(mat, unreal.MaterialExpressionMultiply, x - 500, y)
    mel.connect_material_expressions(inv, '', mul, 'A')
    mel.connect_material_expressions(gain, '', mul, 'B')
    mx = node(mat, unreal.MaterialExpressionMax, x - 350, y)
    mel.connect_material_expressions(mul, '', mx, 'A')
    mx.set_editor_property('const_b', 1.0)
    pw = node(mat, unreal.MaterialExpressionPower, x - 200, y)
    mel.connect_material_expressions(mx, '', pw, 'Base')
    pw.set_editor_property('const_exponent', POWER)
    out = node(mat, unreal.MaterialExpressionMultiply, x, y - 100)
    mel.connect_material_expressions(src, out_name, out, 'A')
    mel.connect_material_expressions(pw, '', out, 'B')
    mel.connect_material_property(out, '', P)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat, False)
    unreal.log(f'[fxday] {path} 加上白天补偿（原自发光来自 {type(src).__name__}.{out_name or "默认"}）')
unreal.log('[fxday] 完成')
