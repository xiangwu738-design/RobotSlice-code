"""彩蛋模型预览（11-02）：打开 build 存下的 .blend 重拍（不重建），再按配置 liveries 换几套涂装各拍一张，看随机涂装 + 品牌色灯条搭不搭。
  blender -b --factory-startup -P tools/blender_easter_preview.py -- dog   [边长]
  blender -b --factory-startup -P tools/blender_easter_preview.py -- drone [边长]
读 Inbox/_easter/build/<PDog|VDrone>_build.blend → 写 Inbox/_easter/preview/<dog|drone>/（和 build 时的图放一起，同名覆盖）。
Workbench 只显示纯色：Shell = PaintA、Frame = 深石墨灰、Accent = 品牌色；PaintB 的折线迷彩和灯的自发光只在 UE 里有。
然后 py -3 tools/easter_sheet.py Inbox/_easter/preview/<dog|drone>
"""
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import easter_blender_lib as L  # noqa: E402


def main():
    args = L.script_args()
    if not args or args[0] not in ('dog', 'drone'):
        raise L.EasterError('参数：dog | drone [边长]')
    which = args[0]
    size = int(args[1]) if len(args) > 1 and args[1].isdigit() else 1200
    res = (size, size * 3 // 4)
    cfg = L.load_config(which, [a for a in args[2:] if '=' in a])
    name = 'PDog' if which == 'dog' else 'VDrone'
    path = os.path.join(L.DIRS['build'], f'{name}_build.blend')
    if not os.path.exists(path):
        raise L.EasterError(f'没有 {path}：先跑 blender_easter_{which}.py')
    bpy.ops.wm.open_mainfile(filepath=path)
    out_dir = os.path.join(L.DIRS['preview'], which)
    sheet = []
    if which == 'dog':
        main_objs = [bpy.data.objects['SK_PDog']]
    else:
        body = bpy.data.objects['SM_VDrone_Body']
        rotor = bpy.data.objects['SM_VDrone_Rotor']
        main_objs = [body]
        for c in body.children:
            if c.name.startswith('SOCKET_Rotor_'):
                d = bpy.data.objects.new('preview_' + c.name, rotor.data)
                bpy.context.scene.collection.objects.link(d)
                d.matrix_world = c.matrix_world.copy()
                main_objs.append(d)
    mats = {s: bpy.data.materials.get(s) for s in L.SLOTS}
    L.render_views(main_objs, out_dir, which, ['front34', 'front', 'side', 'top', 'rear34'], sheet, color_type='MATERIAL', res=res)
    for i, lv in enumerate(cfg.get('liveries', [])):
        for slot, key in (('Shell', 'PaintA'), ('Accent', 'Accent')):
            m = mats.get(slot)
            if m and lv.get(key):
                m.diffuse_color = (*L.hex_rgb(lv[key]), 1.0)
        L.render_views(main_objs, out_dir, f'{which}_涂装{i:02d}', ['front34'], sheet, color_type='MATERIAL', res=(size // 2, size * 3 // 8),
                       note=f'{lv.get("name", "")} A {lv.get("PaintA")} B {lv.get("PaintB")} 灯 {lv.get("Accent")}')
    L.write_sheet(out_dir, sheet, f'彩蛋预览 {which}（{os.path.basename(path)}）', merge=True)


if __name__ == '__main__':
    which = next((a for a in L.script_args() if a in ('dog', 'drone')), 'x')
    L.run_main('easter', f'preview_{which}', main)
