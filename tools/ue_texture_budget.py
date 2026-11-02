"""查一张地图用到的贴图占多少（按资产注册表里的尺寸估算，不加载贴图），可选给远景贴图降分辨率。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_texture_budget.py report /Game/Maps/L_Nature"
                                                         ... apply /Game/Maps/L_Nature
apply：远景废墟（/Game/Env/Ruins）和风化叠层（/Game/Env/Nature/Textures）限到 1024；自然环境包、山谷包（/Game/Env/Valley/Assets）植被石头 4K 的贴图限到 2048。
起因：8G 显存的本子上贴图池只给 700MB，废墟挂上原贴图后超了 831MB（屏幕上红字 TEXTURE STREAMING POOL OVER）"""
import sys
import unreal

args = [a for a in sys.argv[1:] if not a.startswith('-')]
mode = args[0] if args else 'report'
level = args[1] if len(args) > 1 else '/Game/Maps/L_Nature'
reg = unreal.AssetRegistryHelpers.get_asset_registry()
opts = unreal.AssetRegistryDependencyOptions(include_soft_package_references=False, include_hard_package_references=True,
                                             include_searchable_names=False, include_soft_management_references=False,
                                             include_hard_management_references=False)

# 地图 → 网格 → 材质 → 贴图：沿硬引用一路走下去
seen, todo, textures = set(), [level], {}
while todo:
    pkg = todo.pop()
    if pkg in seen or pkg.startswith('/Engine') or pkg.startswith('/Script'):
        continue
    seen.add(pkg)
    for d in reg.get_assets_by_package_name(pkg) or []:
        if str(d.asset_class_path.asset_name) in ('Texture2D', 'TextureCube'):
            dims = str(d.get_tag_value('Dimensions') or '')
            fmt = str(d.get_tag_value('Format') or '')
            textures[pkg] = (dims, fmt)
    for dep in reg.get_dependencies(pkg, opts) or []:
        todo.append(str(dep))


def mib(dims, fmt):
    try:
        w, h = [int(v) for v in dims.split('x')[:2]]
    except ValueError:
        return 0.0
    bpp = 1.0 if ('DXT5' in fmt or 'BC5' in fmt or 'BC7' in fmt) else 0.5 if ('DXT1' in fmt or 'BC4' in fmt) else 4.0
    return w * h * bpp * 1.333 / 1048576.0  # 含 mip


rows = sorted(((mib(*v), k, v[0], v[1]) for k, v in textures.items()), reverse=True)
total = sum(r[0] for r in rows)
by_dir = {}
for m, k, d, f in rows:
    top = '/'.join(k.split('/')[:4])
    by_dir[top] = by_dir.get(top, 0.0) + m
unreal.log(f'[texbudget] {level} 用到贴图 {len(rows)} 张，满分辨率约 {total:.0f} MiB')
for top, m in sorted(by_dir.items(), key=lambda x: -x[1])[:12]:
    unreal.log(f'[texbudget]   {top}: {m:.0f} MiB')
for m, k, d, f in rows[:25]:
    unreal.log(f'[texbudget]   {m:6.1f} MiB  {d:>10} {f:<14} {k}')

if mode == 'apply':
    changed = 0
    saved_mib = 0.0
    for m, k, d, f in rows:
        if k.startswith('/Game/Env/Ruins') or k.startswith('/Game/Env/Nature/Textures'):
            cap = 1024   # 远景废墟、叠层：离对战区一两百米，1K 足够
        elif k.startswith('/Game/Environments/AndreyPestryakov') or k.startswith('/Game/Env/Valley/Assets'):
            cap = 2048   # 植被、石头（自然环境包、山谷包）：机体视角下都不大
        else:
            continue
        try:
            w = max(int(v) for v in d.split('x')[:2])
        except ValueError:
            continue
        if w <= cap:
            continue
        tex = unreal.load_asset(k)
        if not isinstance(tex, unreal.Texture2D):
            continue
        tex.set_editor_property('max_texture_size', cap)
        if unreal.EditorAssetLibrary.save_loaded_asset(tex, False):
            changed += 1
            saved_mib += m * (1.0 - (float(cap) / w) ** 2)
        else:
            unreal.log_warning(f'[texbudget] 存不进 {k}（游戏开着？）')
    unreal.log(f'[texbudget] 限了分辨率的贴图 {changed} 张，约省 {saved_mib:.0f} MiB（按未压缩估）')
