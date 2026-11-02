"""11-02 房间页地图缩略图：从已有截图裁成 16:9、640×360，存到 SourceAssets/ui_maps/T_Map_<地图>.png。
导入游戏：UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_import_map_thumbs.py" -unattended -nosplash
新地图：往 JOBS 加一行（来源图，裁剪框 = 比例 x0 y0 x1 y1，None = 从正中裁最大的 16:9）。
"""
import os
from PIL import Image

ROOT = r"D:\RobotSlice\Saved\Screenshots"
OUT = r"D:\RobotSlice\SourceAssets\ui_maps"
JOBS = {
    # 开场运镜的机位（不带字幕的那几秒）：上下有黑边和一行小字（「LIVE」），裁掉
    "L_Street": (r"Intro\street\street_06.png", (0.0, 0.12, 1.0, 0.88)),
    "L_Nature": (r"Intro\nature\nature_12.png", (0.0, 0.12, 1.0, 0.88)),
    "L_Volcano": (r"Intro\valley\valley_05.png", (0.0, 0.12, 1.0, 0.88)),
    "L_TechPark": (r"TechPark\v11_final\t2_center.png", None),
    "L_City": (r"City\beauty\c7_overview.png", None),
    "L_Dreamfield": (r"Dreamfield\v6\1_far_rise.png", None),
    "L_Pillars": (r"Pillars\final\3_flight.png", None),
    "L_Soviet": (r"Soviet\v8\c0_third_street.png", None),
    "L_Moon": (r"Moon\v9\p_mech_earth.png", None),
    # 测试场：-MechBeauty 拍的远景（不带 HUD，源图存在 SourceAssets/ui_maps/源图），只留场地那块
    "L_Range": (OUT + r"\源图\g5_wide.png", (0.3, 0.2, 0.92, 0.74)),
}

os.makedirs(OUT, exist_ok=True)
for name, (src, box) in JOBS.items():
    im = Image.open(os.path.join(ROOT, src)).convert("RGB")
    w, h = im.size
    if box:
        im = im.crop((int(box[0] * w), int(box[1] * h), int(box[2] * w), int(box[3] * h)))
        w, h = im.size
    # 居中裁成 16:9
    if w * 9 > h * 16:
        nw = h * 16 // 9
        im = im.crop(((w - nw) // 2, 0, (w - nw) // 2 + nw, h))
    else:
        nh = w * 9 // 16
        im = im.crop((0, (h - nh) // 2, w, (h - nh) // 2 + nh))
    im = im.resize((640, 360), Image.LANCZOS)
    p = os.path.join(OUT, f"T_Map_{name}.png")
    im.save(p)
    print(p)
