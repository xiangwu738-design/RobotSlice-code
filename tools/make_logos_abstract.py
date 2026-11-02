"""三家企业的抽象图形标（第二轮，用户：「不用特别专注于字母，可以多搞点抽象的」）。
每家 3 个方案，都在 512×512 里只用直线、矩形和折线画。输出 docs/世界观/标识/抽象/ 下的 SVG 和一张对照图（Edge 无头截图）。
用法：py -3 tools/make_logos_abstract.py"""
import os
import subprocess

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
OUT = os.path.join(ROOT, 'docs', '世界观', '标识', '抽象')
os.makedirs(OUT, exist_ok=True)

COLORS = {
    'NEC': dict(dark=dict(m='#DADFE3', a='#F08C1E'), light=dict(m='#23282D', a='#E07A10')),
    'VHI': dict(dark=dict(m='#D6CEB0', a='#C0352B'), light=dict(m='#353B24', a='#B3261E')),
    'PEC': dict(dark=dict(m='#EEF1F4', a='#3CC8DC'), light=dict(m='#2A2F35', a='#1597AB')),
}


def chevrons_down():
    """三道往下的军衔臂章，最上面一道是点缀色"""
    s = ''
    for k in range(3):
        ty = 96 + 90 * k
        c = '{a}' if k == 0 else '{m}'
        s += f'<polygon points="96,{ty} 152,{ty} 256,{ty + 104} 360,{ty} 416,{ty} 256,{ty + 160}" fill="{c}"/>'
    return s


def lattice():
    """错位排列的方格（奇数行错开半格），其中一格是点缀色"""
    s = ''
    for i in range(4):
        shift = 42 if i % 2 else 0
        for j in range(4 if i % 2 == 0 else 3):
            x, y = 96 + j * 84 + shift, 96 + i * 84
            c = '{a}' if (i, j) == (1, 1) else '{m}'
            s += f'<rect x="{x}" y="{y}" width="56" height="56" fill="{c}"/>'
    return s


def triad():
    """三片朝外的人字形绕中心转 0 / 120 / 240 度：火力、装甲、机动均衡；朝上那片是点缀色"""
    s = ''
    for r in (0, 120, 240):
        c = '{a}' if r == 0 else '{m}'
        s += f'<polygon points="256,72 344,160 304,160 256,112 208,160 168,160" fill="{c}" transform="rotate({r} 256 276)"/>'
    return s + '<polygon points="256,236 290,296 222,296" fill="{m}"/>'


def square_wave():
    ticks = ''.join(f'<rect x="{64 + 64 * k}" y="372" width="6" height="22" fill="{{m}}"/>' for k in range(7))
    return ('<polyline points="64,320 144,320 144,192 224,192 224,320 288,320" fill="none" stroke="{m}" stroke-width="26" '
            'stroke-linejoin="miter" stroke-linecap="square"/>'
            '<polyline points="288,320 288,192 368,192 368,320" fill="none" stroke="{a}" stroke-width="26" '
            'stroke-linejoin="miter" stroke-linecap="square"/>'
            '<polyline points="368,320 448,320" fill="none" stroke="{m}" stroke-width="26" stroke-linecap="square"/>'
            '<rect x="64" y="372" width="384" height="6" fill="{m}"/>' + ticks)


SYMBOLS = {
    'NEC': [
        ('A 地基', '三层台阶式的厚板一层层往上收，顶上一块橙色方块：稳、按部就班、往上盖',
         '<rect x="64" y="352" width="384" height="56" fill="{m}"/><rect x="128" y="272" width="256" height="56" fill="{m}"/>'
         '<rect x="192" y="192" width="128" height="56" fill="{m}"/><rect x="232" y="112" width="48" height="48" fill="{a}"/>'),
        ('B 合拢', '两块 L 形钢件咬合成一个方框，中间一块实心：「Consolidated」，整合并购、资产抱团',
         '<path d="M96 96H406V160H160V406H96Z" fill="{m}"/><path d="M416 416H106V352H352V106H416Z" fill="{a}"/>'
         '<rect x="216" y="216" width="80" height="80" fill="{m}"/>'),
        ('C 晨线', '地平线上升起一块方形的「太阳」，下面三层基础设施越来越窄：「我们建设明天」',
         '<rect x="196" y="176" width="120" height="112" fill="{a}"/><rect x="64" y="300" width="384" height="24" fill="{m}"/>'
         '<rect x="112" y="348" width="288" height="14" fill="{m}"/><rect x="160" y="384" width="192" height="12" fill="{m}"/>'
         '<rect x="208" y="416" width="96" height="10" fill="{m}"/>'),
    ],
    'VHI': [
        ('A 三角均衡', '三片人字形朝外排成三角：火力、装甲、机动三者均衡；朝上那片信号红', triad()),
        ('B 军衔', '三道往下的军衔臂章叠在一起，最上面一道信号红：纪律、等级、可靠', chevrons_down()),
        ('C 准星', '四角框住、四边刻度向内收，中心一颗红色菱形：火力和精确',
         '<path d="M96 96H200V124H124V200H96Z" fill="{m}"/><path d="M416 96H312V124H388V200H416Z" fill="{m}"/>'
         '<path d="M96 416H200V388H124V312H96Z" fill="{m}"/><path d="M416 416H312V388H388V312H416Z" fill="{m}"/>'
         '<rect x="244" y="96" width="24" height="64" fill="{m}"/><rect x="244" y="352" width="24" height="64" fill="{m}"/>'
         '<rect x="96" y="244" width="64" height="24" fill="{m}"/><rect x="352" y="244" width="64" height="24" fill="{m}"/>'
         '<polygon points="256,196 316,256 256,316 196,256" fill="{a}"/>'),
    ],
    'PEC': [
        ('A 错相', '两个方框沿对角线错开半个身位，重叠的那块亮青色：相位差、干涉',
         '<path d="M96 96H336V336H96Z M124 124V308H308V124Z" fill="{m}" fill-rule="evenodd"/>'
         '<path d="M176 176H416V416H176Z M204 204V388H388V204Z" fill="{m}" fill-rule="evenodd"/>'
         '<rect x="204" y="204" width="104" height="104" fill="{a}"/>'),
        ('B 方波', '一段时钟方波，其中一个脉冲亮青色，下面一条时间轴：计算、信号、节拍', square_wave()),
        ('C 晶格', '错位排列的方格像芯片上的单元，其中一格亮青色：架构、专利、生态', lattice()),
    ],
}

NAMES = {'NEC': '新纪建设 NEC', 'VHI': '维克托重工 VHI', 'PEC': '相位电子 PEC'}
MOTTO = {'NEC': '我们建设明天', 'VHI': '值得信赖的力量', 'PEC': '使未来成为可能'}


def svg(key, idx, scheme):
    body = SYMBOLS[key][idx][2].format(**COLORS[key][scheme])
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512">{body}</svg>\n'


rows = []
for key, options in SYMBOLS.items():
    cards = []
    for i, (name, desc, _) in enumerate(options):
        tag = name.split()[0]
        for scheme in ('dark', 'light'):
            suffix = '' if scheme == 'dark' else '_浅底'
            with open(os.path.join(OUT, f'{key}_{tag}{suffix}.svg'), 'w', encoding='utf-8') as f:
                f.write(svg(key, i, scheme))
        cards.append(f'<div class="card"><div class="pair"><div class="d"><img src="{key}_{tag}.svg"></div>'
                     f'<div class="l"><img src="{key}_{tag}_浅底.svg"></div></div><b>{name}</b><span>{desc}</span></div>')
    rows.append(f'<section><h2>{NAMES[key]}<small>「{MOTTO[key]}」</small></h2><div class="cards">{"".join(cards)}</div></section>')

CSS = """
body{margin:0;background:#111417;font-family:'Microsoft YaHei',sans-serif;color:#cfd5da;width:1800px}
h1{font-weight:400;font-size:30px;margin:26px 36px 4px} p{margin:0 36px 14px;color:#8b949c;font-size:16px}
section{margin:0 36px 18px} h2{font-size:22px;font-weight:600;color:#e8ecef;margin:6px 0 10px}
h2 small{font-size:15px;color:#8b949c;margin-left:14px;font-weight:400}
.cards{display:flex;gap:18px} .card{width:564px;display:flex;flex-direction:column;gap:6px}
.pair{display:flex;gap:8px} .pair>div{display:flex;align-items:center;justify-content:center}
.d{background:#1B1F23;width:340px;height:230px} .d img{height:190px}
.l{background:#F3F4F5;width:216px;height:230px} .l img{height:110px}
b{font-size:18px;color:#e8ecef;font-weight:600} span{font-size:14px;color:#9aa3aa;line-height:1.5}
"""
html = ('<!doctype html><html><head><meta charset="utf-8"><style>' + CSS + '</style></head><body>'
        '<h1>三大超级集团 · 抽象图形标（第二轮）</h1>'
        '<p>每家 3 个方案，只用直线、矩形和折线；左大图是深色底（游戏界面），右小图是浅色底。</p>'
        + ''.join(rows) + '</body></html>')
page = os.path.join(OUT, '抽象标识对照.html')
with open(page, 'w', encoding='utf-8') as f:
    f.write(html)
edge = r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
subprocess.run([edge, '--headless=new', '--disable-gpu', '--hide-scrollbars', '--window-size=1800,1240',
                '--screenshot=' + os.path.join(OUT, '抽象标识对照.png'), 'file:///' + os.path.abspath(page).replace('\\', '/')],
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
print('ok')
