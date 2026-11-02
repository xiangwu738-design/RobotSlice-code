"""11-02 去除不可商用素材 · 第一步：盘点（kind=py，只读）。
从会进包的东西出发（DefaultGame.ini 的 MapsToCook 每张图 + DirectoriesToAlwaysCook 每个目录 + 代码里按路径加载的），顺着硬引用 / 软引用一路往下找，
把用到的每个包按「来源文件夹」（/Game 下前两级，比如 /Game/Env/Valley、/Game/AAAVFX）归类：多少个包、多大、被哪些地图 / 目录用到。
结果：日志 [audit]（每个文件夹一行，按大小排），完整清单写 Saved/AssetAudit/audit.json（包名 → 用到它的入口）。"""
import json
import os
import re

import unreal

PROJ = os.path.abspath(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).replace('\\', '/').rstrip('/')
OUT = f'{PROJ}/Saved/AssetAudit'
log = lambda *a: unreal.log_warning('[audit] ' + ' '.join(str(x) for x in a))
ar = unreal.AssetRegistryHelpers.get_asset_registry()
opts = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True,
                                             include_searchable_names=False, include_soft_management_references=False,
                                             include_hard_management_references=False)


def roots():
    out = []
    ini = open(f'{PROJ}/Config/DefaultGame.ini', encoding='utf-8-sig', errors='replace').read()
    for m in re.findall(r'\+MapsToCook=\(FilePath="([^"]+)"\)', ini):
        out.append(('图 ' + m.rsplit('/', 1)[-1], [m]))
    for d in re.findall(r'\+DirectoriesToAlwaysCook=\(Path="([^"]+)"\)', ini):
        pk = [str(a.package_name) for a in ar.get_assets_by_path(d, recursive=True)]
        out.append(('目录 ' + d, sorted(set(pk))))
    return out


def folder(pkg):
    parts = pkg.split('/')
    if len(parts) > 3 and parts[1] == 'Game':
        return '/'.join(parts[:4]) if parts[2] in ('Env', 'Maps') and len(parts) > 4 else '/'.join(parts[:3])
    return '/'.join(parts[:3])


def size_of(pkg):
    if not pkg.startswith('/Game/'):
        return 0
    base = f'{PROJ}/Content/{pkg[len("/Game/"):]}'
    n = 0
    for ext in ('.uasset', '.umap', '.ubulk', '.uexp'):
        if os.path.exists(base + ext):
            n += os.path.getsize(base + ext)
    return n


used = {}  # 包 → 入口集合
for name, starts in roots():
    seen = set(starts)
    stack = list(starts)
    while stack:
        p = stack.pop()
        for d in ar.get_dependencies(p, opts) or []:
            d = str(d)
            if d.startswith('/Script/') or d in seen:
                continue
            seen.add(d)
            stack.append(d)
    for p in seen:
        used.setdefault(p, set()).add(name)
    log(f'{name}：{len(seen)} 个包')

folders = {}
for p, who in used.items():
    f = folder(p)
    e = folders.setdefault(f, {'n': 0, 'bytes': 0, 'who': set()})
    e['n'] += 1
    e['bytes'] += size_of(p)
    e['who'] |= who
log(f'合计 {len(used)} 个包，{len(folders)} 个来源文件夹（按大小）：')
for f, e in sorted(folders.items(), key=lambda kv: -kv[1]['bytes']):
    maps = sorted(w[2:] for w in e['who'] if w.startswith('图 '))
    dirs = sorted(w[3:] for w in e['who'] if w.startswith('目录 '))
    log(f'  {f} | {e["n"]} 包 {e["bytes"] / 1e6:.0f} MB | 图：{",".join(maps) or "-"} | 目录：{",".join(dirs) or "-"}')
os.makedirs(OUT, exist_ok=True)
json.dump({p: sorted(w) for p, w in sorted(used.items())}, open(f'{OUT}/audit.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
# 工程里有、但不进包的顶层文件夹（可以直接不管 / 以后清理）
all_top = sorted({folder(str(a.package_name)) for a in ar.get_assets_by_path('/Game', recursive=True)})
unused = [f for f in all_top if f not in folders]
log(f'不进包的文件夹 {len(unused)} 个：', ', '.join(unused)[:1500])
log('完整清单', f'{OUT}/audit.json')
