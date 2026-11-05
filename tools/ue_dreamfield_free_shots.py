"""梦核旷野图 L_Dreamfield_FreeStudy 的成图截图（照 ue_defense_shots.py 改的）。游戏里（-game -MechBeauty）：第 3.5 秒暂停（成图模式第 13 秒左右自己退出，
要在那之前停住），把地图里的 ShotCam 挪到各个机位拍 HighResShot，记每个机位的帧率。不改 C++、不改地图（机位运行时挪，不保存）。
UnrealEditor.exe RobotSlice.uproject /Game/Maps/L_Dreamfield_FreeStudy -game -MechBeauty -RenderOffscreen -unattended -nosound -ForceRes -ResX=1920 -ResY=1080 -windowed
  "-ExecCmds=py D:/RobotSlice/tools/ue_dreamfield_shots.py"
环境变量：DF_SHOTS_DIR 输出目录（默认 Saved/Screenshots/Dreamfield/now），DF_SHOTS_ONLY 只拍某几个机位（逗号分隔），DF_SHOTS_WAIT 每个机位等几秒
"""
import math
import os
import sys
import time

import unreal

sys.path.append('D:/RobotSlice/tools')
import dreamfield_terrain as terrain  # noqa: E402

OUT = os.environ.get('DF_SHOTS_DIR', 'D:/RobotSlice/Saved/Screenshots/Dreamfield/now').replace('\\', '/')
WAIT = float(os.environ.get('DF_SHOTS_WAIT', '5.0'))
RES = os.environ.get('DF_SHOTS_RES', '1920x1080')
ONLY = [s for s in os.environ.get('DF_SHOTS_ONLY', '').split(',') if s]
GUS_INI = 'D:/RobotSlice/Saved/Config/WindowsEditor/GameUserSettings.ini'


def ini_res():
    """用户自己设的窗口分辨率（编辑器 -game 的 GameUserSettings.ini 里的 ResolutionSizeX/Y）。截图命令带 -ForceRes 1920×1080，
    引擎退出时会把 1920×1080 写回这个文件、盖掉用户的设置（11-01 发现）→ 退出前把分辨率设回原值，引擎退出时写回去的就是原值"""
    try:
        kv = dict(l.strip().split('=', 1) for l in open(GUS_INI, encoding='utf-8-sig') if l.startswith('ResolutionSize'))
        return int(kv['ResolutionSizeX']), int(kv['ResolutionSizeY'])
    except Exception:
        return None


ORIG_RES = ini_res()
os.makedirs(OUT, exist_ok=True)
WALL_X = 300.0


def height(x, y):
    """地面高度（米），和 Blender 生成地面用的是同一个函数"""
    return terrain.height(x, y)


# 平台尺寸：ue_build_dreamfield.py 搭图时写到 Inbox/_dreamwall/dreamfield_layout.json（第二版 11-01 加大加高了平台）；没有就按第一版
try:
    import json
    LAYOUT = json.load(open('D:/RobotSlice/Inbox/_dreamwall/dreamfield_layout.json', encoding='utf-8'))
except Exception:
    LAYOUT = {'plat': {'x0': WALL_X - 70.0, 'x1': WALL_X + 6.0, 'y0': -150.0, 'y1': 150.0, 'top': 12.0}}
PL = LAYOUT['plat']
PT = PL['top']

# 名字, 机位 (x, y, 高, 基准), 看向 (x, y, 绝对高), FOV, 机体 (x, y, 高, 朝向, 基准) 或 None（藏起来）。米。
# 基准：'g' = 离地面高（默认），'p' = 离平台顶面高，'a' = 绝对高度
SHOTS = [
    ('1_far_rise', (-1100, -420, 2.5, 'g'), (300, 120, 150), 66, None),                                   # 远看：墙从草地里拔地而起
    ('2_platform_out', (PL['x1'] - 28, 14, 3.2, 'p'), (-700, -120, 4), 72, (PL['x1'] - 40, 4, 0.0, 200, 'p')),   # 站在平台上往旷野看
    ('3_ground_up', (150, -235, 1.8, 'g'), (300, -120, 165), 82, (176, -214, 0.0, 40, 'g')),               # 草地上仰望墙
    ('4_platform_along', (PL['x0'] + 8, PL['y0'] + 22, 5, 'p'), (WALL_X - 8, PL['y1'] - 10, PT + 18), 74, None),   # 平台上顺着墙看
    ('5_overview', (-420, -760, 170, 'g'), (160, 30, 15), 62, None),                                      # 高处斜俯视全图
    ('6_field_sky', (-120, 60, 1.6, 'g'), (-462, -880, 45), 78, None),                                     # 背对墙看空旷的草地和地平线
    ('7_grass_mech', (40, -330, 2.4, 'g'), (300, -150, 70), 70, (70, -312, 0.0, 35, 'g')),                  # 草地上的机体（看草丛、白花的尺度）
    ('8_hills', (60, 40, 14, 'g'), (-520, 230, -2), 74, None),                                             # 离地 14 米往旷野看：两翼和远处的矮丘
    # 和峡谷包原渲染图（Renders/1–3.png）相近的机位：墙面前 9.8 米、甲板顶上 0.5 米，顺着墙朝 +Y 看
    ('9_canyon_ref', (WALL_X - 9.8, -600, 200.5, 'a'), (WALL_X - 9.8, 400, 15.0), 67.4, None),
    ('10_wall_mid', (150, -80, 30, 'g'), (300, 40, 90), 60, None),                                         # 旷野上 150 米外、30 米高看墙面中段的细节
    ('11_platform_front', (20, -250, 6, 'g'), (PL['x0'], 0, PT * 0.8), 66, None),                          # 旷野上看平台正面（电梯、大门）
    ('12_gate', (PL['x0'] + 12, -46, 3, 'p'), (WALL_X - 10, 0, PT + 22), 76, (PL['x0'] + 30, -14, 0.0, 20, 'p')),   # 平台上看封锁的大门
    ('13_elevator', (PL['x0'] - 70, -150, 3, 'g'), (PL['x0'] - 8, -PL['y1'] / 3, PT * 0.6), 66, None),       # 草地上看电梯
    ('14_field_near', (-60, 200, 1.3, 'g'), (-200, 262, 0.0), 70, None),                                   # 旷野近景：地面和草花
]

QUAL = ['sg.ResolutionQuality', 'sg.ViewDistanceQuality', 'sg.AntiAliasingQuality', 'sg.ShadowQuality', 'sg.GlobalIlluminationQuality',
        'sg.ReflectionQuality', 'sg.PostProcessQuality', 'sg.TextureQuality', 'sg.EffectsQuality', 'sg.FoliageQuality', 'sg.ShadingQuality']
world = None
state = {'t': 0.0, 'phase': 'wait', 'i': -1, 'shot_t': 0.0, 'cam': None, 'done': False}
fps_log = []
log = lambda *a: unreal.log('[dfshots] ' + ' '.join(str(x) for x in a))


def zref(x, y, h, ref):
    return h if ref == 'a' else (PT + h if ref == 'p' else height(x, y) + h)


def place(cam, pawn, shot):
    name, c, look, fov, mech = shot
    cx, cy = c[0], c[1]
    cz = zref(cx, cy, c[2], c[3] if len(c) > 3 else 'g')
    loc = unreal.Vector(cx * 100, cy * 100, cz * 100)
    d = unreal.Vector((look[0] - cx) * 100, (look[1] - cy) * 100, (look[2] - cz) * 100)
    yaw = math.degrees(math.atan2(d.y, d.x))
    pitch = math.degrees(math.atan2(d.z, math.hypot(d.x, d.y)))
    cam.set_actor_location_and_rotation(loc, unreal.Rotator(roll=0, pitch=pitch, yaw=yaw), False, True)
    cc = cam.camera_component
    cc.set_field_of_view(fov)
    cc.set_editor_property('constrain_aspect_ratio', False)
    if pawn:
        if mech:
            mx, my, mz, myaw = mech[:4]
            gz = zref(mx, my, mz, mech[4] if len(mech) > 4 else 'g')
            pawn.set_actor_hidden_in_game(False)
            try:
                hh = pawn.capsule_component.get_scaled_capsule_half_height()
            except Exception:
                hh = 130.0
            pawn.set_actor_location_and_rotation(unreal.Vector(mx * 100, my * 100, gz * 100 + hh + 2), unreal.Rotator(yaw=myaw), False, True)
        else:
            pawn.set_actor_hidden_in_game(True)
    log(name, '机位', [round(cx), round(cy), round(cz, 1)], 'yaw', round(yaw), 'pitch', round(pitch, 1), 'fov', fov)


def tick(dt):
    global world
    if state['done']:
        return
    state['t'] += dt
    if world is None:
        world = unreal.find_object(None, '/Game/Maps/L_Dreamfield_FreeStudy.L_Dreamfield_FreeStudy')
        return
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    pawn = pc.get_controlled_pawn() if pc else None
    if state['phase'] == 'wait':
        if pawn and state['t'] > 3.5:
            try:
                pc.set_editor_property('should_perform_full_tick_when_paused', True)
            except Exception as e:
                log('设不了暂停时更新相机', e)
            unreal.GameplayStatics.set_game_paused(world, True)
            # 画质：编辑器 -game 用 Saved/Config/WindowsEditor/GameUserSettings.ini 的档位（用户自己调到了最低档）。
            # 评估效果要看正常画质：拍照时临时调到「史诗」，退出前改回原值（引擎退出时会把当前档位写回那个文件）
            state['orig_q'] = {}
            if os.environ.get('DF_SHOTS_QUALITY', 'epic') == 'epic':
                for k in QUAL:
                    state['orig_q'][k] = unreal.SystemLibrary.get_console_variable_float_value(k)
                    unreal.SystemLibrary.execute_console_command(world, f'{k} {100 if k == "sg.ResolutionQuality" else 3}')
                log('画质临时调到史诗，原值', state['orig_q'])
            cams = [c for c in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CameraActor) if 'ShotCam' in [str(t) for t in c.tags]]
            state['cam'] = cams[0] if cams else None
            state['pawn'] = pawn
            state['phase'] = 'shoot'
            log('暂停，开始拍；ShotCam', bool(cams))
            # 碰撞自检（编辑器命令行里射线打不到刚摆的东西，放到游戏里查）：平台顶、坡道两头、草地、墙面、隐形墙
            RL = LAYOUT.get('ramp', {'len': 60.0, 'width': 24.0})
            rx = WALL_X - RL['width'] / 2 - 1.0
            ex = PL['x0'] - LAYOUT.get('elev', {}).get('gap', 2.0) - LAYOUT.get('elev', {}).get('size', 14.0) / 2
            for nm, x, y, z0, z1 in (('平台中', (PL['x0'] + WALL_X) / 2, 0, 80, -10), ('草地', 0, 0, 60, -10), ('平台前 5 米', PL['x0'] - 5, 0, 60, -10),
                                     ('北坡顶', rx, PL['y1'] + 2, 80, -10), ('北坡中', rx, PL['y1'] + RL['len'] / 2, 80, -10),
                                     ('南坡中', rx, PL['y0'] - RL['len'] / 2, 80, -10), ('南坡底', rx, PL['y0'] - RL['len'] + 2, 80, -10),
                                     ('电梯 0', ex, -75, 80, -10), ('电梯 1', ex, 75, 80, -10)):
                hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x * 100, y * 100, z0 * 100), unreal.Vector(x * 100, y * 100, z1 * 100),
                                                             unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [pawn], unreal.DrawDebugTrace.NONE, True)
                if isinstance(hit, tuple):
                    hit = hit[1] if hit[0] else None
                tt = hit.to_tuple() if hit else None
                hz = next((round(v.z / 100, 2) for v in tt if isinstance(v, unreal.Vector)), None) if tt and tt[0] else None
                ha = next((v.get_actor_label() if hasattr(v, 'get_actor_label') else v.get_name() for v in tt if isinstance(v, unreal.Actor)), None) if tt and tt[0] else None
                log('碰撞自检', nm, '高', hz, '打到', ha)
            for nm, a_, b_ in (('撞墙', (250, 0, 40), (340, 0, 40)), ('撞侧门', (250, 150, 36), (340, 150, 36)), ('撞两门之间的墙', (250, 75, 30), (340, 75, 30)),
                               ('出西边', (-280, 0, 5), (-330, 0, 5)), ('封顶', (0, 0, 100), (0, 0, 160))):
                if nm.startswith('撞'):   # 墙的碰撞和大门都是 BlockAll
                    hit = unreal.SystemLibrary.capsule_trace_single(world, unreal.Vector(*[v * 100 for v in a_]), unreal.Vector(*[v * 100 for v in b_]), 120, 200,
                                                                    unreal.TraceTypeQuery.TRACE_TYPE_QUERY2, False, [pawn], unreal.DrawDebugTrace.NONE, True)
                else:              # 隐形墙对象类型是 Vehicle、只挡 Pawn：按对象类型查
                    hit = unreal.SystemLibrary.capsule_trace_single_for_objects(world, unreal.Vector(*[v * 100 for v in a_]), unreal.Vector(*[v * 100 for v in b_]),
                                                                                120, 200, [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY5], False, [pawn],
                                                                                unreal.DrawDebugTrace.NONE, True)
                if isinstance(hit, tuple):
                    hit = hit[1] if hit[0] else None
                tt = hit.to_tuple() if hit else None
                hv = next((v for v in tt if isinstance(v, unreal.Vector)), None) if tt and tt[0] else None
                log('碰撞自检', nm, '停在', [round(hv.x / 100, 1), round(hv.y / 100, 1), round(hv.z / 100, 1)] if hv else None)
        return
    if state['phase'] == 'shoot':
        cam = state['cam']
        todo = [s for s in SHOTS if not ONLY or s[0] in ONLY]
        if state['i'] >= 0 and state['t'] - state['shot_t'] > WAIT * 0.4:
            if state.get('real0') is None:
                state['real0'] = time.perf_counter()
                state['frames'] = 0
            state['frames'] += 1
        if state['i'] >= 0 and not state.get('taken') and state['t'] - state['shot_t'] > WAIT:
            name = todo[state['i']][0]
            fps = state['frames'] / max(1e-3, time.perf_counter() - state['real0'])
            unreal.SystemLibrary.execute_console_command(world, f'Shot filename={OUT}/{name}.png nosuffix')
            log('截图', f'{OUT}/{name}.png', '后半段平均帧率', round(fps, 1))
            fps_log.append((name, round(fps, 1)))
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
            place(cam, state['pawn'], todo[state['i']])
            pc.set_view_target_with_blend(cam, 0.0)
        return
    if state['phase'] == 'quit' and state['t'] - state['shot_t'] > 2.0:
        for k, v in state.get('orig_q', {}).items():
            unreal.SystemLibrary.execute_console_command(world, f'{k} {v:g}')
        if state.get('orig_q'):
            log('画质改回原值')
        if ORIG_RES:
            try:
                unreal.GameUserSettings.get_game_user_settings().set_screen_resolution(unreal.IntPoint(*ORIG_RES))
                log('窗口分辨率设回', ORIG_RES)
            except Exception as e:
                log('设不回窗口分辨率', e)
        state['done'] = True
        log('完成，帧率', fps_log)
        unreal.SystemLibrary.execute_console_command(world, 'quit')


unreal.register_slate_post_tick_callback(tick)
log('已挂上逐帧回调，输出', OUT)


