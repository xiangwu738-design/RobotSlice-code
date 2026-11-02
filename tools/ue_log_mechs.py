"""调试：-game 里每 5 秒（游戏时间）把所有机体的位置、速度、控制器打到日志（[mechs]），看 AI 卡在哪。
用法：-ExecCmds="py D:/RobotSlice/tools/ue_log_mechs.py"；环境变量 MECHS_MAP 指定关卡（默认 /Game/Maps/L_Volcano.L_Volcano）
"""
import os

import unreal

MAP = os.environ.get('MECHS_MAP', '/Game/Maps/L_Volcano.L_Volcano')
st = {'world': None, 'next': 0.0}


def tick(dt):
    w = st['world']
    if w is None:
        st['world'] = unreal.find_object(None, MAP)
        return
    t = unreal.GameplayStatics.get_time_seconds(w)
    if t < st['next']:
        return
    st['next'] = t + 5.0
    rows = []
    for p in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Pawn):
        if 'Mech' not in p.get_class().get_name():
            continue
        l, v = p.get_actor_location(), p.get_velocity()
        c = p.get_controller()
        rows.append(f'{p.get_name()[-6:]}({c.get_class().get_name()[:6] if c else "-"}) '
                    f'{l.x / 100:.0f},{l.y / 100:.0f},{l.z / 100:.0f} v{(v.x ** 2 + v.y ** 2) ** 0.5 / 100:.1f}')
    unreal.log(f'[mechs] t={t:.0f} ' + ' | '.join(rows))


unreal.register_slate_post_tick_callback(tick)
unreal.log('[mechs] 已挂上')
