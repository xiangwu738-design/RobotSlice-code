"""企业标识占位符（用户 10-31：「都不太满意，目前就用斜体的对应公司缩写剪影作为占位符吧，另外相位电子缩写就用 PHASE」）。
三家：NEC / VHI / PHASE，用 Bahnschrift 粗体（可变字体取 wght=700）转成曲线，再整体斜切 12 度当斜体，纯色剪影；
不依赖系统字体，任何地方打开都一样。每家出品牌色、纯黑、纯白三版 SVG，外加一张对照图。
用法：py -3.11 tools/make_logo_placeholders.py   （需要 fontTools，本机 3.11 里有）"""
import os
import subprocess

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
OUT = os.path.join(ROOT, 'docs', '世界观', '标识', '占位')
os.makedirs(OUT, exist_ok=True)

BRANDS = {'NEC': '#EE8A1C', 'VHI': '#C23A2E', 'PHASE': '#3CC8DC'}
CN = {'NEC': '新纪建设', 'VHI': '维克托重工', 'PHASE': '相位电子'}
SKEW = 0.21          # tan(12°)：斜体的倾斜量
TRACK = 40           # 字距（字体单位）

font = instantiateVariableFont(TTFont(r'C:\Windows\Fonts\bahnschrift.ttf'), {'wght': 700, 'wdth': 100})
glyphs = font.getGlyphSet()
cmap = font.getBestCmap()
upm = font['head'].unitsPerEm
ascent = font['OS/2'].sCapHeight or int(upm * 0.7)


def word_path(text):
    """把一串字母转成一条 SVG 路径（y 轴朝下），返回 (路径, 宽, 高)"""
    pen = SVGPathPen(glyphs)
    x = 0
    for ch in text:
        name = cmap[ord(ch)]
        # 字体坐标 y 朝上，SVG 朝下：翻转 y，再按 x' = x + SKEW * (高度) 斜切成斜体
        t = TransformPen(pen, (1, 0, SKEW, -1, x, ascent))
        glyphs[name].draw(t)
        x += glyphs[name].width + TRACK
    width = x - TRACK + SKEW * ascent
    return pen.getCommands(), width, ascent


for key, color in BRANDS.items():
    d, w, h = word_path(key)
    pad = 40
    for suffix, fill in (('品牌色', color), ('黑', '#000000'), ('白', '#FFFFFF')):
        svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{-pad} {-pad} {w + 2 * pad:.0f} {h + 2 * pad:.0f}">'
               f'<path d="{d}" fill="{fill}"/></svg>\n')
        with open(os.path.join(OUT, f'{key}_{suffix}.svg'), 'w', encoding='utf-8') as f:
            f.write(svg)

cells = ''.join(
    f'<div class="row"><div class="c"><img src="{k}_品牌色.svg"></div><div class="bw"><img src="{k}_黑.svg"></div>'
    f'<div class="wb"><img src="{k}_白.svg"></div><div class="c small"><img src="{k}_白.svg" style="height:28px">'
    f'<img src="{k}_白.svg" style="height:14px"></div><span>{CN[k]}</span></div>' for k in BRANDS)
html = ('<!doctype html><html><head><meta charset="utf-8"><style>'
        "body{margin:0;background:#0F1214;font-family:'Microsoft YaHei';color:#cfd5da;width:1500px}"
        'h1{font-weight:400;font-size:26px;margin:22px 30px 14px}'
        '.row{display:flex;gap:10px;margin:0 30px 12px;align-items:center}'
        '.row>div{width:300px;height:150px;display:flex;align-items:center;justify-content:center}'
        '.row img{max-height:84px;max-width:260px} .c{background:#16191C} .bw{background:#fff} .wb{background:#000}'
        '.small{flex-direction:column;gap:10px} span{font-size:20px;margin-left:10px}'
        '</style></head><body><h1>企业标识占位：斜体缩写剪影（品牌色 · 黑 · 白 · 小尺寸）</h1>' + cells + '</body></html>')
page = os.path.join(OUT, '占位对照.html')
with open(page, 'w', encoding='utf-8') as f:
    f.write(html)
subprocess.run([r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe', '--headless=new', '--disable-gpu', '--hide-scrollbars',
                '--window-size=1500,580', '--screenshot=' + os.path.join(OUT, '占位对照.png'),
                'file:///' + os.path.abspath(page).replace('\\', '/')], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
print('ok')
