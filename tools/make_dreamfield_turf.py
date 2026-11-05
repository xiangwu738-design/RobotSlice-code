"""从空白画布生成无缝草坪明暗与法线；没有摄影素材或外部图集输入。"""
from pathlib import Path
import random,json
import numpy as np
from PIL import Image,ImageDraw,ImageFilter
out=Path('D:/RobotSlice/SourceAssets/dreamfield_original');N=2048;r=random.Random(831104)
img=Image.new('L',(N,N),72);d=ImageDraw.Draw(img)
for layer,count in enumerate((16000,14000,12000)):
 for _ in range(count):
  x,y=r.uniform(0,N),r.uniform(0,N);a=r.uniform(0,6.28318)
  length=r.uniform(22,105);bend=r.uniform(-.5,.5);width=r.randrange(2,6)
  col=r.randrange(70+layer*18,130+layer*30)
  points=[(x+np.cos(a+bend*t)*length*t,y+np.sin(a+bend*t)*length*t) for t in np.linspace(0,1,5)]
  # 跨边的草叶复制到另一侧，缩小时不会出现贴图拼缝。
  for dx in (-N,0,N):
   for dy in (-N,0,N):
    if x+dx < -120 or x+dx>N+120 or y+dy < -120 or y+dy>N+120:continue
    p=[(px+dx,py+dy) for px,py in points]
    d.line(p,fill=col,width=width)
    d.line(p[-2:],fill=col+8,width=1)
h=np.asarray(img.filter(ImageFilter.GaussianBlur(.65)),dtype=np.float32)/255
detail=np.clip(.50+(h-h.mean())*.8,.10,.9)
Image.fromarray((detail*255).astype('uint8')).save(out/'T_DO_TurfDetail.png')
dx=(np.roll(h,-1,1)-np.roll(h,1,1))*2.1;dy=(np.roll(h,-1,0)-np.roll(h,1,0))*2.1
n=np.stack([-dx,dy,np.ones_like(h)],axis=-1);n/=np.linalg.norm(n,axis=-1,keepdims=True)
Image.fromarray(((n*.5+.5)*255).astype('uint8')).save(out/'T_DO_TurfNormal.png')
(out/'turf_source.json').write_text(json.dumps({'seed':831104,'size':N,'source':'数学曲线草叶，无外部图片','detail_mean':float(detail.mean())},ensure_ascii=False,indent=2),encoding='utf-8')
print('Turf generated',float(detail.mean()))
