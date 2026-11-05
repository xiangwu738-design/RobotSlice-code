"""原模型改造的共用工具：原 UV/握持不动，新增装甲采用机体三色槽。"""
import bpy,math,json,hashlib,struct,sys
from pathlib import Path
from mathutils import Vector
sys.path.insert(0,'D:/RobotSlice/tools')
import build_scifi_weapons as b
ROOT=Path('D:/RobotSlice');OUT=ROOT/'SourceAssets/weapon_refits';OUT.mkdir(exist_ok=True)
BASES=ROOT/'SourceAssets/scifi_weapons/refit_bases'
DEFAULTS={'Paint':('#5a636c',.48,.4),'Secondary':('#383f46',.55,.42),'Accent':('#cf5f1b',.35,.43)}
SLOTS={'Paint':'MI_Robot_Paint','Secondary':'MI_Robot_PaintDark','Accent':'MI_Robot_Accent'}
BASE={};BEFORE={};REMOVED=[];MATINFO={};D=None

def fp(o):
    data=bytearray()
    for v in o.data.vertices:data.extend(struct.pack('fff',*(o.matrix_world@v.co)))
    for layer in o.data.uv_layers:
        for uv in layer.data:data.extend(struct.pack('ff',*uv.uv))
    return hashlib.sha256(data).hexdigest()

def materials(d):
    global MATINFO
    b.MATS={};MATINFO={}
    values=dict(b.COMMON);values.update(DEFAULTS)
    values['Copper']=('#817160',.85,.33)
    values['Brand']=({'VHI':'#ad4f45','PHASE':'#4bafb8','NEC':'#c99447'}[d['maker']],.2,.4)
    for name,(color,metal,rough) in values.items():
        mat=b.material('RF_'+name,color,metal,rough,1.3 if name=='Brand' else 0)
        b.MATS[name]=mat
        MATINFO[mat.name]=dict(slot=SLOTS.get(name,'RF_'+name),color=color,metal=metal,rough=rough,glow=1.3 if name=='Brand' else 0)

def original_material(d):
    short=d['old'][3:]
    texdir=(ROOT/'SourceAssets/guns'/(short+'_tex')) if 'muzzle' in d else ROOT/'Inbox/NO454-20把科幻剑游戏武器3D模型素材/Assets/TEXTURE'/short[-2:]
    mat=bpy.data.materials.new('RF_Original');mat.use_nodes=True;nt=mat.node_tree;bs=nt.nodes.get('Principled BSDF')
    for key in ('BaseColor','Metallic','Roughness','Normal','Emissive'):
        files=[p for p in texdir.glob('*_'+key+'.*') if p.suffix.lower() in ('.png','.jpg','.tga')]
        if not files:continue
        im=nt.nodes.new('ShaderNodeTexImage');im.image=bpy.data.images.load(str(files[0]),check_existing=True)
        im.image.colorspace_settings.name='sRGB' if key in ('BaseColor','Emissive') else 'Non-Color'
        if key=='BaseColor':
            mult=nt.nodes.new('ShaderNodeMixRGB');mult.blend_type='MULTIPLY';mult.inputs[0].default_value=1
            mult.inputs[2].default_value=(.42,.46,.5,1) if 'muzzle' in d else (.72,.76,.8,1)
            nt.links.new(im.outputs['Color'],mult.inputs[1]);nt.links.new(mult.outputs[0],bs.inputs['Base Color'])
        elif key=='Normal':
            normal=nt.nodes.new('ShaderNodeNormalMap');normal.inputs['Strength'].default_value=.65
            nt.links.new(im.outputs[0],normal.inputs['Color']);nt.links.new(normal.outputs[0],bs.inputs['Normal'])
        elif key=='Emissive':
            # 保留原刀身热光与队伍识别逻辑，棚拍用柔和冷光。
            nt.links.new(im.outputs[0],bs.inputs['Emission Color']);bs.inputs['Emission Strength'].default_value=.4
        elif key=='Roughness':
            remap=nt.nodes.new('ShaderNodeMapRange');remap.inputs['To Min'].default_value=.3;remap.inputs['To Max'].default_value=.7
            nt.links.new(im.outputs[0],remap.inputs[0]);nt.links.new(remap.outputs[0],bs.inputs[key])
        else:nt.links.new(im.outputs[0],bs.inputs[key])
    MATINFO[mat.name]=dict(slot='RF_Original',original=short)
    return mat

def begin(d):
    global BASE,BEFORE,REMOVED,D
    D=d;REMOVED=[]
    bpy.ops.wm.open_mainfile(filepath=str(BASES/(d['key']+'.blend')))
    b.PARTS=[];b.OUT=OUT;materials(d)
    BASE={int(o.name.rsplit('_',1)[1]):o for o in bpy.context.scene.objects if o.type=='MESH'}
    BEFORE={i:fp(o) for i,o in BASE.items()}
    original=original_material(d)
    for i,o in BASE.items():o.data.materials.clear();o.data.materials.append(original);o.name=f'原件_{i:03}'

def remove(ids):
    for i in ids:
        if i in BASE:bpy.data.objects.remove(BASE.pop(i),do_unlink=True);REMOVED.append(i)

def coat(ids,role='Secondary'):
    for i in ids:
        if i in BASE:BASE[i].data.materials.clear();BASE[i].data.materials.append(b.MATS[role])

def normals(o):
    bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.mesh.normals_make_consistent(inside=False);bpy.ops.object.mode_set(mode='OBJECT')

def rawprofile(name,pts,w,mat='Paint',y=0):
    n=len(pts);vs=[(x/100,(y+s*w/2)/100,z/100) for s in (-1,1) for x,z in pts]
    fs=[tuple(reversed(range(n))),tuple(range(n,n*2))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    me=bpy.data.meshes.new(name);me.from_pydata(vs,[],fs);me.update()
    o=bpy.data.objects.new(name,me);bpy.context.collection.objects.link(o);o.data.materials.append(b.MATS[mat]);b.PARTS.append(o);normals(o);return o

def bevel(o,r=.08):
    normals(o);bpy.context.view_layer.objects.active=o
    mod=o.modifiers.new('硬表面倒角','BEVEL');mod.width=r/100;mod.segments=2;bpy.ops.object.modifier_apply(modifier=mod.name)
    for p in o.data.polygons:p.use_smooth=True
    mod=o.modifiers.new('平面法线','WEIGHTED_NORMAL');mod.keep_sharp=True
    return o

def cut(o,c):
    normals(c);normals(o);bpy.context.view_layer.objects.active=o
    mod=o.modifiers.new('实际镂空','BOOLEAN');mod.operation='DIFFERENCE';mod.solver='EXACT';mod.object=c
    bpy.ops.object.modifier_apply(modifier=mod.name);b.PARTS.remove(c);bpy.data.objects.remove(c,do_unlink=True)

def panel(name,pts,y,w=.7,holes=(),role='Paint',rad=.1):
    o=rawprofile(name,pts,w,role,y)
    for pts2 in holes:cut(o,bevel(rawprofile('开窗刀具',pts2,w+1.2,'Black',y),.07))
    return bevel(o,rad)

def pair(name,pts,y,w=.7,holes=(),role='Paint',rad=.1):
    return [panel(name,pts,s*y,w,holes,role,rad) for s in (-1,1)]

def slots(x,z,n,step=4,w=2.3,h=1.8):
    return [[(x+i*step,z),(x+i*step+w,z),(x+i*step+w+.5,z+h),(x+i*step+.5,z+h)] for i in range(n)]

def strip(x,z,length,y,role='Accent',height=.25):
    for s in (-1,1):b.box('嵌入识别条',(x,s*y,z),(length,.06,height),role,.025)

def mark(text,x,y,z,size=.7):b.label(text,x,-abs(y),z,size)

def fasteners(points,y,r=.22):b.screws(points,y,r)

def runtime_uv(o):
    # 原源文件存在不同名称的重复 UV；合并前统一实际渲染通道，避免 UE 丢通道。
    # 可编辑文件保留全部原层，运行时仅移除未使用层，不改变有效贴图坐标。
    layers=o.data.uv_layers
    active=next((layer for layer in layers if layer.active_render),layers.active)
    if active is None:return
    before=[tuple(uv.uv) for uv in active.data]
    active_name=active.name
    for layer in list(layers):
        if layer.name!=active_name:layers.remove(layer)
    layers[0].name='UVMap';layers.active_index=0;layers[0].active_render=True
    assert before==[tuple(uv.uv) for uv in layers[0].data]

def export(d,protected=(),render=True):
    for i in protected:assert i in BASE and fp(BASE[i])==BEFORE[i],(d['key'],'contact changed',i)
    # 未更换的原部件始终保留 UV 和几何（仅允许换涂装材质）。
    assert all(fp(o)==BEFORE[i] for i,o in BASE.items()),d['key']
    new=list(b.PARTS)
    for o in new:
        bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
        for m in list(o.modifiers):bpy.ops.object.modifier_apply(modifier=m.name)
        normals(o)
        bpy.ops.object.mode_set(mode='EDIT');bpy.ops.uv.smart_project(angle_limit=math.radians(70),island_margin=.012);bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.file.pack_all();bpy.ops.wm.save_as_mainfile(filepath=str(OUT/(d['key']+'_editable.blend')))
    allparts=list(BASE.values())+new
    for part in allparts:runtime_uv(part)
    bpy.ops.object.select_all(action='DESELECT')
    for o in allparts:o.select_set(True)
    bpy.context.view_layer.objects.active=allparts[0];bpy.ops.object.join();o=bpy.context.object;o.name='SM_RF_'+d['key']
    bpy.context.scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
    bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
    bpy.ops.export_scene.fbx(filepath=str(OUT/(o.name+'.fbx')),use_selection=True,object_types={'MESH'},apply_unit_scale=True,mesh_smooth_type='FACE',add_leaf_bones=False)
    o.data.calc_loop_triangles();vs=[v.co for v in o.data.vertices]
    used={m.name:MATINFO[m.name] for m in o.data.materials}
    assert set(SLOTS.values()).issubset({m['slot'] for m in used.values()}),(d['key'],'missing paint roles')
    report=dict(d,mesh=o.name,triangles=len(o.data.loop_triangles),materials=used,removed=REMOVED,retained=len(BASE),added=len(new),
        geometry_uv_unchanged=True,runtime_uv_channels=len(o.data.uv_layers),active_uv_coordinates_preserved=True,
        protected=list(protected),min=[min(v[i] for v in vs)*100 for i in range(3)],max=[max(v[i] for v in vs)*100 for i in range(3)])
    (OUT/(o.name+'.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/(o.name+'.blend')))
    if render:b.render(o,d)
    print('[REFIT]',d['key'],report['triangles'],'triangles',flush=True)
    return o,report

def rifle():
    global BASE,BEFORE,REMOVED
    d=b.GUNS[0]
    bpy.ops.wm.open_mainfile(filepath=str(ROOT/'SourceAssets/scifi_weapons/rifle_refit/R36_refit_editable.blend'))
    obs=[o for o in bpy.context.scene.objects if o.type=='MESH'];b.PARTS=[];b.OUT=OUT;materials(d)
    oldmap={'Refit_VHI_Armor':'Paint','Refit_Frame':'Secondary','Refit_VHI_Oxide':'Accent','Refit_VHI_Chassis':'Frame',
        'Refit_InductionAlloy':'Copper','Refit_Steel':'Steel','Refit_Black':'Black','Refit_Mark':'Mark'}
    original=original_material(d)
    for o in obs:
        name=o.data.materials[0].name
        target=original if name=='Refit_Gun3_OriginalPBR' else b.MATS[oldmap[name]]
        if o.name.startswith('电容状态灯'):target=b.MATS['Brand']
        o.data.materials.clear();o.data.materials.append(target)
    BASE={i:o for i,o in enumerate(obs)};BEFORE={i:fp(o) for i,o in BASE.items()};REMOVED=[]
    o,report=export(d,render=True)
    report['source_refit']='SM_Gun3: retained 57 original parts; new receiver armor, shroud and muzzle'
    (OUT/(o.name+'.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    # 三组同机位配色，只改三路漆色，机芯/握把/灯保持原样。
    for key,colors in [('Desert',('#a69270','#50483d','#ce6b30')),('Blue',('#466584','#263444','#d6c9a1'))]:
        for role,col in zip(('Paint','Secondary','Accent'),colors):
            bs=b.MATS[role].node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(*b.lin(col),1)
        bpy.context.scene.render.filepath=str(OUT/('Rifle_'+key+'.png'));bpy.ops.render.render(write_still=True)
    return report

if __name__=='__main__':rifle()
