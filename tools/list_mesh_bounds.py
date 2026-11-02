"""只读：列出地图里每个静态网格物体的名字和包围盒尺寸（米）。"""
import sys

import unreal

unreal.EditorLoadingAndSavingUtils.load_map(sys.argv[1])
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
rows = []
for a in sub.get_all_level_actors():
    if isinstance(a, unreal.StaticMeshActor) and not a.actor_has_tag('GeneratedBackground'):
        o, e = a.get_actor_bounds(False)
        rows.append((a.get_actor_label(), o.x / 100, o.y / 100, o.z / 100, e.x / 50, e.y / 50, e.z / 50))
rows.sort(key=lambda r: -(r[4] * r[5]))
for r in rows:
    unreal.log('[bounds] %-40s 中心(%7.0f,%7.0f,%5.0f) 尺寸 %6.0f x %6.0f x %5.0f 米' % r)
