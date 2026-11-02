"""荒野与废墟（L_Nature）落水损毁（11-02，用户确认「L_Nature 的水也会损毁」）：查湖的水面高度和岸边深度，摆 WaterKill 标记（规则见 Source/RobotSlice/MechMapRules.h）。
不带参数只查、不改图；参数 apply 才摆标记并保存（带 WaterKillMark 标签的旧标记先删）。
查的内容：每个 WaterBody 的水面高度、范围；沿湖岸样条往外 / 往里每隔 2 米的地面高度（定 KillDepth：走在岸边浅水不能误杀）；范围外地面低于水面的格子（WaterKill 的矩形不能框住它们）。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_nature_waterkill.py [apply] [depth=厘米]" -AllowCommandletRendering -RenderOffscreen -unattended -nosplash
"""
import math
import sys

import unreal

MAP = '/Game/Maps/L_Nature'
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
log = lambda *a: unreal.log('[natwater] ' + ' '.join(str(x) for x in a))
APPLY = 'apply' in sys.argv[1:]
DEPTH = next((float(a[6:]) for a in sys.argv[1:] if a.startswith('depth=')), 120.0)
unreal.EditorLoadingAndSavingUtils.load_map(MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
actors = sub.get_all_level_actors()
waters = [a for a in actors if 'WaterBody' in a.get_class().get_name()]
log('水体', len(waters), [a.get_class().get_name() for a in waters])
zones = [a for a in actors if 'WaterZone' in a.get_class().get_name()]
log('水区', len(zones))


def ground(x, y, top=60000.0, bottom=-60000.0, ignore=()):
    hit = unreal.SystemLibrary.line_trace_single_for_objects(world, unreal.Vector(x, y, top), unreal.Vector(x, y, bottom), [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1], True, list(ignore),
                                                             unreal.DrawDebugTrace.NONE, True)
    if isinstance(hit, tuple):
        hit = hit[1] if hit[0] else None
    tt = hit.to_tuple() if hit else None
    if not tt or not tt[0]:
        return None
    hv = next((v for v in tt if isinstance(v, unreal.Vector)), None)
    return hv.z if hv else None


info = []
for w in waters:
    o, e = w.get_actor_bounds(False)
    loc = w.get_actor_location()
    spl = w.get_component_by_class(unreal.SplineComponent)
    length = spl.get_spline_length() if spl else 0
    n = spl.get_number_of_spline_points() if spl else 0
    log('水体', w.get_actor_label(), '位置', loc, '范围中心', o, '半宽', e, '样条点', n, '周长（米）', round(length / 100))
    comps = w.get_components_by_class(unreal.PrimitiveComponent)
    for c in comps[:6]:
        log('  组件', c.get_class().get_name(), c.get_collision_profile_name(), c.get_collision_enabled())
    info.append((w, o, e, loc, spl, length))

if info:
    w, o, e, loc, spl, length = info[0]
    wz = loc.z
    log('水面高度（厘米）', wz, '场地中水体范围 x', o.x - e.x, o.x + e.x, 'y', o.y - e.y, o.y + e.y)
    # 沿岸样条取 24 个点，从岸往湖心 / 往外走，看地面离水面多少
    samples = 24
    for i in range(samples):
        d = length * i / samples
        p = spl.get_location_at_distance_along_spline(d, unreal.SplineCoordinateSpace.WORLD)
        to_c = unreal.Vector(o.x - p.x, o.y - p.y, 0)
        lc = math.hypot(to_c.x, to_c.y) or 1.0
        row = []
        for off in (-1000, -600, -300, 0, 200, 400, 600, 800, 1000, 1500, 2000):
            gz = ground(p.x + to_c.x / lc * off, p.y + to_c.y / lc * off, ignore=[w])
            row.append(f'{off / 100:+.0f}m:{(gz - wz) / 100:+.1f}' if gz is not None else f'{off / 100:+.0f}m:--')
        log('岸', i, ' '.join(row))
    # 全图：地面低于水面的格子分布（10 米一格）
    px0, px1 = o.x - e.x - 20000, o.x + e.x + 20000
    py0, py1 = o.y - e.y - 20000, o.y + e.y + 20000
    low_out = []
    low_in = 0
    x = px0
    while x <= px1:
        y = py0
        while y <= py1:
            gz = ground(x, y, ignore=[w])
            if gz is not None and gz < wz:
                inside = abs(x - o.x) <= e.x and abs(y - o.y) <= e.y
                if inside:
                    low_in += 1
                else:
                    low_out.append((round(x / 100), round(y / 100), round((gz - wz) / 100, 1)))
            y += 1000
        x += 1000
    log('地面低于水面：水体范围内', low_in, '格，范围外', len(low_out), '格，例', low_out[:20])
    if APPLY:
        for a in actors:
            if 'WaterKillMark' in [str(t) for t in a.tags]:
                sub.destroy_actor(a)
        m = sub.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(o.x, o.y, wz))
        m.set_actor_label('WaterKill')
        m.set_folder_path('NatureDetail/Rules')
        # 只在湖的外框里算：框外有比水面低的坑也不算落水
        m.tags = ['WaterKill', 'WaterKillMark', f'KillDepth={DEPTH:.0f}', f'HalfX={e.x + 200:.0f}', f'HalfY={e.y + 200:.0f}']
        unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)
        log('已摆 WaterKill 并保存，KillDepth', DEPTH)
