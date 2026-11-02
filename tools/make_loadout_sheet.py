"""配装表（Excel，给用户自己填）：五类部件，每类一行现有部件（带图、按当前游戏数值填好，当填写示例）+ 两行待定。
图片来自 docs/配装表素材（武器是 tools/render_parts.py 的 Blender 棚拍图，框架 / 推进器是 v3 机体棚拍图）。
py -3 tools/make_loadout_sheet.py  →  docs/配装表.xlsx"""
import os

from openpyxl import Workbook
from openpyxl.drawing.image import Image as XLImage
from openpyxl.styles import Alignment, Border, Font, PatternFill, Side
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'docs', '配装表素材')
OUT = os.path.join(ROOT, 'docs', '配装表.xlsx')
MAX_H = 170  # 图片显示的最大高 / 宽（像素）
MAX_W = 380
THUMBS = SRC

FONT = '微软雅黑'
HEAD_FILL = PatternFill('solid', fgColor='2B2F36')
CAT_FILL = PatternFill('solid', fgColor='E8EAED')
EDIT_FILL = PatternFill('solid', fgColor='FFF2CC')  # 黄色：你来填
THIN = Side(style='thin', color='B0B4BA')
BORDER = Border(left=THIN, right=THIN, top=THIN, bottom=THIN)

COLUMNS = [('类别', 9), ('图片', 56), ('名称', 14), ('打法', 34), ('主要数值', 42), ('擅长', 22), ('怕', 22), ('制作 / 备注', 22)]

# 每类：现有部件（图片文件、名称、打法、数值、擅长、怕、备注）
EXISTING = [
    ('框架', '框架_中型.png', '中型', '现在这台机体，均衡', '装甲 12000；失衡上限 1400；行走 24 km/h', '什么都能打', '没有突出的长处', '现成'),
    ('推进器', '推进器_均衡型.png', '均衡型', '跳跃，空中按住空格向上喷射，空中按住 Shift 滑翔',
     '冲刺 54 km/h；快速闪避 94 km/h，耗能 22；能量 100', '地面空中都行', '没有突出的长处', '现成'),
    ('射击', '射击_步枪.png', '步枪', '射击姿态，按住左键连射，中距离；右键瞄准散布变小',
     '每发伤害 75、冲击 22；每秒约 9 发；弹匣 36 / 换弹 2.2 秒；弹速 330 m/s', '稳定输出', '单靠它很难打出失衡', '现成'),
    ('近战', '近战_刀.png', '刀', '近战姿态，按下轻击（连按四段连招），按住 0.22 秒重斩，右键格挡',
     '轻击 380 / 380 / 420 / 650，重斩 1400；积累失衡按冲击的 0.55；格挡挡正面 75°，伤害 ×0.25', '连招快、灵活', '单靠刀打不出失衡', '现成'),
    ('肩部', '肩部_导弹.png', '导弹', 'E 键，准星对住目标 0.8 秒锁定后 4 发追踪',
     '每发伤害 170、冲击 180；冷却 8 秒', '打慢的、站桩的', '快速闪避能甩掉', '现成'),
]
BLANK_ROWS = 2  # 每类再留几行待定


def thumb(name):
    """裁掉四周空白；按显示大小的 2.5 倍存（高分屏 180% 缩放下也清楚），返回 (文件, 显示宽, 显示高)"""
    os.makedirs(THUMBS, exist_ok=True)
    path = os.path.join(SRC, name)
    im = Image.open(path).convert('RGBA')
    bg = Image.new('RGBA', im.size, (255, 255, 255, 255))
    bg.alpha_composite(im)
    box = Image.eval(bg.convert('L'), lambda v: 255 if v < 250 else 0).getbbox()
    if box:
        pad = 16
        box = (max(0, box[0] - pad), max(0, box[1] - pad), min(im.width, box[2] + pad), min(im.height, box[3] + pad))
        bg = bg.crop(box)
    # 显示大小：高不超过 MAX_H、宽不超过 MAX_W（又细又长的剑按宽度来）
    scale = min(MAX_H / bg.height, MAX_W / bg.width)
    dw, dh = int(bg.width * scale), int(bg.height * scale)
    sw, sh = min(bg.width, dw * 5 // 2), min(bg.height, dh * 5 // 2)
    out = os.path.join(THUMBS, '_表格用_' + name)
    bg.convert('RGB').resize((sw, sh), Image.LANCZOS).save(out)
    return out, dw, dh


wb = Workbook()
ws = wb.active
ws.title = '配装表'

# 说明
ws['A1'] = '配装表（五类：框架、推进器、射击、近战、肩部）'
ws['A1'].font = Font(name=FONT, size=16, bold=True)
ws['A2'] = ('黄色格子由你来填写。每类第一行是现在游戏里已有的部件，已按当前数值填好，可以当填写示例，也可以直接改；'
            '后面两行是待定部件，图片列空着，确定以后我来做模型、出图。行不够可以直接插入。')
ws['A2'].font = Font(name=FONT, size=10, color='555555')
ws['A2'].alignment = Alignment(wrap_text=True, vertical='top')
ws.merge_cells(start_row=2, start_column=1, end_row=2, end_column=len(COLUMNS))
ws.row_dimensions[2].height = 34

HEAD = 4
for c, (title, width) in enumerate(COLUMNS, start=1):
    cell = ws.cell(row=HEAD, column=c, value=title)
    cell.font = Font(name=FONT, size=11, bold=True, color='FFFFFF')
    cell.fill = HEAD_FILL
    cell.alignment = Alignment(horizontal='center', vertical='center')
    cell.border = BORDER
    ws.column_dimensions[cell.column_letter].width = width
ws.row_dimensions[HEAD].height = 24
ws.freeze_panes = ws.cell(row=HEAD + 1, column=3)

row = HEAD + 1
for cat, img, *values in EXISTING:
    first = row
    pic = None
    path, dw, dh = thumb(img)
    pic = XLImage(path)
    pic.width, pic.height = dw, dh
    for k in range(1 + BLANK_ROWS):
        ws.row_dimensions[row].height = (dh * 0.75 + 12) if k == 0 else 40
        for c in range(1, len(COLUMNS) + 1):
            cell = ws.cell(row=row, column=c)
            cell.border = BORDER
            cell.alignment = Alignment(wrap_text=True, vertical='center', horizontal='center' if c <= 3 else 'left')
            cell.font = Font(name=FONT, size=10)
            if c >= 3:
                cell.fill = EDIT_FILL
        if k == 0:
            for c, v in enumerate(values, start=3):
                ws.cell(row=row, column=c, value=v)
            ws.add_image(pic, ws.cell(row=row, column=2).coordinate)
        else:
            ws.cell(row=row, column=2, value='（待做）').font = Font(name=FONT, size=10, color='999999')
            ws.cell(row=row, column=3, value='待定 %d' % k).font = Font(name=FONT, size=10, color='999999')
        row += 1
    ws.merge_cells(start_row=first, start_column=1, end_row=row - 1, end_column=1)
    head = ws.cell(row=first, column=1, value=cat)
    head.font = Font(name=FONT, size=12, bold=True)
    head.fill = CAT_FILL
    head.alignment = Alignment(horizontal='center', vertical='center')

os.makedirs(os.path.dirname(OUT), exist_ok=True)
wb.save(OUT)
print(OUT)
