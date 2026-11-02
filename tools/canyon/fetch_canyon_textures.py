"""Poly Haven（CC0）峡谷用贴图：2K 颜色 / 法线(GL) / ARM（+ 位移），放 SourceAssets/canyon/textures/<id>/"""
import json, os, sys, urllib.request
ROOT = r"D:\RobotSlice\SourceAssets\canyon\textures"
UA = {'User-Agent': 'RobotSlice-asset-fetch (personal game project)'}
IDS = sys.argv[1:] or ['rock_06', 'rock_boulder_cracked', 'cliff_side', 'sandy_gravel_02', 'gravelly_sand', 'low_tide_rocks',
                       'rocks_ground_06', 'dry_ground_01', 'worn_rock_natural_01', 'red_sand']
MAPS = {'Diffuse': 'diff', 'nor_gl': 'nor_gl', 'arm': 'arm', 'Displacement': 'disp'}
for aid in IDS:
    d = json.load(urllib.request.urlopen(urllib.request.Request(f'https://api.polyhaven.com/files/{aid}', headers=UA), timeout=60))
    os.makedirs(os.path.join(ROOT, aid), exist_ok=True)
    for key, short in MAPS.items():
        if key not in d:
            print('  缺', aid, key); continue
        res = d[key].get('2k') or d[key].get('1k')
        fmt = 'jpg' if 'jpg' in res else ('png' if 'png' in res else list(res)[0])
        url = res[fmt]['url']
        out = os.path.join(ROOT, aid, f'{aid}_{short}_2k.{fmt}')
        if not os.path.exists(out):
            data = urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=180).read()
            open(out, 'wb').write(data)
        print(aid, short, os.path.getsize(out) // 1024, 'KB')
with open(os.path.join(ROOT, 'CREDITS.md'), 'w', encoding='utf-8') as f:
    f.write('Poly Haven (CC0) https://polyhaven.com/a/<id>\n' + '\n'.join(f'- {i}' for i in IDS) + '\n')
