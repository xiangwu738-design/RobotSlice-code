"""握拳检查（主菜单竖大拇指用的握拳）：没有柄，四指弯到第二、三节碰到手掌。blender -b SKM_Robot_v3.blend -P fist_check.py -- 输出目录"""
import bpy, sys, os, math
from mathutils import Vector
exec(open(os.path.join(os.path.dirname(__file__), 'grip_core.py'), encoding='utf-8').read())
OUT = sys.argv[sys.argv.index('--') + 1]
os.makedirs(OUT, exist_ok=True)


class NoHandle:
    A = Vector((1, 0, 0)); C = Vector((0, 0, 0)); hu = hv = 1.0
    def sdf(self, p):
        return 1e9


H, k, n, d = hand_frame_rest('r')
mats, log = solve('r', NoHandle())
mats = {b: m for b, m in mats.items() if not b.startswith('thumb')}
apply_pose(mats)
print('[fist]', log)
cam = setup()
tgt = H + d * 6 + n * 2
for i, v in enumerate((-n * 0.4 + k * 0.7 + Vector((0, 0, 0.3)), n * 0.9 - k * 0.3, -k + n * 0.3, d + n * 0.4)):
    shoot(cam, tgt, v, 55, f'{OUT}/fist_{i}.png')
