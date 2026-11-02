"""素材配装表（Excel，给用户自己填）：收件箱里所有武器素材各一行带图（docs/素材图，tools/render_catalog.py 渲染），
另一页是机体（框架 / 推进器）和导弹巢。用户填：归入哪一类、名称、打法、主要数值、备注。
py -3 tools/make_asset_sheet.py  →  docs/素材配装表.xlsx"""
import os

from openpyxl import Workbook
from openpyxl.drawing.image import Image as XLImage
from openpyxl.styles import Alignment, Border, Font, PatternFill, Side
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMG = os.path.join(ROOT, 'docs', '素材图')
PARTS = os.path.join(ROOT, 'docs', '配装表素材')
THUMBS = os.path.join(ROOT, 'docs', '素材图', '_表格用')
OUT = os.path.join(ROOT, 'docs', '素材配装表.xlsx')
MAX_H = 170  # 图片显示的最大高 / 宽（像素）
MAX_W = 380

FONT = '微软雅黑'
HEAD_FILL = PatternFill('solid', fgColor='2B2F36')
EDIT_FILL = PatternFill('solid', fgColor='FFF2CC')  # 黄色：你来填
USED_FILL = PatternFill('solid', fgColor='DDEBF7')  # 蓝色：现在游戏里正在用
THIN = Side(style='thin', color='B0B4BA')
BORDER = Border(left=THIN, right=THIN, top=THIN, bottom=THIN)
COLUMNS = [('编号', 8), ('图片', 56), ('现在用在', 16), ('归入哪一类', 14), ('名称', 14), ('打法', 34), ('主要数值', 36), ('备注', 24)]
EDITABLE_FROM = 4  # 第 4 列起是你填的

USED = {('长枪', '03'): '射击：步枪', ('剑', '07'): '近战：刀'}
NOTES = {('长枪', '05'): '素材包缺这一把的贴图，图里是素模'}
HINT = {'剑': '近战 / 其他', '长枪': '射击 / 肩部 / 其他', '手枪': '射击 / 其他'}


def thumb(path):
    """裁掉四周空白；按显示大小的 2.5 倍存（高分屏 180% 缩放下也清楚），返回 (文件, 显示宽, 显示高)"""
    os.makedirs(THUMBS, exist_ok=True)
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
    out = os.path.join(THUMBS, os.path.basename(path))
    bg.convert('RGB').resize((sw, sh), Image.LANCZOS).save(out)
    return out, dw, dh


def sheet(wb, title, intro, rows):
    ws = wb.create_sheet(title)
    ws['A1'] = title
    ws['A1'].font = Font(name=FONT, size=16, bold=True)
    ws['A2'] = intro
    ws['A2'].font = Font(name=FONT, size=10, color='555555')
    ws['A2'].alignment = Alignment(wrap_text=True, vertical='top')
    ws.merge_cells(start_row=2, start_column=1, end_row=2, end_column=len(COLUMNS))
    ws.row_dimensions[2].height = 34
    head = 4
    for c, (name, width) in enumerate(COLUMNS, start=1):
        cell = ws.cell(row=head, column=c, value=name)
        cell.font = Font(name=FONT, size=11, bold=True, color='FFFFFF')
        cell.fill = HEAD_FILL
        cell.alignment = Alignment(horizontal='center', vertical='center')
        cell.border = BORDER
        ws.column_dimensions[cell.column_letter].width = width
    ws.freeze_panes = ws.cell(row=head + 1, column=3)
    r = head + 1
    for num, img, used, note, hint in rows:
        pic = None
        if img:
            path, dw, dh = thumb(img)
            pic = XLImage(path)
            pic.width, pic.height = dw, dh
        ws.row_dimensions[r].height = max(48, (pic.height if pic else 60) * 0.75 + 12)
        for c in range(1, len(COLUMNS) + 1):
            cell = ws.cell(row=r, column=c)
            cell.border = BORDER
            cell.font = Font(name=FONT, size=10)
            cell.alignment = Alignment(wrap_text=True, vertical='center', horizontal='center' if c <= 4 else 'left')
            if c >= EDITABLE_FROM:
                cell.fill = EDIT_FILL
        ws.cell(row=r, column=1, value=num).font = Font(name=FONT, size=12, bold=True)
        if used:
            ws.cell(row=r, column=3, value=used).fill = USED_FILL
        if hint:
            hint_cell = ws.cell(row=r, column=4)
            hint_cell.value = None
            hint_cell.comment = None
        if note:
            ws.cell(row=r, column=8, value=note)
        if pic:
            ws.add_image(pic, ws.cell(row=r, column=2).coordinate)
        r += 1
    return ws


wb = Workbook()
wb.remove(wb.active)

intro = ('黄色格子你来填：这件素材打算当什么（归入哪一类：框架 / 推进器 / 射击 / 近战 / 肩部，不用就留空或写「不用」）、叫什么、怎么打、主要数值、备注。'
         '蓝色是现在游戏里正在用的。图是统一棚拍的，比例不代表游戏里的大小。')
for kind, count in (('剑', 20), ('长枪', 12), ('手枪', 10)):
    rows = []
    for i in range(1, count + 1):
        num = '%02d' % i
        rows.append((num, os.path.join(IMG, '%s_%s.png' % (kind, num)), USED.get((kind, num), ''), NOTES.get((kind, num), ''), HINT[kind]))
    sheet(wb, '%s（%d）' % (kind, count), intro, rows)

sheet(wb, '机体与肩部', '机体是我做的 v3（框架、推进器都从它改），导弹巢是占位模型。轻型 / 重型外壳、其他推进器以后按你填的做。', [
    ('框架', os.path.join(PARTS, '框架_中型.png'), '框架：中型', '', ''),
    ('推进器', os.path.join(PARTS, '推进器_均衡型.png'), '推进器：均衡型', '', ''),
    ('导弹巢', os.path.join(PARTS, '肩部_导弹.png'), '肩部：导弹', '占位模型，可以换', ''),
])

wb.save(OUT)
print(OUT)
