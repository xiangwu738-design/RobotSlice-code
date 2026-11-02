"""11-02 主机带一个真客户端测帧时间（kind=gamepy，地图带 ?listen，比如 "/Game/Maps/L_Pillars?Match?Mode=DM?listen"，args 带 -MechPerf -MechQuitAfter=N）。
用户：「在石林地图，主机有的时候会很卡顿」「联机当主机才卡」。单机测不出来，所以这里在本机再开一个客户端（-nullrhi，不抢显卡）连 127.0.0.1，
主机的 [perf] 日志照常进测试结果；客户端日志 Saved/Logs/NetPerfClient.log，结束前把客户端的 [perf]/[netdiag] 摘要打到 [netperf]。
客户端到点自己退（-MechQuitAfter），这里最后再保险结束一次。"""
import os
import subprocess
import time

import unreal

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.get_project_file_path())
ENGINE = unreal.Paths.convert_relative_path_to_full(unreal.Paths.engine_dir())
UE = os.path.join(ENGINE, 'Binaries', 'Win64', 'UnrealEditor.exe')
LOGDIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_log_dir())
CLOG = os.path.join(LOGDIR, 'NetPerfClient.log')
CLIENT_SECONDS = 150
log = lambda *a: unreal.log_warning('[netperf] ' + ' '.join(str(x) for x in a))
st = {'t0': time.time(), 'proc': None, 'done': False}


def start_client():
    args = [UE, PROJ, '127.0.0.1', '-game', '-MechNoIntro', '-nullrhi', '-nosound', '-unattended', '-nosplash', '-MechPerf', '-MechNetDiag',
            f'-MechQuitAfter={CLIENT_SECONDS}', '-log=NetPerfClient.log']
    try:
        st['proc'] = subprocess.Popen(args, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        log('客户端已启动 pid', st['proc'].pid)
    except OSError as e:
        log('客户端启动失败', e)


def finish():
    st['done'] = True
    p = st['proc']
    if p and p.poll() is None:
        time.sleep(3)
    if p and p.poll() is None:
        p.kill()
        log('客户端没自己退，结束掉')
    try:
        text = open(CLOG, 'rb').read().decode('utf-8', errors='replace').splitlines()
    except OSError as e:
        log('读不到客户端日志', e)
        return
    keep = [l for l in text if '[perf]' in l or '[battle]' in l or 'netdiag' in l.lower()]
    log(f'客户端日志 {len(text)} 行，摘要 {len(keep)} 行（最多 40）：')
    for l in keep[:20] + (keep[-20:] if len(keep) > 40 else keep[20:40]):
        log('  客户端|', l.strip()[:220])


def tick(dt):
    if st['done']:
        return
    t = time.time() - st['t0']
    if st['proc'] is None and t > 8:
        start_client()
    if st['proc'] is not None and t > CLIENT_SECONDS + 20:
        finish()
        unreal.unregister_slate_post_tick_callback(h)


h = unreal.register_slate_post_tick_callback(tick)
log('等 8 秒后开客户端；主机的 [perf] 照常记')
