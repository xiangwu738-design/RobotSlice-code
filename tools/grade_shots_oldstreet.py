"""11-02 整体色调：oldstreet 的固定机位对比图（本地测试流程 gamepy 用；它不能传环境变量，这里先设好再跑 ue_oldstreet_shots.py）。
机位挑和色调有关的（街景、远景、逆光、室内外），最多 12 张（结果分支每项只收 12 张）。输出 Saved/Screenshots/Grade/oldstreet。
"""
import os
import runpy

os.environ['OS_SHOTS_ONLY'] = 'street_eye,street_back,cross,cross_w,author,high,far_fwd,far_back'
os.environ['OS_SHOTS_DIR'] = 'D:/RobotSlice/Saved/Screenshots/Grade/oldstreet'
runpy.run_path('D:/RobotSlice/tools/ue_oldstreet_shots.py', run_name='__main__')
