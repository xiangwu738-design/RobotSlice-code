"""游戏里（-game -MechBeauty）给相位电子园区 L_TechPark 拍环境截图：暂停游戏，把地图里的 ShotCam 依次挪到各机位，等画面稳定后 HighResShot，顺带记每个机位的帧率。
机位用 Blender 坐标（米，y 朝北，高度是绝对高度），脚本里换成 UE 坐标。
UnrealEditor.exe RobotSlice.uproject /Game/Maps/L_TechPark -game -MechBeauty -RenderOffscreen -unattended -nosound -ForceRes -ResX=1920 -ResY=1080 -windowed
  "-ExecCmds=py D:/RobotSlice/tools/ue_techpark_shots.py" ；输出目录用环境变量 TP_SHOTS_DIR（默认 Saved/Screenshots/TechPark/now），TP_SHOTS_ONLY 只拍某几个
"""
import math
import os
import time

import unreal

OUT = os.environ.get('TP_SHOTS_DIR', 'D:/RobotSlice/Saved/Screenshots/TechPark/now').replace(chr(92), '/')
WAIT = float(os.environ.get('TP_SHOTS_WAIT', '5.0'))
RES = os.environ.get('TP_SHOTS_RES', '1920x1080')
ONLY = [s for s in os.environ.get('TP_SHOTS_ONLY', '').split(',') if s]
os.makedirs(OUT, exist_ok=True)

SHOTS = [
    # 名字, 机位(米, Blender 坐标, 绝对高度), 看向(米), FOV
    ('t0_overview', (150, -125, 75.0), (0, 0, 0.0), 60),
    ('t1_mech', (66, 24, 12.0), (77, 12, 9.0), 55),               # 东基地台上的机体（成图模式机体站在第一个死斗出生点）
    ('t2_center', (0, -12, 3.2), (0, 60, 12.0), 70),             # 中央交汇口往北：坡道、大门、远处 PEC 总部
    ('t3_deck', (-38, 49, 10.6), (40, 49, 9.0), 70),             # 北边高架环路上
    ('t4_under', (-30, 49, 3.0), (30, 49, 3.0), 70),              # 环路桥下
    ('t5_ne_corner', (102, 38, 4.0), (60, 70, 14.0), 70),          # 东北角：大堂楼、20 号塔、单轨
    ('t6_south', (60, -48, 4.0), (0, -64, 16.0), 70),             # 南边带：车站和停着的列车
    ('t7_plaza', (36, 22, 3.2), (21, 0, 12.0), 70),               # 广场里的观景塔
    ('t8_hq', (0, 40, 14.0), (-20, 215, 85.0), 50),               # 从北环路看PEC 总部
    ('t9_base_w', (-100, 0, 11.0), (20, 0, 6.0), 70),             # 西基地台往东看
    # 10-31 外观整修加的对比机位（全景 / 街面 / 高处）
    ('c0_south_aerial', (0, -150, 55.0), (0, 60, 15.0), 60),      # 南边高空往北：整个园区 + 大门 + PEC 总部塔
    ('c1_street', (96, -56, 3.0), (40, -20, 6.0), 70),            # 东南角地面（死斗出生点）往西北看广场
    ('c2_high', (64, 60, 30.0), (-30, -20, 0.0), 65),             # 20 号塔顶往西南俯看广场、环路和西基地
]

world = None
state = {'t': 0.0, 'phase': 'wait', 'i': -1, 'shot_t': 0.0, 'cam': None, 'done': False, 'ground': {}}
log = lambda *a: unreal.log('[shots] ' + ' '.join(str(x) for x in a))


def find_world():
    w = unreal.find_object(None, '/Game/Maps/L_TechPark.L_TechPark')
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
    loc = unreal.Vector(c[0] * 100, -c[1] * 100, c[2] * 100)
    d = unreal.Vector(look[0] * 100 - loc.x, -look[1] * 100 - loc.y, look[2] * 100 - loc.z)
    yaw = math.degrees(math.atan2(d.y, d.x))
    pitch = math.degrees(math.atan2(d.z, math.hypot(d.x, d.y)))
    root = cam.get_editor_property('root_component')
    if root.get_editor_property('mobility') != unreal.ComponentMobility.MOVABLE:
        root.set_mobility(unreal.ComponentMobility.MOVABLE)
    cam.set_actor_location_and_rotation(loc, unreal.Rotator(roll=0, pitch=pitch, yaw=yaw), False, True)
    cc = cam.camera_component
    cc.set_field_of_view(fov)
    cc.set_editor_property('constrain_aspect_ratio', False)
    log(name, '机位', c, 'yaw', round(yaw), 'pitch', round(pitch, 1))


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
            try:   # 10-31：顺带记太阳方位 / 高度 / 照度（地图里 LightingDirector 的 TuningSettings 改了北向偏角）
                for d in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.LightingDirector):
                    s = d.get_lighting_state()
                    log('光照', '太阳高度', round(s.sun_elevation, 1), '方位', round(s.sun_azimuth, 1), '照度', round(s.sun_intensity),
                        '天光', round(s.sky_light_intensity, 2), '雾', round(s.fog_density, 4), '云', round(s.cloud_coverage, 2))
            except Exception as e:
                log('读不到光照状态', e)
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
            state['pawn'].set_actor_hidden_in_game(shot[0] != 't1_mech')
            place(cam, shot)
            pc.set_view_target_with_blend(cam, 0.0)
        return
    if state['phase'] == 'quit' and state['t'] - state['shot_t'] > 2.0:
        state['done'] = True
        log('完成，退出')
        unreal.SystemLibrary.execute_console_command(world, 'quit')


unreal.register_slate_post_tick_callback(tick)
log('已挂上逐帧回调，输出', OUT)
