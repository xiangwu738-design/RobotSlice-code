"""按官方公开 API 下载免费资源并记录来源及校验值；不下载预览图充当资源。"""
import urllib.request,json,hashlib,concurrent.futures
from pathlib import Path
ROOT=Path('D:/RobotSlice/SourceAssets/dreamfield_free');ROOT.mkdir(parents=True,exist_ok=True)
UA='RobotSliceAssetPrep/1.0 (local game development)'
def read(url):return urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':UA}),timeout=90).read()
def fetch_asset(name,kind):
 dst=ROOT/name;dst.mkdir(exist_ok=True)
 data=json.loads(read('https://api.polyhaven.com/files/'+name));(dst/'official_files.json').write_text(json.dumps(data,indent=2))
 info=json.loads(read('https://api.polyhaven.com/info/'+name));(dst/'official_info.json').write_text(json.dumps(info,indent=2))
 files=[]
 if kind=='model':
  f=data['fbx']['2k']['fbx'];files.append((name+'.fbx',f))
  for typ in ['Diffuse','Alpha','nor_dx','Rough']:
   item=data[typ]['2k'];ext='jpg' if 'jpg' in item else 'png';files.append((name+'_'+typ+'.'+ext,item[ext]))
 elif kind=='hdri':files.append((name+'.hdr',data['hdri']['8k']['hdr']))
 else:
  for typ in ['Diffuse','nor_dx','Rough']:
   item=data[typ]['2k'];ext='jpg' if 'jpg' in item else 'png';files.append((name+'_'+typ+'.'+ext,item[ext]))
 result={'asset':name,'source':'https://polyhaven.com/a/'+name,'license':'CC0','license_url':'https://polyhaven.com/license','files':[]}
 for filename,entry in files:
  p=dst/filename
  if not p.exists() or hashlib.md5(p.read_bytes()).hexdigest()!=entry['md5']:
   content=read(entry['url']);assert hashlib.md5(content).hexdigest()==entry['md5'];p.write_bytes(content)
  result['files'].append({'path':str(p),'url':entry['url'],'md5':entry['md5'],'bytes':p.stat().st_size})
 (dst/'provenance.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
 print(name,'OK',sum(f['bytes'] for f in result['files']),flush=True);return result
tasks=[('grass_medium_01','model'),('grass_bermuda_01','model'),('kloppenheim_06_puresky','hdri'),('grass_ground','texture'),('concrete_wall_006','texture')]
results=[]
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
 futures={pool.submit(fetch_asset,*task):task[0] for task in tasks}
 for f in concurrent.futures.as_completed(futures):
  try:results.append(f.result())
  except Exception as e:print('FAILED',futures[f],repr(e),flush=True)
(ROOT/'download_manifest.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf8')
assert len(results)==len(tasks),'部分资源下载失败，详见输出'
