"""梦核旷野去掉建模云（11-01 用户：「旷野上面的建模云可以删掉吗」）。只删地图里的 Cloud00.. 这批摆放（文件夹 Sky/Clouds），网格资源留着。可以重复跑。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_dreamfield_remove_clouds.py" -unattended -nosplash
结果写到 Saved/dreamfield_clouds.txt
"""
import unreal

MAP = '/Game/Maps/L_Dreamfield'
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les.load_level(MAP)
removed = []
for a in eas.get_all_level_actors():
    folder = str(a.get_folder_path())
    label = a.get_actor_label()
    if folder == 'Sky/Clouds' or (label.startswith('Cloud') and isinstance(a, unreal.StaticMeshActor)):
        removed.append(label)
        eas.destroy_actor(a)
ok = les.save_current_level()
left = [a.get_actor_label() for a in eas.get_all_level_actors() if 'cloud' in a.get_actor_label().lower()]
open('D:/RobotSlice/Saved/dreamfield_clouds.txt', 'w', encoding='utf-8').write(f'删了 {len(removed)} 个，保存 {ok}，还剩名字带 cloud 的：{left}\n')
