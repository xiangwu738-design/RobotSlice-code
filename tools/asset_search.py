# 只查素材站的公开接口（不下载素材）：Poly Haven、ambientCG、Sketchfab。结果写成 UTF-8 文件。
import json, sys, urllib.parse, urllib.request

out = open(sys.argv[1], 'w', encoding='utf-8')


def get(url):
    req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0 asset-survey'})
    with urllib.request.urlopen(req, timeout=40) as r:
        return json.load(r)


def P(*a):
    print(*a, file=out)
    out.flush()


try:
    P('== Poly Haven 贴图')
    tex = get('https://api.polyhaven.com/assets?t=textures')
    for k in ('snow', 'moon', 'lunar', 'regolith', 'asphalt', 'volcanic', 'ash', 'gravel', 'crater'):
        hit = [v['name'] for v in tex.values() if k in v['name'].lower() or k in ' '.join(v.get('tags', [])).lower()]
        P(k, len(hit), hit[:8])
    P('== Poly Haven 模型')
    mod = get('https://api.polyhaven.com/assets?t=models')
    for k in ('tree', 'birch', 'dead', 'bench', 'lamp', 'car', 'rock', 'boulder', 'bin', 'fence'):
        hit = [v['name'] for v in mod.values() if k in v['name'].lower() or k in ' '.join(v.get('tags', [])).lower()]
        P(k, len(hit), hit[:10])
except Exception as e:
    P('polyhaven 出错', e)

try:
    P('== ambientCG')
    for q in ('moon', 'snow', 'asphalt', 'ground ash', 'rock'):
        d = get('https://ambientcg.com/api/v2/full_json?' + urllib.parse.urlencode({'q': q, 'type': 'Material', 'limit': 8, 'include': 'displayData'}))
        P(q, d.get('numberOfResults'), [a.get('assetId') + ':' + str(a.get('displayName')) for a in d.get('foundAssets', [])][:8])
except Exception as e:
    P('ambientcg 出错', e)

queries = ['lada', 'soviet car', 'soviet bus stop', 'soviet playground', 'soviet street props', 'soviet courtyard', 'russian trash container', 'heating pipes', 'street lamp soviet',
           'dead tree', 'winter tree', 'birch tree winter', 'snow tree', 'moon rock', 'lunar boulder', 'regolith', 'moon surface', 'lunar crater', 'snow particles']
P('== Sketchfab 可下载（最多 6 条）')
for q in queries:
    try:
        d = get('https://api.sketchfab.com/v3/search?' + urllib.parse.urlencode({'type': 'models', 'q': q, 'downloadable': 'true', 'count': 6, 'sort_by': '-likeCount'}))
        P('--', q)
        for m in d.get('results', [])[:6]:
            P('  ', m.get('name', '')[:46], '|', (m.get('license') or {}).get('label'), '|面', m.get('faceCount'), '|赞', m.get('likeCount'), '|', m.get('viewerUrl'))
    except Exception as e:
        P('--', q, '出错', e)
P('DONE')
