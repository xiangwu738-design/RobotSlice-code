"""赛博朋克街道 L_Street 的边缘调查（只读，不存图）：物体包围盒总范围、出生点、俯视地面高度图（逐格向下打射线，有没有地面）。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_street_walls_survey.py" -unattended -nosplash -AllowCommandletRendering
输出看日志里的 [streetwall]。"""
import unreal

unreal.EditorLevelLibrary.load_level('/Game/Maps/L_Street')
world = unreal.EditorLevelLibrary.get_editor_world()
actors = unreal.EditorLevelLibrary.get_all_level_actors()
lo = [1e9, 1e9, 1e9]
hi = [-1e9, -1e9, -1e9]
n = 0
for a in actors:
    if not isinstance(a, unreal.StaticMeshActor):
        continue
    o, e = a.get_actor_bounds(False)
    if e.x > 60000:  # 天空球之类
        continue
    n += 1
    for i, (ov, ev) in enumerate(((o.x, e.x), (o.y, e.y), (o.z, e.z))):
        lo[i] = min(lo[i], ov - ev)
        hi[i] = max(hi[i], ov + ev)
unreal.log(f'[streetwall] 静态网格 {n} 个，包围盒 X {lo[0]:.0f}..{hi[0]:.0f} Y {lo[1]:.0f}..{hi[1]:.0f} Z {lo[2]:.0f}..{hi[2]:.0f}')
for a in actors:
    nm = a.get_class().get_name()
    if 'PlayerStart' in nm or 'TeamSpawn' in nm or 'Bounds' in nm or 'Volume' in nm or 'Spawn' in a.get_actor_label():
        p = a.get_actor_location()
        unreal.log(f'[streetwall] {nm} {a.get_actor_label()} ({p.x:.0f},{p.y:.0f},{p.z:.0f})')

STEP = 500.0
x0, x1 = int(lo[0] // STEP * STEP) - 1000, int(hi[0] // STEP * STEP) + 1000
y0, y1 = int(lo[1] // STEP * STEP) - 1000, int(hi[1] // STEP * STEP) + 1000
# 逐个网格的包围盒盖到格子上：# = 有顶面在 -1.5..3 米之间的东西（路面、地面），^ = 只有更高的东西，. = 空
boxes = []
for a in actors:
    if not isinstance(a, unreal.StaticMeshActor):
        continue
    o, e = a.get_actor_bounds(False)
    if e.x > 60000:
        continue
    boxes.append((o.x - e.x, o.x + e.x, o.y - e.y, o.y + e.y, o.z - e.z, o.z + e.z, e.x * e.y))
    if e.x * e.y > 4000000 and o.z + e.z < 500:
        unreal.log(f'[streetwall] 大面 {a.get_actor_label()} X {o.x - e.x:.0f}..{o.x + e.x:.0f} Y {o.y - e.y:.0f}..{o.y + e.y:.0f} 顶 {o.z + e.z:.0f}')
rows = []
for y in range(y1, y0 - 1, -int(STEP)):
    line = ''
    for x in range(x0, x1 + 1, int(STEP)):
        ground = False
        high = False
        for b in boxes:
            if b[0] <= x <= b[1] and b[2] <= y <= b[3]:
                if b[5] < 400 and b[4] > -400:
                    ground = True
                elif b[5] >= 400:
                    high = True
        line += '#' if ground else ('^' if high else '.')
    rows.append(f'{y:6d} {line}')
unreal.log(f'[streetwall] 俯视图（每格 {STEP:.0f} 厘米，X 从 {x0} 起，# 有低矮的面、^ 只有高的东西、. 空）')
for r in rows:
    unreal.log('[streetwall] ' + r)
