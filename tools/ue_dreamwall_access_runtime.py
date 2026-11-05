"""游戏物理场景检查新墙是否阻挡平台、两侧坡道，以及原封墙是否仍有效。"""
import unreal as u,time,os,json,traceback
from pathlib import Path
R=Path('D:/RobotSlice');FULL=os.environ.get('DF_BASTION_FULL','0')=='1';name='L_Dreamfield' if FULL else 'L_Dreamfield_BastionStudy'
started=time.monotonic();done=False
report={'passed':False,'runtime':True,'map':name,'floors':0,'clear_paths':0,'wall_checks':0,'sealed_gate_checks':0,'failures':[]}
def V(p):return u.Vector(*(x*100 for x in p))
def unpack(hit):
 if isinstance(hit,tuple):hit=hit[1] if hit[0] else None
 return hit.to_tuple() if hit else None
def run(world):
 actors=u.GameplayStatics.get_all_actors_of_class(world,u.Actor);ignore=[a for a in actors if isinstance(a,u.Pawn)]
 def line(a,b):return unpack(u.SystemLibrary.line_trace_single(world,V(a),V(b),u.TraceTypeQuery.ECC_VISIBILITY,True,ignore,u.DrawDebugTrace.NONE,True))
 def floor(label,x,y,z):
  h=line((x,y,z+3),(x,y,z-3));report['floors']+=1
  if not h or not h[0] or abs(h[4].z/100-z)>.08:report['failures'].append({'floor':label,'expected':z,'actual':h[4].z/100 if h and h[0] else None})
 def sweep(label,a,b):
  h=unpack(u.SystemLibrary.capsule_trace_single(world,V(a),V(b),85,220,u.TraceTypeQuery.ECC_VISIBILITY,True,ignore,u.DrawDebugTrace.NONE,True));report['clear_paths']+=1
  if h and h[0]:report['failures'].append({'blocked':label,'actors':[v.get_actor_label() for v in h if isinstance(v,u.Actor)]})
 for x in (200,240,270):
  for y in (-200,-100,0,100,200):floor('platform',x,y,24)
  sweep('platform-long-'+str(x),(x,-210,26.3),(x,210,26.3))
 for y in (-200,-100,0,100,200):sweep('platform-cross-'+str(y),(198,y,26.3),(270,y,26.3))
 # 原封锁门向平台突出，不能把穿门路线误当成应畅通的通道。
 for a,b in [((280,-210,26.3),(280,210,26.3)),((198,0,26.3),(285,0,26.3))]:
  h=unpack(u.SystemLibrary.capsule_trace_single(world,V(a),V(b),85,220,u.TraceTypeQuery.ECC_VISIBILITY,True,ignore,u.DrawDebugTrace.NONE,True));report['sealed_gate_checks']+=1
  names=[v.get_actor_label() for v in h if isinstance(v,u.Actor)] if h else []
  if not h or not h[0] or not any(n.startswith('Gate') for n in names):report['failures'].append({'sealed_gate_missing':names})
 for sign in (-1,1):
  pts=[]
  for t in (.02,.1,.3,.5,.7,.9,.98):
   y=sign*(225+120*t);z=24*(1-t);floor('ramp-'+str(sign),287,y,z);pts.append((287,y,z+2.3))
  for a,b in zip(pts,pts[1:]):sweep('ramp-'+str(sign),a,b)
 for y in (-200,-100,0,100,200):
  h=line((270,y,80),(340,y,80));report['wall_checks']+=1
  if not h or not h[0]:report['failures'].append({'wall_open':y})
 for y in (-210,-90,90,210):
  h=line((275,y,50),(303,y,50));report['wall_checks']+=1
  names=[v.get_actor_label() for v in h if isinstance(v,u.Actor)] if h else []
  if not h or not h[0] or not any(n.startswith('DB_') for n in names):report['failures'].append({'pier_collision_missing':y,'hit':names})
 assert not report['failures'],report['failures']
 report['passed']=True
def tick(dt):
 global done
 if done:return
 world=u.find_object(None,'/Game/Maps/'+name+'.'+name)
 if not world or time.monotonic()-started<3:return
 pc=u.GameplayStatics.get_player_controller(world,0)
 if not pc or not pc.get_controlled_pawn():
  if time.monotonic()-started<60:return
  report['failures'].append('Game pawn unavailable')
 done=True
 try:
  if not report['failures']:run(world)
 except Exception:report['error']=traceback.format_exc();u.log_error(report['error'])
 finally:
  (R/'Saved/Automation/DreamwallAccessRuntime.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
  u.SystemLibrary.execute_console_command(world,'quit')
handle=u.register_slate_post_tick_callback(tick)
