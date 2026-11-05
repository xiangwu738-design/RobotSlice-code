"""以自拟店名绘制八块旧式店招，不采样照片、品牌标识或原街景贴图。"""
from PIL import Image,ImageDraw,ImageFont,ImageFilter
from pathlib import Path
import random,json
OUT=Path('D:/RobotSlice/SourceAssets/original_developer_maps');OUT.mkdir(parents=True,exist_ok=True)
fontpath='C:/Windows/Fonts/simhei.ttf'
font=ImageFont.truetype(fontpath,148);small=ImageFont.truetype(fontpath,34)
shops=[('益民五金商店','五金工具 · 日用杂货',(142,39,29)),('新光照相馆','彩色冲印 证件快照',(34,75,81)),('大众家电维修','电视机 收录机 修理',(37,58,93)),('春风理发店','理发 · 烫发',(43,96,88)),('友谊副食品店','烟酒糖茶 日用百货',(133,46,30)),('新华钟表修理','修表 配钥匙',(29,64,90)),('幸福面馆','家常面食 经济实惠',(128,71,34)),('便民音像出租','录像带 VCD 光盘',(28,76,71))]
im=Image.new('RGB',(2048,2048));d=ImageDraw.Draw(im);r=random.Random(2603)
for i,(name,sub,col) in enumerate(shops):
 y=i*256;d.rectangle((0,y,2047,y+255),fill=col);d.rectangle((14,y+14,2033,y+241),outline=(209,191,144),width=4)
 box=d.textbbox((0,0),name,font=font);x=(2048-(box[2]-box[0]))/2
 d.text((x+3,y+17),name,font=font,fill=(51,40,32));d.text((x,y+13),name,font=font,fill=(229,215,167))
 box=d.textbbox((0,0),sub,font=small);d.text(((2048-box[2])/2,y+195),sub,font=small,fill=(210,199,163))
 for j in range(1500):
  xx=r.randrange(2048);yy=y+r.randrange(256);sz=r.choice([1,1,2,3]);c=tuple(int(v*.76) for v in im.getpixel((xx,yy)));d.line((xx,yy,xx+sz*3,yy+sz),fill=c,width=1)
im.save(OUT/'T_OS2_Signs.png')
(OUT/'signs_source.json').write_text(json.dumps({'source':'自拟通用店名，Pillow 绘图；系统黑体渲染为像素，不分发字体文件','font_rendered':fontpath,'shops':[s[0] for s in shops]},ensure_ascii=False,indent=2),encoding='utf8')
