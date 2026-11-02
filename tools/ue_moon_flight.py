"""月球基地手感实测（不改 C++）：-game 里按环境变量 MOON_TEST_G 改世界重力（运行时写 WorldGravityZ），每 0.1 秒记一次机体高度和速度（[flight]），配合 -MechInputTest 的按键驾驶看跳跃 / 喷射 / 落地。
UnrealEditor.exe RobotSlice.uproject "/Game/Maps/L_Moon?Match?Team=1" -game -MechInputTest -MechPerf -RenderOffscreen -unattended -nosplash -nosound -ForceRes -ResX=1600 -ResY=900 -windowed "-ExecCmds=py D:/RobotSlice/tools/ue_moon_flight.py"
"""
import os

import unreal

G = float(os.environ.get('MOON_TEST_G', '0'))
st = {'w': None, 'next': 0.0, 'set': False}


def tick(dt):
    w = st['w']
    if w is None:
        st['w'] = unreal.find_object(None, '/Game/Maps/L_Moon.L_Moon')
        return
    ws = w.get_world_settings()
    if G and not st['set']:
        try:
            ws.set_editor_property('world_gravity_z', G)
            ws.set_editor_property('global_gravity_z', G)
            st['set'] = True
            unreal.log(f'[flight] 重力改成 {G}')
        except Exception as e:
            unreal.log(f'[flight] 改不了重力 {e}')
            st['set'] = True
    t = unreal.GameplayStatics.get_time_seconds(w)
    if t < st['next']:
        return
    st['next'] = t + 0.1
    pc = unreal.GameplayStatics.get_player_controller(w, 0)
    p = pc.get_controlled_pawn() if pc else None
    if not p:
        return
    l, v = p.get_actor_location(), p.get_velocity()
    unreal.log(f'[flight] {t:.2f} {l.x / 100:.1f} {l.y / 100:.1f} {l.z / 100:.2f} {(v.x ** 2 + v.y ** 2) ** 0.5 / 100:.1f} {v.z / 100:.2f}')


unreal.register_slate_post_tick_callback(tick)
unreal.log('[flight] 已挂上')
