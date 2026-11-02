"""11-02 去组件：相位（白塔）方案重建。照 baita_build.py 原样跑，只把 robot_lookdev 命名空间里的 kit_source 换成自建件。
  blender -b --factory-startup lookdev/rig/SKM_Robot_v3.blend -P tools/kitfree_baita.py -- --scheme L1|BM|H2 --out <出图目录>
另存成 lookdev/factions/baita/blend/白塔_<方案>_kitfree.blend（--suffix _kitfree），原来的 白塔_<方案>.blend 不动。"""
import os
import runpy
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
BT = os.path.join(ROOT, 'lookdev', 'factions', 'baita')
for p in (TOOLS, BT):
    if p not in sys.path:
        sys.path.insert(0, p)
import baita_common  # noqa: E402
import kitfree_parts  # noqa: E402

kitfree_parts.patch_rl_namespace(baita_common.RL)
i = sys.argv.index('--')
sys.argv = sys.argv[:i + 1] + sys.argv[i + 1:] + ['--suffix', '_kitfree', '--save', '--engine', 'workbench', '--views', 'front,34']
runpy.run_path(os.path.join(BT, 'baita_build.py'), run_name='__main__')
