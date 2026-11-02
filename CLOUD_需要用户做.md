# 需要用户做：彩蛋模型（机械狗 / 无人机，11-02）

云端没有 Blender 和 UE，也没有素材。脚本都写好了（`tools/blender_easter_*.py`、`tools/ue_easter_*.py`，说明见 README「彩蛋模型流程（11-02）」），
要你下载两个底子模型、按下面的顺序跑一遍，把图和日志发回来，我按日志改参数 / 脚本。**第一次多半要调一两轮**（朝向、身体高度、零件位置），这是正常的。

所有命令都在 `D:\RobotSlice` 下执行（cmd 或 PowerShell）。`blender` 不在 PATH 里就换成 Blender 5.x 的完整路径，例如
`"C:\Program Files\Blender Foundation\Blender 5.0\blender.exe"`。

---

## 1. 下载（Sketchfab，要登录）

| 放到哪 | 找什么 | 怎么挑 |
|---|---|---|
| `Inbox\_easter\dog\` | 搜 **`Spot Mini`**、`Boston Dynamics Spot`、`spot robot dog`（就是 `docs/地图候选_20261102/总览_机械狗和无人机候选.jpg` 里挑的那只） | 必须有 **Download** 按钮（左下 Downloadable）。**最好带骨架和动作**（页面写 Animated / Rigged，动作名有 walk / idle / sit 更好；没有也行，代码会程序化走路）。腿是分开的零件比整块好。三角面 30 万以内。不要带大地台 / 草地底座的版本（有也会自动去掉）。不要带机械臂的版本 |
| `Inbox\_easter\drone\` | 搜 **`Cyberpunk Drone`**（同一张候选图里的那架） | 要 Downloadable。**四旋翼**、四个桨**是单独的零件**（Sketchfab 页面的 Model Inspector 里能看到分开的物体最好）。三角面 20 万以内 |

- 下载格式：页面 Download → 优先 **Original format**（是 `.blend` 或 `.fbx` 就用它），否则选 **glTF**（自动转换的那个，会是一个 zip）。
- **解压**到上面的目录（zip 里的 `scene.gltf` + `scene.bin` + `textures\` 整个放进去；`.glb` 直接放）。每个目录只放**一个**模型，多了脚本用第一个并在日志里警告。
- 许可：优先 **CC-BY** 或 **CC0**。每个记一行到 `Inbox\链接.txt`（写署名用），格式：
  ```
  彩蛋机械狗底子：<模型名> by <作者> <模型网址> <许可，如 CC-BY-4.0>（2026-11-02 下载）
  彩蛋无人机底子：<模型名> by <作者> <模型网址> <许可>（2026-11-02 下载）
  ```
  如果只能找到 **CC-BY-NC**（非商用）的，也可以先用，记清楚，README「素材来源」里按「只做内部测试」写。「Standard / Editorial」之类的付费许可先别买，发回来我再定。

最后目录应该像这样：
```
D:\RobotSlice\Inbox\_easter\dog\scene.gltf  (+ scene.bin, textures\...)   或 xxx.glb / xxx.fbx / xxx.blend
D:\RobotSlice\Inbox\_easter\drone\scene.gltf (+ ...)
```

## 2. 按顺序跑

每一步都会在 `Inbox\_easter\logs\` 写一个日志（`<步骤>_<时间>.txt`），失败时屏幕最后一行也会写原因。

```bat
cd /d D:\RobotSlice

rem ① 找零件来源（阵营机体 .blend、组件库、肩部 FBX 在哪）——只读，几秒
blender -b --factory-startup -P tools/blender_easter_parts.py -- find

rem ② 底子普查（只读）：尺寸、物体、骨架、动作、材质 + 摆到游戏朝向后的图
blender -b --factory-startup -P tools/blender_easter_survey.py -- dog
blender -b --factory-startup -P tools/blender_easter_survey.py -- drone
py -3 tools/easter_sheet.py Inbox/_easter/survey/dog
py -3 tools/easter_sheet.py Inbox/_easter/survey/drone

rem ③ 看一眼外壳零件（可选，第一次建议跑：相位轻型 / 维克托中型每根骨头上的零件）
blender -b --factory-startup -P tools/blender_easter_parts.py -- PHASE_L
blender -b --factory-startup -P tools/blender_easter_parts.py -- VHI_M
py -3 tools/easter_sheet.py Inbox/_easter/parts/PHASE_L 6 360
py -3 tools/easter_sheet.py Inbox/_easter/parts/VHI_M 6 360

rem ④ 建模 + 导出 FBX（每个一两分钟）
blender -b --factory-startup -P tools/blender_easter_dog.py
blender -b --factory-startup -P tools/blender_easter_drone.py
py -3 tools/easter_sheet.py Inbox/_easter/preview/dog
py -3 tools/easter_sheet.py Inbox/_easter/preview/drone

rem ⑤ 换涂装预览（可选：配置里 8 套涂装各拍一张）
blender -b --factory-startup -P tools/blender_easter_preview.py -- dog
blender -b --factory-startup -P tools/blender_easter_preview.py -- drone
py -3 tools/easter_sheet.py Inbox/_easter/preview/dog
py -3 tools/easter_sheet.py Inbox/_easter/preview/drone

rem ⑥ 导入 UE（先关掉编辑器；别和自动编译 / 测试脚本同时跑）
"D:\EPIC\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" D:\RobotSlice\RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_easter_import.py all" -AllowCommandletRendering -RenderOffscreen -unattended

rem ⑦ 游戏里拍照（测试场，机体旁边摆 4 只狗 + 4 架无人机，各一套涂装）
"D:\EPIC\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" D:\RobotSlice\RobotSlice.uproject "/Game/Maps/L_Range?Range" -game -MechBeauty -RenderOffscreen -unattended -nosound -ForceRes -ResX=1920 -ResY=1080 -windowed -MechHour=13 "-ExecCmds=py D:/RobotSlice/tools/ue_easter_shots.py"
```

⑥ ⑦ 也可以交给自动脚本（`runner_request.json`，结果会回到 results 分支）：
```json
{"build": false, "tests": [
  {"kind": "py", "name": "easter_import", "script": "tools/ue_easter_import.py", "seconds": 600},
  {"kind": "gamepy", "name": "easter_shots", "map": "/Game/Maps/L_Range?Range", "script": "tools/ue_easter_shots.py", "args": ["-MechBeauty"], "seconds": 120}
]}
```

**哪一步失败了就停在那一步**，把那一步的日志发回来就行，不用往下跑。

## 3. 发回来

| 文件 | 看什么 |
|---|---|
| `Inbox\_easter\survey\dog_总览.jpg`、`drone_总览.jpg` | 转到游戏朝向后狗头是不是朝「正前（从 +X 看）」那张、腿的分段颜色对不对、无人机四个旋翼有没有分别标出颜色 |
| `Inbox\_easter\survey\dog_survey.json`、`drone_survey.json` | 物体 / 骨架 / 动作 / 材质清单 |
| `Inbox\_easter\parts\find.json`（+ 第 ③ 步的两张总览图） | 组件库在哪、阵营机体每根骨头上是什么零件 |
| `Inbox\_easter\preview\dog_总览.jpg`、`drone_总览.jpg` | 成品：零件彩色图、成品五个角度、狗的摆姿势检查图（歪头 / 摇尾 / 抬腿时零件有没有跟错）、动作截帧、涂装 |
| `Inbox\_easter\export\PDog_build.json`、`VDrone_build.json` | 骨头 / 插槽位置、尺寸、面数、警告 |
| `Inbox\_easter\logs\` 里每一步**最新**的那个 txt | 出问题时最有用 |
| `D:\RobotSlice\Saved\easter_import.txt` | UE 导入的结果（尺寸核对、插槽、骨头、动作） |
| `D:\RobotSlice\Saved\Screenshots\Easter\*.png`（5 张） | 游戏里的样子（材质、灯、涂装、和机体比大小） |

另外随口告诉我：两个模型的 Sketchfab 网址、许可；看图觉得哪里不像相位 / 维克托、太大太小。

## 4. 常见问题自己先调（不用改代码，命令行后面加 键=值，或改 `tools/easter_dog.json` / `easter_drone.json`）

| 现象 | 加什么 |
|---|---|
| 普查图里狗是倒着 / 侧着的（头不朝 +X） | `forward=+Y`（或 `-X` / `+X` / `-Y`，看图里头朝哪边；survey 和 build 都要加，最好直接改 json 的 forward） |
| 模型躺着（上下不对） | `up=+Y` 或 `up=-Y` |
| 分段图里身体 / 腿分错了 | `body_bottom_m=0.45`（身体底的高度，米，看 survey 日志里的「身体底」和剖面）、`knee_m=0.25` |
| 无人机旋翼没找到或找错 | `rotor_objects="(?i)blade"`（旋翼物体名的正则，看 survey 日志的物体名），或在 json 里写 `rotor_hubs_m` |
| 太大 / 太小 | 狗 `target_length_m=1.5`，无人机 `target_span_m=1.6` |
| 腿太短 / 太长 | `leg_length_scale=0.85`（1 = 不缩短） |
| 找不到阵营机体 `.blend` | json 的 `faction_blend`（默认 `lookdev/rig/factions/{key}.blend`） |
| 某块零件位置 / 大小不对 | json 的 `pieces` 里那一块的 `at` / `push` / `size`；临时试：`pieces.0.size=0.9`（数字是第几块，从 0 数） |

例：`blender -b --factory-startup -P tools/blender_easter_dog.py -- forward=+Y body_bottom_m=0.45`
