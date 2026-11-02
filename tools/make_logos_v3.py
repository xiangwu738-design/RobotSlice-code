"""三家企业标识第三轮（用户：「三家标识我都不太满意，请你参考其他游戏或者影音或者现实中的设计」）。
做法照真实企业 / 影视里的巨型企业：一个能单独用的图形 + 认真排过的字标，再看它印在机身面板上和缩到很小时的样子。
每家 2 个方案。输出 docs/世界观/标识/第三轮/ 下的 SVG 和一张对照图（Edge 无头截图）。
用法：py -3 tools/make_logos_v3.py"""
import math
import os
import subprocess

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
OUT = os.path.join(ROOT, 'docs', '世界观', '标识', '第三轮')
os.makedirs(OUT, exist_ok=True)
BG = '#16191C'

# 每家：m 主色、n 次色（同色系暗一档）、a 点缀色
PAL = {
    'NEC': dict(m='#E6E8EA', n='#9AA3AB', a='#EE8A1C'),
    'VHI': dict(m='#DCD4B6', n='#8C8668', a='#C23A2E'),
    'PEC': dict(m='#EEF1F4', n='#7E8A94', a='#3CC8DC'),
}


def pts(p):
    return ' '.join(f'{x:.1f},{y:.1f}' for x, y in p)


# ---------------------------------------------------------------- 新纪建设
def nec_crest():
    """四方：四个小方块排成菱形（日本财阀家纹那种几何家纹的路子），外面一圈细菱形框；
    四块 = 建筑、能源、物流、金融，最上面那块是点缀色（新纪元）"""
    s = f'<polygon points="256,40 472,256 256,472 40,256" fill="none" stroke="{{m}}" stroke-width="12"/>'
    for (cx, cy), c in (((256, 152), '{a}'), ((152, 256), '{m}'), ((360, 256), '{m}'), ((256, 360), '{m}')):
        r = 74
        s += f'<polygon points="{pts([(cx, cy - r), (cx + r, cy), (cx, cy + r), (cx - r, cy)])}" fill="{c}"/>'
    return s


def nec_block():
    """基石：等轴测的一块方石，顶面点缀色，两个侧面刻出楼层线；方石三个面之间留缝"""
    top = [(256, 72), (416, 164), (256, 256), (96, 164)]
    left = [(96, 164), (256, 256), (256, 440), (96, 348)]
    right = [(256, 256), (416, 164), (416, 348), (256, 440)]
    s = (f'<polygon points="{pts(top)}" fill="{{a}}" stroke="{BG}" stroke-width="10" stroke-linejoin="round"/>'
         f'<polygon points="{pts(left)}" fill="{{m}}" stroke="{BG}" stroke-width="10" stroke-linejoin="round"/>'
         f'<polygon points="{pts(right)}" fill="{{n}}" stroke="{BG}" stroke-width="10" stroke-linejoin="round"/>')
    for k in (1, 2, 3):  # 楼层线：沿两个侧面的底边方向
        t = k / 4
        yl0, yl1 = 164 + (348 - 164) * t, 256 + (440 - 256) * t
        s += f'<line x1="96" y1="{yl0:.1f}" x2="256" y2="{yl1:.1f}" stroke="{BG}" stroke-width="8"/>'
        s += f'<line x1="256" y1="{yl1:.1f}" x2="416" y2="{yl0:.1f}" stroke="{BG}" stroke-width="8"/>'
    return s


# ---------------------------------------------------------------- 维克托重工
def vhi_delta():
    """三角翼：一枚带尾部缺口的三角翼 / 箭头（军工航空企业常见的路子），中间一道缝分成两半，下面一道信号红"""
    left = [(256, 56), (256, 318), (172, 384), (64, 384)]
    right = [(256, 56), (448, 384), (340, 384), (256, 318)]
    return (f'<polygon points="{pts(left)}" fill="{{m}}"/><polygon points="{pts(right)}" fill="{{n}}"/>'
            f'<line x1="256" y1="40" x2="256" y2="330" stroke="{BG}" stroke-width="10"/>'
            '<rect x="186" y="418" width="140" height="22" fill="{a}"/>')


def vhi_shield():
    """盾徽：切角的六边形盾牌，里面三层横装甲板，下面一道信号红的人字（军衔），盾边一圈细线"""
    shield = [(256, 48), (432, 104), (432, 292), (256, 464), (80, 292), (80, 104)]
    s = f'<polygon points="{pts(shield)}" fill="none" stroke="{{m}}" stroke-width="14"/>'
    for k, w in enumerate((272, 240, 208)):
        y = 132 + k * 52
        x0 = 256 - w / 2
        plate = [(x0 + 14, y), (x0 + w - 14, y), (x0 + w, y + 18), (x0 + w - 14, y + 36), (x0 + 14, y + 36), (x0, y + 18)]
        s += f'<polygon points="{pts(plate)}" fill="{{m}}"/>'
    chev = [(150, 300), (196, 300), (256, 354), (316, 300), (362, 300), (256, 404)]
    return s + f'<polygon points="{pts(chev)}" fill="{{a}}"/>'


# ---------------------------------------------------------------- 相位电子
def pec_stripes():
    """相栅：一组横条，每条往右错开一点（相位一点点推移），叠出一个斜的方块；其中一条亮青色（像 IBM 横条字标那一路的企业感）"""
    s = ''
    for i in range(8):
        y = 92 + i * 42
        x = 88 + i * 20
        c = '{a}' if i == 4 else '{m}'
        s += f'<rect x="{x}" y="{y}" width="196" height="24" fill="{c}"/>'
    return s


def pec_iris():
    """光圈：六片直边叶片围出一个转过一角的六边形光孔（相机快门 / 传感器的意思），光孔里亮青色"""
    R, r, rot = 210, 86, math.radians(22)
    outer = [(256 + R * math.cos(math.radians(60 * k - 90)), 256 + R * math.sin(math.radians(60 * k - 90))) for k in range(6)]
    inner = [(256 + r * math.cos(math.radians(60 * k - 90) + rot), 256 + r * math.sin(math.radians(60 * k - 90) + rot)) for k in range(6)]
    s = f'<polygon points="{pts(inner)}" fill="{{a}}"/>'
    for k in range(6):
        blade = [outer[k], outer[(k + 1) % 6], inner[(k + 1) % 6], inner[k]]
        c = '{m}' if k % 2 == 0 else '{n}'
        s += f'<polygon points="{pts(blade)}" fill="{c}" stroke="{BG}" stroke-width="9" stroke-linejoin="round"/>'
    return s


DESIGNS = {
    'NEC': dict(cn='新纪建设', motto='我们建设明天。', ref='参考：日本财阀的几何家纹（老牌、保守、像印章），城建集团常用的方石 / 楼层意象',
                opts=[('A 四方', '四块方石排成菱形家纹，最上面一块是新纪元的橙色；老派、稳、像盖在合同上的章', nec_crest),
                      ('B 基石', '一块等轴测方石，侧面刻着楼层线，顶面橙色；资产、建设、一层层往上', nec_block)],
                word=lambda c: (f'<text x="0" y="0" font-family="Bahnschrift" font-weight="700" font-size="118" letter-spacing="14" fill="{c["m"]}">NEW ERA</text>'
                                f'<text x="4" y="58" font-family="Bahnschrift" font-weight="400" font-size="36" letter-spacing="21" fill="{c["n"]}">CONSOLIDATED</text>'
                                f'<rect x="4" y="80" width="560" height="5" fill="{c["a"]}"/>'
                                f'<text x="4" y="140" font-family="Microsoft YaHei" font-weight="700" font-size="44" letter-spacing="10" fill="{c["m"]}">新纪建设</text>')),
    'VHI': dict(cn='维克托重工', motto='值得信赖的力量。', ref='参考：军工航空企业的三角翼 / 箭头标、军用盾徽和臂章；字标照军品铭牌用窄体粗字',
                opts=[('A 三角翼', '一枚带尾部缺口的三角翼，中缝分成两半，下面一道信号红；速度、火力、航空军工出身', vhi_delta),
                      ('B 盾徽', '切角盾牌里三层横装甲板，下面一道红色军衔人字；装甲、纪律、可靠', vhi_shield)],
                word=lambda c: (f'<text x="0" y="0" font-family="Bahnschrift" font-weight="700" font-stretch="condensed" font-size="150" letter-spacing="12" fill="{c["m"]}">VICTOR</text>'
                                f'<text x="4" y="56" font-family="Bahnschrift" font-weight="600" font-stretch="condensed" font-size="40" letter-spacing="17" fill="{c["n"]}">HEAVY INDUSTRIES</text>'
                                f'<rect x="4" y="78" width="38" height="12" fill="{c["a"]}"/><rect x="50" y="78" width="38" height="12" fill="{c["a"]}"/>'
                                f'<text x="4" y="142" font-family="Microsoft YaHei" font-weight="700" font-size="44" letter-spacing="8" fill="{c["m"]}">维克托重工</text>')),
    'PEC': dict(cn='相位电子', motto='使未来成为可能。', ref='参考：科技企业的横条字标（IBM）、小写细体字标（intel 一类）、相机光圈和传感器',
                opts=[('A 相栅', '一组横条逐条错开，像相位一点点推移，叠成一个斜方块；其中一条亮青色', pec_stripes),
                      ('B 光圈', '六片直边叶片围出一个转过一角的六边形光孔，光孔亮青色；传感器、精密、看得见未来', pec_iris)],
                word=lambda c: (f'<text x="0" y="0" font-family="Bahnschrift" font-weight="300" font-size="158" letter-spacing="4" fill="{c["m"]}">phase</text>'
                                f'<text x="6" y="56" font-family="Bahnschrift" font-weight="400" font-size="30" letter-spacing="15" fill="{c["n"]}">ELECTRONICS CORPORATION</text>'
                                f'<rect x="6" y="78" width="14" height="14" fill="{c["a"]}"/>'
                                f'<text x="6" y="142" font-family="Microsoft YaHei" font-weight="300" font-size="44" letter-spacing="10" fill="{c["m"]}">相位电子</text>')),
}


def mark_svg(key, fn):
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512">{fn().format(**PAL[key])}</svg>\n'


def lockup_svg(key, fn):
    c = PAL[key]
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1400 512">'
            f'<g transform="translate(40,56) scale(0.78)">{fn().format(**c)}</g>'
            f'<g transform="translate(500,250)">{DESIGNS[key]["word"](c)}</g></svg>\n')


def decal_svg(key, fn, code):
    """印在机身面板上：冷灰金属板 + 四颗螺栓 + 标识（主色压暗一点）+ 模板字编号"""
    c = PAL[key]
    body = fn().format(m=c['m'], n=c['n'], a=c['a'])
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 900 260">'
            '<defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#4a525a"/>'
            '<stop offset="1" stop-color="#2c3238"/></linearGradient></defs>'
            '<rect width="900" height="260" fill="url(#g)"/><rect x="10" y="10" width="880" height="240" fill="none" stroke="#5c656e" stroke-width="2"/>'
            + ''.join(f'<circle cx="{x}" cy="{y}" r="7" fill="#20252a"/>' for x in (30, 870) for y in (30, 230)) +
            f'<g transform="translate(70,30) scale(0.39)" opacity="0.92">{body}</g>'
            f'<text x="300" y="150" font-family="Bahnschrift" font-weight="700" font-stretch="condensed" font-size="92" letter-spacing="10" fill="{c["m"]}" opacity="0.85">{code}</text>'
            f'<text x="304" y="196" font-family="Bahnschrift" font-size="20" letter-spacing="5" fill="{c["n"]}">LEAGUE STANDARD FRAME · LOT 0417</text></svg>\n')


cards_html = []
for key, D in DESIGNS.items():
    cards = []
    for name, desc, fn in D['opts']:
        tag = name.split()[0]
        base = f'{key}_{tag}'
        for suffix, text in (('图形', mark_svg(key, fn)), ('组合', lockup_svg(key, fn)), ('机身', decal_svg(key, fn, f'{key}-M{2 if tag == "A" else 3}'))):
            with open(os.path.join(OUT, f'{base}_{suffix}.svg'), 'w', encoding='utf-8') as f:
                f.write(text)
        cards.append(f'''<div class="card"><div class="row"><div class="mk"><img src="{base}_图形.svg"></div>
<div class="lk"><img src="{base}_组合.svg"></div></div>
<div class="row"><div class="dc"><img src="{base}_机身.svg"></div><div class="tiny"><img src="{base}_图形.svg" style="height:48px"><img src="{base}_图形.svg" style="height:24px"><img src="{base}_图形.svg" style="height:14px"></div></div>
<b>{name}</b><span>{desc}</span></div>''')
    cards_html.append(f'<section><h2>{D["cn"]} {key}<small>「{D["motto"][:-1]}」　{D["ref"]}</small></h2><div class="cards">{"".join(cards)}</div></section>')

CSS = """
body{margin:0;background:#0F1214;font-family:'Microsoft YaHei',sans-serif;color:#cfd5da;width:1800px}
h1{font-weight:400;font-size:30px;margin:24px 36px 4px} p{margin:0 36px 12px;color:#8b949c;font-size:15px}
section{margin:0 36px 16px} h2{font-size:22px;font-weight:600;color:#e8ecef;margin:4px 0 8px}
h2 small{font-size:14px;color:#8b949c;margin-left:12px;font-weight:400}
.cards{display:flex;gap:20px} .card{width:854px;display:flex;flex-direction:column;gap:6px}
.row{display:flex;gap:8px} .mk{background:#16191C;width:230px;height:200px;display:flex;align-items:center;justify-content:center}
.mk img{height:170px} .lk{background:#16191C;width:616px;height:200px;display:flex;align-items:center} .lk img{width:600px}
.dc img{width:600px;display:block} .tiny{background:#16191C;width:238px;display:flex;align-items:center;justify-content:space-evenly}
b{font-size:17px;color:#e8ecef;font-weight:600} span{font-size:13px;color:#9aa3aa;line-height:1.5}
"""
html = ('<!doctype html><html><head><meta charset="utf-8"><style>' + CSS + '</style></head><body>'
        '<h1>三大超级集团 · 标识第三轮</h1>'
        '<p>照真实企业和影视巨型企业的做法：能单独使用的图形 + 认真排的字标；每个方案再看印在机身面板上、缩到 48 / 24 / 14 像素时还认不认得出。</p>'
        + ''.join(cards_html) + '</body></html>')
page = os.path.join(OUT, '第三轮对照.html')
with open(page, 'w', encoding='utf-8') as f:
    f.write(html)
edge = r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
subprocess.run([edge, '--headless=new', '--disable-gpu', '--hide-scrollbars', '--window-size=1800,1560',
                '--screenshot=' + os.path.join(OUT, '第三轮对照.png'), 'file:///' + os.path.abspath(page).replace('\\', '/')],
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
print('ok')
