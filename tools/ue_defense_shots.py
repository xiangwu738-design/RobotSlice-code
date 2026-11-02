"""保卫战地图 L_Defense 的氛围试验截图（10-31，照 ue_nature_shots.py 改的）。游戏里（-game -MechBeauty）：暂停游戏，依次把视角切到几个机位，等画面稳定后 HighResShot。
不改 C++、不改地图：机位用地图里已有的 CameraActor（运行时挪位置，不保存）。
UnrealEditor.exe RobotSlice.uproject /Game/Maps/L_Defense -game -MechBeauty -RenderOffscreen -unattended -nosound -ForceRes -ResX=1920 -ResY=1080 -windowed
  -ExecCmds="py D:/RobotSlice/tools/ue_defense_shots.py" ；输出目录用环境变量 NATURE_SHOTS_DIR（默认 Saved/Screenshots/Nature/now）
"""
import math
import os
import time

import unreal

OUT = os.environ.get('NATURE_SHOTS_DIR', 'D:/RobotSlice/Saved/Screenshots/Defense/now').replace('\\', '/')
WAIT = float(os.environ.get('NATURE_SHOTS_WAIT', '5.0'))
RES = os.environ.get('NATURE_SHOTS_RES', '1920x1080')
ONLY = [s for s in os.environ.get('NATURE_SHOTS_ONLY', '').split(',') if s]
os.makedirs(OUT, exist_ok=True)

# 出生点 (238,22) 朝向 140°；湖心 (45,20)，半径约 100 米；地形 X -252~504、Y -252~378
SHOTS = [
    # 名字, 机位(米), 看向(米), FOV；高度都是离地面。n* 和 ue_nature_shots.py 的同名机位一样（拿 Saved/Screenshots/Nature/after 对比改前）
    ('n1_spawn', (247, 13, 6.0), (169, 80, 2.0), 70),
    ('n2_ruins_west', (205, 45, 8.0), (-90, -20, 20.0), 45),
    ('n3_ruins_north', (200, 60, 8.0), (40, 265, 40.0), 45),
    ('n5_ground', (222, 28, 3.0), (185, 62, 0.0), 65),
    ('n8_overview', (330, -60, 45.0), (150, 60, 0.0), 65),
    ('w1_wreck', (212, -18, 4.0), (180, -47, 2.0), 65),          # 出生点西南 90 米那台机甲残骸
    ('w2_wreck_south', (268, -62, 5.0), (238, -98, 3.0), 65),    # 南边 120 米那台
]

world = None
state = {'t': 0.0, 'phase': 'wait', 'i': -1, 'shot_t': 0.0, 'cam': None, 'done': False, 'ground': {}}
log = lambda *a: unreal.log('[shots] ' + ' '.join(str(x) for x in a))


def find_world():
    w = unreal.find_object(None, '/Game/Maps/L_Defense.L_Defense')
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
    cz = ground_z(cx, cy) + c[2] * 100   # 机位高度 = 地面 + 偏移
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
        if pawn and state['t'] > 9.5:   # 等烟柱、火长起来再暂停（暂停后特效定格；成图模式第 13 秒左右自己拍完就退出，要在那之前停住）   # 成图模式第一帧已经把 HUD 关了、机体摆好
            try:   # 暂停时相机默认不更新（画面停在暂停那一刻），让玩家控制器暂停时照常更新相机
                pc.set_editor_property('should_perform_full_tick_when_paused', True)
            except Exception as e:
                log('设不了暂停时更新相机', e)
            unreal.SystemLibrary.execute_console_command(world, 'grass.DensityScale 0.45')
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
            state['pawn'].set_actor_hidden_in_game(True)
            place(cam, shot)
            pc.set_view_target_with_blend(cam, 0.0)
        return
    if state['phase'] == 'quit' and state['t'] - state['shot_t'] > 2.0:
        state['done'] = True
        log('完成，退出')
        unreal.SystemLibrary.execute_console_command(world, 'quit')


unreal.register_slate_post_tick_callback(tick)
log('已挂上逐帧回调，输出', OUT)
