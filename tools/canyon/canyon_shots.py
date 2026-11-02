"""峡谷截图（游戏里跑，-game）：暂停后把视角依次切到几个机位，HighResShot。美术图和灰盒用同一组机位，方便对照。
美术图：UnrealEditor.exe RobotSlice.uproject /Game/Maps/L_Canyon -game -MechBeauty -MechHour=13 -MechWeather=Clear -RenderOffscreen -unattended -nosound
         -ForceRes -ResX=1920 -ResY=1080 -windowed -ExecCmds="py D:/RobotSlice/tools/canyon/canyon_shots.py"
灰盒：   ... "/Game/Maps/L_Range?Core?Team=3" -MechAutoBattle -MechCoreTime=60 ...，环境变量 CANYON_SHOTS_OFFSET_Z=30000（灰盒搭在测试场上空 300 米）
环境变量：CANYON_SHOTS_DIR（输出目录）、CANYON_SHOTS_WAIT（每个机位等几秒）、CANYON_SHOTS_START（开拍前等几秒）、CANYON_SHOTS_ONLY（逗号分隔）"""
import math
import os
import time

import unreal

OUT = os.environ.get('CANYON_SHOTS_DIR', 'D:/RobotSlice/Saved/Screenshots/Canyon').replace('\\', '/')
WAIT = float(os.environ.get('CANYON_SHOTS_WAIT', '4.0'))
START = float(os.environ.get('CANYON_SHOTS_START', '6.0'))
OZ = float(os.environ.get('CANYON_SHOTS_OFFSET_Z', '0'))
ONLY = [s for s in os.environ.get('CANYON_SHOTS_ONLY', '').split(',') if s]
RES = os.environ.get('CANYON_SHOTS_RES', '1920x1080')
os.makedirs(OUT, exist_ok=True)

# 名字, 机位（厘米，灰盒坐标）, 看向, 视野
SHOTS = [
    ('c00_top', (0, 0, 32000), (0, 1, 0), 52),
    ('c01_aerial_w', (-17500, -9000, 7500), (-1000, 0, 300), 60),
    ('c02_aerial_e', (16500, 8500, 6500), (1000, 0, 300), 60),
    ('c03_floor_w', (-10600, -600, 330), (0, 0, 450), 78),
    ('c04_center', (-3600, -1100, 300), (0, 250, 650), 78),
    ('c05_center_rev', (3200, 900, 320), (-200, -300, 600), 78),
    ('c06_ledge_s', (-6200, -2150, 1180), (3000, -2100, 1050), 78),
    ('c07_ledge_n', (5600, 2150, 1180), (-4000, 2150, 1050), 78),
    ('c08_cave', (-1300, 3200, 300), (-6000, 3200, 320), 82),
    ('c09_cave_gate', (-6500, 3200, 300), (-3000, 3200, 300), 82),
    ('c10_stationA_w', (-6900, -900, 520), (-8800, 1000, 100), 75),
    ('c11_stationA_e', (6900, -900, 520), (8800, 1000, 100), 75),
    ('c12_stationB_w', (-6600, -1100, 1500), (-8200, -2150, 950), 75),
    ('c13_spawn_w', (-10000, 900, 420), (-12600, -200, 300), 78),
    ('c14_ramp', (900, 700, 320), (3800, 1550, 900), 75),
    ('c15_plateau_n', (2500, 3700, 2500), (-3500, -2500, 1300), 70),
]

world = None
state = {'t': 0.0, 'phase': 'wait', 'i': -1, 'shot_t': 0.0, 'cam': None, 'done': False}
log = lambda *a: unreal.log('[canyonshots] ' + ' '.join(str(x) for x in a))


def find_world():
    for p in ('/Game/Maps/L_Canyon.L_Canyon', '/Game/Maps/L_Range.L_Range'):
        w = unreal.find_object(None, p)
        if w:
            return w
    return None


def place(cam, shot):
    name, c, look, fov = shot
    loc = unreal.Vector(c[0], c[1], c[2] + OZ)
    d = unreal.Vector(look[0] - c[0], look[1] - c[1], look[2] - c[2])
    yaw = math.degrees(math.atan2(d.y, d.x))
    pitch = math.degrees(math.atan2(d.z, math.hypot(d.x, d.y)))
    root = cam.get_editor_property('root_component')
    if root and root.get_editor_property('mobility') != unreal.ComponentMobility.MOVABLE:
        root.set_mobility(unreal.ComponentMobility.MOVABLE)
    cam.set_actor_location_and_rotation(loc, unreal.Rotator(roll=0, pitch=pitch, yaw=yaw), False, True)
    if isinstance(cam, unreal.CameraActor):
        cc = cam.camera_component
        cc.set_field_of_view(fov)
        cc.set_editor_property('constrain_aspect_ratio', False)
    unreal.SystemLibrary.execute_console_command(world, f'fov {fov}')
    log(name, '机位', [round(v) for v in (loc.x, loc.y, loc.z)], 'yaw', round(yaw), 'pitch', round(pitch, 1))


def tick(dt):
    global world
    if state['done']:
        return
    state['t'] += dt
    if world is None:
        world = find_world()
        return
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    if state['phase'] == 'wait':
        if pc and state['t'] > START:
            try:
                pc.set_editor_property('should_perform_full_tick_when_paused', True)
            except Exception as e:
                log('设不了暂停时更新相机', e)
            unreal.GameplayStatics.set_game_paused(world, True)
            cams = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CameraActor)
            cam = cams[0] if cams else None
            if cam is None:   # 灰盒：拿夺芯导演（没有模型）当视角
                for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
                    if 'MechCoreDirector' in a.get_class().get_name():
                        cam = a
                        break
            state['cam'] = cam
            pawn = pc.get_controlled_pawn()
            for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Pawn):
                p.set_actor_hidden_in_game(True)
            unreal.SystemLibrary.execute_console_command(world, 'showhud 0') if False else None
            log('开拍，视角用', cam.get_name() if cam else None)
            state['phase'] = 'shoot'
        return
    if state['phase'] == 'shoot':
        cam = state['cam']
        todo = [s for s in SHOTS if not ONLY or s[0] in ONLY]
        if state['i'] >= 0 and not state.get('taken') and state['t'] - state['shot_t'] > WAIT:
            name = todo[state['i']][0]
            unreal.SystemLibrary.execute_console_command(world, f'HighResShot {RES} filename={OUT}/{name}.png')
            log('截图', f'{OUT}/{name}.png')
            state['taken'] = True
            state['taken_t'] = state['t']
            return
        if state['i'] < 0 or (state.get('taken') and state['t'] - state['taken_t'] > 0.8):
            state['taken'] = False
            state['i'] += 1
            state['shot_t'] = state['t']
            if state['i'] >= len(todo):
                state['phase'] = 'quit'
                return
            place(cam, todo[state['i']])
            pc.set_view_target_with_blend(cam, 0.0)
        return
    if state['phase'] == 'quit' and state['t'] - state['shot_t'] > 2.0:
        state['done'] = True
        log('完成，退出')
        unreal.SystemLibrary.execute_console_command(world, 'quit')


unreal.register_slate_post_tick_callback(tick)
log('已挂上逐帧回调，输出', OUT, '高度偏移', OZ)
