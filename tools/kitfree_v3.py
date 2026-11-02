"""11-02 去组件：v3 绑骨工程重建（维克托三台的外壳建在它上面）。照 README「机体资源流程」第 2 步的命令跑 robot_lookdev.py，
只在 main() 之前插 tools/kitfree_parts.py 的替换（液压杆 / 线缆 / 减震都换成自建件、不再按 KIT_LITE 减面）。
  blender -b --factory-startup -P tools/kitfree_v3.py
输出 lookdev/rig/kitfree/SKM_Robot_v3.fbx / .blend（原来的 lookdev/rig/SKM_Robot_v3.* 不动）。"""
import os
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
LOOKDEV = os.path.join(ROOT, 'lookdev')
sys.path.insert(0, TOOLS)
import kitfree_parts  # noqa: E402

rl = os.path.join(LOOKDEV, 'robot_lookdev.py')
src = kitfree_parts.patch_rl_source(open(rl, encoding='utf-8').read())
out = os.path.join(LOOKDEV, 'rig', 'kitfree', 'SKM_Robot_v3.fbx')
os.makedirs(os.path.dirname(out), exist_ok=True)
sys.argv = ['blender', '--', '--design', 'v3', '--kit', '--rig', os.path.join(LOOKDEV, 'rig', 'SKM_Manny_Simple.fbx'), '--export', out, '--blend']
exec(compile(src, rl, 'exec'), {'__name__': 'robot_lookdev', '__file__': rl, '__kitfree__': kitfree_parts})
print('[kitfree] v3 绑骨工程（去组件）', out, flush=True)
