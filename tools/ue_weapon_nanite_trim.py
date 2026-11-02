"""武器减面（10-31 用户：「武器相对于角色的精度高很多，没有必要这么精细」）：只改各把武器网格的 Nanite「保留三角形比例」，
不重新导入、不动原始网格数据，随时能改回（参数 restore）。目标：枪约 2.5 万面、刀 / 手枪约 1.2 万面（一整台机体 2–10 万面），
本来就比目标少的不动。源面数是 SourceAssets 里 FBX 的三角面数（Blender 数的）。改前的 .uasset 备份在 D:/归档/RobotSlice_武器减面前_20261031。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_weapon_nanite_trim.py [restore]" -AllowCommandletRendering -RenderOffscreen -unattended
"""
import sys

import unreal

eas = unreal.EditorAssetLibrary
RESTORE = 'restore' in sys.argv[1:]

# 网格: (源三角面数, 目标)
TARGETS = {
    'SM_Gun11': (341023, 25000),   # 霰弹枪
    'SM_Gun6': (262888, 25000),    # 冲锋枪
    'SM_Gun2': (163261, 25000),    # 充能步枪
    'SM_Gun3': (119199, 25000),    # 步枪
    'SM_Gun9': (20930, 25000),     # 狙击枪（不动）
    'SM_Pistol7': (35970, 12000),  # 双持手枪（一把；手上两把）
    'SM_Sword07': (87773, 12000),  # 双手刀
    'SM_Sword14': (29266, 12000),  # 大剑
    'SM_Sword11': (22698, 12000),  # 快刀
    'SM_Sword16': (16837, 12000),  # 双刀（一把；手上两把）
    'SM_Sword03': (13960, 12000),  # 匕首（不动）
    'SM_Sword13': (13592, 12000),  # 单手刀（不动）
    'SM_Sword20': (9169, 12000),   # 钝器（不动）
}

total0 = total1 = 0
for name, (src, want) in TARGETS.items():
    path = f'/Game/Weapons/Meshes/{name}'
    mesh = eas.load_asset(path)
    if not mesh:
        unreal.log_warning(f'[trim] 找不到 {path}')
        continue
    keep = 1.0 if RESTORE else min(1.0, want / src)
    ns = mesh.get_editor_property('nanite_settings')
    old = ns.get_editor_property('keep_percent_triangles')
    total0 += src
    total1 += round(src * keep)
    if abs(old - keep) < 1e-4:
        unreal.log(f'[trim] {name}：保留 {keep:.3f} 没变（{src} → 约 {round(src * keep)}）')
        continue
    ns.set_editor_property('keep_percent_triangles', keep)
    # 命令行里拿不到 StaticMeshEditorSubsystem（返回 None）：直接设属性，PostEditChangeProperty 会重建网格（含 Nanite 数据）
    mesh.set_editor_property('nanite_settings', ns)
    got = mesh.get_editor_property('nanite_settings').get_editor_property('keep_percent_triangles')
    assert abs(got - keep) < 1e-4, f'{name} 没设上：{got}'
    eas.save_loaded_asset(mesh, False)
    unreal.log(f'[trim] {name}：保留 {old:.3f} → {keep:.3f}（{src} → 约 {round(src * keep)} 面）')
unreal.log(f'[trim] 13 把合计 {total0} → 约 {total1} 面')
