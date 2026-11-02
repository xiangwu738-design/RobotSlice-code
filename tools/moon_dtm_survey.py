"""月球基地：看 NASA Apollo 17 高程图（NAC_DTM_APOLLO17_2M.TIF，32 位浮点、2 米/像素、未压缩 TIFF）。
不依赖 tifffile：自己读 TIFF 标签，再用 numpy memmap 按需取，不把 950 MB 读进内存。
py -3 -I tools/moon_dtm_survey.py [输出目录]   → 打印 TIFF 标签、高程范围；写 16 米/像素的总览图（dtm_overview.png，带阴影 + 坐标网格）和 dtm_16m.npy
"""
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

TIF = 'D:/RobotSlice/Inbox/_moon_nasa/dtm/NAC_DTM_APOLLO17_2M.TIF'
OUT = sys.argv[1] if len(sys.argv) > 1 else 'D:/RobotSlice/Inbox/_moon_nasa/work'
os.makedirs(OUT, exist_ok=True)


def read_tiff_tags(path):
    TYPES = {1: 'B', 2: 's', 3: 'H', 4: 'I', 5: 'II', 11: 'f', 12: 'd', 13: 'I', 16: 'Q', 17: 'q'}
    with open(path, 'rb') as f:
        hdr = f.read(16)
        en = '<' if hdr[:2] == b'II' else '>'
        ver = struct.unpack(en + 'H', hdr[2:4])[0]
        big = ver == 43
        if big:
            off = struct.unpack(en + 'Q', hdr[8:16])[0]
        else:
            off = struct.unpack(en + 'I', hdr[4:8])[0]
        f.seek(off)
        n = struct.unpack(en + ('Q' if big else 'H'), f.read(8 if big else 2))[0]
        tags = {}
        for i in range(n):
            raw = f.read(20 if big else 12)
            if big:
                tag, typ, cnt = struct.unpack(en + 'HHQ', raw[:12])
                val = raw[12:]
            else:
                tag, typ, cnt = struct.unpack(en + 'HHI', raw[:8])
                val = raw[8:]
            ch = TYPES.get(typ, 'B')
            size = struct.calcsize(en + ch) * (1 if ch != 'II' else 1)
            if ch == 's':
                total = cnt
            else:
                total = cnt * struct.calcsize(en + ch)
            here = f.tell()
            inline = total <= (8 if big else 4)
            if not inline:
                f.seek(struct.unpack(en + ('Q' if big else 'I'), val)[0])
                data = f.read(min(total, 8192 if tag != 273 else total))
            else:
                data = val[:total]
            if ch == 's':
                v = data.split(b'\0')[0].decode('latin1')
            elif total > len(data):
                v = f'<{cnt} 项>'
            else:
                v = struct.unpack(en + ch * cnt, data) if ch != 'II' else struct.unpack(en + 'II' * cnt, data)
                if len(v) == 1:
                    v = v[0]
            tags[tag] = v
            f.seek(here)
    return en, tags


NAMES = {256: 'width', 257: 'height', 258: 'bits', 259: 'compression', 273: 'strip_offsets', 277: 'samples', 278: 'rows_per_strip', 279: 'strip_bytes',
         284: 'planar', 339: 'sample_format', 322: 'tile_w', 323: 'tile_h', 324: 'tile_offsets', 33550: 'pixel_scale', 33922: 'tiepoint',
         34735: 'geokeys', 34736: 'geo_double', 34737: 'geo_ascii', 42113: 'gdal_nodata', 42112: 'gdal_meta'}


def open_dtm():
    en, t = read_tiff_tags(TIF)
    w, h = t[256], t[257]
    so = t[273]
    if not isinstance(so, tuple) and not isinstance(so, str):
        so = (so,)
    if isinstance(so, str):
        raise RuntimeError('条带偏移表太长，没读到')
    start = so[0]
    assert so[1] - so[0] == w * 4 and so[-1] - so[0] == (h - 1) * w * 4, '条带不连续'
    return np.memmap(TIF, dtype=en + 'f4', mode='r', offset=start, shape=(h, w)), t


if __name__ == '__main__':
    en, t = read_tiff_tags(TIF)
    for k, v in sorted(t.items()):
        s = str(v)
        print(NAMES.get(k, k), s[:300])
    w, h = t[256], t[257]
    # 整张是连续条带时可以 memmap（文件 952511711 字节 ≈ 12049×19760×4 + 头）
    print('文件大小', os.path.getsize(TIF), '像素字节', w * h * 4)
    # 条带太多时偏移表读不全（上面只读 8K），取第一条：直接读偏移表开头
    with open(TIF, 'rb') as f:
        hdr = f.read(16)
        big = struct.unpack(en + 'H', hdr[2:4])[0] == 43
    print('BigTIFF', big)
    dtm, t2 = open_dtm()
    step = 8
    small = np.array(dtm[::step, ::step], dtype=np.float32)
    print('抽样', small.shape, '原始范围', float(small.min()), float(small.max()))
    bad = (small < -20000) | (small > 20000) | ~np.isfinite(small)
    print('无效比例', float(bad.mean()))
    good = small[~bad]
    print('有效范围', float(good.min()), float(good.max()), '分位', np.percentile(good, [1, 25, 50, 75, 99]).round(1))
    np.save(f'{OUT}/dtm_16m.npy', small)
    # 阴影图
    filled = small.copy()
    filled[bad] = np.median(good)
    gy, gx = np.gradient(filled, 16.0)
    az, alt = np.radians(135), np.radians(25)
    lx, ly, lz = np.cos(alt) * np.sin(az), np.cos(alt) * np.cos(az), np.sin(alt)
    nz = 1 / np.sqrt(gx * gx + gy * gy + 1)
    sh = np.clip((-gx * nz * lx + gy * nz * ly + nz * lz), 0, 1)
    img = (sh * 255).astype(np.uint8)
    rgb = np.stack((img, img, img), -1)
    rgb[bad] = (120, 30, 30)
    im = Image.fromarray(rgb)
    d = ImageDraw.Draw(im)
    font = ImageFont.truetype(r'C:\Windows\Fonts\msyh.ttc', 12)
    # 网格：每 2 公里一条线（标原图像素 / 公里）
    for km in range(0, 42, 2):
        y = int(km * 1000 / 16)
        d.line([(0, y), (im.width, y)], fill=(255, 200, 0), width=1)
        d.text((2, y + 1), f'{km}km', fill=(255, 230, 0), font=font)
    for km in range(0, 26, 2):
        x = int(km * 1000 / 16)
        d.line([(x, 0), (x, im.height)], fill=(0, 200, 255), width=1)
        d.text((x + 2, 14), f'{km}km', fill=(0, 230, 255), font=font)
    im.save(f'{OUT}/dtm_overview.png')
    print('写', f'{OUT}/dtm_overview.png', im.size)
