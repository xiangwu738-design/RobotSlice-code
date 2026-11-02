"""原版峡谷图 L_MegaCanyon 的成图截图（照 ue_dreamfield_shots.py 改的；坐标按 Blender 写，米）。
游戏里（-game -MechBeauty）第 3.5 秒暂停，把地图里的 ShotCam 挪到各机位拍 HighResShot；第一张和原包相机完全对齐（拿来和 Renders/1–3.png 对比）。
UnrealEditor.exe RobotSlice.uproject /Game/Maps/L_MegaCanyon -game -MechBeauty -RenderOffscreen -unattended -nosound -ForceRes -ResX=1920 -ResY=1080 -windowed
  "-ExecCmds=py D:/RobotSlice/tools/ue_megacanyon_shots.py"
环境变量：MC_SHOTS_DIR 输出目录（默认 Saved/Screenshots/MegaCanyon/now），MC_SHOTS_ONLY 只拍某几个机位，MC_SHOTS_RES 分辨率
"""
import json
import math
import os
import time

import unreal

OUT = os.environ.get('MC_SHOTS_DIR', 'D:/RobotSlice/Saved/Screenshots/MegaCanyon/now').replace('\\', '/')
WAIT = float(os.environ.get('MC_SHOTS_WAIT', '5.0'))
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
RES = os.environ.get('MC_SHOTS_RES', '1920x1080')
ONLY = [s for s in os.environ.get('MC_SHOTS_ONLY', '').split(',') if s]
os.makedirs(OUT, exist_ok=True)
CAM = json.load(open('D:/RobotSlice/Inbox/_megacanyon/export/megacanyon.json', encoding='utf-8'))['camera']
c0, f0 = CAM['loc'], CAM['fwd']
FOV0 = math.degrees(2 * math.atan(CAM['sensor'] / 2 / CAM['lens']))     # 27 毫米、36 毫米宽底片 → 水平 67.4°

# 名字, 机位 (Blender x, y, z), 看向 (Blender x, y, z), 水平 FOV, 机体 (x, y, z, UE 朝向) 或 None
SHOTS = [
    ('1_ref_camera', tuple(c0), (c0[0] + f0[0] * 1000, c0[1] + f0[1] * 1000, c0[2] + f0[2] * 1000), FOV0, None),   # 和原包相机对齐
    ('2_deck_mech', (-32.0, 34.0, 14.5), (14.0, -120.0, -10.0), 72, (-20.0, 0.0, 9.9, 90.0)),                    # 甲板上，机体在出生点
    ('3_in_canyon', (-0.5, 40.0, -45.0), (0.5, -400.0, -110.0), 70, None),                                        # 沟里往下看
    ('4_lit_wall', (-6.0, -60.0, -30.0), (12.0, -150.0, -55.0), 75, None),                                        # 沟里贴近被照亮的那面墙
    ('5_planet', (-30.0, 80.0, 16.0), (-141.0, -1140.0, 120.0), 70, None),                                        # 甲板上看行星
    ('6_above', (-190.0, 220.0, 260.0), (0.0, -260.0, -40.0), 66, None),                                          # 高处斜俯视
]

QUAL = ['sg.ResolutionQuality', 'sg.ViewDistanceQuality', 'sg.AntiAliasingQuality', 'sg.ShadowQuality', 'sg.GlobalIlluminationQuality',
        'sg.ReflectionQuality', 'sg.PostProcessQuality', 'sg.TextureQuality', 'sg.EffectsQuality', 'sg.FoliageQuality', 'sg.ShadingQuality']
world = None
state = {'t': 0.0, 'phase': 'wait', 'i': -1, 'shot_t': 0.0, 'cam': None, 'done': False}
fps_log = []
log = lambda *a: unreal.log('[mcshots] ' + ' '.join(str(x) for x in a))
B = lambda x, y, z: unreal.Vector(x * 100.0, -y * 100.0, z * 100.0)


def place(cam, pawn, shot):
    name, c, look, fov, mech = shot
    loc = B(*c)
    tgt = B(*look)
    d = tgt - loc
    yaw = math.degrees(math.atan2(d.y, d.x))
    pitch = math.degrees(math.atan2(d.z, math.hypot(d.x, d.y)))
    cam.set_actor_location_and_rotation(loc, unreal.Rotator(roll=0, pitch=pitch, yaw=yaw), False, True)
    cc = cam.camera_component
    cc.set_field_of_view(fov)
    cc.set_editor_property('constrain_aspect_ratio', False)
    if pawn:
        if mech:
            try:
                hh = pawn.capsule_component.get_scaled_capsule_half_height()
            except Exception:
                hh = 130.0
            pawn.set_actor_hidden_in_game(False)
            pawn.set_actor_location_and_rotation(B(mech[0], mech[1], mech[2]) + unreal.Vector(0, 0, hh + 2), unreal.Rotator(yaw=mech[3]), False, True)
        else:
            pawn.set_actor_hidden_in_game(True)
    log(name, '机位', [round(v, 1) for v in c], 'yaw', round(yaw, 1), 'pitch', round(pitch, 1), 'fov', round(fov, 1))


def tick(dt):
    global world
    if state['done']:
        return
    state['t'] += dt
    if world is None:
        world = unreal.find_object(None, '/Game/Maps/L_MegaCanyon.L_MegaCanyon')
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
            cams = [c for c in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CameraActor) if 'ShotCam' in [str(t) for t in c.tags]]
            state['cam'] = cams[0] if cams else None
            state['pawn'] = pawn
            state['phase'] = 'shoot'
            # 画质：编辑器 -game 读 Saved/Config/WindowsEditor/GameUserSettings.ini，11-01 那里是全最低档（阴影关、分辨率 50%），
            # 截出来没有投影、发糊。拍照时临时调到「史诗」，退出前改回原值（引擎退出时会把当前档位写回这个文件，不能改掉用户的设置）
            state['orig_q'] = {}
            if os.environ.get('MC_SHOTS_QUALITY', 'epic') == 'epic':
                for k in QUAL:
                    state['orig_q'][k] = unreal.SystemLibrary.get_console_variable_float_value(k)
                    unreal.SystemLibrary.execute_console_command(world, f'{k} {100 if k == "sg.ResolutionQuality" else 3}')
                log('画质临时调到史诗，原值', state['orig_q'])
            dbg = os.environ.get('MC_DEBUG', '')     # 排查用：nosun = 关太阳、nosky = 关天光，看某块亮面是直射光还是环境光照的
            if dbg in ('nosun', 'nosky'):
                cls = unreal.DirectionalLight if dbg == 'nosun' else unreal.SkyLight
                for l in unreal.GameplayStatics.get_all_actors_of_class(world, cls):
                    l.light_component.set_intensity(0.0)
                log('调试：关掉', cls.__name__)
            p = pawn.get_actor_location()
            log('暂停；出生后机体位置（米）', [round(p.x / 100, 1), round(-p.y / 100, 1), round(p.z / 100, 1)], '（Blender 坐标）')
            # 碰撞自检：甲板顶、沟口两边、沟底、隐形边界
            for nm, a_, b_ in (('甲板（单元那片）', (-20, 0, 60), (-20, 0, -5)), ('远处甲板', (-40, -1500, 60), (-40, -1500, -5)),
                               ('沟底', (-0.5, -800, -300), (-0.5, -800, -700))):
                hit = unreal.SystemLibrary.line_trace_single(world, B(*a_), B(*b_), unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [pawn],
                                                             unreal.DrawDebugTrace.NONE, True)
                if isinstance(hit, tuple):
                    hit = hit[1] if hit[0] else None
                tt = hit.to_tuple() if hit else None
                hv = next((v for v in tt if isinstance(v, unreal.Vector)), None) if tt and tt[0] else None
                log('碰撞自检', nm, '高', round(hv.z / 100, 2) if hv else None)
            for nm, a_, b_, obj in (('横穿沟（从沟中间往右）', (-0.5, -100, -20), (30, -100, -20), False), ('飞出东边', (290, -100, 50), (320, -100, 50), True),
                                    ('封顶', (0, -100, 1400), (0, -100, 1600), True)):
                if obj:
                    hit = unreal.SystemLibrary.capsule_trace_single_for_objects(world, B(*a_), B(*b_), 120, 200, [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY5],
                                                                                False, [pawn], unreal.DrawDebugTrace.NONE, True)
                else:
                    hit = unreal.SystemLibrary.capsule_trace_single(world, B(*a_), B(*b_), 120, 200, unreal.TraceTypeQuery.TRACE_TYPE_QUERY2, False,
                                                                    [pawn], unreal.DrawDebugTrace.NONE, True)
                if isinstance(hit, tuple):
                    hit = hit[1] if hit[0] else None
                tt = hit.to_tuple() if hit else None
                hv = next((v for v in tt if isinstance(v, unreal.Vector)), None) if tt and tt[0] else None
                log('碰撞自检', nm, '停在', [round(hv.x / 100, 1), round(-hv.y / 100, 1), round(hv.z / 100, 1)] if hv else None)
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
            unreal.SystemLibrary.execute_console_command(world, f'HighResShot {RES} filename={OUT}/{name}.png')
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
