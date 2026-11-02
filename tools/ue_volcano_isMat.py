"""M_WorldPBR 打开“用于实例化静态网格”（夺芯灰盒只留碰撞时掩体 / 岩洞内壁 / 桥用它的实例，ISM 上没这个开关会显示成默认网格材质）。"""
import unreal
m = unreal.load_asset('/Game/Env/Materials/M_WorldPBR')
unreal.log(f'[ismat] 原来 {m.get_editor_property("used_with_instanced_static_meshes")}')
if not m.get_editor_property('used_with_instanced_static_meshes'):
    m.set_editor_property('used_with_instanced_static_meshes', True)
    unreal.MaterialEditingLibrary.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m, False)
unreal.log(f'[ismat] 现在 {m.get_editor_property("used_with_instanced_static_meshes")}')
