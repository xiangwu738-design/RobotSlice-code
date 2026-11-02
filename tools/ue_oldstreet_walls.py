"""老街：把活动范围收窄到主街上（11-02 用户：限制玩家行动在这条街上）。不重搭地图，只改已有的东西：
- 两侧隐形墙 Wall_Left / Wall_Right（标签 OldStreetWall）挪到两排楼正面那条线（地图 Y = ±HALF_Y 米），横路、楼后、楼顶都进不去；
- MechArena / OldStreetBounds 点的 HalfY 标签跟着改（AI 出界判断、小地图）；
- 出生点（TeamSpawn0/1、DMSpawn）在主街外面的挪回街里（Y 夹到 ±(HALF_Y - 2.5) 米）。
两排楼正面：tools/oldstreet_layout.py 的 FRONT_W / FRONT_E（原包 -5.5 / 6.5，中线 XC 0.5，放大 1.3）→ 地图 Y = ±7.8 米。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_oldstreet_walls.py" -AllowCommandletRendering -RenderOffscreen -unattended -nosplash
"""
import unreal

DST_MAP = '/Game/Maps/L_OldStreet'
HALF_Y = 7.6          # 米：比楼正面（7.8）往里一点，机体蹭不到楼面上的招牌
SPAWN_PAD = 2.5       # 出生点离墙至少这么远
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
log = lambda *a: unreal.log_warning('[oswalls] ' + ' '.join(str(x) for x in a))

unreal.EditorLoadingAndSavingUtils.load_map(DST_MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
for a in sub.get_all_level_actors():
    tags = [str(t) for t in a.tags]
    label = a.get_actor_label()
    if 'OldStreetWall' in tags:
        loc = a.get_actor_location()
        sign = 1.0 if loc.y > 0 else -1.0
        thick = a.get_actor_scale3d().y * 100.0
        new = unreal.Vector(loc.x, sign * (HALF_Y * 100.0 + thick / 2.0), loc.z)
        a.set_actor_location(new, False, False)
        log('墙', label, 'Y', round(loc.y / 100, 2), '→', round(new.y / 100, 2), '米')
    elif 'MechArena' in tags or 'OldStreetBounds' in tags:
        a.tags = [unreal.Name(f'HalfY={HALF_Y * 100:.0f}') if t.startswith('HalfY=') else unreal.Name(t) for t in tags]
        log('范围点', label, [str(t) for t in a.tags])
    elif 'DMSpawn' in tags or 'TeamSpawn0' in tags or 'TeamSpawn1' in tags:
        loc = a.get_actor_location()
        lim = (HALF_Y - SPAWN_PAD) * 100.0
        if abs(loc.y) > lim:
            new = unreal.Vector(loc.x, max(-lim, min(lim, loc.y)), loc.z)
            a.set_actor_location(new, False, False)
            log('出生点挪回街里', label, 'Y', round(loc.y / 100, 2), '→', round(new.y / 100, 2))
        else:
            log('出生点', label, 'Y', round(loc.y / 100, 2), '不用挪')
unreal.EditorLoadingAndSavingUtils.save_map(world, DST_MAP)
log('已存')
