"""在 RobotSlice 工程里跑（10-30 战斗调整加的资源）：
- SourceAssets/sfx_guard/*.wav → /Game/Audio/Combat/S_*（格挡 guard_block、弹反 parry_ting + parry_ring、破防 guard_break、受击闷响 hurt_thud；
  来源见 SourceAssets/sfx_guard/来源.txt）
- 充能步枪的激光 / 光束材质 /Game/Weapons/Materials/M_MechBeam：叠加、不受光、单面；
  自发光 = 颜色参数 Color × 强度 Intensity × 中间亮边缘暗（1 - 菲涅耳）× 白天补偿 max(1, 曝光倒数×1.3)^0.85
  （白天自动曝光比夜里高约 14 档，不补偿就看不见，和 tools/ue_fx_daylight.py 给特效加的一样）。
  勾上「用于实例化静态网格」（代码里用 InstancedStaticMeshComponent 画分段圆柱）。
  材质已经有了就不重建（游戏代码引用着，清空节点会 !IsRooted 崩）；要改先在编辑器里删掉再跑。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_import_combat_fx.py" -AllowCommandletRendering -RenderOffscreen
"""
import os

import unreal

eas = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
SFX = 'D:/RobotSlice/SourceAssets/sfx_guard'


def task(filename, dest, name):
    t = unreal.AssetImportTask()
    t.filename = filename
    t.destination_path = dest
    t.destination_name = name
    t.automated = True
    t.replace_existing = True
    t.save = True
    return t


# ---------------------------------------------------------------- 音效
wavs = sorted(f for f in os.listdir(SFX) if f.endswith('.wav'))
tools.import_asset_tasks([task(f'{SFX}/{f}', '/Game/Audio/Combat', 'S_' + os.path.splitext(f)[0]) for f in wavs])
for f in wavs:
    p = '/Game/Audio/Combat/S_' + os.path.splitext(f)[0]
    unreal.log(f'[combatfx] 音效 {p} {"有" if eas.does_asset_exist(p) else "没导进去"}')


# ---------------------------------------------------------------- 光束材质
def beam_material():
    name = 'M_MechBeam'
    path = f'/Game/Weapons/Materials/{name}'
    if eas.does_asset_exist(path):
        unreal.log(f'[combatfx] {path} 已经有了，不重建')
        return
    m = tools.create_asset(name, '/Game/Weapons/Materials', unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided', False)
    m.set_editor_property('used_with_instanced_static_meshes', True)
    color = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -1000, -200)
    color.set_editor_property('parameter_name', 'Color')
    color.set_editor_property('default_value', unreal.LinearColor(1.0, 0.3, 0.1, 1.0))
    inten = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -1000, 0)
    inten.set_editor_property('parameter_name', 'Intensity')
    inten.set_editor_property('default_value', 10.0)
    cs = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -750, -100)
    mel.connect_material_expressions(color, '', cs, 'A')
    mel.connect_material_expressions(inten, '', cs, 'B')
    # 圆柱截面上中间亮、边缘暗：1 - 菲涅耳，再平方让边缘软一点
    fres = mel.create_material_expression(m, unreal.MaterialExpressionFresnel, -1000, 200)
    fres.set_editor_property('exponent', 1.5)
    fres.set_editor_property('base_reflect_fraction', 0.0)
    inv = mel.create_material_expression(m, unreal.MaterialExpressionOneMinus, -800, 200)
    mel.connect_material_expressions(fres, '', inv, '')
    sq = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -650, 200)
    mel.connect_material_expressions(inv, '', sq, 'A')
    mel.connect_material_expressions(inv, '', sq, 'B')
    prof = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -500, 0)
    mel.connect_material_expressions(cs, '', prof, 'A')
    mel.connect_material_expressions(sq, '', prof, 'B')
    # 白天补偿
    eye = mel.create_material_expression(m, unreal.MaterialExpressionEyeAdaptationInverse, -1000, 400)
    k = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -1000, 500)
    k.set_editor_property('r', 1.3)
    eyek = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -800, 420)
    mel.connect_material_expressions(eye, '', eyek, 'A')
    mel.connect_material_expressions(k, '', eyek, 'B')
    one = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -800, 540)
    one.set_editor_property('r', 1.0)
    mx = mel.create_material_expression(m, unreal.MaterialExpressionMax, -650, 450)
    mel.connect_material_expressions(eyek, '', mx, 'A')
    mel.connect_material_expressions(one, '', mx, 'B')
    ex = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -650, 560)
    ex.set_editor_property('r', 0.85)
    pw = mel.create_material_expression(m, unreal.MaterialExpressionPower, -500, 470)
    mel.connect_material_expressions(mx, '', pw, 'Base')
    mel.connect_material_expressions(ex, '', pw, 'Exponent')
    pw.set_editor_property('desc', 'BeamDayBoost')
    out = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -300, 200)
    mel.connect_material_expressions(prof, '', out, 'A')
    mel.connect_material_expressions(pw, '', out, 'B')
    mel.connect_material_property(out, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    unreal.log(f'[combatfx] 建好 {path}')


beam_material()
unreal.log('[combatfx] 完成')
