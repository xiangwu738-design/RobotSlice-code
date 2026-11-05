"""在现有 SM_Gun3 上改造外壳。保留原始 UV、握持和内部机构；不覆盖原资源。"""
import bpy,sys,math,json,hashlib,struct
from pathlib import Path
from mathutils import Vector
sys.path.insert(0,'D:/RobotSlice/tools')
import build_scifi_weapons as b

ROOT=Path('D:/RobotSlice')
OUT=ROOT/'SourceAssets/scifi_weapons/rifle_refit'
bpy.ops.wm.open_mainfile(filepath=str(OUT/'base_components.blend'))
b.OUT=OUT;b.PARTS=[];b.MATS={}
for n,(c,m,r) in b.COMMON.items():b.MATS[n]=b.material('Refit_'+n,c,m,r)
for n,c in b.PALETTES['VHI'].items():b.MATS[n]=b.material('Refit_VHI_'+n,c,.45,.4,1.2 if n=='Glow' else 0)
b.MATS['Paint']=b.material('Refit_VHI_Armor','#677078',.58,.37)
b.MATS['Secondary']=b.material('Refit_VHI_Chassis','#232c32',.65,.4)
b.MATS['Accent']=b.material('Refit_VHI_Oxide','#a34b3e',.3,.45)
b.MATS['Copper']=b.material('Refit_InductionAlloy','#817160',.85,.33)

# 直接沿用原来的四张 PBR 贴图；模型改造过程不重排保留部件的 UV。
mat=bpy.data.materials.new('Refit_Gun3_OriginalPBR');mat.use_nodes=True
nt=mat.node_tree;bs=nt.nodes.get('Principled BSDF')
texdir=ROOT/'SourceAssets/guns/Gun3_tex'
for channel in ('BaseColor','Metallic','Roughness','Normal'):
    node=nt.nodes.new('ShaderNodeTexImage');node.image=bpy.data.images.load(str(next(texdir.glob('*_'+channel+'.png'))),check_existing=True)
    node.image.colorspace_settings.name='sRGB' if channel=='BaseColor' else 'Non-Color'
    if channel=='BaseColor':
        mul=nt.nodes.new('ShaderNodeMixRGB');mul.blend_type='MULTIPLY';mul.inputs[0].default_value=1;mul.inputs[2].default_value=(.42,.46,.5,1)
        nt.links.new(node.outputs['Color'],mul.inputs[1]);nt.links.new(mul.outputs[0],bs.inputs['Base Color'])
    elif channel=='Normal':
        normal=nt.nodes.new('ShaderNodeNormalMap');normal.inputs['Strength'].default_value=.65
        nt.links.new(node.outputs['Color'],normal.inputs['Color']);nt.links.new(normal.outputs[0],bs.inputs['Normal'])
    elif channel=='Roughness':
        rr=nt.nodes.new('ShaderNodeMapRange');rr.inputs['To Min'].default_value=.3;rr.inputs['To Max'].default_value=.7
        nt.links.new(node.outputs['Color'],rr.inputs[0]);nt.links.new(rr.outputs[0],bs.inputs[channel])
    else:nt.links.new(node.outputs['Color'],bs.inputs[channel])

base={int(o.name.split('_')[-1]):o for o in bpy.context.scene.objects if o.type=='MESH'}
for o in base.values():o.data.materials.clear();o.data.materials.append(mat)

def mesh_fingerprint(o):
    data=bytearray()
    for v in o.data.vertices:data.extend(struct.pack('fff',*(o.matrix_world@v.co)))
    for layer in o.data.uv_layers:
        for uv in layer.data:data.extend(struct.pack('ff',*uv.uv))
    return hashlib.sha256(data).hexdigest()
contact_ids=(19,33,39,47,60,61)
contact_before={i:mesh_fingerprint(base[i]) for i in contact_ids}

def joined_copy(objects,name):
    bpy.ops.object.select_all(action='DESELECT');dups=[]
    for ob in objects:
        cp=ob.copy();cp.data=ob.data.copy();bpy.context.collection.objects.link(cp);cp.select_set(True);dups.append(cp)
    bpy.context.view_layer.objects.active=dups[0];bpy.ops.object.join();o=bpy.context.object;o.name=name
    return o

def preview(objects,name,clay=False):
    # 两版使用同一构图与光源，便于比较真实外形变化。
    ob=joined_copy(objects,name)
    others=[o for o in bpy.context.scene.objects if o.type=='MESH' and o!=ob]
    for o in others:o.hide_render=True
    b.render(ob,dict(key=name))
    sc=bpy.context.scene;sc.cycles.samples=48
    center=Vector((.14,0,.12));length=1.25
    cam=sc.camera;cam.location=center+Vector((length*.63,-length*1.5,length*.57))
    cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.ortho_scale=1.39
    sc.render.filepath=str(OUT/(name+'_quarter.png'));bpy.ops.render.render(write_still=True)
    if clay:
        sc.view_layers[0].material_override=b.material('Refit_Clay','#777777',0,.55)
        sc.render.filepath=str(OUT/(name+'_clay.png'));bpy.ops.render.render(write_still=True)
        sc.view_layers[0].material_override=None
    bpy.data.objects.remove(ob,do_unlink=True)
    for o in others:o.hide_render=False
    for o in list(bpy.context.scene.objects):
        if o.type in ('CAMERA','LIGHT'):bpy.data.objects.remove(o,do_unlink=True)

preview(list(base.values()),'Original')

# 旧外盖留在独立参考文件，不在改造文件里叠两层。
removed={0,1,20,21,26,27,36,40,41,42,43,48,49}
for i in removed:bpy.data.objects.remove(base.pop(i),do_unlink=True)
for i,o in base.items():o.name=f'原模型保留_{i:03}'
# 承力纵梁与新装甲统一为深色金属，保留少量机芯钢件作为材质对比。
for i in (28,29,32,44,46,58,59,63,64,65,66,67,68):
    base[i].data.materials.clear();base[i].data.materials.append(b.MATS['Frame'])

def normals(o):
    bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.mesh.normals_make_consistent(inside=False);bpy.ops.object.mode_set(mode='OBJECT')

def profile(name,pts,width,mat='Paint',y=0):
    n=len(pts);vs=[(x/100,(y+s*width/2)/100,z/100) for s in (-1,1) for x,z in pts]
    fs=[tuple(reversed(range(n))),tuple(range(n,n*2))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    me=bpy.data.meshes.new(name);me.from_pydata(vs,[],fs);me.update()
    o=bpy.data.objects.new(name,me);bpy.context.collection.objects.link(o);o.data.materials.append(b.MATS[mat]);b.PARTS.append(o);normals(o);return o

def loft(name,sections,mat='Paint'):
    vs=[]
    for x,lo,hi,w,c in sections:
        vs.extend([(x,-w+c,lo),(x,w-c,lo),(x,w,lo+c),(x,w,hi-c),(x,w-c,hi),(x,-w+c,hi),(x,-w,hi-c),(x,-w,lo+c)])
    fs=[tuple(reversed(range(8))),tuple(range((len(sections)-1)*8,len(sections)*8))]
    for k in range(len(sections)-1):
        for j in range(8):fs.append((k*8+j,k*8+(j+1)%8,(k+1)*8+(j+1)%8,(k+1)*8+j))
    me=bpy.data.meshes.new(name);me.from_pydata([tuple(c/100 for c in v) for v in vs],[],fs);me.update()
    o=bpy.data.objects.new(name,me);bpy.context.collection.objects.link(o);o.data.materials.append(b.MATS[mat]);b.PARTS.append(o);normals(o);return o

def bevel(o,width=.09):
    normals(o);bpy.context.view_layer.objects.active=o
    m=o.modifiers.new('加工倒角','BEVEL');m.width=width/100;m.segments=3;bpy.ops.object.modifier_apply(modifier=m.name)
    for p in o.data.polygons:p.use_smooth=True
    m=o.modifiers.new('硬表面法线','WEIGHTED_NORMAL');m.keep_sharp=True;m.weight=50
    return o

def cut(o,c):
    normals(c);normals(o);bpy.context.view_layer.objects.active=o
    m=o.modifiers.new('结构开孔','BOOLEAN');m.operation='DIFFERENCE';m.solver='EXACT';m.object=c
    bpy.ops.object.modifier_apply(modifier=m.name)
    b.PARTS.remove(c);bpy.data.objects.remove(c,do_unlink=True)

# 新护罩包围原机芯。两侧开窗露出原来的精细散热夹具。
shroud=loft('新制折面前护罩',[(17.8,14.0,22.7,5.2,.7),(21,13.8,23.5,5.85,1.2),
    (46,13.8,23.5,5.85,1.2),(50,14,23.1,5.3,.95),(68.5,14.4,22.4,4.7,.9),(73,15.2,20.8,3.5,.65)])
inside=loft('护罩内部空腔',[(17,14.8,22.3,5.0,.4),(46,14.8,22.3,5.1,.4),
    (50,14.8,22.0,4.55,.4),(69,15.2,21.3,4.0,.4),(74,15.8,20.2,3,.35)],'Black')
cut(shroud,inside)
for sign in (-1,1):
    for a,e in ((21.2,29.5),(31.5,39.5),(41.5,46.5)):
        pts=[(a,16),(e-.9,16),(e,19.8),(e-.8,21),(a+.5,21),(a,20)]
        cut(shroud,bevel(profile('散热机芯观察窗',pts,3,'Black',sign*5.2),.17))
    for a in (52,58,64):
        pts=[(a,16.1),(a+3.4,16.1),(a+4,19.8),(a+.6,19.8)]
        cut(shroud,bevel(profile('前级散热窗',pts,3,'Black',sign*4.4),.14))
    # 外壳上的细窄阴刻分缝，与实际模块分界对应。
    cut(shroud,profile('模块锁缝',[(48.3,14),(48.48,14),(48.48,23),(48.3,23)],1,'Black',sign*5.3))
bevel(shroud,.1)

# 覆盖原前部芯块的侧面为细密散热片；原枪管、下部承架保留。
for sign in (-1,1):
    backing=profile('前级热沉底',[(50.4,15),(69.6,15),(70.3,19.6),(51.2,20.1)],.12,'Secondary',sign*3.8);bevel(backing,.06)
    for j in range(22):
        x=51.2+j*.82
        b.bar('前级热沉鳍',(x,sign*3.94,15.7),(x+.38,sign*3.94,19.5),.17,.3,'Copper',.025)
    # 窄幅品牌色在散热器之下，显露即可，不把灯条铺满枪身。
    b.box('护罩品牌线',(32.5,sign*5.91,14.9),(17,.055,.23),'Accent',.025)
    for x in (19.4,47.8):
        b.cylinder('护罩沉头销',(x,sign*5.48,16.0),.29,.16,'Steel','Y',24,.02)

# 后装甲顺着原枪托的折线闭合，预留原来的操作区域和握把。
for sign in (-1,1):
    armor=profile('后机匣整体装甲',[(-44.4,5.6),(-41.6,1.4),(-27.2,1.4),(-18.3,7.9),(-13.7,13.8),(-40.7,13.8),(-44.4,11.6)],1.2,'Paint',sign*3.28)
    pocket=[(-39,6),(-34.3,3.7),(-27.8,3.7),(-22.5,7.9),(-24.6,10.4),(-38.8,10.4)]
    cut(armor,bevel(profile('后部电容嵌舱',pocket,1.1,'Black',sign*3.83),.15))
    bevel(armor,.15)
    inner=profile('电容舱内盖',[(-38.5,6.3),(-34.2,4.2),(-28,4.2),(-23.1,8),(-24.8,9.9),(-38.5,9.9)],.12,'Secondary',sign*3.4);bevel(inner,.08)
    for x in (-35.3,-32.6,-29.9):
        b.bar('埋入式电容触点',(x,sign*3.52,5.5),(x+.6,sign*3.52,8.8),.55,.15,'Copper',.06)
    b.box('电容状态窗',(-26.5,sign*3.54,8.7),(2.7,.12,.8),'Black',.1)
    for j in range(3):b.box('电容状态灯',(-27.3+j*.75,sign*3.62,8.7),(.33,.03,.4),'Accent',.025)
    for x,z in ((-41.5,10.9),(-27,12.1),(-19.4,10.9)):
        b.cylinder('装甲固定轴',(x,sign*3.96,z),.27,.12,'Secondary','Y',24,.02)
        b.cylinder('装甲内六角',(x,sign*4.03,z),.12,.015,'Black','Y',6,0)
    # 贴合旧上机匣的薄肩托盖，沿原枪轴收窄。
cheek=loft('新肩托脊盖',[(-40,17.5,20.3,3.2,.6),(-36,17.4,22.1,3.6,.8),(-25,17.4,22.1,3.6,.8),(-20,17.8,20.8,2.8,.55)])
bevel(cheek,.16)

# 原瞄具保留内部光学件；下压外形高度，使它属于原有安装座。
for i in (18,23,24,25,31):
    for v in base[i].data.vertices:
        v.co.z=(25.4+(v.co.z*100-25.4)*.78)/100
# 用暗色光学框架衬出已有镜片，不修改其 UV。
for i in (18,24,25):base[i].data.materials.clear();base[i].data.materials.append(b.MATS['Secondary'])

# 新枪口护圈与原枪管同轴，末端仍是游戏规定的 77 厘米。
nose=loft('前端定向护圈',[(72.8,14.2,20.6,3.0,.6),(76,14.4,20.2,2.65,.6),(77,15,19.6,2.3,.55)],'Secondary')
bore=b.cylinder('护圈枪膛孔',(75,0,17.26),1.58,6,'Black','X',40,0)
for m in list(bore.modifiers):bore.modifiers.remove(m)
cut(nose,bore);bevel(nose,.08)
b.ring('枪口内衬',76.4,17.26,1.56,1.17,1.2,'Steel',n=32)

# 标识落在装甲实面，字号与原枪的结构尺寸匹配。
b.label('VHI  /  R36',-36.8,-3.91,11.3,.82)
b.label('THERMAL CELL  /  04',-40.2,-3.91,2.7,.33)
b.label('36',49.6,-5.13,20.6,1.3)
for sign in (-1,1):b.box('后部品牌刻线',(-38.9,sign*3.93,12.1),(1.8,.04,.3),'Accent',.03)

# 新造部件单独展开，原部件的法线、UV、握持尺寸全部保留。
for o in b.PARTS:
    bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
    for m in list(o.modifiers):bpy.ops.object.modifier_apply(modifier=m.name)
    normals(o)
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.uv.smart_project(angle_limit=math.radians(70),island_margin=.01);bpy.ops.object.mode_set(mode='OBJECT')
allparts=list(base.values())+b.PARTS
bpy.ops.file.pack_all()
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'R36_refit_editable.blend'))
combined=joined_copy(allparts,'SM_SF_RifleRefit')
bpy.context.scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
bpy.ops.export_scene.fbx(filepath=str(OUT/'SM_SF_RifleRefit.fbx'),use_selection=True,object_types={'MESH'},apply_unit_scale=True,mesh_smooth_type='FACE',add_leaf_bones=False)
combined.data.calc_loop_triangles()
report=dict(source='/Game/Weapons/Meshes/SM_Gun3',source_components=70,retained_components=len(base),replaced_components=sorted(removed),
    added_components=len(b.PARTS),triangles=len(combined.data.loop_triangles),status='geometry_preview_not_integrated',
    retained_uv=True,grip_geometry_retained=True,muzzle=[77,0,17],script='tools/refit_existing_rifle.py')
assert all(mesh_fingerprint(base[i])==contact_before[i] for i in contact_ids), '握持部件或原 UV 发生变化'
report['contact_mesh_and_uv_checks']={str(i):'unchanged' for i in contact_ids}
report['source_fbx_sha256']=hashlib.sha256((ROOT/'SourceAssets/guns/SM_Gun3.fbx').read_bytes()).hexdigest()
(OUT/'refit_manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
bpy.data.objects.remove(combined,do_unlink=True)
preview(allparts,'Refit',clay=True)
print('[REFIT]',json.dumps(report),flush=True)
