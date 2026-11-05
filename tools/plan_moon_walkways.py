"""采样原月面，计算下载平台模块的摆放高度；不修改月面或创建网格。"""
from pathlib import Path
import numpy as np,json,math
R=Path('D:/RobotSlice');h=np.load(R/'Inbox/_moon_nasa/work/near.npy')
def ground(x,y):
 fx=x/2+504;fy=y/2+504;ix=int(math.floor(fx));iy=int(math.floor(fy));a=fx-ix;b=fy-iy
 return float((1-a)*(1-b)*h[iy,ix]+a*(1-b)*h[iy,ix+1]+(1-a)*b*h[iy+1,ix]+a*b*h[iy+1,ix+1])
buildings=json.loads((R/'Saved/Automation/MoonFreeScene.json').read_text())['buildings']
find=lambda x,y:next(b for b in buildings if b['x']==x and b['y']==y and b['kind']!='control')
floor=ground(0,0)+.06
routes=[('east', (58,0,floor),(92,0,find(112,0)['z']+.015)),('west',(-58,0,floor),(-92,0,find(-112,0)['z']+.015)),('north',(0,58,floor),(0,126,find(0,150)['z']+.015)),('south',(0,-58,floor),(0,-190,find(0,-218)['z']+.015)),('east_outer',(132,0,find(112,0)['z']+.015),(178,0,find(214,0)['z']+.015)),('west_outer',(-132,0,find(-112,0)['z']+.015),(-180,0,find(-212,0)['z']+.015))]
result=[]
for name,start,end in routes:
 dx=end[0]-start[0];dy=end[1]-start[1];length=math.hypot(dx,dy);count=math.ceil(length/4);width=8;nx=-dy/length;ny=dx/length
 points=[]
 for i in range(count+1):
  t=i/count;x=start[0]+dx*t;y=start[1]+dy*t
  terrain=max(ground(x+nx*w,y+ny*w) for w in (-4,-2,0,2,4))
  z=max(start[2]+(end[2]-start[2])*t,terrain+.045)
  points.append([x,y,z])
 # 从地形最高点向两端平滑接坡，控制道路最大坡度为 10%，两端必须对上原房间地板。
 for _ in range(2):
  for i in range(1,len(points)):points[i][2]=max(points[i][2],points[i-1][2]-.1*length/count)
  for i in range(len(points)-2,-1,-1):points[i][2]=max(points[i][2],points[i+1][2]-.1*length/count)
 assert abs(points[0][2]-start[2])<.04 and abs(points[-1][2]-end[2])<.04,(name,points[0],start,points[-1],end)
 slope=max(abs(points[i+1][2]-points[i][2])/(length/count) for i in range(count))
 result.append({'name':name,'width':width,'points':points,'length':length,'max_grade':slope})
out=R/'SourceAssets/moon_free/walkways.json';out.write_text(json.dumps({'retained_floor_top':floor,'routes':result},indent=2))
print([(r['name'],round(r['length'],2),round(r['max_grade'],4)) for r in result])
