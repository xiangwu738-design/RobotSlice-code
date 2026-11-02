"""11-02 整体色调：soviet 的固定机位对比图（本地测试流程 gamepy 用；它不能传环境变量，这里先设好再跑 ue_soviet_shots.py）。
机位挑和色调有关的（街景、远景、逆光、室内外），最多 12 张（结果分支每项只收 12 张）。输出 Saved/Screenshots/Grade/soviet。
"""
import os
import runpy

os.environ['SV_SHOTS_ONLY'] = 'a0_aerial_sw,s0_street_n_w,s1_street_n_mid,s2_cross,y0_yard_center,y1_spawn_w,c0_third_street,c2_third_cross,p3_playground,f1_fly_mid'
os.environ['SV_SHOTS_DIR'] = 'D:/RobotSlice/Saved/Screenshots/Grade/soviet'
runpy.run_path('D:/RobotSlice/tools/ue_soviet_shots.py', run_name='__main__')
