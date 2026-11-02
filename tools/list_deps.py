"""在旧的校园工程里跑（只读）：列出一张地图递归依赖的所有包，写成 JSON。
UnrealEditor-Cmd 旧工程.uproject -run=pythonscript -script="list_deps.py 地图包名 输出.json"
"""
import json
import sys

import unreal

args = sys.argv[1:]
root, out = args[0], args[1]
extra = args[2:]  # 额外要带上的目录（比如灯光示例资源）

ar = unreal.AssetRegistryHelpers.get_asset_registry()
ar.search_all_assets(True)
opts = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True,
                                             include_hard_package_references=True,
                                             include_searchable_names=False,
                                             include_soft_management_references=False,
                                             include_hard_management_references=False)
seen, stack = set(), [root]
for folder in extra:
    for a in ar.get_assets_by_path(folder, recursive=True):
        stack.append(str(a.package_name))
while stack:
    pkg = stack.pop()
    if pkg in seen:
        continue
    seen.add(pkg)
    for dep in ar.get_dependencies(pkg, opts) or []:
        dep = str(dep)
        if dep not in seen:
            stack.append(dep)

game = sorted(p for p in seen if p.startswith('/Game/'))
script = sorted(p for p in seen if p.startswith('/Script/'))
other = sorted(p for p in seen if not p.startswith(('/Game/', '/Script/', '/Engine/')))
json.dump({'root': root, 'game': game, 'script': script, 'other_mounts': other}, open(out, 'w', encoding='utf-8'),
          ensure_ascii=False, indent=1)
unreal.log(f'[deps] {root}: game={len(game)} script={len(script)} other={len(other)}')
