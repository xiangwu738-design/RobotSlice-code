"""列出特效课程包里各粒子资源的类型（Niagara / Cascade）和用户参数，挑能用的。"""
import unreal
eas = unreal.EditorAssetLibrary
for folder in ('Course_04/Particles', 'Course_00/Particle', 'Course_01/Particles/Ex_01_Sword_Trail', 'Course_01/Particles/Ex_07_Fire_Explosion',
               'Course_01/Particles/Ex_02_Earth_Explosion', 'Course_03/Particles', 'Course_02/Particle'):
    for p in eas.list_assets(f'/Game/AAAVFX/{folder}', recursive=True, include_folder=False):
        a = eas.load_asset(p)
        extra = ''
        if isinstance(a, unreal.NiagaraSystem):
            try:
                extra = str([str(v.get_editor_property('name')) for v in a.get_exposed_parameters().get_editor_property('user_parameters')])[:150]
            except Exception as e:
                extra = ''
        unreal.log(f'[vfx] {p.split(".")[0].replace("/Game/AAAVFX/", "")} {a.get_class().get_name()} {extra}')
