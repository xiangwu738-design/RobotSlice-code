"""在 RobotSlice 工程里跑：自然图 /Game/Maps/L_Nature。
以自然环境包的示例图 APEP01-Map-1（草甸、枯桦树、湖、雪山，自带日光）为底，在出生点四周远处摆一圈废墟残楼（K035 素材），
楼脚贴着地形（向下打射线找地面），太高的山坡上不放。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="ue_build_nature.py" -AllowCommandletRendering -RenderOffscreen
"""
import math
import random

import unreal

SRC_MAP = '/Game/Environments/AndreyPestryakov/Environment_Pack_01/Maps/APEP01-Map-1'
DST_MAP = '/Game/Maps/L_Nature'
RUINS = '/Game/Env/Ruins/Meshes'
TAG = 'Nature'
COUNT = 40
DIST = (260.0, 700.0)    # 离出生点多远（米）
eas = unreal.EditorAssetLibrary
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
rng = random.Random(1999)

unreal.EditorLoadingAndSavingUtils.load_map(SRC_MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
world.get_world_settings().set_editor_property('default_game_mode', unreal.load_class(None, '/Script/RobotSlice.MechGameMode'))
for a in sub.get_all_level_actors():
    if TAG in [str(t) for t in a.tags]:
        sub.destroy_actor(a)

starts = [a for a in sub.get_all_level_actors() if isinstance(a, unreal.PlayerStart)]
spawn = starts[0].get_actor_location() if starts else unreal.Vector(23762, 2244, 300)
lands = [a for a in sub.get_all_level_actors() if isinstance(a, unreal.LandscapeProxy)]
lo = unreal.Vector(-1e9, -1e9, 0)
hi = unreal.Vector(1e9, 1e9, 0)
if lands:
    xs, ys = [], []
    for l in lands:
        o, e = l.get_actor_bounds(False)
        xs += [o.x - e.x, o.x + e.x]
        ys += [o.y - e.y, o.y + e.y]
    lo, hi = unreal.Vector(min(xs), min(ys), 0), unreal.Vector(max(xs), max(ys), 0)
unreal.log(f'[nature] 出生点 {spawn}，地形 {len(lands)} 块，范围 X {lo.x / 100:.0f}~{hi.x / 100:.0f} Y {lo.y / 100:.0f}~{hi.y / 100:.0f} 米')


def ground(x, y):
    r = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, spawn.z + 200000), unreal.Vector(x, y, spawn.z - 200000),
                                               unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [], unreal.DrawDebugTrace.NONE, True)
    if isinstance(r, tuple):
        r = r[1] if r[0] else None
    if r is None:
        return None
    t = r.to_tuple()
    return t[4].z if t[0] else None


big = [m for m in eas.list_assets(RUINS, recursive=False, include_folder=False) if '_LG_' in m]
mid = [m for m in eas.list_assets(RUINS, recursive=False, include_folder=False) if '_MD_' in m]
placed = skipped = 0
for i in range(COUNT * 3):
    if placed >= COUNT:
        break
    ang = rng.uniform(0, 2 * math.pi)
    d = rng.uniform(*DIST) * 100
    x, y = spawn.x + math.cos(ang) * d, spawn.y + math.sin(ang) * d
    if not (lo.x + 3000 < x < hi.x - 3000 and lo.y + 3000 < y < hi.y - 3000):
        skipped += 1
        continue
    z = ground(x, y)
    if z is None or z - spawn.z > 12000:   # 没打到地面或在高山上
        skipped += 1
        continue
    mesh = eas.load_asset(rng.choice(big if d > 45000 or rng.random() < 0.6 else mid))
    a = sub.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x, y, z - rng.uniform(200, 600)),
                                   unreal.Rotator(pitch=0, yaw=rng.uniform(0, 360), roll=rng.uniform(-3, 3)))
    a.tags = [TAG]
    a.set_folder_path('Nature/Ruins')
    c = a.static_mesh_component
    c.set_static_mesh(mesh)
    c.set_editor_property('mobility', unreal.ComponentMobility.STATIC)
    a.set_actor_scale3d(unreal.Vector(1, 1, 1) * rng.uniform(1.0, 1.7))
    placed += 1

unreal.EditorLoadingAndSavingUtils.save_map(world, DST_MAP)
unreal.log(f'[nature] 已另存 {DST_MAP}：废墟 {placed} 栋（跳过 {skipped} 个位置）')
