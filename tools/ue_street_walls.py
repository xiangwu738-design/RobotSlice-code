"""赛博朋克街道 L_Street 加隐形墙（11-02 玩家：「赛博朋克街道加空气墙，防止掉下去摔死」）。
地面是一整块大平面（SM_196_Plane，X -6040..7331、Y -12785..11468，顶面 -5 厘米），平面外面什么都没有，机体从边缘冲出去或被打飞就掉下去。
在平面四边往里 1 米处立四面墙（BlockingVolume，只挡 Pawn：对别的通道全忽略，子弹、镜头、导弹、AI 视线照常穿过），高 125 米（飞行也出不去）。
可以重复跑：先删带 StreetWall 标签的旧墙再建。存图之前先把 L_Street.umap 备份到 D:/归档/RobotSlice_街道空气墙_20261102/（本脚本不备份，手动先做）。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_street_walls.py" -unattended -nosplash -AllowCommandletRendering
"""
import unreal

MAP = '/Game/Maps/L_Street'
TAG = 'StreetWall'
X0, X1 = -6040.0, 7331.0
Y0, Y1 = -12785.0, 11468.0
INSET = 100.0   # 墙内侧面离平面边缘多远
THICK = 400.0
Z0, Z1 = -500.0, 12000.0

unreal.EditorLevelLibrary.load_level(MAP)
for a in unreal.EditorLevelLibrary.get_all_level_actors():
    if TAG in [str(t) for t in a.tags]:
        unreal.EditorLevelLibrary.destroy_actor(a)

cz = (Z0 + Z1) / 2
hz = (Z1 - Z0) / 2
# (名字, 中心 x, 中心 y, 半长 x, 半长 y)
mx0, mx1, my0, my1 = X0 + INSET, X1 - INSET, Y0 + INSET, Y1 - INSET
walls = [
    ('West', mx0 - THICK / 2, (my0 + my1) / 2, THICK / 2, (my1 - my0) / 2 + THICK),
    ('East', mx1 + THICK / 2, (my0 + my1) / 2, THICK / 2, (my1 - my0) / 2 + THICK),
    ('South', (mx0 + mx1) / 2, my0 - THICK / 2, (mx1 - mx0) / 2 + THICK, THICK / 2),
    ('North', (mx0 + mx1) / 2, my1 + THICK / 2, (mx1 - mx0) / 2 + THICK, THICK / 2),
]
for name, x, y, hx, hy in walls:
    v = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.BlockingVolume, unreal.Vector(x, y, cz))
    v.set_actor_label(f'StreetWall_{name}')
    v.tags = [TAG]
    o, e = v.get_actor_bounds(False)
    unreal.log(f'[streetwall] {name} 默认体积半尺寸 ({e.x:.0f},{e.y:.0f},{e.z:.0f})')
    if e.x > 1 and e.y > 1 and e.z > 1:
        # 默认体积是一个立方体：按半尺寸换算缩放
        v.set_actor_scale3d(unreal.Vector(hx / e.x, hy / e.y, hz / e.z))
    bc = v.get_component_by_class(unreal.BrushComponent)
    if bc:
        # 只挡 Pawn
        bc.set_collision_response_to_all_channels(unreal.CollisionResponseType.ECR_IGNORE)
        bc.set_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN, unreal.CollisionResponseType.ECR_BLOCK)
    o, e = v.get_actor_bounds(False)
    unreal.log(f'[streetwall] {name} 缩放后中心 ({o.x:.0f},{o.y:.0f},{o.z:.0f}) 半尺寸 ({e.x:.0f},{e.y:.0f},{e.z:.0f})')

if unreal.EditorLevelLibrary.save_current_level():
    unreal.log('[streetwall] 已存图')
else:
    unreal.log_warning('[streetwall] 存图失败')
