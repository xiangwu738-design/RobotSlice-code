"""游戏里（-game -MechBeauty）给 L_City 拍成图：暂停游戏，依次把视角切到几个机位，等画面稳定后 HighResShot。
不改 C++、不改地图：机位用地图里的 CameraActor（ShotCam）（运行时挪位置，不保存）。
UnrealEditor.exe RobotSlice.uproject /Game/Maps/L_City -game -MechBeauty -RenderOffscreen -unattended -nosound -ForceRes -ResX=1920 -ResY=1080 -windowed
  -ExecCmds="py D:/RobotSlice/tools/ue_city_shots.py" ；输出目录用环境变量 CITY_SHOTS_DIR（默认 Saved/Screenshots/City/beauty）
"""
import math
import os
import time

import unreal

OUT = os.environ.get('CITY_SHOTS_DIR', 'D:/RobotSlice/Saved/Screenshots/City/beauty').replace('\\', '/')
WAIT = float(os.environ.get('CITY_SHOTS_WAIT', '5.0'))
RES = os.environ.get('CITY_SHOTS_RES', '1920x1080')
ONLY = [s for s in os.environ.get('CITY_SHOTS_ONLY', '').split(',') if s]
os.makedirs(OUT, exist_ok=True)

# 城区图（plan 是 Blender 坐标、Y 向北；这里直接写 UE 坐标：Y 向南 = Blender 的 -Y）。高度都是离地面（射线打到的第一面）
SHOTS = [
    # 名字, 机位(米), 看向(米), FOV
    ('c0_top', (0, 0, 420.0), (0, 1.5, 0.0), 60),                    # 正上方俯视
    ('c1_spawn_blue', (-100, 0, 4.0), (0, 0, 4.0), 70),              # 蓝队出生点看大道
    ('c2_avenue_tower', (-30, 12, 5.0), (20, 0, 6.0), 70),           # 大道中段：天桥、转播塔赔率屏
    ('c3_site', (-28, -88, 6.0), (0, -45, 6.0), 70),                 # 工地 + 在建框架楼（北边，UE Y 为负）
    ('c4_plaza', (12, 75, 4.0), (-8, 25, 6.0), 70),                  # 南边广场看站亭和天桥
    ('c5_viaduct', (45, 85, 3.5), (45, -20, 12.0), 70),             # 高架桥面
    ('c6_rooftops', (-66, -30, 20.0), (0, 0, 6.0), 70),             # 西北联排屋顶看场地中心
    ('c7_overview', (-120, 120, 70.0), (0, 0, 0.0), 65),             # 西南高处总览
    ('c8_stW', (-45, 85, 4.0), (-45, -40, 6.0), 70),                 # 西街
    ('c9_hotel_roof', (95, -55, 2.5), (0, 0, 4.0), 70),             # 东北旅馆屋顶（最高点）
]
# 灰块修复对比（10-31）：西南停车场和大道上的盖布车（材质缺 Nanite 用法标记 → 游戏里是引擎灰格子）、东南 65 米写字楼（C02）、
# 北边旅馆（C01）。CITY_SHOTS_SET=gray 时只拍这几个 + c2 / c5 / c8
GRAY_SHOTS = [
    ('g1_parking_cars', (-47, 50, 5.0), (-88, 58, 0.5), 60),        # 西南停车场的盖布车（从西街看过去）
    ('g2_avenue_cars', (-45, 2, 2.5), (-20, 4, 1.0), 60),            # 大道上的盖布车
    ('g3_tower_c02', (40, 22, 6.0), (78, 47, 22.0), 70),            # 东南 65 米写字楼西面
    ('g4_hotel_c01', (-48, -52, 30.0), (-86, -95, 30.0), 65),       # 北边旅馆
]
if os.environ.get('CITY_SHOTS_SET') == 'gray':
    SHOTS = GRAY_SHOTS + [s for s in SHOTS if s[0] in ('c2_avenue_tower', 'c5_viaduct', 'c8_stW')]

world = None
state = {'t': 0.0, 'phase': 'wait', 'i': -1, 'shot_t': 0.0, 'cam': None, 'done': False, 'ground': {}}
log = lambda *a: unreal.log('[shots] ' + ' '.join(str(x) for x in a))


def find_world():
    w = unreal.find_object(None, '/Game/Maps/L_City.L_City')
    return w


def ground_z(x, y):
    hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, 200000), unreal.Vector(x, y, -200000),
                                                 unreal.TraceTypeQuery.ECC_VISIBILITY, False, [], unreal.DrawDebugTrace.NONE, True)
    if isinstance(hit, tuple):
        hit = hit[1] if hit[0] else None
    if hit is None:
        return 0.0
    t = hit.to_tuple()
    return t[4].z if t[0] else 0.0


def place(cam, shot):
    name, c, look, fov = shot
    cx, cy = c[0] * 100, c[1] * 100
    cz = (ground_z(cx, cy) if c[2] < 100 else 0.0) + c[2] * 100   # 机位高度 = 地面 + 偏移（很高的俯视机位按绝对高度）
    lx, ly = look[0] * 100, look[1] * 100
    lz = ground_z(lx, ly) + look[2] * 100 if look[2] is not None else 0.0
    loc = unreal.Vector(cx, cy, cz)
    d = unreal.Vector(lx - cx, ly - cy, lz - cz)
    yaw = math.degrees(math.atan2(d.y, d.x))
    pitch = math.degrees(math.atan2(d.z, math.hypot(d.x, d.y)))
    root = cam.get_editor_property('root_component')
    if root.get_editor_property('mobility') != unreal.ComponentMobility.MOVABLE:   # 示例图的机位是静态的，挪不动
        root.set_mobility(unreal.ComponentMobility.MOVABLE)
    cam.set_actor_location_and_rotation(loc, unreal.Rotator(roll=0, pitch=pitch, yaw=yaw), False, True)
    cc = cam.camera_component
    cc.set_field_of_view(fov)
    cc.set_editor_property('constrain_aspect_ratio', False)
    got = cam.get_actor_location()
    log(name, '机位', [round(v / 100, 1) for v in (cx, cy, cz)], '实际', [round(v / 100, 1) for v in (got.x, got.y, got.z)], 'yaw', round(yaw), 'pitch', round(pitch, 1))


def tick(dt):
    global world
    if state['done']:
        return
    state['t'] += dt
    if world is None:
        world = find_world()
        return
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    pawn = pc.get_controlled_pawn() if pc else None
    if state['phase'] == 'wait':
        if pawn and state['t'] > 3.0:   # 成图模式第一帧已经把 HUD 关了、机体摆好
            try:   # 暂停时相机默认不更新（画面停在暂停那一刻），让玩家控制器暂停时照常更新相机
                pc.set_editor_property('should_perform_full_tick_when_paused', True)
            except Exception as e:
                log('设不了暂停时更新相机', e)
            unreal.GameplayStatics.set_game_paused(world, True)
            cams = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CameraActor)
            log('地图里的 CameraActor', len(cams))
            state['cam'] = cams[0] if cams else None
            state['pawn'] = pawn
            state['phase'] = 'shoot'
        return
    if state['phase'] == 'shoot':
        cam = state['cam']
        if state['i'] >= 0 and state['t'] - state['shot_t'] > WAIT * 0.4:   # 机位切过去 40% 时间后开始数帧（避开加载）
            if state.get('real0') is None:
                state['real0'] = time.perf_counter()
                state['frames'] = 0
            state['frames'] += 1
        todo = [s for s in SHOTS if not ONLY or s[0] in ONLY]
        if state['i'] >= 0 and not state.get('taken') and state['t'] - state['shot_t'] > WAIT:
            # 先截图，等几帧（截图在下一帧渲染时才拍）再挪到下一个机位，不然拍到的是下一个机位
            name = todo[state['i']][0]
            fps = state['frames'] / max(1e-3, time.perf_counter() - state['real0'])
            unreal.SystemLibrary.execute_console_command(world, f'HighResShot {RES} filename={OUT}/{name}.png')
            log('截图', f'{OUT}/{name}.png', '这一机位后半段平均帧率', round(fps, 1))
            state['taken'] = True
            state['taken_t'] = state['t']
            return
        if state['i'] < 0 or (state.get('taken') and state['t'] - state['taken_t'] > 0.8):
            state['taken'] = False
            state['i'] += 1
            state['shot_t'] = state['t']
            state['frames'] = 0
            state['real0'] = None
            if state['i'] >= len(todo):
                state['phase'] = 'quit'
                return
            shot = todo[state['i']]
            state['pawn'].set_actor_hidden_in_game(shot[0] != 'c1_spawn_blue')
            place(cam, shot)
            pc.set_view_target_with_blend(cam, 0.0)
        return
    if state['phase'] == 'quit' and state['t'] - state['shot_t'] > 2.0:
        state['done'] = True
        log('完成，退出')
        unreal.SystemLibrary.execute_console_command(world, 'quit')


unreal.register_slate_post_tick_callback(tick)
log('已挂上逐帧回调，输出', OUT)
