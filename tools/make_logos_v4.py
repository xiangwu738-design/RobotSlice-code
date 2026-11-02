"""三家企业标识第四轮（用户：「标识需要在黑色剪影或者白色剪影的情况下非常显著；维克托和相位左侧（三角翼、相栅）的感觉已经有点对了，
下一轮多做几个比比看，也可以做名称和几何结合的」）。
每个方案由三部分组成：主体、点缀（彩色版用点缀色，剪影版和主体同色）、镂空（真透明，用遮罩做，剪影里照样是洞）。
对照图每个方案四格：彩色（深底）、黑剪影（白底）、白剪影（黑底）、缩小到 32 / 16 像素。
输出 docs/世界观/标识/第四轮/<企业>/ 下的 SVG，每家一张对照图。用法：py -3 tools/make_logos_v4.py"""
import math
import os
import subprocess

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
OUT = os.path.join(ROOT, 'docs', '世界观', '标识', '第四轮')
PAL = {'NEC': ('#E6E8EA', '#EE8A1C'), 'VHI': ('#DCD4B6', '#C23A2E'), 'PEC': ('#EEF1F4', '#3CC8DC')}
CONDENSED = "font-family=\"Bahnschrift\" font-weight=\"700\" font-stretch=\"condensed\""


def poly(p, fill='{m}'):
    return f'<polygon points="{" ".join(f"{x:.1f},{y:.1f}" for x, y in p)}" fill="{fill}"/>'


def rect(x, y, w, h, fill='{m}'):
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{fill}"/>'


def delta_x(y, top=52, base=412, apex=256, half=200):
    """三角形 (apex,top)-(apex±half,base) 在高度 y 处的左右边"""
    t = (y - top) / (base - top)
    return apex - half * t, apex + half * t


# ---------------------------------------------------------------- 维克托重工：围绕三角翼
def v_delta():
    return (poly([(256, 56), (448, 384), (340, 384), (256, 318), (172, 384), (64, 384)]),
            rect(186, 418, 140, 22, '{a}'), rect(251, 40, 10, 290))


def v_nested():
    outer = [(256, 48), (464, 424), (48, 424)]
    hole = [(256, 150), (392, 392), (120, 392)]
    return (poly(outer), poly([(256, 222), (330, 360), (182, 360)], '{a}'), poly(hole, '#000'))


def v_bands():
    l0, r0 = delta_x(196)
    tip = poly([(256, 52), (r0, 196), (l0, 196)], '{a}')
    body = poly([(l0, 196), (r0, 196), (456, 412), (56, 412)])
    cuts = ''.join(rect(40, y, 432, 14) for y in (196, 268, 340))
    return body, tip, cuts


def v_wing_v():
    return (poly([(40, 112), (196, 112), (256, 272), (316, 112), (472, 112), (304, 424), (208, 424)]),
            rect(228, 52, 56, 34, '{a}'), '')


def v_formation():
    lead = [(256, 56), (344, 236), (256, 196), (168, 236)]
    left = [(150, 244), (238, 424), (150, 384), (62, 424)]
    right = [(362, 244), (450, 424), (362, 384), (274, 424)]
    return poly(left) + poly(right), poly(lead, '{a}'), ''


def v_plate():
    body = poly([(256, 44), (460, 420), (52, 420)])
    lx, rx = delta_x(236, 44, 420, 256, 204)
    band_hole = rect(lx - 20, 236, rx - lx + 40, 84)
    text = f'<text x="256" y="306" text-anchor="middle" {CONDENSED} font-size="88" letter-spacing="6" fill="{{m}}">VHI</text>'
    return body, text + rect(206, 440, 100, 18, '{a}'), band_hole


# ---------------------------------------------------------------- 相位电子：围绕相栅
def p_shear():
    s, acc = '', ''
    for i in range(8):
        r = rect(88 + i * 20, 92 + i * 42, 196, 24, '{a}' if i == 4 else '{m}')
        if i == 4:
            acc += r
        else:
            s += r
    return s, acc, ''


def p_wave():
    s, acc = '', ''
    for i in range(10):
        x = 150 + 64 * math.sin(i / 9 * 2 * math.pi)
        r = rect(round(x), 76 + i * 37, 212, 21, '{a}' if i == 2 else '{m}')
        if i == 2:
            acc += r
        else:
            s += r
    return s, acc, ''


def p_split():
    s = ''
    for i in range(9):
        y = 84 + i * 38
        if i < 4:
            s += rect(150, y, 266, 22)
        elif i > 4:
            s += rect(96, y, 266, 22)
    return s, rect(64, 84 + 4 * 38, 384, 22, '{a}'), ''


def p_letter():
    """扫描线拼出的 P"""
    s, acc = '', ''
    for i in range(9):
        y = 76 + i * 40
        if i in (0, 4):
            s += rect(112, y, 288, 24)
        elif i in (1, 2, 3):
            s += rect(112, y, 64, 24)
            r = rect(336, y, 64, 24, '{a}' if i == 2 else '{m}')
            if i == 2:
                acc += r
            else:
                s += r
        else:
            s += rect(112, y, 64, 24)
    return s, acc, ''


def p_frame():
    s = '<rect x="120" y="96" width="272" height="320" fill="none" stroke="{m}" stroke-width="22"/>'
    for i in range(5):
        if i != 2:
            s += rect(166, 150 + i * 46, 180, 20)
    return s, rect(166, 150 + 2 * 46, 300, 20, '{a}'), ''


def p_three():
    s, acc = '', ''
    for k in range(3):
        for j in range(4):
            r = rect(72 + 112 * k, 104 + 100 * k + j * 26 - 52, 144, 16, '{a}' if k == 1 else '{m}')
            if k == 1:
                acc += r
            else:
                s += r
    return s, acc, ''


# ---------------------------------------------------------------- 新纪建设：新方向（剪影要强）
def n_bridge():
    arch = '<polyline points="64,300 120,222 184,170 256,150 328,170 392,222 448,300" fill="none" stroke="{m}" stroke-width="26" stroke-linejoin="miter"/>'
    hangers = ''.join(f'<rect x="{x - 5}" y="{y}" width="10" height="{300 - y}" fill="{{m}}"/>'
                      for x, y in ((120, 222), (184, 170), (256, 150), (328, 170), (392, 222)))
    piers = rect(96, 332, 30, 92) + rect(386, 332, 30, 92) + rect(48, 436, 416, 14)
    return arch + hangers + piers, rect(48, 300, 416, 32, '{a}'), ''


def n_crane():
    mast = rect(196, 128, 40, 312)
    cuts = ''.join(rect(206, y, 20, 20) for y in range(150, 420, 40))
    jib = rect(72, 112, 392, 26) + rect(72, 138, 72, 52)
    apex = poly([(196, 112), (236, 112), (216, 56)])
    ties = ('<line x1="216" y1="60" x2="80" y2="114" stroke="{m}" stroke-width="8"/>'
            '<line x1="216" y1="60" x2="456" y2="114" stroke="{m}" stroke-width="8"/>')
    hook = '<rect x="397" y="138" width="6" height="150" fill="{m}"/>'
    base = rect(140, 440, 152, 22)
    return mast + jib + apex + ties + hook + base, rect(386, 288, 28, 34, '{a}'), cuts


def n_gate():
    pillars = rect(120, 196, 64, 228) + rect(328, 196, 64, 228)
    beam = rect(120, 232, 272, 22)
    base = rect(56, 424, 400, 28)
    return pillars + beam + base, rect(64, 126, 384, 56, '{a}'), ''


def n_letters():
    n = rect(64, 160, 34, 192) + rect(162, 160, 34, 192) + poly([(64, 160), (98, 160), (196, 352), (162, 352)])
    e = rect(224, 160, 34, 192) + rect(224, 160, 104, 30) + rect(224, 322, 104, 30)
    c = rect(356, 160, 34, 192) + rect(356, 160, 96, 30) + rect(356, 322, 96, 30)
    return n + e + c + rect(224, 241, 84, 30), rect(48, 384, 416, 26, '{a}'), ''


def n_dawn():
    block = rect(80, 96, 352, 320)
    holes = rect(80, 300, 352, 18) + rect(196, 168, 120, 132) + rect(80, 346, 352, 10) + rect(80, 380, 352, 6)
    return block, rect(96, 112, 40, 40, '{a}'), holes


def n_seal():
    frame = '<rect x="72" y="72" width="368" height="368" fill="none" stroke="{m}" stroke-width="30"/>'
    text = ('<text x="256" y="246" text-anchor="middle" font-family="Microsoft YaHei" font-weight="700" font-size="150" fill="{m}">新</text>'
            '<text x="256" y="404" text-anchor="middle" font-family="Microsoft YaHei" font-weight="700" font-size="150" fill="{m}">纪</text>')
    return frame + text, rect(380, 380, 44, 44, '{a}'), ''


DESIGNS = {
    'VHI': ('维克托重工', [('A 三角翼', '上一轮的三角翼：尾部缺口 + 中缝 + 信号红短条', v_delta),
                         ('B 双层箭', '三角框里套一个实心小三角：层层装甲、箭头向前', v_nested),
                         ('C 装甲带', '实心三角被三道横缝切成装甲层，尖端信号红；像模板喷字', v_bands),
                         ('D 翼 V', '两片后掠翼合成 V，上方一块红色铭牌：名字和几何合在一起', v_wing_v),
                         ('E 编队', '三枚三角翼排成编队，领头那枚红色：纪律、协同、向前', v_formation),
                         ('F 铭牌带', '三角翼中间横着挖出一条铭牌，里面是 VHI：名字嵌进几何', v_plate)]),
    'PEC': ('相位电子', [('A 相栅', '上一轮的相栅：横条逐条错开成斜方块，一条青色', p_shear),
                       ('B 相位波', '横条左右来回错开，整体轮廓是一道波：相位最直白的样子', p_wave),
                       ('C 错相', '上半截往右、下半截往左错开，中间一条青色横贯：相位差', p_split),
                       ('D 栅 P', '扫描线拼出一个 P：名字和几何合在一起', p_letter),
                       ('E 溢出', '方框里一组横条，其中青色那条伸出框外：使未来成为可能', p_frame),
                       ('F 三相', '三组横条沿对角线错开：三相电、三层架构', p_three)]),
    'NEC': ('新纪建设', [('A 桥', '一座桁架拱桥：战后重建、基础设施；桥面橙色', n_bridge),
                       ('B 塔吊', '塔吊剪影：起重臂、配重、吊钩，橙色吊钩块；一眼就是工地', n_crane),
                       ('C 门', '两根立柱托一道厚横梁的大门，横梁橙色：新纪元之门、机构感', n_gate),
                       ('D 梁字 NEC', '用梁和板拼出 NEC，下面一道橙色地基线：名字和几何合在一起', n_letters),
                       ('E 晨窗', '一整块方石上挖出地平线和一扇方形的日出：剪影里是洞', n_dawn),
                       ('F 新纪印', '方印里两个字「新纪」，角上一块橙色印记：老派、官方、像公文章', n_seal)]),
}


def svg(fn, main, acc):
    """主体按镂空遮罩挖洞；点缀 / 叠加层画在最上面，不受遮罩影响"""
    body, overlay, cuts = fn()
    body = body.replace('{m}', main).replace('{a}', acc)
    overlay = overlay.replace('{m}', main).replace('{a}', acc)
    if cuts:
        holes = cuts.replace('fill="{m}"', 'fill="#000"').replace('fill="{a}"', 'fill="#000"')
        mask = f'<defs><mask id="h"><rect width="512" height="512" fill="#fff"/>{holes}</mask></defs>'
        return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512">{mask}<g mask="url(#h)">{body}</g>{overlay}</svg>\n'
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512">{body}{overlay}</svg>\n'


CSS = """
body{margin:0;background:#0F1214;font-family:'Microsoft YaHei',sans-serif;color:#cfd5da;width:1800px}
h1{font-weight:400;font-size:28px;margin:22px 32px 2px} p{margin:0 32px 12px;color:#8b949c;font-size:15px}
.grid{display:grid;grid-template-columns:repeat(3,568px);gap:18px 20px;margin:0 32px 24px}
.card{display:flex;flex-direction:column;gap:6px} .row{display:flex;gap:6px}
.box{width:136px;height:136px;display:flex;align-items:center;justify-content:center} .box img{width:112px;height:112px}
.c{background:#16191C} .bw{background:#FFFFFF} .wb{background:#000000}
.tiny{background:#16191C;width:136px;height:136px;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:10px}
b{font-size:17px;color:#e8ecef;font-weight:600} span{font-size:13px;color:#9aa3aa;line-height:1.5}
"""

for key, (cn, opts) in DESIGNS.items():
    d = os.path.join(OUT, key)
    os.makedirs(d, exist_ok=True)
    main, acc = PAL[key]
    cards = ''
    for name, desc, fn in opts:
        tag = name.split()[0]
        for suffix, (m, a) in (('彩色', (main, acc)), ('黑剪影', ('#000000', '#000000')), ('白剪影', ('#FFFFFF', '#FFFFFF'))):
            with open(os.path.join(d, f'{key}_{tag}_{suffix}.svg'), 'w', encoding='utf-8') as f:
                f.write(svg(fn, m, a))
        cards += (f'<div class="card"><div class="row"><div class="box c"><img src="{key}_{tag}_彩色.svg"></div>'
                  f'<div class="box bw"><img src="{key}_{tag}_黑剪影.svg"></div><div class="box wb"><img src="{key}_{tag}_白剪影.svg"></div>'
                  f'<div class="tiny"><img src="{key}_{tag}_白剪影.svg" style="width:32px;height:32px">'
                  f'<img src="{key}_{tag}_白剪影.svg" style="width:16px;height:16px"></div></div>'
                  f'<b>{name}</b><span>{desc}</span></div>')
    html = ('<!doctype html><html><head><meta charset="utf-8"><style>' + CSS + '</style></head><body>'
            f'<h1>{cn} {key} · 标识第四轮</h1><p>每个方案四格：彩色（深底） · 黑剪影（白底） · 白剪影（黑底） · 缩到 32 / 16 像素</p>'
            f'<div class="grid">{cards}</div></body></html>')
    page = os.path.join(d, f'{key}_第四轮.html')
    with open(page, 'w', encoding='utf-8') as f:
        f.write(html)
    edge = r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
    subprocess.run([edge, '--headless=new', '--disable-gpu', '--hide-scrollbars', '--window-size=1800,520',
                    '--screenshot=' + os.path.join(OUT, f'{cn}_第四轮.png'), 'file:///' + os.path.abspath(page).replace('\\', '/')],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
print('ok')
