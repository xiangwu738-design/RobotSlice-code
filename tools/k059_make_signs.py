"""城市图的几块世界观牌子（程序画的简单图，只用中文和数字）→ SourceAssets/city_signs/*.png
py -3 -I tools/k059_make_signs.py
- odds_board：转播塔上的赔率屏（7×4.2 米）
- evac_notice：贴在墙上的疏散通告（4×2.4 米）
- event_ad：联排屋顶的赛事广告（14×7 米）
- sponsor_ad：旅馆南墙的保险广告（18×9 米）
"""
import os

from PIL import Image, ImageDraw, ImageFilter, ImageFont

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'SourceAssets', 'city_signs')
os.makedirs(OUT, exist_ok=True)
F = r'C:\Windows\Fonts\msyhbd.ttc'
FR = r'C:\Windows\Fonts\msyh.ttc'


def font(size, bold=True):
    return ImageFont.truetype(F if bold else FR, size)


def center(dr, y, text, f, fill, W):
    w = dr.textlength(text, font=f)
    dr.text(((W - w) / 2, y), text, font=f, fill=fill)


# ---- 赔率屏：深底、青 / 橙两队，像体育直播的比分牌
W, H = 1000, 600
im = Image.new('RGB', (W, H), (8, 12, 18))
dr = ImageDraw.Draw(im)
for y in range(0, H, 4):      # LED 扫描线
    dr.line([(0, y), (W, y)], fill=(4, 6, 9))
dr.rectangle([0, 0, W, 92], fill=(18, 26, 36))
center(dr, 14, '城区裁决赛 · 第 07 场', font(56), (235, 240, 245), W)
dr.rectangle([40, 130, W // 2 - 20, 430], outline=(60, 200, 255), width=5)
dr.rectangle([W // 2 + 20, 130, W - 40, 430], outline=(255, 120, 40), width=5)
center(dr, 0, '', font(10), (0, 0, 0), W)
for x0, name, odds, col in ((40, '蓝方', '1.85', (60, 200, 255)), (W // 2 + 20, '红方', '2.10', (255, 120, 40))):
    cw = W // 2 - 60
    f1, f2 = font(64), font(150)
    w = dr.textlength(name, font=f1)
    dr.text((x0 + (cw - w) / 2, 150), name, font=f1, fill=col)
    w = dr.textlength(odds, font=f2)
    dr.text((x0 + (cw - w) / 2, 235), odds, font=f2, fill=(245, 245, 245))
dr.rectangle([0, 470, W, H], fill=(18, 26, 36))
center(dr, 492, '实时赔率 · 下注请认准官方渠道', font(46, False), (200, 210, 220), W)
im = im.filter(ImageFilter.GaussianBlur(0.6))
im.save(os.path.join(OUT, 'odds_board.png'))

# ---- 疏散通告：白纸黑字红章
W, H = 1000, 600
im = Image.new('RGB', (W, H), (232, 230, 222))
dr = ImageDraw.Draw(im)
dr.rectangle([12, 12, W - 12, H - 12], outline=(170, 30, 30), width=8)
center(dr, 40, '疏散通告', font(96), (170, 25, 25), W)
lines = ['本街区今日 09:00 至 18:00 征用为公开裁决赛场地。',
         '请居民于开赛前撤离，贵重物品随身携带。',
         '赛事造成的财产损失由胜方企业依法赔付。',
         '围观、拍摄、下注请移步官方观赛区。']
for i, t in enumerate(lines):
    dr.text((60, 175 + i * 64), t, font=font(37, False), fill=(30, 30, 30))
dr.text((W - 560, H - 92), '城市赛事管理局', font=font(44), fill=(40, 40, 40))
dr.ellipse([W - 220, H - 150, W - 100, H - 30], outline=(190, 40, 40), width=7)
im.save(os.path.join(OUT, 'evac_notice.png'))

# ---- 赛事广告：冷灰底、巨大标题
W, H = 1400, 700
im = Image.new('RGB', (W, H), (26, 30, 36))
dr = ImageDraw.Draw(im)
for i in range(0, W, 70):     # 斜条纹底
    dr.line([(i, H), (i + 300, 0)], fill=(32, 37, 44), width=26)
dr.rectangle([0, H - 150, W, H], fill=(235, 120, 30))
center(dr, 90, '城区裁决赛', font(230), (240, 244, 248), W)
center(dr, 380, '今天的胜负 · 明天的股价', font(82, False), (170, 205, 230), W)
center(dr, H - 128, '全程直播  第 3 机位  正在为您转播', font(74), (20, 20, 22), W)
im.save(os.path.join(OUT, 'event_ad.png'))

# ---- 保险广告（黑色幽默）
W, H = 1400, 700
im = Image.new('RGB', (W, H), (232, 236, 240))
dr = ImageDraw.Draw(im)
dr.rectangle([0, 0, 380, H], fill=(30, 90, 160))
dr.text((60, 200), '安心', font=font(150), fill=(255, 255, 255))
dr.text((60, 380), '保险', font=font(150), fill=(255, 255, 255))
dr.text((440, 120), '机甲误伤险', font=font(170), fill=(30, 90, 160))
dr.text((440, 340), '赛区住户专享 · 首月半价', font=font(74, False), fill=(40, 44, 50))
dr.text((440, 460), '房子塌了也能笑着搬家', font=font(74, False), fill=(40, 44, 50))
dr.rectangle([440, 590, 1340, 600], fill=(30, 90, 160))
im.save(os.path.join(OUT, 'sponsor_ad.png'))
print('[signs] →', OUT, sorted(os.listdir(OUT)))
