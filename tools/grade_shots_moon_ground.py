"""11-02 月球地面清晰度：近景月壤 + 第三人称高度的地面（本地测试流程 gamepy 用，见 grade_shots_moon.py）。"""
import os
import runpy

os.environ['MOON_SHOTS_ONLY'] = 'm_ground_soil,b_spawn_w,c_north_road,e_south_tanks'
os.environ['MOON_SHOTS_DIR'] = 'D:/RobotSlice/Saved/Screenshots/Grade/moon_ground'
runpy.run_path('D:/RobotSlice/tools/ue_moon_shots.py', run_name='__main__')
