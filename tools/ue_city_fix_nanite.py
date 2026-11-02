"""城区图东南 65 米写字楼（K005 C02，SM_CB_C02）在游戏里是一整块没窗户的灰墙（10-31 灰块之一）：
Blender 里和导出的 FBX 里外立面都是全的（窗框、玻璃、窗台，10 万面），UE 里这栋的 Nanite 关不关画面都一样，碰撞射线也只打到砖墙壳
（LOD0 / 回退网格只剩 3634 面）→ 这栋按回退网格在画，回退网格把窗户简化没了。
这里把它的回退网格设成不简化（FallbackRelativeError 0），重建渲染数据、存盘：不管走 Nanite 还是回退网格都是全的外立面。
**10-31 试过没生效**（改完回退网格还是 3634 面，看来设属性没触发重建），C02 已用备份还原；原因没查清，见 docs/城区AI_搁置进度_20261031.md。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript "-script=D:/RobotSlice/tools/ue_city_fix_nanite.py [网格名,...]" -AllowCommandletRendering -RenderOffscreen
"""
import sys

import unreal

eas = unreal.EditorAssetLibrary
NAMES = next((a.split(',') for a in sys.argv[1:] if a.startswith('SM_')), ['SM_CB_C02'])
for n in NAMES:
    m = eas.load_asset(f'/Game/Env/City/Buildings/{n}')
    if not m:
        unreal.log_warning(f'[nanite] 没有 {n}')
        continue
    before = m.get_num_triangles(0)
    nan = m.get_editor_property('nanite_settings')
    nan.set_editor_property('fallback_relative_error', 0.0)
    nan.set_editor_property('fallback_percent_triangles', 1.0)
    m.set_editor_property('nanite_settings', nan)   # 默认通知：重建 Nanite 和回退网格
    after = m.get_num_triangles(0)
    ok = eas.save_loaded_asset(m, False)
    unreal.log(f'[nanite] {n} 回退网格 {before} → {after} 面，存盘 {ok}')
