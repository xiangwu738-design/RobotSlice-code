"""11-02 去组件试点：新纪中型（NEC_M = 减曲面方案 M2）。Blender 命令行：
  blender -b --factory-startup -P tools/kitfree_run.py -- before|after <输出目录> [<存的 .blend>]
before：原样跑 lookdev/factions/dingshan/flat 的 M2（带组件），只出 Workbench 图；
after ：先把 ds_common.kit_source 换成 tools/kitfree_parts.py 的自建件再跑，出图并另存 .blend（不覆盖原来的 DS_M2.blend）。
出图走现成的 ds_render.py（--scheme M2 --engine wb --views front,side,back,q34 --noref）。"""
import os
import runpy
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
DS = os.path.join(ROOT, 'lookdev', 'factions', 'dingshan')
FLAT = os.path.join(DS, 'flat')
i = sys.argv.index('--')
mode, out = sys.argv[i + 1], sys.argv[i + 2]
blend = sys.argv[i + 3] if len(sys.argv) > i + 3 else ''
for p in (TOOLS, DS, FLAT):
    if p not in sys.path:
        sys.path.insert(0, p)
import ds_common  # noqa: E402

if mode == 'after':
    import kitfree_parts  # noqa: E402
    kitfree_parts.patch(ds_common, 'ds_common')
import ds_flat  # noqa: E402,F401  注册 M1 / M2 / H1 / H2
os.makedirs(out, exist_ok=True)
args = ['--scheme', 'M2', '--engine', 'wb', '--views', 'front,side,back,q34', '--noref', '--out', out, '--tag', mode]
if blend:
    args += ['--blend', blend]
sys.argv = sys.argv[:i + 1] + args
runpy.run_path(os.path.join(DS, 'ds_render.py'), run_name='__main__')
print('[kitfree] 用到的素材件', dict(ds_common.USED_KIT))
