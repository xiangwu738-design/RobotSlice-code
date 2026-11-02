"""在 RobotSlice 工程里跑，修游戏里能看出来的卡顿源：
  materials —— 街道材质 / 曳光弹材质缺“用于 Nanite / 实例化网格”的标记：每次启动游戏都在运行中现编译着色器、编好前显示默认材质。给母材质打上标记并重存。
  niagara   —— 特效包里的特效每次启动第一次放时都要现编译（开枪、爆炸那一下卡 0.1–0.25 秒）。重存一遍让编译结果存进资源。
  lights    —— 列出街道地图里投影的灯（48 盏灯默认全都投影，渲染线程要为每盏灯重画一遍场景）；加 apply 时只留够亮且照得到场地的灯投影。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_fix_perf.py materials niagara lights [apply]" -AllowCommandletRendering -RenderOffscreen
"""
import sys

import unreal

ARGS = sys.argv[1:]
eal = unreal.EditorAssetLibrary
ARENA = (-1300.0, 3100.0, 1300.0, 11300.0)  # 街道对战场地 X、Y 范围（MechGameMode::ComputeArena）

MATERIALS = {
    'used_with_nanite': ['/Game/Env/Street/Materials/MI_St_' + n for n in (
        'MT_Wall', 'MT_Wall_2', 'MT_Metall', 'MT_Metall_2', 'MT_Metall_Yellow', 'MT_Fasade', 'MT_Plast', 'MT_Magazin', 'Cyber', 'Cyber_2',
        'Emmision', 'Emmision_2', 'Emmision_3', 'Emmision_001', 'Black', '3x_MT_Concrete', '3x_MT_Concrete_2', '3x_MT_Tiles')] +
    ['/Game/Env/Materials/MI_Asphalt'],
    'used_with_instanced_static_meshes': ['/Game/Weapons/Materials/MI_Tracer'],
}
NIAGARA = [
    '/Game/AAAVFX/Course_04/Particles/Ex_03_Muzzle', '/Game/AAAVFX/Course_04/Particles/Ex_03_Hit_01',
    '/Game/AAAVFX/Course_01/Particles/Ex_01_Sword_Trail/Trail_01', '/Game/AAAVFX/Course_04/Particles/Ex_02_Exp_02',
    '/Game/AAAVFX/Course_04/Particles/Ex_02_Exp_01', '/Game/AAAVFX/Course_00/Particle/Ex_02_Projectile/Projectile_Exp_01',
    '/Game/AAAVFX/Course_01/Particles/Ex_02_Earth_Explosion/Exp_01',
]

if 'materials' in ARGS:
    for flag, paths in MATERIALS.items():
        bases = {}
        for p in paths:
            mi = unreal.load_asset(p)
            if not mi:
                unreal.log_warning(f'[perf] 找不到 {p}')
                continue
            base = mi.get_base_material()
            bases[base.get_path_name()] = base
        for path, base in bases.items():
            if not base.get_editor_property(flag):
                base.set_editor_property(flag, True)
                unreal.MaterialEditingLibrary.recompile_material(base)
            eal.save_loaded_asset(base, False)
            unreal.log(f'[perf] 母材质 {path} 打上 {flag}')
        for p in paths:
            mi = unreal.load_asset(p)
            if mi:
                eal.save_loaded_asset(mi, False)
        unreal.log(f'[perf] 重存材质实例 {len(paths)} 个')

if 'niagara' in ARGS:
    for p in NIAGARA:
        s = unreal.load_asset(p)
        if s:
            eal.save_loaded_asset(s, False)
            unreal.log(f'[perf] 重存特效 {p}')

if 'lights' in ARGS:
    unreal.EditorLoadingAndSavingUtils.load_map('/Game/Maps/L_Street')
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    rows = []
    for a in sub.get_all_level_actors():
        for lc in a.get_components_by_class(unreal.LocalLightComponent):
            r = lc.get_editor_property('attenuation_radius')
            inten = lc.get_editor_property('intensity')
            rows.append((inten * r * r, a, lc, r, inten))
    rows.sort(key=lambda x: -x[0])
    shadow = sum(1 for x in rows if x[2].get_editor_property('cast_shadows'))
    unreal.log(f'[perf] 局部灯 {len(rows)} 盏，投影的 {shadow} 盏')
    for i, (score, a, lc, r, inten) in enumerate(rows):
        unreal.log(f'[perf]   {i:2d} {a.get_actor_label():32s} {type(lc).__name__:20s} 强度 {inten:8.0f} 范围 {r:6.0f} '
                   f'投影 {lc.get_editor_property("cast_shadows")} 位置 {a.get_actor_location()}')
    if 'apply' in ARGS:
        # 只有够亮（强度 ≥ 1000）且照得到对战场地（X -13~31 米、Y 13~113 米、高 0~12 米）的灯投影，其余霓虹 / 店招小灯不投影
        kept = []
        for score, a, lc, r, inten in rows:
            p = a.get_actor_location()
            dx = max(ARENA[0] - p.x, 0.0, p.x - ARENA[1])
            dy = max(ARENA[2] - p.y, 0.0, p.y - ARENA[3])
            dz = max(p.z - 1200.0, 0.0)
            keep = inten >= 1000 and (dx * dx + dy * dy + dz * dz) ** 0.5 < r
            lc.set_editor_property('cast_shadows', keep)
            if keep:
                kept.append(a.get_actor_label())
        world = unreal.EditorLevelLibrary.get_editor_world()
        unreal.EditorLoadingAndSavingUtils.save_map(world, '/Game/Maps/L_Street')
        unreal.log(f'[perf] 投影的灯从 {len(rows)} 盏减到 {len(kept)} 盏：{kept}，已存 L_Street')
