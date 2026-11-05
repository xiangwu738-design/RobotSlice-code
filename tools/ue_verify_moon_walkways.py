"""实际场景射线与胶囊扫掠：平台连续、门口可通行；蓝穹独立由运行时用例验证。"""
import unreal as u,json,math,traceback
from pathlib import Path
R=Path('D:/RobotSlice');report={'passed':False,'floor_checks':0,'sweep_checks':0,'failures':[]}
def V(p):return u.Vector(*(x*100 for x in p))
def unpack(hit):
 if isinstance(hit,tuple):hit=hit[1] if hit[0] else None
 return hit.to_tuple() if hit else None
try:
 assert 'MOON_RUNTIME_WORLD' in globals(),'请通过 ue_moon_walkways_runtime.py 在游戏运行时检查；编辑器命令行没有完整 HISM 物理场景'
 world=MOON_RUNTIME_WORLD;assert world
 report['runtime']='MOON_RUNTIME_WORLD' in globals()
 actors=u.GameplayStatics.get_all_actors_of_class(world,u.Actor);shield=[a for a in actors if a.actor_has_tag('MoonShield') or isinstance(a,u.Pawn)]
 plan=json.loads((R/'SourceAssets/moon_free/walkways.json').read_text())
 for r in plan['routes']:
  points=r['points']
  for i,(a,b) in enumerate(zip(points,points[1:])):
   mid=[(a[k]+b[k])/2 for k in range(3)]
   hit=unpack(u.SystemLibrary.line_trace_single(world,V([*mid[:2],mid[2]+1]),V([*mid[:2],mid[2]-1]),u.TraceTypeQuery.ECC_VISIBILITY,True,shield,u.DrawDebugTrace.NONE,True))
   report['floor_checks']+=1
   if not hit or not hit[0] or abs(hit[4].z/100-mid[2])>.025:report['failures'].append({'route':r['name'],'floor':i,'expected':mid[2],'actual':hit[4].z/100 if hit and hit[0] else None})
   result=unpack(u.SystemLibrary.capsule_trace_single(world,V([*a[:2],a[2]+2.3]),V([*b[:2],b[2]+2.3]),85,220,u.TraceTypeQuery.ECC_VISIBILITY,True,shield,u.DrawDebugTrace.NONE,True))
   report['sweep_checks']+=1
   if result and result[0]:report['failures'].append({'route':r['name'],'segment':i,'hit':[v.get_actor_label() for v in result if isinstance(v,u.Actor)]})
  dx=points[-1][0]-points[0][0];dy=points[-1][1]-points[0][1];length=math.hypot(dx,dy);dx/=length;dy/=length
  for end in (points[0],points[-1]):
   a=[end[0]-4*dx,end[1]-4*dy,end[2]+2.3];b=[end[0]+4*dx,end[1]+4*dy,end[2]+2.3]
   result=unpack(u.SystemLibrary.capsule_trace_single(world,V(a),V(b),85,220,u.TraceTypeQuery.ECC_VISIBILITY,True,shield,u.DrawDebugTrace.NONE,True));report['sweep_checks']+=1
   if result and result[0]:report['failures'].append({'route':r['name'],'entrance':end,'hit':[v.get_actor_label() for v in result if isinstance(v,u.Actor)]})
 assert not report['failures'],report['failures'][:8]
 report['passed']=True
except Exception:report['error']=traceback.format_exc();raise
finally:(R/'Saved/Automation/MoonWalkwaysVerify.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
