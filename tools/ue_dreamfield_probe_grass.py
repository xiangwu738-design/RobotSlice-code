"""看 StyleHex 免费草花包（/Game/StyleHex_Studio，用户 10-31 下载）的网格尺寸、面数、材质参数，并把草花图集导出成 PNG 看颜色。只读，不改资源。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_dreamfield_probe_grass.py" -AllowCommandletRendering -RenderOffscreen -unattended
"""
import os

import unreal

ROOT = '/Game/StyleHex_Studio'
OUT = 'D:/RobotSlice/Inbox/_dreamwall/stylehex_probe'
os.makedirs(OUT, exist_ok=True)
eas = unreal.EditorAssetLibrary
log = lambda *a: unreal.log('[probe] ' + ' '.join(str(x) for x in a))

for p in sorted(eas.list_assets(ROOT, recursive=True, include_folder=False)):
    a = eas.load_asset(p)
    if isinstance(a, unreal.StaticMesh):
        b = a.get_bounding_box()
        size = [round((b.max.x - b.min.x)), round((b.max.y - b.min.y)), round((b.max.z - b.min.z))]
        mats = [str(s.get_editor_property('material_interface').get_path_name()) if s.get_editor_property('material_interface') else None
                for s in a.get_editor_property('static_materials')]
        log('网格', a.get_name(), '尺寸厘米', size, 'z', round(b.min.z), '~', round(b.max.z), '三角形', a.get_num_triangles(0), 'LOD', a.get_num_lods(),
            'Nanite', a.get_editor_property('nanite_settings').enabled, '材质', mats)
    elif isinstance(a, unreal.MaterialInstanceConstant):
        log('材质实例', a.get_path_name(), '父', a.get_editor_property('parent').get_path_name() if a.get_editor_property('parent') else None)
        for v in a.get_editor_property('scalar_parameter_values'):
            log('   s', v.get_editor_property('parameter_info').get_editor_property('name'), v.get_editor_property('parameter_value'))
        for v in a.get_editor_property('vector_parameter_values'):
            log('   v', v.get_editor_property('parameter_info').get_editor_property('name'), v.get_editor_property('parameter_value'))
        for v in a.get_editor_property('texture_parameter_values'):
            t = v.get_editor_property('parameter_value')
            log('   t', v.get_editor_property('parameter_info').get_editor_property('name'), t.get_path_name() if t else None)
        try:
            for n in unreal.MaterialEditingLibrary.get_static_switch_parameter_names(a):
                log('   sw', n, unreal.MaterialEditingLibrary.get_material_instance_static_switch_parameter_value(a, n))
        except Exception as e:
            log('   sw 读不到', e)
    elif isinstance(a, unreal.Material):
        log('母材质', a.get_path_name(), 'blend', a.get_editor_property('blend_mode'), 'shading', a.get_editor_property('shading_model'),
            'two_sided', a.get_editor_property('two_sided'))
        for fn in (unreal.MaterialEditingLibrary.get_scalar_parameter_names, unreal.MaterialEditingLibrary.get_vector_parameter_names,
                   unreal.MaterialEditingLibrary.get_static_switch_parameter_names, unreal.MaterialEditingLibrary.get_texture_parameter_names):
            try:
                log('   ', fn.__name__, list(fn(a)))
            except Exception as e:
                log('   ', fn.__name__, e)
    elif isinstance(a, unreal.Texture2D) and 'Atlas' in a.get_name():
        t = unreal.AssetExportTask()
        t.object = a
        t.filename = f'{OUT}/{a.get_name()}.png'
        t.automated = True
        t.replace_identical = True
        t.prompt = False
        ok = unreal.Exporter.run_asset_export_task(t)
        log('导出贴图', a.get_name(), ok, a.blueprint_get_size_x(), a.blueprint_get_size_y())
log('完成')
