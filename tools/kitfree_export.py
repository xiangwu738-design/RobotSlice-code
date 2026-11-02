"""11-02 机体导出包装（不改 lookdev 里的原脚本）：照 export_factions.py 自己插 robot_lookdev 钩子的做法，读原脚本源码、在两处插钩子再执行。
  blender -b --factory-startup <源 .blend> -P tools/kitfree_export.py -- [--normals fix|keep] [--kitfree] <export_factions 原参数，如 --key NEC_M --out 目录>
--normals fix（默认）：合成一个网格之后、写临时文件之前，重算法线：清掉自定义法线 → 全部面平滑 → 按 30° 标硬边 → 加权法线（面积加权、保留硬边）应用。
    原因（11-02 用户：「新纪系列在游戏内的渲染为什么显得曲面很多，一直都有这个问题」）：鼎山的方块整面都是平滑着色，边缘靠第二层倒角「小圆角」
    的 harden normals 撑着；导出的 game_mods 把「小圆角」整个删掉、其余倒角改 1 段，平滑着色就跨着整个方块插值，游戏里像鼓起来的枕头。
--kitfree：组件已经换成自建件（tools/kitfree_parts.py）的源文件——自建件本来就是低面，不再按 KIT_FINAL 0.2 减面。"""
import math
import os
import sys

import bpy

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
SRC = os.path.join(ROOT, 'lookdev', 'rig', 'factions', 'export_factions.py')
i = sys.argv.index('--')
rest = sys.argv[i + 1:]
mode = 'fix'
kitfree = False
if '--normals' in rest:
    j = rest.index('--normals')
    mode = rest[j + 1]
    del rest[j:j + 2]
if '--kitfree' in rest:
    rest.remove('--kitfree')
    kitfree = True
sys.argv = sys.argv[:i + 1] + rest


def fix_normals(ob, angle_deg=30.0):
    me = ob.data
    bpy.ops.object.select_all(action='DESELECT')
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    if me.has_custom_normals:
        bpy.ops.mesh.customdata_custom_splitnormals_clear()
    me.polygons.foreach_set('use_smooth', [True] * len(me.polygons))
    me.set_sharp_from_angle(angle=math.radians(angle_deg))
    md = ob.modifiers.new('加权法线', 'WEIGHTED_NORMAL')
    md.mode = 'FACE_AREA'
    md.weight = 50
    md.keep_sharp = True
    md.thresh = 0.01
    bpy.ops.object.modifier_move_to_index(modifier=md.name, index=0)
    bpy.ops.object.modifier_apply(modifier=md.name)
    print(f'[kitfree] 法线重算：{len(me.polygons)} 面，硬边角度 {angle_deg}°，加权法线（面积、保留硬边）', flush=True)


def hook(robot):
    if mode == 'fix':
        fix_normals(robot)
    else:
        print('[kitfree] 法线保持原样（对比用）', flush=True)


src = open(SRC, encoding='utf-8').read()
anchor = "tmp = os.path.join(TMP, f'{A.key}_{os.getpid()}_mesh.blend')"
assert anchor in src, 'export_factions.py 变了，钩子要改'
src = src.replace(anchor, '__hook__(robot)\n' + anchor, 1)
if kitfree:
    assert 'KIT_FINAL = 0.2' in src
    src = src.replace('KIT_FINAL = 0.2', 'KIT_FINAL = 1.0', 1)
exec(compile(src, SRC, 'exec'), {'__name__': '__main__', '__file__': SRC, '__hook__': hook})
