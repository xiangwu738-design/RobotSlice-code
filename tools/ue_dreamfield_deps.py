"""梦核旷野图（L_Dreamfield）打包前的引用检查（11-01，主会话要把这张图打进包）：
从 /Game/Maps/L_Dreamfield 顺着资源注册表的依赖（硬引用 + 软引用）一层层往下找，按内容文件夹分组列出来，查有没有引用了却不存在的包。
打包（cook）只跟着地图的引用走：这里列出来的就是会被打进去的；脚本 / C++ 里按路径临时加载的东西不在这张表里，要另外处理。
UnrealEditor-Cmd.exe RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_dreamfield_deps.py" -unattended
结果写到日志（[dfdeps]）和 Inbox/_dreamwall/dreamfield_deps.json
"""
import json

import unreal

MAP = '/Game/Maps/L_Dreamfield'
log = lambda *a: unreal.log('[dfdeps] ' + ' '.join(str(x) for x in a))
ar = unreal.AssetRegistryHelpers.get_asset_registry()
ar.search_all_assets(True)
opt = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True,
                                            include_searchable_names=False, include_soft_management_references=False,
                                            include_hard_management_references=False)
seen, todo, missing = set(), [MAP], []
while todo:
    pkg = todo.pop()
    if pkg in seen:
        continue
    seen.add(pkg)
    if pkg.startswith('/Script/'):
        continue
    if not unreal.EditorAssetLibrary.does_directory_exist(pkg.rsplit('/', 1)[0]) or not ar.get_assets_by_package_name(pkg):
        missing.append(pkg)
        continue
    for d in ar.get_dependencies(pkg, opt) or []:
        d = str(d)
        if d not in seen:
            todo.append(d)


def root(p):
    parts = p.split('/')
    if p.startswith('/Script/'):
        return '/Script（C++ 类）'
    if p.startswith('/Game/Env/'):
        return '/'.join(parts[:4])
    return '/'.join(parts[:3])


groups = {}
for p in sorted(seen):
    groups.setdefault(root(p), []).append(p)
for g, ps in sorted(groups.items()):
    log(g, len(ps), '个包', '' if len(ps) > 6 else ps)
log('引用了却找不到的包', missing)
json.dump({'map': MAP, 'groups': groups, 'missing': missing}, open('D:/RobotSlice/Inbox/_dreamwall/dreamfield_deps.json', 'w', encoding='utf-8'),
          ensure_ascii=False, indent=1)
log('完成，共', len(seen), '个包')
