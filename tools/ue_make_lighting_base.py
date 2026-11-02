"""从 V29 校园地图里只留天空灯光（灯光总控 + 环境装置）和出生点，另存为 /Game/Maps/L_LightingBase。
以后场景都从这张底图生成，校园模型就可以从工程里移走。"""
import unreal

sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
unreal.EditorLoadingAndSavingUtils.load_map('/Game/CampusV29/Maps/L_XiuzhouCampus_V29')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
keep = ('LightingDirector', 'LightingEnvironmentRig', 'PlayerStart', 'WorldSettings')
for a in sub.get_all_level_actors():
    cls = a.get_class().get_name()
    if not any(k in cls for k in keep) and 'Brush' not in cls:
        sub.destroy_actor(a)
world.get_world_settings().set_editor_property('default_game_mode', unreal.load_class(None, '/Script/RobotSlice.MechGameMode'))
unreal.EditorLoadingAndSavingUtils.save_map(world, '/Game/Maps/L_LightingBase')
unreal.log(f'[base] 已另存 /Game/Maps/L_LightingBase，保留 {[a.get_actor_label() for a in sub.get_all_level_actors()]}')
