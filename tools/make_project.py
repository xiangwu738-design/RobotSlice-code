"""从 UE 5.8 的 C++ 第三人称模板搭出 RobotSlice 工程，并带上：
- 模板共享资源包（角色、输入、关卡原型）
- 旧校园工程的 AdvancedLightingSystem 插件（天空、大气、天气）
- V29 校园地图及其依赖（去掉旧玩家角色、FPV 无人机这类不需要的部分）
旧工程只读，不改任何文件。可重复运行（已存在的文件跳过）。"""
import json
import os
import re
import shutil

ENGINE = r'D:\EPIC\UE_5.8'
TEMPLATE = os.path.join(ENGINE, r'Templates\TP_ThirdPerson')
PACKS = os.path.join(ENGINE, r'Templates\TemplateResources\High')
OLD = r'C:\Users\ASUS\Documents\ChatGPT\PlanetaryEngine\xiuzhou-campus\ue5\XiuzhouCampus'
DST = r'D:\RobotSlice'
NAME = 'RobotSlice'
T = 'TP_ThirdPerson'


def rename(text):
    text = text.replace(T.upper(), NAME.upper()).replace(T.lower(), NAME.lower())
    return re.sub(re.escape(T), NAME, text, flags=re.IGNORECASE)


def copy_tree(src, dst, skip_dirs=(), text_ext=(), renamer=None):
    n = 0
    for root, dirs, files in os.walk(src):
        dirs[:] = [d for d in dirs if d not in skip_dirs]
        rel = os.path.relpath(root, src)
        for f in files:
            out_rel = os.path.join(rel, f)
            if renamer:
                out_rel = renamer(out_rel)
            out = os.path.normpath(os.path.join(dst, out_rel))
            if os.path.exists(out):
                continue
            os.makedirs(os.path.dirname(out), exist_ok=True)
            if renamer and f.lower().endswith(text_ext):
                data = open(os.path.join(root, f), encoding='utf-8-sig').read()
                open(out, 'w', encoding='utf-8').write(renamer(data))
            else:
                shutil.copy2(os.path.join(root, f), out)
            n += 1
    return n


# 1. 模板：源码和配置改名，内容原样
n = copy_tree(os.path.join(TEMPLATE, 'Source'), os.path.join(DST, 'Source'), text_ext=('.cpp', '.h', '.cs'), renamer=rename)
print('源码', n)
n = copy_tree(os.path.join(TEMPLATE, 'Content'), os.path.join(DST, 'Content'))
print('模板内容', n)
for ini in ('DefaultEditor.ini', 'DefaultEditorPerProjectUserSettings.ini', 'DefaultGame.ini', 'DefaultInput.ini'):
    out = os.path.join(DST, 'Config', ini)
    if not os.path.exists(out):
        os.makedirs(os.path.dirname(out), exist_ok=True)
        open(out, 'w', encoding='utf-8').write(rename(open(os.path.join(TEMPLATE, 'Config', ini), encoding='utf-8-sig').read()))

# 2. 共享资源包
for mount in ('Characters', 'Input', 'LevelPrototyping'):
    n = copy_tree(os.path.join(PACKS, mount, 'Content'), os.path.join(DST, 'Content', mount))
    print('资源包', mount, n)

# 3. 天空灯光插件（不带旧编译产物，在新工程里重新编译）
n = copy_tree(os.path.join(OLD, 'Plugins', 'AdvancedLightingSystem'), os.path.join(DST, 'Plugins', 'AdvancedLightingSystem'),
              skip_dirs=('Binaries', 'Intermediate'))
print('灯光插件', n)

# 4. V29 地图依赖：去掉旧玩家角色、二次动态、旧输入/模板蓝图（新工程有自己的一份）
deps = json.load(open(os.path.join(DST, 'tools', 'deps_v29.json'), encoding='utf-8'))
SKIP = ('/Game/CharacterSecondaryMotion/', '/Game/Characters/', '/Game/Input/', '/Game/ThirdPerson/', '/Game/Campus/Player/')
copied = missing = 0
for pkg in deps['game']:
    if pkg.startswith(SKIP):
        continue
    rel = pkg[len('/Game/'):].replace('/', os.sep)
    base = os.path.join(OLD, 'Content', rel)
    found = False
    for ext in ('.uasset', '.umap', '.uexp', '.ubulk', '.uptnl'):
        src = base + ext
        if os.path.exists(src):
            found = True
            out = os.path.join(DST, 'Content', rel + ext)
            if not os.path.exists(out):
                os.makedirs(os.path.dirname(out), exist_ok=True)
                shutil.copy2(src, out)
                copied += 1
    missing += not found
print('地图资源文件', copied, '找不到的包', missing)

# 5. 工程文件
uproject = {
    'FileVersion': 3,
    'EngineAssociation': '5.8',
    'Category': 'Games',
    'Description': 'HX-07 working title: high-mobility robot combat vertical slice',
    'Modules': [{'Name': NAME, 'Type': 'Runtime', 'LoadingPhase': 'Default',
                 'AdditionalDependencies': ['Engine', 'AIModule', 'UMG']}],
    'Plugins': [
        {'Name': 'ModelingToolsEditorMode', 'Enabled': True, 'TargetAllowList': ['Editor']},
        {'Name': 'StateTree', 'Enabled': True},
        {'Name': 'GameplayStateTree', 'Enabled': True},
        {'Name': 'EnhancedInput', 'Enabled': True},
        {'Name': 'PythonScriptPlugin', 'Enabled': True, 'TargetAllowList': ['Editor']},
        {'Name': 'EditorScriptingUtilities', 'Enabled': True, 'TargetAllowList': ['Editor']},
        {'Name': 'AdvancedLightingSystem', 'Enabled': True},
    ],
}
path = os.path.join(DST, NAME + '.uproject')
if not os.path.exists(path):
    json.dump(uproject, open(path, 'w', encoding='utf-8'), indent='\t')

# 6. DefaultEngine.ini：模板内容 + 改名重定向 + 旧工程的渲染设置
eng = os.path.join(DST, 'Config', 'DefaultEngine.ini')
if not os.path.exists(eng):
    text = open(os.path.join(TEMPLATE, 'Config', 'DefaultEngine.ini'), encoding='utf-8-sig').read()
    text = text.replace('r.Lumen.TraceMeshSDFs=0\n', '')
    text = text.replace('[/Script/Engine.RendererSettings]\n', '[/Script/Engine.RendererSettings]\n'
                        'r.RayTracing=True\nr.SkinCache.CompileShaders=True\n'
                        'r.DistanceFields.SupportEvenIfHardwareRayTracingSupported=1\n')
    text += f'''
[/Script/Engine.Engine]
NearClipPlane=5.0
+ActiveGameNameRedirects=(OldGameName="{T}",NewGameName="/Script/{NAME}")
+ActiveGameNameRedirects=(OldGameName="/Script/{T}",NewGameName="/Script/{NAME}")

[CoreRedirects]
+ClassRedirects=(OldName="/Script/{T}.{T}Character",NewName="/Script/{NAME}.{NAME}Character")
+ClassRedirects=(OldName="/Script/{T}.{T}GameMode",NewName="/Script/{NAME}.{NAME}GameMode")
+ClassRedirects=(OldName="/Script/{T}.{T}PlayerController",NewName="/Script/{NAME}.{NAME}PlayerController")

[ConsoleVariables]
r.Interchange.FeatureFlags.Import.FBX=False
r.EyeAdaptation.CachedLightingPreExposure=8
'''
    open(eng, 'w', encoding='utf-8').write(text)
print('完成')
