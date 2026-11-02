"""11-02 整体色调：moon 的固定机位对比图（本地测试流程 gamepy 用；它不能传环境变量，这里先设好再跑 ue_moon_shots.py）。
机位挑和色调有关的（街景、远景、逆光、室内外），最多 12 张（结果分支每项只收 12 张）。输出 Saved/Screenshots/Grade/moon。
"""
import os
import runpy

os.environ['MOON_SHOTS_ONLY'] = 'b_spawn_w,c_north_road,d_north_plaza,e_south_tanks,f_dome_in,g_low_fly,i_earth,l_rim_view,p_mech_earth'
os.environ['MOON_SHOTS_DIR'] = 'D:/RobotSlice/Saved/Screenshots/Grade/moon'
runpy.run_path('D:/RobotSlice/tools/ue_moon_shots.py', run_name='__main__')
