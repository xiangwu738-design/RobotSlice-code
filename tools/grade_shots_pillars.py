"""11-02 整体色调：pillars 的固定机位对比图（本地测试流程 gamepy 用；它不能传环境变量，这里先设好再跑 ue_pillars_shots.py）。
机位挑和色调有关的（街景、远景、逆光、室内外），最多 12 张（结果分支每项只收 12 张）。输出 Saved/Screenshots/Grade/pillars。
"""
import os
import runpy

os.environ['PL_SHOTS_ONLY'] = '2_overview,3_flight,4_mid,5_close,6_low_water,7_island,8_mech_spawn,10_between'
os.environ['PL_SHOTS_DIR'] = 'D:/RobotSlice/Saved/Screenshots/Grade/pillars'
runpy.run_path('D:/RobotSlice/tools/ue_pillars_shots.py', run_name='__main__')
