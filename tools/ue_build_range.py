"""测试场 /Game/Maps/L_Range：只有平地（原型网格，方便看距离）、几块掩体、一座 10 米高台（试飞行型推进器）、距离标和木桩点。
天空灯光沿用 L_LightingBase；木桩由游戏模式在带「Dummy」标签的 TargetPoint 上生成（AMechGameMode::SpawnRange）。
重复运行会从 L_LightingBase 重新开始，不叠加。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_build_range.py" """
import unreal

SRC_MAP = '/Game/Maps/L_LightingBase'
DST_MAP = '/Game/Maps/L_Range'
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
cube = unreal.load_asset('/Engine/BasicShapes/Cube')
grid = unreal.load_asset('/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray')
dark = unreal.load_asset('/Game/LevelPrototyping/Materials/MI_PrototypeGrid_TopDark')


def V(x, y, z):
    """米 → 厘米"""
    return unreal.Vector(x * 100.0, y * 100.0, z * 100.0)


def block(label, center, size, mat, yaw=0.0):
    """center、size 都是米；Cube 本身 1 米见方"""
    a = sub.spawn_actor_from_class(unreal.StaticMeshActor, V(*center), unreal.Rotator(pitch=0.0, yaw=yaw, roll=0.0))
    a.set_actor_label(label)
    a.set_folder_path('Range')
    a.tags = ['Range']
    c = a.static_mesh_component
    c.set_static_mesh(cube)
    c.set_editor_property('mobility', unreal.ComponentMobility.STATIC)
    c.set_material(0, mat)
    a.set_actor_scale3d(unreal.Vector(*size))
    return a


unreal.EditorLoadingAndSavingUtils.load_map(SRC_MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()

# 地面 300 × 300 米，顶面在 0
block('Floor', (60, 0, -0.5), (300, 300, 1), grid)
# 跑道两边的矮墙（掩体）
block('Cover_L1', (18, -10, 1.5), (1.2, 8, 3), dark)
block('Cover_R1', (30, 9, 1.5), (8, 1.2, 3), dark)
block('Cover_L2', (42, -14, 2.5), (6, 6, 5), dark)
block('Cover_C', (62, 2, 1.5), (1.2, 10, 3), dark)
# 10 米高台（地面型推进器上不去，飞行型 / 均衡型能飞上去）
block('Platform', (35, 24, 9.6), (12, 12, 0.8), dark)
for dx in (-5, 5):
    for dy in (-5, 5):
        block('Pillar', (35 + dx, 24 + dy, 4.6), (1, 1, 9.2), grid)
# 远处 100 米一面墙当背景、也挡子弹
block('BackWall', (105, 0, 6), (2, 80, 12), dark)

# 出生点：原点朝 +X
starts = [a for a in sub.get_all_level_actors() if isinstance(a, unreal.PlayerStart)]
start = starts[0] if starts else sub.spawn_actor_from_class(unreal.PlayerStart, V(0, 0, 1.5), unreal.Rotator())
start.set_actor_location(V(0, 0, 1.5), False, False)
start.set_actor_rotation(unreal.Rotator(pitch=0.0, yaw=0.0, roll=0.0), False)

# 木桩点（米）：10 / 25 / 50 / 80 米，一个在高台上，一个躲在掩体后
for name, (x, y, z) in (('10m', (10, 0, 0)), ('25m', (25, -4, 0)), ('50m', (50, 0, 0)), ('80m', (80, 6, 0)),
                        ('高台', (35, 24, 10.0)), ('掩体后', (20, -14, 0))):
    tp = sub.spawn_actor_from_class(unreal.TargetPoint, V(x, y, z + 0.5), unreal.Rotator())
    tp.tags = ['Dummy']
    tp.set_actor_label('Dummy_' + name)
    tp.set_folder_path('Range/Dummies')

# 距离标：跑道左边立牌子
for d in (10, 25, 50, 80):
    block('Marker_%d' % d, (d, -8, 1.0), (0.3, 0.3, 2.0), grid)
    t = sub.spawn_actor_from_class(unreal.TextRenderActor, V(d, -8, 2.4), unreal.Rotator(pitch=0.0, yaw=180.0, roll=0.0))
    t.set_actor_label('Label_%d' % d)
    t.set_folder_path('Range')
    comp = t.text_render
    comp.set_editor_property('text', '%d m' % d)
    comp.set_editor_property('world_size', 120.0)
    comp.set_editor_property('horizontal_alignment', unreal.HorizTextAligment.EHTA_CENTER)
    comp.set_editor_property('text_render_color', unreal.Color(r=255, g=140, b=40, a=255))

unreal.EditorLoadingAndSavingUtils.save_map(world, DST_MAP)
unreal.log('[range] 已另存 ' + DST_MAP)
