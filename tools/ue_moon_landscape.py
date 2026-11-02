"""月球基地：新建 L_Moon，把 near_height.png 导成 Landscape（走 Datasmith 的 Landscape 导入：脚本里没有别的办法调 ALandscape::Import，不用写 C++）。
工程平时不开 Datasmith 插件，只在跑这个脚本时命令行临时开：
UnrealEditor-Cmd RobotSlice.uproject -EnablePlugins=DatasmithImporter,DatasmithContent -run=pythonscript -script="D:/RobotSlice/tools/ue_moon_landscape.py" -AllowCommandletRendering -RenderOffscreen -unattended -nosplash
导完把 Datasmith 留在 Landscape 根组件上的用户数据、DatasmithSceneActor、临时场景资产都清掉，存盘的地图不依赖插件。
地形变了（moon_terrain_bake.py 重跑）要重跑这个脚本（只替换 Landscape，别的东西 ue_build_moon.py 再摆）。
"""
import json
import os

import unreal

W = 'D:/RobotSlice/Inbox/_moon_nasa/work'
DST = '/Game/Maps/L_Moon'
TMP = '/Game/Env/Moon/_DS'
L = json.load(open(f'{W}/layout_final.json', encoding='utf-8'))['landscape']
eas = unreal.EditorAssetLibrary
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
log = lambda *a: unreal.log('[moon] ' + ' '.join(str(x) for x in a))

if eas.does_asset_exist(DST):
    unreal.EditorLoadingAndSavingUtils.load_map(DST)
    for a in sub.get_all_level_actors():
        if isinstance(a, (unreal.LandscapeProxy,)):
            sub.destroy_actor(a)
            log('删掉旧 Landscape')
else:
    assert les.new_level(DST), '新建关卡失败'
    log('新建', DST)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()

xml = f"""<?xml version="1.0" encoding="UTF-8"?>
<DatasmithUnrealScene>
	<Version>0.24</Version>
	<SDKVersion>5.8</SDKVersion>
	<Host>RobotSliceMoon</Host>
	<Application Vendor="RobotSlice" ProductName="moon" ProductVersion="1"/>
	<User ID="" OS=""/>
	<Landscape label="MoonTerrain" name="MoonTerrain" layer="">
		<Transform tx="0.0" ty="0.0" tz="{L['zmid'] * 100:.3f}" sx="{L['xy']:.1f}" sy="{L['xy']:.1f}" sz="{L['z']:.1f}" qx="0.0" qy="0.0" qz="0.0" qw="1.0"/>
		<Heightmap value="{W}/near_height.png"/>
		<Material PathName=""/>
	</Landscape>
</DatasmithUnrealScene>
"""
path = f'{W}/moon_landscape.udatasmith'
open(path, 'w', encoding='utf-8').write(xml)
scene = unreal.DatasmithSceneElement.construct_datasmith_scene_from_file(path)
assert scene, '读不了 udatasmith'
assert scene.translate_scene(), 'translate 失败'
opts = scene.get_options(unreal.DatasmithImportOptions)
if opts:
    opts.base_options.scene_handling = unreal.DatasmithImportScene.CURRENT_LEVEL
res = scene.import_scene(TMP)
log('Datasmith 导入', res.import_succeed if hasattr(res, 'import_succeed') else res)
scene.destroy_scene()

land = None
for a in sub.get_all_level_actors():
    cls = a.get_class().get_name()
    if isinstance(a, unreal.Landscape):
        land = a
    elif 'DatasmithSceneActor' in cls:
        sub.destroy_actor(a)
        log('删掉 DatasmithSceneActor')
assert land, '没找到 Landscape'
loc = land.get_actor_location()
scl = land.get_actor_scale3d()
log('Landscape 位置', loc, '缩放', scl)
land.tags = ['MoonTerrain']
land.set_actor_label('MoonTerrain')
land.set_folder_path('Moon/Terrain')
mat = eas.load_asset('/Game/Env/Moon/Materials/MI_Moon_Ground')
land.set_editor_property('landscape_material', mat)
# 清掉 Datasmith 的用户数据（模板），不然平时不开插件时读图会警告
for c in land.get_components_by_class(unreal.ActorComponent):
    try:
        lst = c.get_editor_property('asset_user_data')
        if lst:
            log('清用户数据', c.get_name(), [x.get_class().get_name() for x in lst])
            c.set_editor_property('asset_user_data', [])
    except Exception as e:
        log('读不了用户数据', c.get_name(), e)
try:
    land.get_editor_property('tags')
    for k in ('use_landscape_for_cull_distance',):
        pass
except Exception:
    pass
land.set_editor_property('static_lighting_lod', 0) if False else None
# 让地形层内容写进最终高度图（存盘前引擎本来也会冲一次；这里显式跑一次）
try:
    land.force_layers_full_update()
    log('force_layers_full_update 完成')
except Exception as e:
    log('force_layers_full_update 不能从 Python 调', e)
    unreal.SystemLibrary.execute_console_command(world, 'landscape.ForceLayersFullUpdate')
unreal.EditorLoadingAndSavingUtils.save_map(world, DST)
log('已保存', DST)
# 抽查高度（射线）
for x, y in ((0, 0), (300, 300), (-395, -275), (470, 0), (0, -370), (600, 500)):
    hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x * 100, y * 100, 200000), unreal.Vector(x * 100, y * 100, -200000), unreal.TraceTypeQuery.ECC_VISIBILITY,
                                                 False, [], unreal.DrawDebugTrace.NONE, True)
    t = hit.to_tuple() if hit else None
    log('高度', x, y, t[4].z / 100 if t and t[0] else '没打到')
if eas.does_directory_exist(TMP):
    log('临时目录里的资产', eas.list_assets(TMP, recursive=True, include_folder=False))
