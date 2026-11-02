"""三家企业标识（SVG）：图形标 + 横排组合标 + 一张对照页（HTML，Edge 无头截图成 PNG）。
用法：py -3 tools/make_logos.py   输出到 docs/世界观/标识/
图形标都在 512×512 里画，只用直线、矩形和折线（用户偏好方正利落）。字体：英文 Bahnschrift、中文微软雅黑（系统字体，正式用时再转曲线）。"""
import os, subprocess

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'docs', '世界观', '标识')
os.makedirs(OUT, exist_ok=True)

# 每家：图形（{m}=主色，{a}=点缀色，{b}=底色，用于镂空/模板字的断口），两套配色（深底用 / 浅底用）
BRANDS = {
    'NEC': dict(
        cn='新纪建设', en='NEW ERA CONSOLIDATED', motto='我们建设明天。',
        # 两根工字钢立柱 + 一道斜撑 = N；底下一条地基线。斜撑是橙色警示色
        symbol='''
  <path d="M84 84H188V114H152V398H188V428H84V398H120V114H84Z" fill="{m}"/>
  <path d="M324 84H428V114H392V398H428V428H324V398H360V114H324Z" fill="{m}"/>
  <path d="M152 114H200L360 342V398H344L152 158Z" fill="{a}"/>
  <rect x="84" y="450" width="344" height="16" fill="{m}"/>
  <rect x="84" y="474" width="344" height="6" fill="{a}"/>''',
        dark=dict(m='#DADFE3', a='#F08C1E', b='#1B1F23'),
        light=dict(m='#23282D', a='#E07A10', b='#F3F4F5'),
        word=dict(weight=700, spacing=10, en_spacing=9),
    ),
    'VHI': dict(
        cn='维克托重工', en='VICTOR HEAVY INDUSTRIES', motto='值得信赖的力量。',
        # 八角徽章 + 模板字断口的 V 形 + 一道信号红的军衔条
        symbol='''
  <polygon points="170,46 342,46 466,170 466,342 342,466 170,466 46,342 46,170" fill="{b}" stroke="{m}" stroke-width="14" stroke-linejoin="miter"/>
  <rect x="186" y="104" width="140" height="18" fill="{a}"/>
  <polygon points="126,160 204,160 256,312 308,160 386,160 290,404 222,404" fill="{m}"/>
  <rect x="112" y="256" width="288" height="12" fill="{b}"/>''',
        dark=dict(m='#D6CEB0', a='#C0352B', b='#353B24'),
        light=dict(m='#353B24', a='#B3261E', b='#E9E4D2'),
        word=dict(weight=700, spacing=26, en_spacing=8, stencil=True),
    ),
    'PEC': dict(
        cn='相位电子', en='PHASE ELECTRONICS CORPORATION', motto='使未来成为可能。',
        # 相位符号 Φ：切角方环劈成左右两半、上下错开（相位差），中间一道竖线
        symbol='''
  <path d="M234 118H140L96 162V306L140 350H234V316H154L130 292V176L154 152H234Z" fill="{m}"/>
  <path d="M278 162H372L416 206V350L372 394H278V360H358L382 336V220L358 196H278Z" fill="{m}"/>
  <rect x="241" y="70" width="30" height="372" fill="{a}"/>''',
        dark=dict(m='#EEF1F4', a='#3CC8DC', b='#1A1E22'),
        light=dict(m='#2A2F35', a='#1597AB', b='#F4F6F8'),
        word=dict(weight=300, spacing=34, en_spacing=10),
    ),
}

def symbol_svg(key, scheme):
    c = BRANDS[key][scheme]
    body = BRANDS[key]['symbol'].format(**c)
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512">{body}\n</svg>\n'

def lockup_svg(key, scheme):
    B = BRANDS[key]; c = B[scheme]; w = B['word']
    body = B['symbol'].format(**c)
    stencil = ''
    mask = ''
    if w.get('stencil'):
        # 模板字：字母中间横着断开一道（和图形里的断口一致）
        mask = '<defs><mask id="st"><rect width="1500" height="512" fill="#fff"/><rect x="0" y="196" width="1500" height="10" fill="#000"/></mask></defs>'
        stencil = ' mask="url(#st)"'
    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1500 512">{mask}
 <g transform="translate(24,24) scale(0.906)">{body}
 </g>
 <g font-family="Bahnschrift, 'Segoe UI', sans-serif" fill="{c['m']}">
  <text x="560" y="270" font-size="210" font-weight="{w['weight']}" letter-spacing="{w['spacing']}"{stencil}>{key}</text>
  <text x="566" y="350" font-size="40" font-weight="400" letter-spacing="{w['en_spacing']}">{B['en']}</text>
 </g>
 <rect x="566" y="378" width="120" height="6" fill="{c['a']}"/>
 <text x="566" y="452" font-family="'Microsoft YaHei', sans-serif" font-size="54" font-weight="{700 if w['weight'] >= 600 else 300}" letter-spacing="6" fill="{c['m']}">{B['cn']}</text>
</svg>
'''

for key in BRANDS:
    for scheme in ('dark', 'light'):
        tag = '' if scheme == 'dark' else '_浅底'
        open(os.path.join(OUT, f'{key}_图形{tag}.svg'), 'w', encoding='utf-8').write(symbol_svg(key, scheme))
        open(os.path.join(OUT, f'{key}_组合{tag}.svg'), 'w', encoding='utf-8').write(lockup_svg(key, scheme))

# 对照页：每家一行，深底 / 浅底各一组（图形 + 组合标），右边写理念
rows = []
for key, B in BRANDS.items():
    rows.append(f'''<section>
  <div class="cell dark"><img class="lock" src="{key}_组合.svg"><img class="icon" src="{key}_图形.svg"></div>
  <div class="cell light"><img class="lock" src="{key}_组合_浅底.svg"><img class="icon" src="{key}_图形_浅底.svg"></div>
  <div class="motto"><b>{B['cn']}</b><span>{B['en']}</span><i>「{B['motto'][:-1]}」</i></div>
</section>''')
html = f'''<!doctype html><html><head><meta charset="utf-8"><style>
body{{margin:0;background:#111417;font-family:'Microsoft YaHei',sans-serif;color:#cfd5da;width:1800px}}
h1{{font-weight:400;font-size:30px;margin:28px 36px 8px}} p{{margin:0 36px 20px;color:#8b949c;font-size:17px}}
section{{display:flex;gap:16px;margin:0 36px 16px;align-items:stretch}}
.cell{{display:flex;align-items:center;gap:22px;padding:22px 26px;width:700px;border-radius:2px}}
.cell img.lock{{height:170px}} .cell img.icon{{height:56px;margin-left:auto;align-self:flex-end}}
.dark{{background:#1B1F23}} .light{{background:#F3F4F5}}
.motto{{display:flex;flex-direction:column;justify-content:center;gap:8px;font-size:16px}}
.motto b{{font-size:24px;color:#e8ecef;font-weight:600}} .motto i{{font-style:normal;color:#aab3ba}}
</style></head><body><h1>三大超级集团 · 标识草案</h1>
<p>图形标只用直线、矩形和折线；每家一个主色 + 一个点缀色。左：深色底（游戏界面）；右：浅色底（印刷 / 机身白色面板）。</p>
{''.join(rows)}</body></html>'''
open(os.path.join(OUT, '标识对照.html'), 'w', encoding='utf-8').write(html)
print('ok', OUT)
