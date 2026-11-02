"""从 Poly Haven（全部 CC0）下载场景素材：模型取 glTF（含贴图），材质取 2K 的颜色 / 法线(GL) / ARM 贴图。
用法：py -3 fetch_polyhaven.py [--dry]   （--dry 只统计大小不下载）
文件放在 SourceAssets/polyhaven/{models,textures}/<id>/，并写 SourceAssets/polyhaven/CREDITS.md。"""
import json
import os
import sys
import urllib.request

ROOT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'SourceAssets', 'polyhaven')
UA = {'User-Agent': 'RobotSlice-asset-fetch (personal game project)'}
RES = '2k'

MODELS = [
    # 建筑与立体结构（Hidden Alley 合集）
    'modular_urban_apartments_facade', 'modular_factory_facade', 'modular_fire_escape', 'modular_metal_gutter',
    'modular_airduct_rectangular_01', 'modular_airduct_circular_01', 'modular_electric_cables',
    'modular_electricity_poles', 'modular_pipes', 'modular_industrial_pipes_01', 'modular_chainlink_fence',
    'rollershutter_door', 'rollershutter_window_01',
    # 街面道具
    'concrete_road_barrier', 'concrete_road_barrier_02', 'exterior_aircon_unit', 'utility_box_01', 'utility_box_02',
    'security_camera_01', 'security_light', 'street_lamp_01', 'street_lamp_02', 'metal_trash_can', 'barrel_stove',
    'Barrel_01', 'barrel_03', 'covered_car', 'old_tyre', 'water_manhole_cover',
]
TEXTURES = ['asphalt_02', 'asphalt_03', 'concrete_floor_worn_02', 'concrete_wall_006', 'concrete_wall_008',
            'corrugated_iron_02', 'container_side', 'factory_wall', 'blue_metal_plate', 'brick_wall_02']
TEX_MAPS = {'Diffuse': 'diff', 'nor_gl': 'nor_gl', 'arm': 'arm'}


def get_json(url):
    return json.load(urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=60))


def plan():
    jobs = []
    for mid in MODELS:
        files = get_json(f'https://api.polyhaven.com/files/{mid}')
        g = files['gltf'][RES]['gltf']
        jobs.append(('models', mid, os.path.basename(g['url']), g['url'], g.get('size', 0)))
        for rel, inc in g.get('include', {}).items():
            jobs.append(('models', mid, rel, inc['url'], inc.get('size', 0)))
    for tid in TEXTURES:
        files = get_json(f'https://api.polyhaven.com/files/{tid}')
        for key, short in TEX_MAPS.items():
            if key not in files:
                continue
            f = files[key][RES]['jpg']
            jobs.append(('textures', tid, f'{tid}_{short}_{RES}.jpg', f['url'], f.get('size', 0)))
    return jobs


def main():
    jobs = plan()
    total = sum(j[4] for j in jobs)
    print(f'{len(MODELS)} 个模型、{len(TEXTURES)} 套材质，{len(jobs)} 个文件，共 {total / 1e6:.0f} MB')
    if '--dry' in sys.argv:
        return
    done = 0
    for kind, aid, rel, url, size in jobs:
        out = os.path.join(ROOT, kind, aid, rel.replace('/', os.sep))
        if os.path.exists(out) and (not size or os.path.getsize(out) == size):
            continue
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=300) as r, open(out + '.part', 'wb') as f:
            while True:
                chunk = r.read(1 << 20)
                if not chunk:
                    break
                f.write(chunk)
        os.replace(out + '.part', out)
        done += 1
    with open(os.path.join(ROOT, 'CREDITS.md'), 'w', encoding='utf-8') as f:
        f.write('# Poly Haven 素材（CC0 1.0，可商用、无需署名）\n\n来源 https://polyhaven.com ，分辨率 2K。\n\n')
        for aid in MODELS:
            f.write(f'- 模型 `{aid}`：https://polyhaven.com/a/{aid}\n')
        for aid in TEXTURES:
            f.write(f'- 材质 `{aid}`：https://polyhaven.com/a/{aid}\n')
    print('下载完成，新下载', done, '个文件')


if __name__ == '__main__':
    main()
