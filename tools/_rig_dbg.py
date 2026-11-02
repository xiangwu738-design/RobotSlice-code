import unreal
unreal.EditorLoadingAndSavingUtils.load_map('/Game/Maps/L_Alley')
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in sub.get_all_level_actors():
    cls = a.get_class().get_name()
    if 'Lighting' in cls or 'Fog' in cls or 'Sky' in cls:
        comps = [c.get_class().get_name() + ':' + c.get_name() for c in a.get_components_by_class(unreal.ActorComponent)]
        unreal.log(f'[rig] {cls} {a.get_actor_label()} -> {comps}')
        for c in a.get_components_by_class(unreal.ExponentialHeightFogComponent):
            unreal.log(f'[rig]   fog density={c.get_editor_property("fog_density")} falloff={c.get_editor_property("fog_height_falloff")} volumetric={c.get_editor_property("volumetric_fog")}')
