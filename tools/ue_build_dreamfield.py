"""梦核旷野图 /Game/Maps/L_Dreamfield：一望无际的草地 + 从草地里拔地而起的巨构墙（科幻峡谷包只取一面）+ 墙脚的二级平台 + 粉蓝天空。
用户 10-31：「保卫战模式的正式地图方向是巨构防御工事」「我的想法是只取峡谷一边，在下面做一个二级平台，岩壁放在一个旷野地图上」
「找个梦核风格的旷野，和这个巨构结合吧」。先不放堡垒（以后做），草地是占位（用户下载的草地素材到了再换）。
整张图脚本生成、可以重复跑：图已存在就删掉带 Dreamfield 标签的东西再摆；不存在就新建空关卡。不用 L_LightingBase（那套 LightingDirector
会被 GameMode 按地图名改时间天气），天空、太阳、雾、后期都是这张图自己的。
先跑 tools/ue_dreamfield_import.py（网格、材质）。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_build_dreamfield.py" -AllowCommandletRendering -RenderOffscreen -unattended

布局（米，UE 坐标，地面 z≈0）：
- 墙面（碰撞平面）在 X=+300，朝 -X；旷野在 -X 那边。墙顶甲板高 200 米，沿 Y 铺 ±2.7 公里（两头埋进雾里）。
- 平台（11-01 第二版）：X 190~306（进深 110）、Y -225~225（长 450）、顶面 24 米；两头各一条贴墙的坡道（120 米长、11°）下到草地；
  正面两部电梯（AMechElevator），墙上平台高度处三扇封锁的大门。尺寸都在下面的 PLAT / RAMP / ELEV / GATES。
- 活动范围 X -300~300、Y -450~450，四周隐形墙、头顶 130 米封顶；+X 那边就是墙本身的碰撞。
"""
import json
import math
import random
import sys

import unreal

sys.path.append('D:/RobotSlice/tools')
import dreamfield_terrain as terrain  # noqa: E402

DST = '/Game/Maps/L_Dreamfield'
DEST = '/Game/Env/Dreamfield'
TAG = 'Dreamfield'
WALL_META = json.load(open('D:/RobotSlice/Inbox/_dreamwall/export/dreamwall_wall.json', encoding='utf-8'))
CLOUD_META = json.load(open('D:/RobotSlice/Inbox/_dreamwall/export/dreamwall_clouds.json', encoding='utf-8'))

S = 1.2              # 墙的缩放：峡谷包原尺度下楼柱群约 25 米宽、挂下来 176 米；机体 2.4 米比真人大一圈，放大 1.2 倍细节读得清、也更压迫
WALL_X = 300.0       # 墙面碰撞平面
H = 200.0            # 甲板顶离草地的高度
DROP2 = 70.0 * S     # 第二层楼柱群往下挪的距离（填满墙脚到中段，楼柱底埋进草地）
WALL_HALF = 2700.0   # 墙沿 Y 铺多远（两头都埋进雾里）
# 第二版（11-01，用户「这个平台有点小，而且有点低」）：70×300×12 → 110 深 × 450 长 × 24 高。
# 24 米 = 10 倍机体高：从旷野看平台是一道真正的城墙基座，机体喷射也要用掉一截能量才上得去，平时走电梯 / 坡道；
# 进深 110 米放得下堡垒本体加前后的活动空间，450 米长能放两翼的副堡。坡道 120 米长、坡度还是 11°（炮车能爬），贴着墙往两头落。
PLAT = {'x0': WALL_X - 110.0, 'x1': WALL_X + 6.0, 'y0': -225.0, 'y1': 225.0, 'top': 24.0}
RAMP = {'len': 120.0, 'width': 24.0, 'thick': 30.0}
# 电梯（AMechElevator，主会话写的 C++ 类）：贴平台正面三分之一和三分之二处，轿厢 14 米见方，和立面留 2 米缝（檐口凸出 1.5 米）
ELEV = {'ys': (-75.0, 75.0), 'size': 14.0, 'gap': 2.0, 'move': 9.0, 'stop': 4.0}
# 封锁的大门：墙上平台高度处，中间一扇大门（平台的视觉中心）+ 两边各一扇小门；门扇在楼柱群前面（碰撞平面往外 13–17 米）
GATES = [{'y': 0.0, 'w': 64.0, 'h': 48.0, 'pillar': 10.0, 'lintel': 10.0, 'beams': (0.3, 0.55, 0.8), 'lights': 7},
         {'y': -150.0, 'w': 32.0, 'h': 32.0, 'pillar': 6.0, 'lintel': 6.0, 'beams': (0.35, 0.72), 'lights': 3},
         {'y': 150.0, 'w': 32.0, 'h': 32.0, 'pillar': 6.0, 'lintel': 6.0, 'beams': (0.35, 0.72), 'lights': 3}]
ARENA = {'hx': 300.0, 'hy': 450.0, 'ceil': 130.0}
SUN_YAW, SUN_PITCH = 52.0, -36.0          # 光照方向：太阳在 -X、-Y 那边 36° 高，和墙面法线水平夹 52°：正面和朝太阳的侧面都受光（接近白），
                                          # 背光侧面、楼柱投在衬墙上的影子、悬挑底下只剩很弱的蓝色天光（深蓝灰），中间调少。
                                          # 第三轮（用户「亮面更白、阴影更蓝更深」）；第二轮 70°/-30° 擦墙：正面只有 0.3 的光，整片中灰偏紫；
                                          # 第一版 25°/-35° 几乎正面打光、天光又亮，一片白
SKY_ANGLE = 191.7                         # HDR 天空绕 Z 转多少度，让 HDR 里贴地平线的太阳光晕落在太阳方位（232°）附近
eas = unreal.EditorAssetLibrary
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
rng = random.Random(1031)
log = lambda *a: unreal.log('[dreamfield] ' + ' '.join(str(x) for x in a))
_cache = {}


def asset(path):
    if path not in _cache:
        _cache[path] = eas.load_asset(path) if eas.does_asset_exist(path) else None
        if _cache[path] is None:
            unreal.log_warning(f'[dreamfield] 缺资源 {path}')
    return _cache[path]


def V(x, y, z):
    return unreal.Vector(x * 100.0, y * 100.0, z * 100.0)


def setp(obj, name, value):
    try:
        obj.set_editor_property(name, value)
    except Exception as e:
        log('设不了', obj.get_class().get_name(), name, e)


def tagged(a, folder, label=None, extra=()):
    a.tags = [TAG] + list(extra)
    a.set_folder_path(f'Dreamfield/{folder}')
    if label:
        a.set_actor_label(label)
    return a


def mesh_actor(mesh, loc, rot=None, scale=(1, 1, 1), folder='Misc', label=None, mat=None, collide=False, shadow=True, lumen=True):
    a = sub.spawn_actor_from_class(unreal.StaticMeshActor, loc, rot or unreal.Rotator())
    tagged(a, folder, label)
    a.set_actor_scale3d(unreal.Vector(*scale))
    c = a.static_mesh_component
    c.set_static_mesh(mesh)
    c.set_editor_property('mobility', unreal.ComponentMobility.STATIC)
    if mat:
        for i in range(max(1, len(mesh.get_editor_property('static_materials')))):
            c.set_material(i, mat)
    if not collide:
        # 只设 CollisionEnabled 不够：引擎球 / 方块自带的简单碰撞还在（第一版天空球把整张图包在一个实心球里，射线一打就中）
        c.set_collision_profile_name('NoCollision')
        c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        c.set_editor_property('can_character_step_up_on', unreal.CanBeCharacterBase.ECB_NO)
    if not shadow:
        c.set_editor_property('cast_shadow', False)
    if not lumen:   # 天空球、行星这种几十公里外的大东西：不进 Lumen 场景 / 距离场，不进光追
        setp(c, 'affect_distance_field_lighting', False)
        setp(c, 'affect_dynamic_indirect_lighting', False)
        setp(c, 'visible_in_ray_tracing', False)
    return a


def box(name, c, size, folder, mat=None, collide=True, visible=True, shadow=True, yaw=0.0, rot=None):
    """引擎 1 米方块（自带方块碰撞），c = 中心（米），size = 尺寸（米）"""
    cube = asset('/Engine/BasicShapes/Cube')
    a = mesh_actor(cube, V(*c), rot or unreal.Rotator(roll=0, pitch=0, yaw=yaw), size, folder, name, mat, collide, shadow)
    if not visible:
        a.set_actor_hidden_in_game(True)
        comp = a.static_mesh_component
        comp.set_editor_property('visible', False)
        setp(comp, 'affect_distance_field_lighting', False)
        setp(comp, 'affect_dynamic_indirect_lighting', False)
    return a


def ism_actor(label, mesh, mat, transforms, folder, hism=False, shadow=True, cull=None):
    """同一个网格摆很多份：一个 Actor 上挂实例化组件（InstancedStaticMeshComponent；草花用分层的 HISM，按距离剔除）。
    拿不到组件就退回一份一个 Actor（草花不退回：几万个 Actor 不现实）"""
    try:
        a = sub.spawn_actor_from_class(unreal.Actor, unreal.Vector(0, 0, 0))
        sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
        root = sds.k2_gather_subobject_data_for_instance(a)[0]
        cls = unreal.HierarchicalInstancedStaticMeshComponent if hism else unreal.InstancedStaticMeshComponent
        params = unreal.AddNewSubobjectParams(parent_handle=root, new_class=cls, blueprint_context=None)
        h, fail = sds.add_new_subobject(params)
        if fail and str(fail):
            raise RuntimeError(str(fail))
        data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h)
        comp = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(data)
        comp.set_static_mesh(mesh)
        comp.set_editor_property('mobility', unreal.ComponentMobility.STATIC)
        if mat:
            for i in range(max(1, len(mesh.get_editor_property('static_materials')))):
                comp.set_material(i, mat)
        comp.set_collision_profile_name('NoCollision')
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        if not shadow:
            comp.set_editor_property('cast_shadow', False)
        if cull:
            setp(comp, 'instance_start_cull_distance', int(cull[0] * 100))
            setp(comp, 'instance_end_cull_distance', int(cull[1] * 100))
        comp.add_instances(transforms, False, True)
        tagged(a, folder, label)
        n = comp.get_instance_count()
        log('实例化', label, n, '份')
        return a
    except Exception as e:
        log('实例化组件失败，退回一份一个 Actor：', e)
        try:
            sub.destroy_actor(a)
        except Exception:
            pass
        if hism:
            return None
        for i, t in enumerate(transforms):
            mesh_actor(mesh, t.translation, t.rotation.rotator(), (t.scale3d.x, t.scale3d.y, t.scale3d.z), folder, f'{label}_{i:03d}', mat)
        return None


def xf(loc, rot=None, scale=(1, 1, 1)):
    return unreal.Transform(loc, rot or unreal.Rotator(), unreal.Vector(*scale))


# ---------------------------------------------------------------- 关卡：已有就清掉自己摆的，没有就新建
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if eas.does_asset_exist(DST):
    unreal.EditorLoadingAndSavingUtils.load_map(DST)
    n = 0
    for a in sub.get_all_level_actors():
        if TAG in [str(t) for t in a.tags]:
            sub.destroy_actor(a)
            n += 1
    log('重建：删掉旧的', n, '个')
else:
    assert les.new_level(DST), '新建关卡失败'
    log('新建', DST)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
world.get_world_settings().set_editor_property('default_game_mode', unreal.load_class(None, '/Script/RobotSlice.MechGameMode'))

MI = {k: asset(f'{DEST}/Materials/{k}') for k in ('MI_DW_Wall', 'MI_DW_Platform', 'MI_DW_Deck', 'MI_DW_Backing', 'MI_DF_Grass', 'MI_DF_GrassBlades', 'MI_DF_Flowers', 'MI_DF_Sky', 'MI_DF_Planet',
                                                  'MI_DF_Moon', 'MI_DF_Cloud', 'MI_DW_Hazard', 'MI_DW_LightRed', 'MI_DW_LightAmber', 'MI_DW_Steel',
                                                  'MI_DW_GateLeaf')}
M = lambda n: asset(f'{DEST}/Meshes/{n}')

# ---------------------------------------------------------------- 地面
mesh_actor(M('SM_DF_GroundNear'), V(0, 0, 0), folder='Ground', label='GroundNear', mat=MI['MI_DF_Grass'], collide=True)
mesh_actor(M('SM_DF_GroundFar'), V(0, 0, 0), folder='Ground', label='GroundFar', mat=MI['MI_DF_Grass'], shadow=False)

# ---------------------------------------------------------------- 草和白花（StyleHex 免费草花包，用户 10-31 下载）
# 梦核要「短而均匀、亮的草坪，少量白花点缀」：草丛按 2.2 米的抖动网格铺满场地（外加 60 米边），放大、压矮一点（机体 2.4 米，
# 原大 40–65 厘米的草丛几乎看不见；第二版分高草片 / 矮草坪，见下面）；白花按低频噪声聚成一片片。草花不碰撞、不投影，90 米开始淡出、120 米剔除（远处靠地面草色接上）。
# 用户也下了 Megascans Kikuyu Grass（写实草片，Inbox/梦核旷野）：草片本身不种（面数和透明叠加都重、和卡通云 / 动漫草不搭），
# 第二版拿它的草叶图集撒成地面的草坪贴图（tools/dreamfield_lawn_textures.py → M_DF_Grass），近处地面有一根根草叶的细节。
GRASS_ROOT = '/Game/StyleHex_Studio/Free_Packs/FREE_Stylized_Grass_Flowers_Foliage/Meshes'
GRASS_STEP = 2.2
GRASS_BOX = (-360.0, 296.0, -510.0, 510.0)


def ground_h(x, y):
    """地面高度（米）：和 Blender 生成地面用的是同一个函数（tools/dreamfield_terrain.py）"""
    return terrain.height(x, y)


def on_structure(x, y, pad=1.5):
    """平台、坡道、墙脚上不长草"""
    p = PLAT
    if x > p['x0'] - pad - 2.0 and p['y0'] - pad < y < p['y1'] + pad:
        return True
    if x > WALL_X - RAMP['width'] - 2.0 - pad and (p['y1'] - pad < y < p['y1'] + RAMP['len'] + pad or p['y0'] - RAMP['len'] - pad < y < p['y0'] + pad):
        return True
    for ey in ELEV['ys']:      # 电梯井
        if x > p['x0'] - ELEV['gap'] - ELEV['size'] - 4.0 - pad and abs(y - ey) < ELEV['size'] / 2 + 3.0 + pad:
            return True
    return x > WALL_X - 4.0


def white_share(mesh):
    """花丛用的是一张草花图集：按 UV 落在图集里白色雏菊格子的比例挑「白花」那几丛（图集坐标是看 T_Foliage_Atlas_BC 量的）"""
    white = [(0.64, 0.79, 0.155, 0.305), (0.825, 0.98, 0.16, 0.32), (0.03, 0.105, 0.36, 0.41), (0.18, 0.245, 0.45, 0.515)]
    colored = [(0.64, 0.79, 0.0, 0.155), (0.82, 0.98, 0.0, 0.16), (0.37, 0.42, 0.02, 0.15), (0.46, 0.61, 0.01, 0.27),
               (0.19, 0.31, 0.04, 0.15), (0.04, 0.31, 0.68, 0.78)]
    try:
        sec = unreal.ProceduralMeshLibrary.get_section_from_static_mesh(mesh, 0, 0)
        uvs = sec[3]
    except Exception as e:
        log('读不到 UV', mesh.get_name(), e)
        return None
    inside = lambda u, v, boxes: any(b[0] <= u <= b[1] and b[2] <= v <= b[3] for b in boxes)
    w = sum(1 for t in uvs if inside(t.x % 1.0, t.y % 1.0, white))
    c = sum(1 for t in uvs if inside(t.x % 1.0, t.y % 1.0, colored))
    return w, c, len(uvs)


grass_meshes = [asset(f'{GRASS_ROOT}/SM_Fol_Grass_Clump_{i}') for i in (1, 2, 3)]
grass_meshes = [m for m in grass_meshes if m]
flower_meshes = []
for i in (1, 2, 3, 4):
    m = asset(f'{GRASS_ROOT}/SM_Fol_Flower_Clump_{i}')
    if not m:
        continue
    ws = white_share(m)
    log('花丛', m.get_name(), '白 / 彩色 / 总 UV 点', ws)
    if ws is None or ws[0] >= ws[1]:
        flower_meshes.append(m)
if not flower_meshes:
    flower_meshes = [m for m in (asset(f'{GRASS_ROOT}/SM_Fol_Flower_Clump_{i}') for i in (1, 3)) if m]
plant_meshes = [m for m in (asset(f'{GRASS_ROOT}/SM_Fol_Plant_{i}') for i in (19, 9, 4)) if m]


def patch(x, y, a=41.0, b=53.0, c=71.0, o=0.0):
    """低频起伏的 0–1 噪声（草长得高 / 密的地方一片一片的）"""
    return 0.5 + 0.5 * math.sin(x / a + 0.3 + o) * math.sin(y / b + 1.7 + o) * math.cos((x + y) / c + o)


if grass_meshes and MI.get('MI_DF_GrassBlades'):
    # 第二版（11-01，用户「旷野精细度太低」）：
    # - 主草层：2.2 米抖动网格，按低频噪声分「高草片」（放大 1.9–2.4 倍）和「矮草坪」（1.35–1.75 倍、压矮），稀的地方随机空掉一部分；
    # - 近处细草层：再撒一层小草丛（0.75–1.1 倍），30 米开始淡出、45 米剔除 → 脚边密、远处靠地面的草坪贴图；
    # - 白花：成片的花丛大小不一，另外零星散几丛；
    # - 小株植物（StyleHex 的 Plant 19 / 9 / 4）稀稀拉拉点缀。
    per = {m.get_name(): [] for m in grass_meshes + flower_meshes + plant_meshes}
    near_per = {m.get_name(): [] for m in grass_meshes}
    x0, x1, y0, y1 = GRASS_BOX
    nx, ny = int((x1 - x0) / GRASS_STEP), int((y1 - y0) / GRASS_STEP)
    for ix in range(nx):
        for iy in range(ny):
            x = x0 + (ix + rng.uniform(0.1, 0.9)) * GRASS_STEP
            y = y0 + (iy + rng.uniform(0.1, 0.9)) * GRASS_STEP
            if on_structure(x, y):
                continue
            pz = patch(x, y)
            if pz < 0.3 and rng.random() < 0.45:      # 稀的地方
                continue
            if pz > 0.63:
                s_, zs = rng.uniform(1.9, 2.4), rng.uniform(0.9, 1.1)
            else:      # 矮草坪：和第一版差不多大（第一次缩到 1.15–1.65、压到 0.7，远看草丛没了，一片平绿）
                s_, zs = rng.uniform(1.35, 1.75), rng.uniform(0.75, 0.95)
            m = rng.choice(grass_meshes)
            per[m.get_name()].append(xf(V(x, y, ground_h(x, y) - 0.03), unreal.Rotator(yaw=rng.uniform(0, 360)), (s_, s_, s_ * zs)))
    NEAR_STEP = 2.3
    nx, ny = int((PLAT['x0'] - x0) / NEAR_STEP), int((y1 - y0) / NEAR_STEP)     # 旷野全场（平台顶上不铺）
    for ix in range(nx):
        for iy in range(ny):
            x = x0 + (ix + 0.5 + rng.uniform(-0.4, 0.4)) * NEAR_STEP
            y = y0 + (iy + 0.5 + rng.uniform(-0.4, 0.4)) * NEAR_STEP
            if on_structure(x, y):
                continue
            s_ = rng.uniform(0.75, 1.1)
            m = rng.choice(grass_meshes)
            near_per[m.get_name()].append(xf(V(x, y, ground_h(x, y) - 0.03), unreal.Rotator(yaw=rng.uniform(0, 360)), (s_, s_, s_ * rng.uniform(0.7, 0.9))))
    fl = 0
    area = (x1 - x0) * (y1 - y0)
    for k in range(int(area / 16.0)):          # 成片的白花
        x, y = rng.uniform(x0, x1), rng.uniform(y0, y1)
        pz = patch(x, y, 37.0, 29.0, 53.0, 0.9)
        if pz < 0.7 or rng.random() > (pz - 0.7) * 3.3 or on_structure(x, y, 3.0):
            continue
        s_ = rng.uniform(1.0, 1.9)
        m = rng.choice(flower_meshes)
        per[m.get_name()].append(xf(V(x, y, ground_h(x, y) - 0.02), unreal.Rotator(yaw=rng.uniform(0, 360)), (s_, s_, s_)))
        fl += 1
    for k in range(int(area / 350.0)):         # 零星散的白花
        x, y = rng.uniform(x0, x1), rng.uniform(y0, y1)
        if on_structure(x, y, 3.0):
            continue
        s_ = rng.uniform(0.9, 1.5)
        m = rng.choice(flower_meshes)
        per[m.get_name()].append(xf(V(x, y, ground_h(x, y) - 0.02), unreal.Rotator(yaw=rng.uniform(0, 360)), (s_, s_, s_)))
        fl += 1
    npl = 0
    for k in range(int(area / 220.0)):         # 小株植物
        x, y = rng.uniform(x0, x1), rng.uniform(y0, y1)
        if on_structure(x, y, 3.0) or not plant_meshes:
            continue
        s_ = rng.uniform(1.3, 2.0)
        m = rng.choice(plant_meshes)
        per[m.get_name()].append(xf(V(x, y, ground_h(x, y) - 0.02), unreal.Rotator(yaw=rng.uniform(0, 360)), (s_, s_, s_ * rng.uniform(0.8, 1.1))))
        npl += 1
    for m in grass_meshes:
        ism_actor(f'Grass_{m.get_name()[7:]}', m, MI['MI_DF_GrassBlades'], per[m.get_name()], 'Ground/Grass', hism=True, shadow=False, cull=(90, 120))
        ism_actor(f'GrassNear_{m.get_name()[7:]}', m, MI['MI_DF_GrassBlades'], near_per[m.get_name()], 'Ground/Grass', hism=True, shadow=False,
                  cull=(30, 45))
    for m in flower_meshes:
        ism_actor(f'Flowers_{m.get_name()[7:]}', m, MI['MI_DF_Flowers'], per[m.get_name()], 'Ground/Grass', hism=True, shadow=False, cull=(70, 95))
    for m in plant_meshes:
        ism_actor(f'Plants_{m.get_name()[7:]}', m, MI['MI_DF_Flowers'], per[m.get_name()], 'Ground/Grass', hism=True, shadow=False, cull=(80, 110))
    log('草丛', sum(len(per[m.get_name()]) for m in grass_meshes), '近处细草', sum(len(near_per[m.get_name()]) for m in grass_meshes),
        '白花', fl, '小株植物', npl)
else:
    log('没有 StyleHex 草花包或草的材质实例，只铺地面草色')

# ---------------------------------------------------------------- 巨构墙：两层楼柱群（上层贴着甲板、下层往下挪 DROP2 并错开半段），隔一段镜像一次
wm = WALL_META['meshes']['SM_DW_WallDetail']
unit = (wm['max'][1] - wm['min'][1]) * S
count = int(math.ceil(2 * WALL_HALF / unit))
n_wall = 0
for layer, (z, shift) in enumerate(((H, 0.0), (H - DROP2, 0.37))):
    for k in range(count + layer):
        y = -WALL_HALF + (k + 0.5 - shift) * unit
        mirror = (k + layer) % 2 == 1
        mesh_actor(M('SM_DW_WallDetail'), V(WALL_X, y, z), scale=(S, -S if mirror else S, S), folder=f'Wall/Layer{layer}',
                   label=f'Wall{layer}_{k:02d}', mat=MI['MI_DW_Wall'])
        n_wall += 1
log('墙段', n_wall, '份，每段', round(unit), '米，', wm['tris'], '三角形')

# 衬墙：楼柱群背后最深处的一整面（楼柱之间的缝里看到的是它），只看不碰
back = WALL_META['back_x'] * S
box('WallBacking', (WALL_X + back + 20.0, 0, (H - 6) / 2), (40.0, 2 * WALL_HALF + 400, H - 6 + 8), 'Wall', MI['MI_DW_Backing'], collide=False)
# 墙的碰撞：碰撞平面往后 40 米厚的隐形方块，挡一切（子弹、机体、镜头）。楼柱群本身不碰撞（34 万面的复杂碰撞太贵）
wc = box('WallCollision', (WALL_X + 20.0, 0, H / 2 - 5), (40.0, 2 * WALL_HALF + 400, H + 10), 'Wall', None, collide=True, visible=False)
wc.static_mesh_component.set_collision_profile_name('BlockAll')

# 甲板：Cube.009 的面板单元按原包阵列的步长沿墙铺一排当檐口（场地封顶 130 米，从下面只看得到最前面这一排；后面是一整块平的「高原」）。
# 第一版在第二层楼柱群顶边那个高度（约 114 米）也铺了一排当「中层台沿」，11-01 用户：「墙另外也有一些多余的平台平面」→ 去掉，只留顶上的檐口
dm = WALL_META['deck']
step_a = abs(dm['step_a'][1]) * S       # 沿墙
step_b = dm['step_b'][0] * S            # 往墙里
tile = WALL_META['meshes']['SM_DW_DeckTile']
tile_yc = -(tile['min'][1] + tile['max'][1]) / 2 * S    # 单元在 UE 局部坐标里的 y 中心（Blender y 取反）
cols = int(math.ceil(2 * WALL_HALF / step_a)) + 1
tf = []
for z in (H,):
    for i in range(cols):
        y = -WALL_HALF + i * step_a - tile_yc
        tf.append(xf(V(WALL_X, y, z), None, (S, S, S)))
ism_actor('DeckTiles', M('SM_DW_DeckTile'), MI['MI_DW_Deck'], tf, 'Wall/Deck')
# 甲板单元底面从下往上看有一排排白色小点（沿面板缝，第二轮在单元厚度里垫了薄板也没消），第三轮改成：
# 垫板的底面比单元底面再低 5 厘米，从下往上看到的是一整块暗色平板（底面本来就在阴影里，细节看不见），檐口正面照旧是单元本身
tile_x0, tile_x1 = WALL_X + tile['min'][0] * S, WALL_X + tile['max'][0] * S
tile_z0 = tile['min'][2] * S          # 单元底面（负数，相对甲板顶）
for z in (H,):
    top, bottom = z - 0.4 * S, z + tile_z0 - 0.05
    box(f'DeckUnderlay_{int(z)}', ((tile_x0 + tile_x1) / 2 + 0.3, 0, (top + bottom) / 2), (tile_x1 - tile_x0 - 0.2, 2 * WALL_HALF + 400, top - bottom),
        'Wall/Deck', MI['MI_DW_Backing'], collide=False)
box('DeckPlateau', (WALL_X + 25.0 + 2000.0, 0, H - 1.5 * S - 10.0), (4000.0, 2 * WALL_HALF + 400, 20.0), 'Wall/Deck', MI['MI_DW_Deck'],
    collide=False)

# ---------------------------------------------------------------- 二级平台（墙脚，以后放堡垒）
p = PLAT
box('Platform', ((p['x0'] + p['x1']) / 2, (p['y0'] + p['y1']) / 2, (p['top'] - 4.0) / 2),
    (p['x1'] - p['x0'], p['y1'] - p['y0'], p['top'] + 4.0), 'Platform', MI['MI_DW_Platform'])
# 前沿檐口：比立面凸出 1.5 米、顶面和平台齐平（不绊脚）
box('PlatformLip', (p['x0'] - 0.75 + 0.5, 0, p['top'] - 0.7), (2.5, p['y1'] - p['y0'] + 3.0, 1.4), 'Platform', MI['MI_DW_Platform'])
# 两头的坡道（给地面敌人 / 炮车走）：厚方块绕 X 轴倾斜，顶面从平台边（24 米）落到草地，底下埋进地里，侧面是竖的
ang = math.atan2(p['top'], RAMP['len'])
L = math.hypot(p['top'], RAMP['len'])
T = RAMP['thick']
for side in (1, -1):
    yt = (p['y1'] if side > 0 else p['y0'])                # 坡顶（接平台）
    top_mid = (yt + side * RAMP['len'] / 2, p['top'] / 2)  # 顶面中点 (y, z)
    # 顶面法线（朝上、往坡下方向歪）
    ny, nz = side * math.sin(ang), math.cos(ang)
    cy, cz = top_mid[0] - ny * T / 2, top_mid[1] - nz * T / 2
    best = None
    for roll in (math.degrees(ang), -math.degrees(ang)):
        r = unreal.Rotator(roll=roll, pitch=0, yaw=0)
        d = unreal.MathLibrary.greater_greater_vector_rotator(unreal.Vector(0, side, 0), r)    # 局部 +Y（往坡下）转完以后应朝下
        if d.z < 0:
            best = r
    a = box(f'Ramp{"N" if side > 0 else "S"}', (WALL_X - RAMP['width'] / 2 - 1.0, cy, cz), (RAMP['width'], L, T), 'Platform',
            MI['MI_DW_Platform'], rot=best)
# 平台立面上的装饰柱（峡谷包 2–15 米的方块件），不碰撞
tall = [n for n, m in WALL_META['meshes'].items() if n.startswith('SM_DW_Bit') and m['max'][2] > 7.0]
y = p['y0'] + 6.0
nb = 0
while y < p['y1'] - 5.0:
    if any(abs(y - ey) < ELEV['size'] / 2 + 6.0 for ey in ELEV['ys']):     # 电梯井那一段空着
        y += 4.0
        continue
    n = rng.choice(tall)
    m = WALL_META['meshes'][n]
    h = m['max'][2]
    sc = min(1.8, (p['top'] - rng.uniform(1.0, 5.0)) / h)
    depth = (m['max'][0] - m['min'][0]) * sc
    mesh_actor(M(n), V(p['x0'] - depth * 0.35, y, 0.0), unreal.Rotator(yaw=rng.choice((0.0, 180.0))), (sc, sc, sc), 'Platform/Bits',
               f'PlatBit{nb:02d}', MI['MI_DW_Wall'])
    nb += 1
    y += (m['max'][1] - m['min'][1]) * sc + rng.uniform(5.0, 12.0)
log('平台装饰柱', nb)


def rbox(name, x0, x1, y0, y1, z0, z1, folder, mat, collide=True, shadow=True):
    """按两个对角（米）摆的方块"""
    return box(name, ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), (abs(x1 - x0), abs(y1 - y0), abs(z1 - z0)), folder, mat, collide=collide,
               shadow=shadow)


# ---------------------------------------------------------------- 电梯（第二版 11-01，用户「做一个电梯上下」）
# AMechElevator（主会话写的 C++ 类）按服务器时间在底和顶之间往返，联机各端一致；Actor 位置 = 电梯在底部时平台顶面的中心。
# 这里只摆它和井架。类还没编译时摆一块占位的轿厢，编译好以后重跑本脚本。
elev_cls = None
try:
    elev_cls = unreal.load_class(None, '/Script/RobotSlice.MechElevator')
except Exception as e:
    log('读不到电梯类', e)
cs_ = ELEV['size']
for i, ey in enumerate(ELEV['ys']):
    cx = p['x0'] - ELEV['gap'] - cs_ / 2
    gz = ground_h(cx, ey)
    if elev_cls:
        e = tagged(sub.spawn_actor_from_class(elev_cls, V(cx, ey, gz), unreal.Rotator()), 'Platform/Elevators', f'Elevator{i}')
        for k, v in (('travel', (p['top'] - gz) * 100.0), ('size', unreal.Vector(cs_ * 100.0, cs_ * 100.0, 100.0)), ('move_time', ELEV['move']),
                     ('stop_time', ELEV['stop']), ('phase', i * (ELEV['move'] + ELEV['stop'])), ('platform_material', MI['MI_DW_Steel'])):
            setp(e, k, v)
        log('电梯', i, '底', round(gz, 2), '升', round(p['top'] - gz, 2), '米')
    else:
        rbox(f'ElevatorPlaceholder{i}', cx - cs_ / 2, cx + cs_ / 2, ey - cs_ / 2, ey + cs_ / 2, gz - 1.0, gz, 'Platform/Elevators', MI['MI_DW_Steel'])
        log('电梯类 /Script/RobotSlice.MechElevator 还没编译：摆了占位轿厢', i)
    # 井架：四根立柱（贴平台那两根半埋进立面）+ 顶上一圈横梁 + 两侧的交叉斜撑；朝旷野那一面敞开（从草地走进轿厢），不挡平台边缘
    hw = cs_ / 2 + 1.2
    top = p['top'] + 9.0
    for sx in (-1, 1):
        for sy in (-1, 1):
            rbox(f'Elev{i}_Post{sx}{sy}', cx + sx * hw - 0.7, cx + sx * hw + 0.7, ey + sy * hw - 0.7, ey + sy * hw + 0.7, gz - 1.0, top,
                 'Platform/Elevators', MI['MI_DW_Steel'])
    for sy in (-1, 1):
        rbox(f'Elev{i}_BeamX{sy}', cx - hw - 0.7, cx + hw + 0.7, ey + sy * hw - 0.6, ey + sy * hw + 0.6, top - 1.6, top, 'Platform/Elevators',
             MI['MI_DW_Steel'])
    for sx in (-1, 1):
        rbox(f'Elev{i}_BeamY{sx}', cx + sx * hw - 0.6, cx + sx * hw + 0.6, ey - hw - 0.7, ey + hw + 0.7, top - 1.6, top, 'Platform/Elevators',
             MI['MI_DW_Steel'])
    # 两侧（±Y 面）的交叉斜撑：每 9 米一节，两根斜杆交叉
    seg = 9.0
    nseg = int((top - gz) // seg)
    span = 2 * hw
    for sy in (-1, 1):
        for k in range(nseg):
            z0_ = gz + k * seg
            angd = math.degrees(math.atan2(seg, span))
            ln_ = math.hypot(seg, span)
            for sgn in (-1, 1):
                b = box(f'Elev{i}_Brace{sy}_{k}_{sgn}', (cx, ey + sy * hw, z0_ + seg / 2), (ln_, 0.5, 0.5), 'Platform/Elevators', MI['MI_DW_Steel'],
                        collide=False, rot=unreal.Rotator(roll=0, pitch=sgn * angd, yaw=0))
    # 导轨：贴着平台立面的两根竖条
    for sy in (-1, 1):
        rbox(f'Elev{i}_Rail{sy}', p['x0'] - 0.6, p['x0'] + 0.2, ey + sy * (cs_ / 2 - 1.0) - 0.3, ey + sy * (cs_ / 2 - 1.0) + 0.3, gz, p['top'],
             'Platform/Elevators', MI['MI_DW_Steel'], collide=False)
    # 上下口的黄黑警示条：草地上轿厢三面一圈、平台顶上轿厢对着的那一段边
    for nm, a_, b_, c_, d_ in (('Out', cx - cs_ / 2 - 1.2, cx - cs_ / 2, ey - cs_ / 2 - 1.2, ey + cs_ / 2 + 1.2),
                               ('L', cx - cs_ / 2, cx + cs_ / 2, ey - cs_ / 2 - 1.2, ey - cs_ / 2),
                               ('R', cx - cs_ / 2, cx + cs_ / 2, ey + cs_ / 2, ey + cs_ / 2 + 1.2)):
        rbox(f'Elev{i}_HazardGround{nm}', a_, b_, c_, d_, gz - 0.2, gz + 0.04, 'Platform/Elevators', MI['MI_DW_Hazard'], collide=False, shadow=False)
    rbox(f'Elev{i}_HazardTop', p['x0'] + 0.2, p['x0'] + 3.0, ey - cs_ / 2, ey + cs_ / 2, p['top'] - 0.02, p['top'] + 0.04, 'Platform/Elevators',
         MI['MI_DW_Hazard'], collide=False, shadow=False)
    # 顶上四角的琥珀色灯
    for sx in (-1, 1):
        for sy in (-1, 1):
            rbox(f'Elev{i}_Lamp{sx}{sy}', cx + sx * hw - 0.5, cx + sx * hw + 0.5, ey + sy * hw - 0.5, ey + sy * hw + 0.5, top, top + 0.8,
                 'Platform/Elevators', MI['MI_DW_LightAmber'], collide=False, shadow=False)

# ---------------------------------------------------------------- 封锁的大门（第二版 11-01，用户「进不了墙内的，设定是门被封锁」）
# 门框（两根门柱 + 门楣）从墙里伸出来、门扇在楼柱群前面，关着、焊死：门缝和门脚刷黄黑警示条，几道加固横梁横跨两扇门，
# 门楣和门柱上一排红色状态灯。全部是挡一切的方块（碰撞照旧是墙）。
ng = 0
for g in GATES:
    gy, W, Hg, pw, lh = g['y'], g['w'], g['h'], g['pillar'], g['lintel']
    z0 = p['top']
    fx0, fx1 = WALL_X - 22.0, WALL_X + 2.0           # 门框前后
    lx0, lx1 = WALL_X - 17.0, WALL_X - 13.0          # 门扇
    tag_ = f'Gate{ng}'
    for sy in (-1, 1):
        rbox(f'{tag_}_Pillar{sy}', fx0, fx1, gy + sy * W / 2, gy + sy * (W / 2 + pw), z0 - 1.0, z0 + Hg + lh, 'Wall/Gates', MI['MI_DW_Steel'])
    rbox(f'{tag_}_Lintel', fx0, fx1, gy - W / 2 - pw, gy + W / 2 + pw, z0 + Hg, z0 + Hg + lh, 'Wall/Gates', MI['MI_DW_Steel'])
    for sy in (-1, 1):
        rbox(f'{tag_}_Leaf{sy}', lx0, lx1, gy + sy * 0.25, gy + sy * W / 2, z0, z0 + Hg, 'Wall/Gates', MI['MI_DW_Steel'])
    # 门缝和门脚的警示条（贴在门扇前面 5 厘米）
    rbox(f'{tag_}_HazardSeam', lx0 - 0.08, lx0 - 0.02, gy - W * 0.025 - 0.8, gy + W * 0.025 + 0.8, z0, z0 + Hg, 'Wall/Gates', MI['MI_DW_Hazard'],
         collide=False, shadow=False)
    rbox(f'{tag_}_HazardFoot', lx0 - 0.08, lx0 - 0.02, gy - W / 2, gy + W / 2, z0, z0 + Hg * 0.07, 'Wall/Gates', MI['MI_DW_Hazard'],
         collide=False, shadow=False)
    # 加固横梁（横跨两扇门，两头压在门柱上）+ 门缝中间一块刷警示条的锁死夹板。
    # 门扇用深色钢、横梁用浅色：横梁一道道压在暗门上（第一次门扇浅、横梁深而且凸出 3.5 米，太阳从上面照下来像一排书架）
    bh = 4.2 if W > 40 else 3.0
    for k, f in enumerate(g['beams']):
        zc = z0 + Hg * f
        rbox(f'{tag_}_Beam{k}', lx0 - 1.8, lx0, gy - W / 2 - pw * 0.6, gy + W / 2 + pw * 0.6, zc - bh / 2, zc + bh / 2, 'Wall/Gates',
             MI['MI_DW_GateLeaf'])
    rbox(f'{tag_}_Clamp', lx0 - 2.6, lx0, gy - W * 0.07, gy + W * 0.07, z0 + Hg * 0.42, z0 + Hg * 0.48 + bh, 'Wall/Gates', MI['MI_DW_Hazard'])
    # 门扇上的竖肋（加固横梁从肋前面压过去）
    nrib = 4 if W > 40 else 2
    for sy in (-1, 1):
        for k in range(nrib):
            ry = gy + sy * (W / 2) * (k + 0.5) / nrib
            rbox(f'{tag_}_Rib{sy}_{k}', lx0 - 0.6, lx0, ry - 0.7, ry + 0.7, z0 + Hg * 0.07, z0 + Hg - 0.5, 'Wall/Gates', MI['MI_DW_Steel'],
                 collide=False)
    # 红色状态灯：门楣正面一排、两根门柱中段各一个
    nl = g['lights']
    for k in range(nl):
        ly = gy - W / 2 + (k + 0.5) * W / nl
        rbox(f'{tag_}_Light{k}', fx0 - 0.4, fx0, ly - 1.4, ly + 1.4, z0 + Hg + lh * 0.35, z0 + Hg + lh * 0.65, 'Wall/Gates', MI['MI_DW_LightRed'],
             collide=False, shadow=False)
    for sy in (-1, 1):
        rbox(f'{tag_}_PillarLight{sy}', fx0 - 0.4, fx0, gy + sy * (W / 2 + pw / 2) - 1.0, gy + sy * (W / 2 + pw / 2) + 1.0, z0 + Hg * 0.45,
             z0 + Hg * 0.55, 'Wall/Gates', MI['MI_DW_LightRed'], collide=False, shadow=False)
    ng += 1
log('封锁的大门', ng, '扇')

# ---------------------------------------------------------------- 天空、行星、云
sphere = asset('/Engine/BasicShapes/Sphere')    # 直径 1 米
mesh_actor(sphere, V(0, 0, 0), scale=(120000, 120000, 120000), folder='Sky', label='SkyDome', mat=MI['MI_DF_Sky'], shadow=False, lumen=False)


def sky_dir(yaw, elev):
    return (math.cos(math.radians(elev)) * math.cos(math.radians(yaw)), math.cos(math.radians(elev)) * math.sin(math.radians(yaw)),
            math.sin(math.radians(elev)))


# 大行星：墙后上方（从旷野看墙时挂在墙顶上面，像峡谷包渲染图那样），30 公里外、角半径 13°；离墙近时下半截被墙挡住
d = sky_dir(15.0, 28.0)
R = 30000.0 * math.tan(math.radians(13.0))
mesh_actor(sphere, V(d[0] * 30000, d[1] * 30000, d[2] * 30000), unreal.Rotator(roll=0, pitch=0, yaw=200), (2 * R,) * 3, 'Sky', 'Planet',
           MI['MI_DF_Planet'], shadow=False, lumen=False)
d = sky_dir(6.0, 54.0)
R = 20000.0 * math.tan(math.radians(1.6))
mesh_actor(sphere, V(d[0] * 20000, d[1] * 20000, d[2] * 20000), None, (2 * R,) * 3, 'Sky', 'Moon', MI['MI_DF_Moon'], shadow=False, lumen=False)

clouds = sorted(CLOUD_META['meshes'])
nc = 0
for i in range(30):
    if i < 8:      # 墙后高原上空、贴着墙顶一点（从旷野看在墙沿上方飘）
        x, y_, z = WALL_X + rng.uniform(200, 1400), rng.uniform(-2200, 2200), H + rng.uniform(50, 160)
    else:          # 旷野四周的远处
        a_ = rng.uniform(0, 2 * math.pi)
        dist = rng.uniform(1100, 3800)
        x, y_, z = math.cos(a_) * dist, math.sin(a_) * dist, rng.uniform(260, 700)
        if x > WALL_X - 100:
            x = WALL_X + rng.uniform(300, 1800)
            z = H + rng.uniform(60, 260)
    n = rng.choice(clouds)
    continue  # 11-01 用户：「旷野上面的建模云可以删掉吗」→ 不摆建模云，要恢复就去掉这一行
    s_ = rng.uniform(5.0, 10.0)
    mesh_actor(M(n), V(x, y_, z), unreal.Rotator(yaw=rng.uniform(0, 360)), (s_, s_, s_ * rng.uniform(1.3, 1.8)), 'Sky/Clouds', f'Cloud{i:02d}',
               MI['MI_DF_Cloud'], shadow=False)
    nc += 1
log('云', nc)

# ---------------------------------------------------------------- 光：太阳 + 天光（HDR）+ 高度雾 + 后期
sun = tagged(sub.spawn_actor_from_class(unreal.DirectionalLight, V(0, 0, 50), unreal.Rotator(roll=0, pitch=SUN_PITCH, yaw=SUN_YAW)),
             'Light', 'Sun')
lc = sun.light_component
lc.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
for k, v in (('intensity', 11.0), ('temperature', 6300.0), ('use_temperature', True), ('light_source_angle', 2.0),
             ('light_color', unreal.Color(r=255, g=244, b=236, a=255)), ('cast_shadows', True)):
    setp(lc, k, v)

sky = tagged(sub.spawn_actor_from_class(unreal.SkyLight, V(0, 0, 60)), 'Light', 'SkyLight')
sc_ = sky.light_component
sc_.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
setp(sc_, 'source_type', unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
setp(sc_, 'cubemap', asset(f'{DEST}/Textures/T_DF_SkyCube'))
setp(sc_, 'source_cubemap_angle', SKY_ANGLE)
setp(sc_, 'intensity', 0.22)
setp(sc_, 'light_color', unreal.Color(r=150, g=186, b=255, a=255))   # 天光压低、偏蓝：背光面和凹处是原图那种深蓝灰
setp(sc_, 'lower_hemisphere_is_black', False)
setp(sc_, 'lower_hemisphere_color', unreal.LinearColor(0.16, 0.2, 0.08, 1))
setp(sc_, 'real_time_capture', False)

fog = tagged(sub.spawn_actor_from_class(unreal.ExponentialHeightFog, V(0, 0, 0)), 'Light', 'HeightFog')
fc = fog.component
# 第一版密度 0.03：1.4 公里外的墙只剩一层影子，天空也被染成灰紫。现在 300 米外约 12%、1.4 公里约 45%、6 公里地面边缘基本盖住；
# 雾只管 14 公里以内（天空球 60 公里、行星 30 公里不受雾，天边的颜色在天空材质里渐变到雾色）
# 雾从 400 米外才起来（第一版 30 米、第二版 250 米），墙附近的阴影不被粉紫雾染紫，远处照样淡进雾里
for k, v in (('fog_density', 0.009), ('fog_height_falloff', 0.1), ('fog_max_opacity', 1.0), ('start_distance', 40000.0),
             ('fog_cutoff_distance', 1400000.0),
             ('fog_inscattering_luminance', unreal.LinearColor(1.72, 1.45, 1.78, 1)),
             ('directional_inscattering_luminance', unreal.LinearColor(1.3, 0.95, 0.8, 1)),
             ('directional_inscattering_exponent', 6.0), ('directional_inscattering_start_distance', 20000.0),
             ('enable_volumetric_fog', False)):
    setp(fc, k, v)

ppv = tagged(sub.spawn_actor_from_class(unreal.PostProcessVolume, V(0, 0, 0)), 'Light', 'PP_Dreamfield')
ppv.set_editor_property('unbound', True)
ppv.set_editor_property('priority', 10.0)
pp = ppv.get_editor_property('settings')
EV = 0.85    # 固定曝光（EV100），不随看天 / 看地变（第三轮太阳 11、天光 0.22；旷野、草、云的底色同比压低，看上去不变）
for k, v in (('auto_exposure_method', unreal.AutoExposureMethod.AEM_HISTOGRAM), ('auto_exposure_min_brightness', EV),
             ('auto_exposure_max_brightness', EV), ('auto_exposure_bias', 0.0), ('auto_exposure_apply_physical_camera_exposure', False),
             ('bloom_intensity', 0.55), ('vignette_intensity', 0.22), ('film_grain_intensity', 0.05), ('scene_fringe_intensity', 0.25),
             ('color_saturation', unreal.Vector4(1.0, 1.0, 1.0, 1.0)), ('color_contrast', unreal.Vector4(1.0, 1.0, 1.0, 1.06)),
             ('color_gain_highlights', unreal.Vector4(1.0, 1.01, 1.03, 1.0)),
             ('color_offset_shadows', unreal.Vector4(0.0, 0.004, 0.016, 0.0)),   # 暗部往蓝偏，不往紫偏（第二轮暗部偏紫）
             ('color_gain_shadows', unreal.Vector4(0.86, 0.94, 1.14, 1.0)), ('indirect_lighting_intensity', 0.35)):
    setp(pp, 'override_' + k, True)
    setp(pp, k, v)
ppv.set_editor_property('settings', pp)

# ---------------------------------------------------------------- 边界、出生点、场地标记、截图机位
A = ARENA
walls = [('Bound_W', (-A['hx'] - 1, 0, A['ceil'] / 2), (2, 2 * A['hy'] + 4, A['ceil'] + 20)),
         ('Bound_N', (0, A['hy'] + 1, A['ceil'] / 2), (2 * A['hx'] + 4, 2, A['ceil'] + 20)),
         ('Bound_S', (0, -A['hy'] - 1, A['ceil'] / 2), (2 * A['hx'] + 4, 2, A['ceil'] + 20)),
         ('Bound_Ceiling', (0, 0, A['ceil'] + 1), (2 * A['hx'] + 4, 2 * A['hy'] + 4, 2))]
for name, c, s in walls:
    a = box(name, c, s, 'Bounds', None, collide=True, visible=False, shadow=False)
    comp = a.static_mesh_component
    # 照城区图：先设 Custom 再改对象类型 / 响应（不然存盘读回来按 BlockAll，挡子弹和镜头）；对象类型 Vehicle、只挡 Pawn
    comp.set_collision_profile_name('Custom')
    comp.set_collision_object_type(unreal.CollisionChannel.ECC_VEHICLE)
    comp.set_collision_response_to_all_channels(unreal.CollisionResponseType.ECR_IGNORE)
    comp.set_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN, unreal.CollisionResponseType.ECR_BLOCK)
    a.tags = [TAG, 'ArenaWall']

ps = [a for a in sub.get_all_level_actors() if isinstance(a, unreal.PlayerStart)]
start_loc = V(WALL_X - 50.0, 0.0, PLAT['top'] + 1.5)     # 平台上、中间大门前 30 米，朝旷野
if ps:
    ps[0].set_actor_location(start_loc, False, False)
    ps[0].set_actor_rotation(unreal.Rotator(yaw=180.0), False)
    for extra in ps[1:]:
        sub.destroy_actor(extra)
else:
    tagged(sub.spawn_actor_from_class(unreal.PlayerStart, start_loc, unreal.Rotator(yaw=180.0)), 'Spawns', 'PlayerStart')
for name, loc, yaw in (('TeamSpawn0', V(WALL_X - 50.0, 0, PLAT['top'] + 0.2), 180.0), ('TeamSpawn1', V(-250.0, 0, 0.5), 0.0)):
    tagged(sub.spawn_actor_from_class(unreal.TargetPoint, loc, unreal.Rotator(yaw=yaw)), 'Spawns', name, [name])
t = tagged(sub.spawn_actor_from_class(unreal.TargetPoint, V(0, 0, 0)), 'Bounds', 'MechArena',
           ['MechArena', f'HalfX={A["hx"] * 100:.0f}', f'HalfY={A["hy"] * 100:.0f}'])
cam = tagged(sub.spawn_actor_from_class(unreal.CameraActor, V(0, 0, 20)), 'Misc', 'ShotCam', ['ShotCam'])
cam.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)

# 坡道自检：顶面两头的高度（向下打射线）
for yy in (PLAT['y1'] + 2, PLAT['y1'] + RAMP['len'] - 2, PLAT['y0'] - 2, PLAT['y0'] - RAMP['len'] + 2, 0.0):
    try:
        r = unreal.SystemLibrary.line_trace_single(world, V(WALL_X - 12, yy, 80), V(WALL_X - 12, yy, -20),
                                                   unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
        if isinstance(r, tuple):
            r = r[1] if r[0] else None
        tt = r.to_tuple() if r else None
        hit_z = next((round(v.z / 100, 2) for v in tt if isinstance(v, unreal.Vector)), None) if tt and tt[0] else None
        hit_a = next((v.get_actor_label() for v in tt if isinstance(v, unreal.Actor)), None) if tt and tt[0] else None
        log('坡道 / 平台自检 y', yy, '顶面高', hit_z, '打到', hit_a)
    except Exception as e:
        log('自检失败', e)

json.dump({'plat': PLAT, 'ramp': RAMP, 'elev': ELEV, 'gates': GATES, 'wall_x': WALL_X},
          open('D:/RobotSlice/Inbox/_dreamwall/dreamfield_layout.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
unreal.EditorLoadingAndSavingUtils.save_map(world, DST)
log('已保存', DST, '：墙', n_wall, '段，云', nc, '，平台装饰', nb)
