"""给地图加一台截图用的 CameraActor（标签 ShotCam，ue_core_shots.py 运行时挪它拍图）。已有就不加。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_add_shotcam.py /Game/Maps/L_Volcano" -nullrhi -unattended
"""
import sys

import unreal

MAP = [a for a in sys.argv[1:] if not a.startswith('-')][0]
unreal.EditorLoadingAndSavingUtils.load_map(MAP)
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
have = [a for a in sub.get_all_level_actors() if isinstance(a, unreal.CameraActor) and 'ShotCam' in [str(t) for t in a.tags]]
if have:
    unreal.log(f'[shotcam] {MAP} 已有 ShotCam')
else:
    cam = sub.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(0, 0, 50000))
    cam.set_actor_label('ShotCam')
    cam.tags = [unreal.Name('ShotCam')]
    cam.get_editor_property('root_component').set_mobility(unreal.ComponentMobility.MOVABLE)
    cam.camera_component.set_editor_property('constrain_aspect_ratio', False)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)
    unreal.log(f'[shotcam] {MAP} 加了 ShotCam 并保存')
