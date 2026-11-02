"""把导出的武器模型（Saved/WeaponIconMeshes/*.obj）画成纯白剪影 + 透明底（SourceAssets/ui_weapon_icons/T_WI_*.png）。流程见 tools/make_weapon_icons.sh。
只看模型的三角形：正交投影、全部填白，就是这件武器真实的轮廓（扳机护圈这类镂空还在），和灯光、材质无关，重跑结果不变。
贴图高度统一 64 像素、宽度取 64 / 128 / 256（贴图要有 Mip，只能是 2 的幂），武器按比例放进去：HUD 按贴图的宽高比画，手枪窄、狙击枪宽。
另外输出 _contact.png：所有图标放大看 + 1080p / 4K 实际显示大小，用来定摆放（weapon_icon_table.POSE）。
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import weapon_icon_table as table

HEIGHT = 64
SUPER = 6        # 先画大 6 倍再缩小：边缘抗锯齿
MARGIN = 2       # 四周留的像素（缩小时边缘不被裁）


def load_obj(path):
    verts, tris = [], []
    with open(path, encoding='utf-8', errors='ignore') as f:
        for line in f:
            if line.startswith('v '):
                verts.append([float(x) for x in line.split()[1:4]])
            elif line.startswith('f '):
                idx = [int(p.split('/')[0]) for p in line.split()[1:]]
                idx = [i - 1 if i > 0 else len(verts) + i for i in idx]
                for k in range(1, len(idx) - 1):
                    tris.append((idx[0], idx[k], idx[k + 1]))
    return np.array(verts, dtype=np.float64), np.array(tris, dtype=np.int64)


def project(verts, pose):
    """返回屏幕坐标（x 向右、y 向下，单位和模型一样）"""
    if 'axes' in pose:
        h, v = pose['axes']       # 指定哪个轴横放、哪个轴竖放（OBJ 里的轴号）
    else:
        ext = verts.max(axis=0) - verts.min(axis=0)
        order = np.argsort(-ext)
        h, v = int(order[0]), int(order[1])
    x = verts[:, h] * (-1 if pose.get('flipx') else 1)
    y = verts[:, v] * (1 if pose.get('flipy') else -1) * pose.get('yscale', 1.0)
    return np.stack([x, y], axis=1)


def silhouette(xy, tris, size_w, size_h):
    """画到 size_w × size_h 的 L 图里（已含超采样），按比例放进去、居中"""
    lo, hi = xy.min(axis=0), xy.max(axis=0)
    span = hi - lo
    scale = min(size_w / span[0], size_h / span[1])
    off = np.array([size_w, size_h]) * 0.5 - (lo + hi) * 0.5 * scale
    p = xy * scale + off
    img = Image.new('L', (int(size_w), int(size_h)), 0)
    d = ImageDraw.Draw(img)
    for a, b, c in tris:
        d.polygon([tuple(p[a]), tuple(p[b]), tuple(p[c])], fill=255)
    return img


def tight(img):
    box = img.getbbox()
    return img.crop(box) if box else img


def render(name, verts, tris, pose):
    xy = project(verts, pose)
    lo, hi = xy.min(axis=0), xy.max(axis=0)
    aspect = (hi[0] - lo[0]) / max(hi[1] - lo[1], 1e-6)
    pair = pose.get('pair')
    if pair == 'offset':
        aspect = 1.45  # 两把错开以后的大致比例，用 128 宽的贴图
    elif pair == 'cross':
        aspect = 4.0
    # 贴图宽：放得下这个比例的最小的 2 的幂（最宽 4:1，再长的武器就按宽度缩小、变细）
    cell = 64 if aspect <= 1.15 else 128 if aspect <= 2.3 else 256
    w, h = cell - MARGIN * 2, HEIGHT - MARGIN * 2
    W, H = w * SUPER, h * SUPER
    if pair == 'offset':
        # 一前一后两把：后面那把往左上错开，前面那把周围留一道缝，一眼看出是两把
        K = 1.3
        one = tight(silhouette(xy, tris, W / K, H / K))
        dx, dy = W - one.width, H - one.height
        back = Image.new('L', (W, H), 0)
        front = Image.new('L', (W, H), 0)
        back.paste(one, (0, 0))
        front.paste(one, (dx, dy))
        gap = front.filter(ImageFilter.MaxFilter(4 * SUPER + 1))
        big = Image.fromarray(np.maximum(np.array(back) * (np.array(gap) < 128), np.array(front)).astype(np.uint8))
    elif pair == 'cross':
        # 两把交叉成 X：后面那把在交叉处留一道缝
        one = tight(silhouette(xy, tris, W * 0.97, H))
        base = Image.new('L', (W, H), 0)
        base.paste(one, ((W - one.width) // 2, (H - one.height) // 2))
        ang = pose.get('angle', 10)
        a = base.rotate(ang, resample=Image.BICUBIC, center=(W / 2, H / 2))
        b = base.transpose(Image.FLIP_TOP_BOTTOM).rotate(-ang, resample=Image.BICUBIC, center=(W / 2, H / 2))
        gap = b.filter(ImageFilter.MaxFilter(3 * SUPER + 1))
        big = Image.fromarray(np.maximum(np.array(a) * (np.array(gap) < 128), np.array(b)).astype(np.uint8))
    else:
        big = silhouette(xy, tris, W, H)
    alpha = big.resize((w, h), Image.LANCZOS)
    out = Image.new('L', (cell, HEIGHT), 0)
    out.paste(alpha, (MARGIN, MARGIN))
    rgba = Image.merge('RGBA', (Image.new('L', out.size, 255), Image.new('L', out.size, 255), Image.new('L', out.size, 255), out))
    return rgba, cell, aspect


def main():
    os.makedirs(table.OUT_DIR, exist_ok=True)
    rows = []
    for name, path in table.items():
        obj = f'{table.MESH_DIR}/{name}.obj'
        if not os.path.exists(obj):
            print(f'[weaponicon] {name}：没有 {obj}（先跑 ue_export_weapon_meshes.py）')
            continue
        verts, tris = load_obj(obj)
        pose = table.POSE.get(name, {})
        rgba, cell, aspect = render(name, verts, tris, pose)
        rgba.save(f'{table.OUT_DIR}/T_WI_{name}.png')
        ext = verts.max(axis=0) - verts.min(axis=0)
        print(f'[weaponicon] {name:11s} {os.path.basename(path):20s} {len(tris):7d} 三角形  长宽高 {ext[0]:6.1f} {ext[1]:6.1f} {ext[2]:6.1f}  比例 {aspect:5.2f}  贴图 {cell}×{HEIGHT}')
        rows.append((name, rgba))
    # 总览：左边放大 3 倍看形状，右边是实际显示大小（1080p 高 18 像素、4K 高 36 像素）
    font = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 14)
    rh = 200
    cols = 3
    n = len(rows)
    sheet = Image.new('RGBA', (cols * 560, ((n + cols - 1) // cols) * rh), (28, 30, 34, 255))
    d = ImageDraw.Draw(sheet)
    for i, (name, im) in enumerate(rows):
        x0, y0 = (i % cols) * 560 + 10, (i // cols) * rh + 8
        d.text((x0, y0), name, fill=(150, 200, 255, 255), font=font)
        big = im.resize((im.width * 2, im.height * 2), Image.LANCZOS)
        sheet.alpha_composite(big, (x0, y0 + 24))
        for dh, yy in ((18, y0 + 24 + 134 - 104), (36, y0 + 24 + 134 - 60)):
            small = im.resize((max(1, im.width * dh // HEIGHT), dh), Image.BOX)
            sheet.alpha_composite(small, (x0 + 530 - small.width, yy))
    sheet.save(f'{table.OUT_DIR}/_contact.png')
    print(f'[weaponicon] 输出 {len(rows)} 张到 {table.OUT_DIR}，总览 _contact.png')


if __name__ == '__main__':
    main()
