"""游戏里（-game）给苏联板楼图 L_Soviet 拍环境截图：暂停游戏，把地图里的 ShotCam 挪到各机位，等画面稳定后 HighResShot，顺带记每个机位帧率。
机位用 Blender 坐标（米，y 朝北，绝对高度）。
UnrealEditor.exe RobotSlice.uproject "/Game/Maps/L_Soviet?Range" -game -MechInfiniteEnergy -RenderOffscreen -unattended -nosound -nosplash -ForceRes -ResX=1920 -ResY=1080 -windowed "-ExecCmds=py D:/RobotSlice/tools/ue_soviet_shots.py"
环境变量 SV_SHOTS_DIR（默认 Saved/Screenshots/Soviet/now）、SV_SHOTS_ONLY（只拍某几个）、SV_SHOTS_WAIT
"""
import math
import os
import time

import unreal

OUT = os.environ.get('SV_SHOTS_DIR', 'D:/RobotSlice/Saved/Screenshots/Soviet/now').replace(chr(92), '/')
WAIT = float(os.environ.get('SV_SHOTS_WAIT', '5.0'))
RES = os.environ.get('SV_SHOTS_RES', '1920x1080')
ONLY = [s for s in os.environ.get('SV_SHOTS_ONLY', '').split(',') if s]
os.makedirs(OUT, exist_ok=True)

GALLERY = [
    ('g0_trees', (0, 105, 18.0), (0, 40, 3.0), 75),
    ('g1_trees_low', (-55, 18, 3.0), (10, 70, 12.0), 70),
    ('g2_bush_rocks', (0, -1, 9.0), (0, 18, 1.5), 75),
    ('g3_props', (0, -14, 7.0), (0, -3, 0.5), 85),
    ('g3b_props_l', (-28, -9, 4.0), (-24, 2, 0.5), 60),
    ('g3c_props_r', (28, -9, 4.0), (24, 2, 0.5), 60),
    ('g4_grass', (0, -22, 3.5), (0, -13, 0.0), 70),
    ('g5_snow', (-32, -5, 25.0), (-32, -50, 0.0), 80),
    ('g6_snow_low', (-78, -25, 2.5), (-78, -50, 0.0), 75),
    ('g7_snow_low', (-33, -25, 2.5), (-33, -50, 0.0), 75),
    ('g8_snow_low', (12, -25, 2.5), (12, -50, 0.0), 75),
    ('g9_snow_low', (57, -25, 2.5), (57, -50, 0.0), 75),
]
SHOTS = GALLERY if os.environ.get('SV_SHOTS_MAP') else [
    # 名字, 机位(米), 看向(米), FOV
    ('a0_aerial_sw', (-146, -106, 58.0), (20, 20, 0.0), 75),         # 西南高空
    ('a1_top', (0, 0, 290.0), (0, 0.5, 0.0), 75),                    # 正上方（看不全边缘）
    ('s0_street_n_w', (-140, 50, 2.4), (140, 50, 4.0), 70),          # 北长街西头贴地往东：300 米视线
    ('s1_street_n_mid', (-20, 46, 2.4), (60, 58, 6.0), 70),          # 北长街中段
    ('s2_cross', (50, -100, 2.4), (50, 100, 6.0), 70),               # 东横街南头往北
    ('y0_yard_center', (-36, -14, 2.4), (20, 12, 5.0), 75),          # 中院
    ('y1_spawn_w', (-146, -3, 3.0), (-60, 3, 8.0), 75),              # 蓝队出生广场往东：MD_F 门洞
    ('y2_backlane', (-146, 103, 2.4), (-20, 103, 8.0), 70),          # 北后巷
    ('r0_roof_md_b', (-20, 90, 49.0), (40, -40, 20.0), 70),          # MD_B 屋顶（47 米）往东南
    ('r1_roof_sm', (-112, 25, 26.0), (0, 50, 10.0), 70),             # 西大院北楼屋顶
    ('f0_fly_low', (-120, 50, 16.0), (40, 48, 8.0), 75),             # 低空沿长街飞
    ('f1_fly_mid', (-30, 110, 35.0), (20, -20, 10.0), 75),           # 楼顶高度俯看
    ('m0_mech', (-130, 8, 4.0), (-135, 0, 2.0), 55),                 # 出生点的机体（比例）
    # 常用视角：第三人称（机体身后 4~5 米高）
    ('c0_third_street', (-104, 46, 4.5), (-60, 50, 3.0), 80),        # 北长街
    ('c1_third_yard', (-44, 12, 4.5), (0, -6, 3.0), 80),             # 中院入口
    ('c2_third_cross', (44, -30, 5.0), (52, 40, 5.0), 80),           # 东横街
    ('c3_high_e', (100, 0, 40.0), (0, 0, 5.0), 80),                  # 高处俯看中院
    ('c4_ceiling', (0, -70, 60.0), (0, 60, 15.0), 80),               # 限高处
    ('c5_roof_e', (127, 80, 53.0), (0, 50, 8.0), 75),                # 最高的楼顶往西
    # 小品特写
    ('p0_garages', (-48, 3, 3.0), (-22, 9, 1.5), 60),                # 中院车库 + 火箭滑梯
    ('p1_cars', (-136, 48, 2.5), (-124, 57, 1.0), 60),               # 路边的车
    ('p2_busstop', (-48, 54, 2.5), (-40, 62, 1.5), 60),              # 公交站
    ('p3_playground', (10, 4, 3.0), (28, 8, 1.0), 60),               # 秋千滑梯
    ('p4_lamps', (-30, 49, 2.0), (30, 52, 8.0), 70),                 # 路灯
]

world = None
state = {'t': 0.0, 'phase': 'wait', 'i': -1, 'shot_t': 0.0, 'cam': None, 'done': False}
log = lambda *a: unreal.log('[shots] ' + ' '.join(str(x) for x in a))


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
        mp = os.environ.get('SV_SHOTS_MAP', '/Game/Maps/L_Soviet')
        world = unreal.find_object(None, mp + '.' + mp.rsplit('/', 1)[-1])
        return
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    pawn = pc.get_controlled_pawn() if pc else None
    if state['phase'] == 'wait':
        if pawn and state['t'] > float(os.environ.get('SV_SHOTS_DELAY', '12')):   # 等贴图流送、粒子铺开
            try:
                pc.set_editor_property('should_perform_full_tick_when_paused', True)
            except Exception as e:
                log('设不了暂停时更新相机', e)
            # 不暂停：要看到下雪；相机单独放
            cams = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CameraActor)
            log('地图里的 CameraActor', len(cams), '机体位置', pawn.get_actor_location())
            state['cam'] = cams[0] if cams else None
            state['pawn'] = pawn
            state['phase'] = 'shoot'
        return
    if state['phase'] == 'shoot':
        cam = state['cam']
        if state['i'] >= 0 and state['t'] - state['shot_t'] > WAIT * 0.4:
            if state.get('real0') is None:
                state['real0'] = time.perf_counter()
                state['frames'] = 0
            state['frames'] += 1
        todo = [s for s in SHOTS if not ONLY or s[0] in ONLY]
        if state['i'] >= 0 and not state.get('taken') and state['t'] - state['shot_t'] > WAIT:
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
            place(cam, shot)
            pc.set_view_target_with_blend(cam, 0.0)
        return
    if state['phase'] == 'quit' and state['t'] - state['shot_t'] > 2.0:
        state['done'] = True
        log('完成，退出')
        unreal.SystemLibrary.execute_console_command(world, 'quit')


unreal.register_slate_post_tick_callback(tick)
log('已挂上逐帧回调，输出', OUT)
