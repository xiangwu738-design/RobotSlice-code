"""11-02 老街过接缝对比（本地测试流程 gamepy 用，地图 "/Game/Maps/L_OldStreet?Range"）：跑 ue_oldstreet_seamtest.py，
瞬移那一帧不通知镜头切换（-MechLoopNoCut 的做法）；只拍前两个机位、瞬移后第 0 / 1 / 3 / 8 帧（结果分支每项只收 12 张）。"""
import os

os.environ['OS_SEAM_CUT'] = '0'
os.environ['OS_SEAM_TAG'] = 'grade_nocut'
src = open('D:/RobotSlice/tools/ue_oldstreet_seamtest.py', encoding='utf-8').read()
src = src.replace('KS = [0, 1, 2, 3, 5, 8, 16]', 'KS = [0, 1, 3, 8]')
g = {'__name__': '__main__', '__file__': 'D:/RobotSlice/tools/ue_oldstreet_seamtest.py'}
exec(compile(src, 'ue_oldstreet_seamtest.py', 'exec'), g)
g['CASES'][:] = g['CASES'][:2]
