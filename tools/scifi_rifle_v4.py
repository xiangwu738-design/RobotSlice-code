"""步枪造型研究：连续变截面壳体、真实开孔、嵌入式接缝。
独立候选，不修改当前装备表或已导入资源。
"""
import sys, math, json
from pathlib import Path
sys.path.insert(0, 'D:/RobotSlice/tools')
import bpy
from mathutils import Vector
import build_scifi_weapons as b

OUT=Path('D:/RobotSlice/SourceAssets/scifi_weapons/rifle_v4')
OUT.mkdir(parents=True, exist_ok=True)
b.OUT=OUT
b.setup('VHI')
d=b.GUNS[0]
b.MATS['Paint']=b.material('SF_VHI_GraphiteCoating','#4c565d',.4,.4)
b.MATS['Secondary']=b.material('SF_AnodizedChassis','#242c33',.4,.48)
b.MATS['Steel']=b.material('SF_BeadBlastedSteel','#829099',.88,.32)
b.MATS['Accent']=b.material('SF_VHI_OxideMark','#994741',.18,.52)

def finish_material(mat,colour,rough):
    nt=mat.node_tree;nodes=nt.nodes;links=nt.links
    bs=nodes.get('Principled BSDF');co=b.lin(colour)
    tex=nodes.new('ShaderNodeTexCoord')
    noise=nodes.new('ShaderNodeTexNoise');noise.inputs['Scale'].default_value=75
    noise.inputs['Detail'].default_value=3
    links.new(tex.outputs['Object'],noise.inputs['Vector'])
    ramp=nodes.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].color=tuple(v*.83 for v in co)+(1,)
    ramp.color_ramp.elements[1].color=tuple(v*1.07 for v in co)+(1,)
    links.new(noise.outputs['Fac'],ramp.inputs[0]);links.new(ramp.outputs[0],bs.inputs['Base Color'])
    rr=nodes.new('ShaderNodeMapRange');rr.inputs['To Min'].default_value=rough-.04;rr.inputs['To Max'].default_value=rough+.05
    links.new(noise.outputs['Fac'],rr.inputs[0]);links.new(rr.outputs[0],bs.inputs['Roughness'])
    grain=nodes.new('ShaderNodeTexNoise');grain.inputs['Scale'].default_value=2100;grain.inputs['Detail'].default_value=2
    links.new(tex.outputs['Object'],grain.inputs['Vector'])
    bump=nodes.new('ShaderNodeBump');bump.inputs['Strength'].default_value=.14;bump.inputs['Distance'].default_value=.000035
    links.new(grain.outputs['Fac'],bump.inputs['Height']);links.new(bump.outputs[0],bs.inputs['Normal'])
finish_material(b.MATS['Paint'],'#4c565d',.4)
finish_material(b.MATS['Secondary'],'#242c33',.48)
finish_material(b.MATS['Rubber'],'#171c20',.73)

def normals(o):
    bpy.ops.object.select_all(action='DESELECT')
    o.select_set(True); bpy.context.view_layer.objects.active=o
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.normals_make_consistent(inside=False);bpy.ops.object.mode_set(mode='OBJECT')

def mesh(name,verts,faces,mat):
    me=bpy.data.meshes.new(name);me.from_pydata([tuple(v/100 for v in p) for p in verts],[],faces);me.update()
    o=bpy.data.objects.new(name,me);bpy.context.collection.objects.link(o)
    o.data.materials.append(b.MATS[mat]);b.PARTS.append(o);normals(o)
    return o

def bevel(o,w=.07):
    normals(o)
    bpy.context.view_layer.objects.active=o
    m=o.modifiers.new('制造圆角','BEVEL');m.width=w/100;m.segments=2
    bpy.ops.object.modifier_apply(modifier=m.name)
    for p in o.data.polygons:p.use_smooth=True
    m=o.modifiers.new('保留大平面','WEIGHTED_NORMAL');m.keep_sharp=True;m.weight=50
    return o

def profile(name,pts,width,mat='Secondary',y=0):
    n=len(pts)
    return mesh(name,[(x,y+s*width/2,z) for s in (-1,1) for x,z in pts],
        [tuple(reversed(range(n))),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)],mat)

def loft(name,sections,mat='Paint'):
    # 每个截面独立控制宽度及上下折面，避免整块二维侧板挤出的观感。
    verts=[]
    for x,lo,hi,w,c in sections:
        if name=='一体式上机匣':
            # 顶部宽斜肩过渡到中央脊线；侧面不再是一整张竖直平板。
            verts += [(x,-w+c,lo),(x,w-c,lo),(x,w,lo+c),
                      (x,w,hi-c*1.8),(x,w-c*.65,hi-c*.45),
                      (x,w*.35,hi+.6),(x,-w*.35,hi+.6),
                      (x,-w+c*.65,hi-c*.45),(x,-w,hi-c*1.8),(x,-w,lo+c)]
        else:
            verts += [(x,-w+c,lo),(x,w-c,lo),(x,w,lo+c),(x,w,hi-c),
                      (x,w-c,hi),(x,-w+c,hi),(x,-w,hi-c),(x,-w,lo+c)]
    n=10 if name=='一体式上机匣' else 8
    faces=[tuple(reversed(range(n))),tuple(range((len(sections)-1)*n,len(sections)*n))]
    for k in range(len(sections)-1):
        for j in range(n):faces.append((k*n+j,k*n+(j+1)%n,(k+1)*n+(j+1)%n,(k+1)*n+j))
    return mesh(name,verts,faces,mat)

def cut(o,cutter):
    normals(cutter);normals(o)
    m=o.modifiers.new('实切凹槽','BOOLEAN');m.operation='DIFFERENCE';m.solver='EXACT';m.object=cutter
    bpy.context.view_layer.objects.active=o;bpy.ops.object.modifier_apply(modifier=m.name)
    b.PARTS.remove(cutter);bpy.data.objects.remove(cutter,do_unlink=True)

def prism(name,pts,w,mat='Secondary',y=0,rad=.08):
    return bevel(profile(name,pts,w,mat,y),rad)

def slot(o,name,pts,y,depth,rad=.06):
    c=profile(name,pts,depth,'Black',y)
    if rad:
        bevel(c,rad)
        for m in list(c.modifiers):c.modifiers.remove(m)
    cut(o,c)

def fastener(o,x,z,y,r=.36):
    sign=1 if y>0 else -1
    # 头部低于外壳，不是凸在侧板上的银色铆钉。
    c=b.cylinder('螺钉沉孔',(x,y,z),r+.13,.5,'Black','Y',24,.025)
    for m in list(c.modifiers):c.modifiers.remove(m)
    cut(o,c)
    b.cylinder('内六角紧固件',(x,y-sign*.12,z),r,.12,'Secondary','Y',24,.025)
    b.cylinder('六角扳手孔',(x,y-sign*.045,z),r*.43,.015,'Black','Y',6,0)

# 从肩部到枪口的主壳是一条连续折面；接缝从属于这条轮廓。
shell=loft('一体式上机匣',[
    (-43,16.2,22.8,3.1,.8),(-39,15.5,24.2,3.35,1.0),
    (-22,14.8,24.7,3.45,1.05),(-12,13.4,25,4.0,1.05),
    (9,13.4,25,4.0,1.05),(21,14.4,23.8,3.85,1.0),
    (48,14.4,23.8,3.85,1.0),(65,14.9,22.3,3.4,.9),(70,15.8,20.8,2.9,.7)])

# 两侧纵向减重槽在实体壳体里切出，形成主次折面而非外挂盖板。
for sign in (-1,1):
    front=[(25,16.8),(57.5,16.8),(64,18.1),(64,20.2),(57,22),(27,22),(24.5,20.4)]
    slot(shell,'护木铣削沉台',front,sign*4.0,2.5,.14)
    # 沉台后面的深色薄钢衬贴着槽底，轮廓与壳体一致。
    prism('护木热屏蔽衬板',[(25.4,17.1),(57.4,17.1),(63.6,18.3),(63.6,20),(56.9,21.7),(27.1,21.7),(24.9,20.3)],.1,'Secondary',sign*2.82,.06)
    rear=[(-39,18.2),(-24,17.8),(-15.6,19.4),(-15.6,21.4),(-35,21.9),(-39,20.8)]
    slot(shell,'肩托纵向减重槽',rear,sign*3.55,1.1,.11)
    prism('肩托内嵌纵梁',[(-38.4,18.7),(-24,18.3),(-16.3,19.8),(-16.3,20.8),(-35,21.3),(-38.4,20.4)],.12,'Secondary',sign*2.97,.07)
    # 尾部压入式锁销及细窄接缝，均在肩托轮廓之内。
    b.cylinder('肩托锁销',(-36.6,sign*3.09,19.7),.42,.14,'Steel','Y',24,.03)
    b.box('肩托锁销槽',(-36.6,sign*3.17,19.7),(.41,.015,.08),'Black',.015)

# 两侧真实凹槽贯穿护木的厚壁；内部暗色散热件有实际深度。
for sign in (-1,1):
    for x in (31,38,45,52):
        pts=[(x,17.8),(x+4.6,17.8),(x+5.4,20.1),(x+.8,20.1)]
        slot(shell,'护木散热窗口',pts,sign*3.5,3.5,.14)
        # 连同内衬一起开口；避免外壳有孔但内衬像贴纸堵住。
        for lining in list(b.PARTS):
            if lining.name.startswith('护木热屏蔽衬板') and lining.location.y==0:
                slot(lining,'内衬散热孔',pts,sign*2.82,.35,.1)
        prism('散热腔底',pts,.08,'Black',sign*1.8,.04)
        for j in range(4):
            xx=x+.9+j*.83
            b.bar('腔内散热鳍',(xx,sign*2.05,18.1),(xx+.4,sign*2.05,19.7),.13,.17,'Steel',.015)
    # 实切浅槽和与壳体齐平的维护盖，间隙只有约 1mm。
    pocket=[(-10,16.2),(4.5,16.2),(7,18.3),(7,21.5),(-10,21.5)]
    slot(shell,'维护盖嵌槽',pocket,sign*4.04,1.25,.12)
    cover=[(-9.87,16.33),(4.45,16.33),(6.87,18.35),(6.87,21.37),(-9.87,21.37)]
    prism('内嵌维护盖',cover,.13,'Paint',sign*3.51,.04)
    for x,z in [(-8.9,17.4),(5.4,20.2),(23.5,21.6),(61.5,16.2)]:
        # 变截面前端另设紧固深度。
        if x<10:continue
        yy=3.85 if x<48 else 3.52
        fastener(shell,x,z,sign*yy,.29)

# 非对称抛壳口，内滑块低于周围的机匣表面。
port=[(8.4,16.6),(19.5,16.6),(21.1,18.4),(20.6,20.8),(8.4,20.8)]
slot(shell,'侧置操作窗口',port,-3.95,2.7,.15)
prism('操作窗口内壁',port,.12,'Black',-2.55,.06)
prism('滑动闭锁机构',[(9,17.1),(18.8,17.1),(20.3,18.5),(20,20.1),(9,20.1)],.22,'Steel',-2.95,.09)
b.box('闭锁滑轨',(14.2,-3.09,18.7),(8.2,.16,.45),'Secondary',.06)
b.box('收折拉机柄',(10.1,-3.6,18.65),(2.6,1.3,.9),'Secondary',.14)

# 在机匣上切出上段散热细缝，避免用全长齿条打断主体线条。
for x in (-20,-16,-12):
    slot(shell,'后部导热口',[(x,22),(x+2.5,22),(x+2.9,22.7),(x+.4,22.7)],-3.8,1.9,.07)
# 上表面有窄的凹入安装槽，不以厚齿条遮盖主轮廓。
topcut=b.box('顶部接口槽刀具',(37.8,0,24.4),(27,2.4,.9),'Black',.15)
for m in list(topcut.modifiers):topcut.modifiers.remove(m)
cut(shell,topcut)
b.box('顶部接口槽底',(37.8,0,24.0),(26.4,2,.08),'Secondary',.08)
for x in (27,32,37,42,47):b.box('沉入接口齿',(x,0,24.15),(.7,1.9,.23),'Steel',.05)
bevel(shell,.075)

# 连续下框架同时承接肩托、弹仓、握把和扳机环。
lower=profile('整体下机匣及肩托骨架',[
    (-44,5),(-39,3),(-27,3),(-22,5),(-14,5),(-9,8),(-5,9),
    (-3,10),(3,10),(4.3,3.4),(10,3.2),(14,5),(17,11.8),(41,12.8),
    (44,15.1),(20,15.8),(-12,15.8),(-39,18),(-44,17)],5.6)
slot(lower,'肩托镂空',[(-39,7),(-29,6),(-24.5,8.6),(-24.5,13.8),(-39,15)],0,9,.7)
slot(lower,'扳机护圈开口',[(4.3,5.2),(9.5,4.9),(12.2,6.2),(13.3,10.8),(3.8,10.8)],0,9,.5)
bevel(lower,.12)
# 背部动力匣装入骨架，用明显的插接方向而不是表面贴块。
pack=prism('后置供能匣',[(-23.2,5.5),(-20.4,.5),(-11.1,.5),(-8.8,8.9),(-10,14.1),(-22,14.3)],5.5,'Secondary',rad=.18)
for sign in (-1,1):
    inset=prism('动力匣防滑面',[(-21,5),(-19.3,2),(-12.5,2),(-11.5,7.5),(-12.2,11.7),(-20.6,11.7)],.08,'Rubber',sign*2.79,.08)
    for x in (-19.5,-17.5,-15.5):
        slot(inset,'握持细槽',[(x,4),(x+.22,4),(x+.8,9),(x+.58,9)],sign*2.83,.2,.025)
    b.box('匣体电量窗',(-16.3,sign*2.86,12.8),(5.1,.04,.38),'Black',.04)
    for x in (-18,-17,-16):b.box('电量指示',(x,sign*2.9,12.8),(.55,.03,.18),'Accent',.015)

# 掌面按握持坐标设计成锥形截面，去除外贴横圈。
handle=prism('整体握把芯',[(-5.2,-5.8),(1.5,-6.3),(4.5,7.8),(2,11.9),(-3.2,11.3),(-5.5,7.1),(-7,-3.5)],4.35,'Secondary',rad=.22)
for sign in (-1,1):
    prism('嵌入掌垫',[(-4.7,-4.8),(.4,-5.1),(2.5,5.9),(.6,8.3),(-3.6,7.8),(-5.7,-2.7)],.1,'Rubber',sign*2.22,.16)
    for i in range(7):
        z=-3.5+i*1.25
        b.box('掌垫浅纹',(-2.1+.16*z,sign*2.29,z),(3.7,.025,.065),'Secondary',.015,rot=(0,-9,0))
b.bar('扳机',(6.5,0,11),(5.9,0,6.3),.6,.9,'Steel',.1)

# 前握持位置保留，直接包住下机匣的窄腰。
prism('前掌防滑托',[(22.2,10),(23.8,9),(36.8,9),(39,11.7),(38,14.1),(22.2,14.1)],3.2,'Rubber',rad=.23)
for x in (25,28,31,34):b.box('前掌排水槽',(x,0,9.01),(.12,2.5,.05),'Secondary',.015)

# 肩垫与上壳以同一斜面相接。
prism('肩托缓冲垫',[(-46,5.4),(-43.8,4.2),(-42.7,6),(-42.7,22.8),(-44.3,23.2),(-46,21.5)],6.5,'Rubber',rad=.25)
for z in (8,11,14,17,20):b.box('肩垫压纹',(-46.02,0,z),(.05,5,.11),'Secondary',.015)

# 枪口采用与外壳一致的八边截面，开口可看到内壁和实际枪膛。
nose=loft('枪口整流罩',[(69,14.5,21.7,3.45,.9),(74.8,14.5,20.6,3.15,.8),(77,15.1,19.7,2.55,.7)],'Secondary')
c=b.cylinder('枪口贯穿孔',(74,0,17),1.53,12,'Black','X',32,0)
for m in list(c.modifiers):c.modifiers.remove(m)
cut(nose,c)
for sign in (-1,1):
    slot(nose,'制退排气口',[(71,16),(73.5,16),(73.5,19),(71,19)],sign*3.1,2,.12)
bevel(nose,.06)
b.ring('枪膛钢衬',75,17,1.52,1.17,3.8,'Steel',n=32)
b.cylinder('膛内暗面',(72.9,0,17),1.16,.06,'Black','X',32,0)

# 低矮光学模块由一个实体外壳开孔，底座沿主壳的折面贴合。
prism('瞄具鞍座',[(-10,24.6),(11,24.6),(10,26.4),(-9,26.4)],4.0,'Secondary',rad=.1)
optic=loft('一体光学模块',[(-8.7,26.1,29.5,2.45,.6),(-6,26.1,31.3,2.8,.65),(5.5,26.1,31.3,2.8,.65),(8.5,26.1,28.8,2.25,.5)],'Secondary')
window=loft('光路贯穿孔',[(-10,27,29.1,1.75,.35),(10,27,29.1,1.75,.35)],'Black')
cut(optic,window);bevel(optic,.09)
b.box('前光学玻璃',(6.4,0,28.05),(.07,3.35,1.9),'Lens',.03)
for sign in (-1,1):
    b.cylinder('光学调节钮',(-5,sign*2.83,28.2),.65,.3,'Secondary','Y',32,.05)
    b.box('光学旋钮刻度',(-5,sign*3.0,28.2),(.6,.03,.07),'Mark',.01)
    b.box('模式指示槽',(2.8,sign*2.81,28.2),(2.3,.035,.4),'Black',.04)
    b.box('模式指示点',(2.1,sign*2.84,28.2),(.42,.025,.15),'Accent',.02)

# 控件、刻线与识别码控制在小尺度，侧面大部分留给结构。
for sign in (-1,1):
    b.cylinder('快慢机转轴',(-5.8,sign*2.96,12.3),.65,.3,'Secondary','Y',24,.05)
    b.box('快慢机柄',(-5.1,sign*3.16,12.4),(1.9,.24,.38),'Steel',.08,rot=(0,-18,0))
    b.cylinder('拆解横销',(15.3,sign*2.89,13.3),.32,.12,'Steel','Y',24,.025)
    b.box('品牌识别嵌条',(-11,sign*3.98,18.7),(.36,.04,2.7),'Accent',.02)
b.label('VHI  /  R36',-8.9,-3.59,19.1,.83)
b.label('ELECTROTHERMAL   /   MK.IV',-8.9,-3.59,17.9,.32)
b.label('06  2418',24.0,-3.89,16.1,.38)
b.label('HOT',58,-3.76,20.8,.43)
# 激光蚀刻条码，不用大面积高对比涂块充当细节。
for i in range(21):
    b.box('追溯码',(24+i*.19,-3.892,16.9),(.04 if i%3 else .1,.016,.42),'Mark',.005)

# 以纵向截面控制真实厚薄：后托、机匣、护木、枪口各有不同体积。
# 同时变换外壳及其内部机构，保留凹槽/紧固件间的装配关系。
def width_factor(x):
    stations=[(-46,1),(-39,1.08),(-22,1.12),(-12,1.45),(9,1.45),(21,1.05),(48,.96),(65,1.03),(77,1.13)]
    for (a,fa),(e,fe) in zip(stations,stations[1:]):
        if a<=x<=e:return fa+(fe-fa)*(x-a)/(e-a)
    return stations[0][1] if x<stations[0][0] else stations[-1][1]

for o in b.PARTS:
    bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
    for mod in list(o.modifiers):bpy.ops.object.modifier_apply(modifier=mod.name)
    inv=o.matrix_world.inverted()
    # 下框架和供能匣不参与扩张，避免非平面多边形跨过掌面。
    upper=min((o.matrix_world@v.co).z for v in o.data.vertices)*100>=13.0
    if upper:
        for v in o.data.vertices:
            p=o.matrix_world@v.co;x=p.x*100
            p.y*=width_factor(x)
            v.co=inv@p
    normals(o)
    if o.name=='一体式上机匣':
        for face in o.data.polygons:face.use_smooth=True
        split=o.modifiers.new('保留折肩硬边','EDGE_SPLIT');split.split_angle=math.radians(18)
        split.use_edge_angle=True;split.use_edge_sharp=True
        bpy.context.view_layer.objects.active=o;bpy.ops.object.modifier_apply(modifier=split.name)
    m=o.modifiers.new('变截面后的平面法线','WEIGHTED_NORMAL');m.keep_sharp=True;m.weight=50

# 编辑源保留所有命名组件；导出的运行时网格另存一个文件。
for o in b.PARTS:
    if o.type=='MESH':normals(o)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'R36_editable.blend'))
o,meta=b.recalc_and_join(d)
b.render(o,d)
sc=bpy.context.scene
sc.cycles.samples=64
cam=sc.camera
corners=[o.matrix_world@Vector(v) for v in o.bound_box];center=sum(corners,Vector())/8
length=max(v.x for v in corners)-min(v.x for v in corners)
cam.location=center+Vector((length*.63,-length*1.5,length*.57))
cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler()
cam.data.ortho_scale=length*1.12
sc.render.filepath=str(OUT/'R36_threequarter.png');bpy.ops.render.render(write_still=True)
# 增加前上方视角，直接检查机匣宽度、收腰、槽深。
cam.location=center+Vector((length*.95,-length*1.15,length*.85))
cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler()
cam.data.ortho_scale=length*1.06
sc.render.filepath=str(OUT/'R36_depth.png');bpy.ops.render.render(write_still=True)
# 中性灰材质检验实体结构，排除颜色遮掩几何问题。
clay=b.material('几何检验灰','#777777',0,.55)
sc.view_layers[0].material_override=clay
sc.render.filepath=str(OUT/'R36_clay.png');bpy.ops.render.render(write_still=True)
sc.view_layers[0].material_override=None
print('[R36 V4]',json.dumps(meta),flush=True)
