"""（RobotSlice 用）给 L_Volcano 的近景地形重导高度和图层（build_volcano_heights.py 改了挖法以后跑）；原注释：在草稿工程里把火山包 Level_1 改成 L_Volcano（编辑器模式跑，要逐帧等地形编辑层合并）：
删 8K / 4K 地形；1K 地形 = 近景（1 米一格，导入挖好的高度 + 图层），2K 地形 = 远景（4 米一格，整张原地形转到游戏坐标、近景下面压低）；
挪火山口烟雾；另存 /Game/Maps/L_Volcano。不动原关卡文件（另存）。
UnrealEditor.exe MapLab.uproject -ExecutePythonScript="D:/RobotSlice_MapLab/_tools/maplab_build_volcano.py" -unattended -nosplash
数据目录：环境变量 VOLCANO_DATA（build_volcano_heights.py 的输出）
"""
import json
import math
import os
import time

import unreal

DATA = os.environ.get('VOLCANO_DATA', 'D:/RobotSlice_MapLab/_survey/volcano_build').replace('\\', '/')
META = json.load(open(f'{DATA}/meta.json'))
LEVEL = '/Game/Maps/L_Volcano'
TMP = '/Game/_VolcanoTmp'
log = lambda *a: unreal.log('[volcano] ' + ' '.join(str(x) for x in a))
st = {'phase': 0, 't': 0.0, 'mark': 0.0, 'done': False}
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def import_png(name, gray):
    t = unreal.AssetImportTask()
    t.set_editor_property('filename', f'{DATA}/{name}.png')
    t.set_editor_property('destination_path', TMP)
    t.set_editor_property('destination_name', name)
    t.set_editor_property('automated', True)
    t.set_editor_property('replace_existing', True)
    t.set_editor_property('save', False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    tex = unreal.load_asset(f'{TMP}/{name}')
    tex.set_editor_property('srgb', False)
    tex.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property('filter', unreal.TextureFilter.TF_NEAREST)
    tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_GRAYSCALE if gray
                            else unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
    tex.set_editor_property('never_stream', True)
    return tex


def to_rt(tex, n):
    w = world()
    rt = unreal.RenderingLibrary.create_render_target2d(w, n, n, unreal.TextureRenderTargetFormat.RTF_RGBA8, unreal.LinearColor(0, 0, 0, 1), False)
    canvas, size, ctx = unreal.RenderingLibrary.begin_draw_canvas_to_render_target(w, rt)
    canvas.draw_texture(tex, unreal.Vector2D(0, 0), unreal.Vector2D(n, n), unreal.Vector2D(0, 0), unreal.Vector2D(1, 1),
                        unreal.LinearColor(1, 1, 1, 1), unreal.BlendMode.BLEND_OPAQUE, 0.0, unreal.Vector2D(0.5, 0.5))
    unreal.RenderingLibrary.end_draw_canvas_to_render_target(w, ctx)
    return rt


def landscapes():
    return {a.get_actor_label(): a for a in eas.get_all_level_actors() if isinstance(a, unreal.LandscapeProxy)}


def step():
    ph = st['phase']
    if ph == 0:
        st['phase'] = 1
        log('加载', LEVEL)
        unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
        st['mark'] = st['t']
        return
    if ph == 1:
        if st['t'] - st['mark'] < 4:
            return
        st['tex'] = {'near_height_rg': import_png('near_height_rg', False)}
        for k in META['order']:
            st['tex'][f'near_w_{k}'] = import_png(f'near_w_{k}', True)
        try:
            unreal.AutomationLibrary.finish_loading_before_screenshot()
        except Exception as e:
            log('等贴图编译失败', e)
        st['phase'] = 2
        st['mark'] = st['t']
        return
    if ph == 2:
        if st['t'] - st['mark'] < 3:
            return
        near = landscapes()['Volcano_Near']
        log('高度导入', near.landscape_import_heightmap_from_render_target(to_rt(st['tex']['near_height_rg'], META['near_n']), True, 0))
        names = {str(n).replace(' ', ''): n for n in near.get_target_layer_names(False)}
        for k in META['order'] * 3:
            log('图层导入', k, near.landscape_import_weightmap_from_render_target(to_rt(st['tex'][f'near_w_{k}'], META['near_n']), names[k], 0))
        st['phase'] = 3
        st['mark'] = st['t']
        return
    if ph == 3:
        if st['t'] - st['mark'] < 20:
            return
        ok = unreal.EditorLoadingAndSavingUtils.save_map(world(), LEVEL)
        if not ok:
            ok = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, False)
        log('存地图', ok)
        log('删临时贴图', unreal.EditorAssetLibrary.delete_directory(TMP))
        st['phase'] = 4
        st['mark'] = st['t']
        return
    if ph == 4 and st['t'] - st['mark'] > 3:
        st['done'] = True
        log('完成')
        unreal.SystemLibrary.quit_editor()


def tick(dt):
    if st['done'] or st.get('busy'):
        return
    st['t'] += dt
    st['busy'] = True
    try:
        step()
    except Exception as e:
        import traceback
        log('出错', st['phase'], e, traceback.format_exc())
        st['done'] = True
        unreal.SystemLibrary.quit_editor()
    st['busy'] = False


unreal.EditorPythonScripting.set_keep_python_script_alive(True)   # 否则 -ExecutePythonScript 跑完脚本就关编辑器
unreal.register_slate_post_tick_callback(tick)
log('已挂回调', DATA)
