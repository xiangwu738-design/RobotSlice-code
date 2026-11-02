"""城区图（L_City）屏幕材质普查：从 ue_city_shots.py 的 10 个成图机位往画面里打 96×54 根射线（复杂碰撞，按碰撞面查材质槽），
统计每个机位画面里各材质占多少，标出「纯色」材质（M_CityPBR 实例没给底色贴图，只有 Tint）和引擎默认材质——画面里成片的纯色灰面就是「大灰块」。
只读不改。日志 [census]，结果 Saved/city_mat_census.json。
在游戏里跑（编辑器 commandlet 里加载的关卡没有物理，射线全打空）：
UnrealEditor.exe RobotSlice.uproject /Game/Maps/L_City -game -MechBeauty -nullrhi -unattended -nosound "-ExecCmds=py D:/RobotSlice/tools/ue_city_mat_census.py"
跑完自己退出。
"""
import json
import math

import unreal

OUT = 'D:/RobotSlice/Saved/city_mat_census.json'
NX, NY = 96, 54
# 和 ue_city_shots.py 一致（UE 坐标，米；高度 < 100 = 离地面）
SHOTS = [
    ('c0_top', (0, 0, 420.0), (0, 1.5, 0.0), 60),
    ('c1_spawn_blue', (-100, 0, 4.0), (0, 0, 4.0), 70),
    ('c2_avenue_tower', (-30, 12, 5.0), (20, 0, 6.0), 70),
    ('c3_site', (-28, -88, 6.0), (0, -45, 6.0), 70),
    ('c4_plaza', (12, 75, 4.0), (-8, 25, 6.0), 70),
    ('c5_viaduct', (45, 85, 3.5), (45, -20, 12.0), 70),
    ('c6_rooftops', (-66, -30, 20.0), (0, 0, 6.0), 70),
    ('c7_overview', (-120, 120, 70.0), (0, 0, 0.0), 65),
    ('c8_stW', (-45, 85, 4.0), (-45, -40, 6.0), 70),
    ('c9_hotel_roof', (95, -55, 2.5), (0, 0, 4.0), 70),
]
eas = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary


def log(msg):
    unreal.log(f'[census] {msg}')


world = None


def trace(a, b):
    hit = unreal.SystemLibrary.line_trace_single(world, a, b, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [],
                                                 unreal.DrawDebugTrace.NONE, True)
    if isinstance(hit, tuple):
        hit = hit[1] if hit[0] else None
    if hit is None:
        return None
    t = hit.to_tuple()
    return t if t[0] else None


def ground_z(x, y):
    t = trace(unreal.Vector(x, y, 200000), unreal.Vector(x, y, -200000))
    return t[4].z if t else 0.0


_flat = {}


def flat_info(m):
    """纯色？（M_CityPBR 实例的 BaseColorTex 还是默认白图）→ (是否纯色, Tint)"""
    key = m.get_path_name()
    if key not in _flat:
        info = (False, None)
        if isinstance(m, unreal.MaterialInstanceConstant):
            try:
                try:
                    tex = mel.get_material_instance_texture_parameter_value(m, 'BaseColorTex')
                    tint = mel.get_material_instance_vector_parameter_value(m, 'Tint')
                except Exception:   # 游戏里没有编辑器的材质库，用运行时接口
                    tex = m.k2_get_texture_parameter_value('BaseColorTex')
                    tint = m.k2_get_vector_parameter_value('Tint')
                if tex is None or tex.get_name() == 'T_City_White':
                    info = (True, [round(tint.r, 2), round(tint.g, 2), round(tint.b, 2)])
            except Exception:
                pass
        _flat[key] = info
    return _flat[key]


def run():
  result = {}
  for name, c, look, fov in SHOTS:
      cx, cy = c[0] * 100, c[1] * 100
      cz = (ground_z(cx, cy) if c[2] < 100 else 0.0) + c[2] * 100
      lx, ly = look[0] * 100, look[1] * 100
      lz = ground_z(lx, ly) + look[2] * 100
      eye = unreal.Vector(cx, cy, cz)
      fwd = unreal.Vector(lx - cx, ly - cy, lz - cz)
      fl = math.sqrt(fwd.x ** 2 + fwd.y ** 2 + fwd.z ** 2)
      f = (fwd.x / fl, fwd.y / fl, fwd.z / fl)
      up0 = (0, 0, 1) if abs(f[2]) < 0.99 else (1, 0, 0)
      r = (f[1] * up0[2] - f[2] * up0[1], f[2] * up0[0] - f[0] * up0[2], f[0] * up0[1] - f[1] * up0[0])
      rl = math.sqrt(sum(v * v for v in r))
      r = tuple(v / rl for v in r)
      u = (r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0])
      th = math.tan(math.radians(fov) / 2)
      tv = th * NY / NX
      mats = {}
      owners = {}
      sky = 0
      for j in range(NY):
          for i in range(NX):
              sx = ((i + 0.5) / NX * 2 - 1) * th
              sy = (1 - (j + 0.5) / NY * 2) * tv
              d = tuple(f[k] + r[k] * sx + u[k] * sy for k in range(3))
              end = unreal.Vector(eye.x + d[0] * 300000, eye.y + d[1] * 300000, eye.z + d[2] * 300000)
              t = trace(eye, end)
              if not t:
                  sky += 1
                  continue
              comp, face = t[10], t[15]
              mp = '?'
              if comp:
                  try:
                      res = comp.get_material_from_collision_face_index(face)
                      mat = res[0] if isinstance(res, tuple) else res
                  except Exception:
                      mat = None
                  if mat is None and isinstance(comp, unreal.PrimitiveComponent):
                      mat = comp.get_material(0)
                  if mat:
                      mp = mat.get_path_name().split('.')[-1]
                      if mp not in mats:
                          fl_, tint = flat_info(mat)
                          mats[mp] = {'n': 0, 'flat': fl_, 'tint': tint, 'engine': mat.get_path_name().startswith('/Engine/')}
                      mats[mp]['n'] += 1
                      ow = t[9].get_actor_label() if t[9] else '?'
                      owners.setdefault(mp, {})
                      owners[mp][ow] = owners[mp].get(ow, 0) + 1
      tot = NX * NY
      result[name] = {'sky': sky, 'mats': mats, 'owners': owners}
      flat = sorted(((k, v) for k, v in mats.items() if v['flat'] or v['engine']), key=lambda kv: -kv[1]['n'])
      log(f'{name}：天空 {100 * sky / tot:.0f}%，纯色 / 默认材质占画面 {100 * sum(v["n"] for k, v in flat) / tot:.0f}%')
      for k, v in flat[:8]:
          top = sorted(owners[k].items(), key=lambda kv: -kv[1])[:4]
          log(f'   {k} {100 * v["n"] / tot:.1f}% 色 {v["tint"]} 引擎 {v["engine"]}，在 {top}')
      big = sorted(mats.items(), key=lambda kv: -kv[1]['n'])[:6]
      log('   画面最多：' + '，'.join(f'{k} {100 * v["n"] / tot:.0f}%' for k, v in big))
  json.dump(result, open(OUT, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
  log(f'写好 {OUT}')


st = {'t0': None}


def tick(dt):
    global world
    if world is None:
        world = unreal.find_object(None, '/Game/Maps/L_City.L_City')
        return
    t = unreal.GameplayStatics.get_time_seconds(world)
    if st['t0'] is None:
        st['t0'] = t
    if t - st['t0'] < 2.0 or st.get('done'):
        return
    st['done'] = True
    try:
        run()
    finally:
        unreal.SystemLibrary.execute_console_command(world, 'quit')


unreal.register_slate_post_tick_callback(tick)
log('已挂上')
