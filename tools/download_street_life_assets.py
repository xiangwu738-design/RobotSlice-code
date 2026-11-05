"""从 Poly Haven 官方接口下载街边生活道具，保留 glTF 及其全部依赖与校验记录。"""
from pathlib import Path
import urllib.request,json,hashlib,concurrent.futures
ROOT=Path('D:/RobotSlice/SourceAssets/millennium_street_existing/life');ROOT.mkdir(parents=True,exist_ok=True)
NAMES=['plastic_monobloc_chair_01','folding_wooden_stool','small_wooden_table_01','plastic_crate_02','potted_plant_02']
def read(url):
 return urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'RobotSliceAssetPrep/1.0 (local game development)'}),timeout=90).read()
def download(name):
 d=ROOT/name;d.mkdir(exist_ok=True)
 files=json.loads(read('https://api.polyhaven.com/files/'+name));info=json.loads(read('https://api.polyhaven.com/info/'+name))
 (d/'official_files.json').write_text(json.dumps(files,indent=2));(d/'official_info.json').write_text(json.dumps(info,indent=2))
 entry=files['gltf']['2k']['gltf'];jobs=[(Path(entry['url']).name,entry),*entry.get('include',{}).items()];out=[]
 for rel,item in jobs:
  p=d/rel;p.parent.mkdir(parents=True,exist_ok=True)
  assert p.resolve().is_relative_to(d.resolve())
  if not p.exists() or hashlib.md5(p.read_bytes()).hexdigest()!=item['md5']:
   content=read(item['url']);assert hashlib.md5(content).hexdigest()==item['md5'];p.write_bytes(content)
  out.append({'path':rel,'url':item['url'],'md5':item['md5'],'bytes':p.stat().st_size})
 report={'id':name,'source':'https://polyhaven.com/a/'+name,'license':'CC0-1.0','license_source':'https://polyhaven.com/license','files':out}
 (d/'provenance.json').write_text(json.dumps(report,indent=2));print(name,'OK',sum(x['bytes'] for x in out),flush=True)
 return report
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:results=list(pool.map(download,NAMES))
(ROOT/'manifest.json').write_text(json.dumps(results,indent=2))
