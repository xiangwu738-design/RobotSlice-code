"""本地测试流程 kind=py 用：它不能带参数，这里设好 all 再跑 ue_easter_import.py。"""
import runpy
import sys

sys.argv = ['D:/RobotSlice/tools/ue_easter_import.py', 'all']
runpy.run_path('D:/RobotSlice/tools/ue_easter_import.py', run_name='__main__')
