"""调试：-game 里第 14 / 18 / 22 秒（真实时间）各 dump 一帧的游戏线程统计（stat dumpframe），看游戏线程时间花在哪。
用法：-ExecCmds="py D:/RobotSlice/tools/k051_statdump.py"，结果在日志里（Stat 树形输出）"""
import time

import unreal

st = {'t0': time.perf_counter(), 'done': set(), 'on': False}


def tick(dt):
    t = time.perf_counter() - st['t0']
    if not st['on'] and t > 10:
        unreal.SystemLibrary.execute_console_command(None, 'stat game')
        unreal.SystemLibrary.execute_console_command(None, 'stat physics')
        st['on'] = True
    for k in (14, 18, 22):
        if t > k and k not in st['done']:
            st['done'].add(k)
            unreal.log(f'[statdump] t={k}')
            unreal.SystemLibrary.execute_console_command(None, 'stat dumpframe -ms=0.4 -root=GameThread')


unreal.register_slate_post_tick_callback(tick)
unreal.log('[statdump] 已挂上')
