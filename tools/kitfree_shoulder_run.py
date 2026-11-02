"""11-02 去组件：肩部装备重建。照原脚本跑，只把 make_shoulder_kits.part 换成 tools/kitfree_shoulder.part，
并让 use() 遇到自建件（kitfree=True）不再减面（原件动辄几万面，脚本按 ratio 减；自建件本来就低面）。
  blender -b --factory-startup -P tools/kitfree_shoulder_run.py -- game|tactical <输出目录>
原来的输出（Inbox/_肩部候选/游戏模型、游戏模型_战术）不动。"""
import os
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
LOOKDEV = os.path.join(ROOT, 'lookdev')
for p in (TOOLS, LOOKDEV):
    if p not in sys.path:
        sys.path.insert(0, p)
import kitfree_shoulder  # noqa: E402
import make_shoulder_kits as K  # noqa: E402

K.part = kitfree_shoulder.part
i = sys.argv.index('--')
which, out = sys.argv[i + 1], sys.argv[i + 2]
path = os.path.join(LOOKDEV, 'make_shoulder_game.py' if which == 'game' else 'make_shoulder_tactical.py')
src = open(path, encoding='utf-8').read()
a = "    if ratio:\n        mod = ob.modifiers.new('d', 'DECIMATE')"
assert src.count(a) == 1, 'use() 变了'
src = src.replace(a, "    if ratio and not ob.get('kitfree'):\n        mod = ob.modifiers.new('d', 'DECIMATE')")
sys.argv = [sys.argv[0], '--', out]
exec(compile(src, path, 'exec'), {'__name__': '__main__', '__file__': path})
print('[kitfree] 肩部重建完成', which, out, flush=True)
