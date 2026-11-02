"""只读：打开地图，统计各类 Actor 数量、来自哪个模块，并给出世界设置里的游戏模式。"""
import collections
import json
import sys

import unreal

level, out = sys.argv[1], sys.argv[2]
unreal.EditorLoadingAndSavingUtils.load_map(level)
world = unreal.EditorLevelLibrary.get_editor_world()
actors = unreal.EditorLevelLibrary.get_all_level_actors()
by_class = collections.Counter()
samples = {}
for a in actors:
    cls = a.get_class()
    path = cls.get_path_name()
    by_class[path] += 1
    samples.setdefault(path, a.get_actor_label())
ws = world.get_world_settings()
gm = ws.get_editor_property('default_game_mode')
data = {
    'actors': len(actors),
    'game_mode_override': gm.get_path_name() if gm else None,
    'classes': [{'class': k, 'count': v, 'sample': samples[k]} for k, v in by_class.most_common()],
}
json.dump(data, open(out, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
unreal.log(f'[actors] {len(actors)} actors, {len(by_class)} classes, gamemode={data["game_mode_override"]}')
