#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""生成软件著作权登记用的「源程序」文档。

格式要求（中国版权保护中心）：
  · 每页 50 行；
  · 提交前、后各连续 30 页（共 60 页）；总行数不足 60 页时全部提交；
  · 页眉标注软件名称与版本号，页脚标注页码。

用法：python tools/make_source_doc.py
产物：docs/软著/见报-源程序.docx
"""
import os
import sys

from docx import Document
from docx.enum.section import WD_SECTION
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_BREAK, WD_LINE_SPACING
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Pt, RGBColor

SOFTWARE = "见报电子报纸下载器"
VERSION = "V1.2.0"
OWNER = "李强"

LINES_PER_PAGE = 50
SUBMIT_PAGES = 30          # 前 30 页 + 后 30 页
WRAP_AT = 104              # 单行最多字符数，超出折行
FONT_SIZE = Pt(8)
LINE_SPACING = Pt(12.5)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 按逻辑顺序排列，读起来像一份完整的程序
FILES = [
    "core.h",
    "util.cpp",
    "sources.cpp",
    "job.h",
    "job.cpp",
    "pdfmerge.cpp",
    "ui_theme.h",
    "ui_draw.h",
    "ui_draw.cpp",
    "ui.h",
    "ui_paint.cpp",
    "ui_app.cpp",
    "main.cpp",
]


def read_lines(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    lines = text.replace("\r\n", "\n").replace("\r", "\n").replace("\t", "    ").split("\n")
    while lines and lines[-1] == "":
        lines.pop()
    return lines


def wrap(line, width):
    """超长行按宽度折行，续行加 4 空格缩进以示连续。"""
    line = line.rstrip()
    if len(line) <= width:
        return [line]
    out = []
    indent = "    "
    first = True
    rest = line
    while rest:
        cap = width if first else width - len(indent)
        if len(rest) <= cap:
            out.append(rest if first else indent + rest)
            break
        cut = cap
        # 尽量在空格处断开，避免把标识符劈开
        space = rest.rfind(" ", 0, cap)
        if space > cap * 0.6:
            cut = space
        out.append(rest[:cut] if first else indent + rest[:cut])
        rest = rest[cut:].lstrip(" ")
        first = False
    return out


def add_field(paragraph, instr, placeholder="1"):
    run = paragraph.add_run()
    begin = OxmlElement("w:fldChar")
    begin.set(qn("w:fldCharType"), "begin")
    instr_text = OxmlElement("w:instrText")
    instr_text.set(qn("xml:space"), "preserve")
    instr_text.text = instr
    sep = OxmlElement("w:fldChar")
    sep.set(qn("w:fldCharType"), "separate")
    text = OxmlElement("w:t")
    text.text = placeholder
    end = OxmlElement("w:fldChar")
    end.set(qn("w:fldCharType"), "end")
    for el in (begin, instr_text, sep, text, end):
        run._r.append(el)
    return run


def set_font(run, name="Consolas", size=FONT_SIZE, east="宋体", bold=False, color=None):
    run.font.name = name
    run.font.size = size
    run.font.bold = bold
    if color is not None:
        run.font.color.rgb = color
    rpr = run._element.get_or_add_rPr()
    rfonts = rpr.find(qn("w:rFonts"))
    if rfonts is None:
        rfonts = OxmlElement("w:rFonts")
        rpr.append(rfonts)
    rfonts.set(qn("w:ascii"), name)
    rfonts.set(qn("w:hAnsi"), name)
    rfonts.set(qn("w:eastAsia"), east)


def main():
    lines = []
    per_file = {}
    for name in FILES:
        path = os.path.join(ROOT, "src", name)
        src = read_lines(path)
        per_file[name] = len(src)
        for ln in src:
            lines.extend(wrap(ln, WRAP_AT))

    total = len(lines)
    need = SUBMIT_PAGES * LINES_PER_PAGE * 2
    if total <= need:
        head, tail, omitted = lines, [], 0
    else:
        head = lines[: SUBMIT_PAGES * LINES_PER_PAGE]
        tail = lines[-(SUBMIT_PAGES * LINES_PER_PAGE - 1):]
        omitted = total - len(head) - len(tail)

    if omitted:
        body = head + ["……（此处省略中间部分，全文共 %d 行，仅打印前 1500 行与后 1499 行）……" % total] + tail
    else:
        body = head

    pages = (len(body) + LINES_PER_PAGE - 1) // LINES_PER_PAGE

    doc = Document()

    section = doc.sections[0]
    section.page_width = Cm(21.0)
    section.page_height = Cm(29.7)
    section.top_margin = Cm(2.2)
    section.bottom_margin = Cm(2.0)
    section.left_margin = Cm(2.2)
    section.right_margin = Cm(2.0)
    section.header_distance = Cm(1.0)
    section.footer_distance = Cm(1.0)

    normal = doc.styles["Normal"]
    normal.font.name = "Consolas"
    normal.font.size = FONT_SIZE
    normal.element.rPr.rFonts.set(qn("w:eastAsia"), "宋体")
    pf = normal.paragraph_format
    pf.space_before = Pt(0)
    pf.space_after = Pt(0)
    pf.line_spacing_rule = WD_LINE_SPACING.EXACTLY
    pf.line_spacing = LINE_SPACING

    # ---- 页眉
    header = section.header
    hp = header.paragraphs[0]
    hp.text = ""
    hp.alignment = WD_ALIGN_PARAGRAPH.LEFT
    set_font(hp.add_run("%s %s   源程序" % (SOFTWARE, VERSION)), size=Pt(9), color=RGBColor(0x44, 0x44, 0x44))
    for r in hp.runs:
        r.font.name = "宋体"
    # 页眉下划线（Word 的页眉默认有样式，这里补一条细线更清晰）
    p_pr = hp._p.get_or_add_pPr()
    borders = OxmlElement("w:pBdr")
    bottom = OxmlElement("w:bottom")
    bottom.set(qn("w:val"), "single")
    bottom.set(qn("w:sz"), "4")
    bottom.set(qn("w:space"), "1")
    bottom.set(qn("w:color"), "999999")
    borders.append(bottom)
    p_pr.append(borders)

    # ---- 页脚：第 X 页 / 共 Y 页
    footer = section.footer
    fp = footer.paragraphs[0]
    fp.text = ""
    fp.alignment = WD_ALIGN_PARAGRAPH.CENTER
    r = fp.add_run("第 ")
    set_font(r, name="宋体", size=Pt(9), east="宋体")
    add_field(fp, " PAGE ")
    r = fp.add_run(" 页  共 %d 页" % pages)
    set_font(r, name="宋体", size=Pt(9), east="宋体")
    for run in fp.runs:
        run.font.name = "宋体"

    # ---- 正文
    for i, text in enumerate(body):
        p = doc.add_paragraph()
        p.paragraph_format.line_spacing_rule = WD_LINE_SPACING.EXACTLY
        p.paragraph_format.line_spacing = LINE_SPACING
        p.paragraph_format.space_before = Pt(0)
        p.paragraph_format.space_after = Pt(0)
        if text:
            run = p.add_run(text)
            set_font(run)
        else:
            p.add_run("")
        at_page_end = ((i + 1) % LINES_PER_PAGE == 0) and (i != len(body) - 1)
        if at_page_end:
            p.add_run().add_break(WD_BREAK.PAGE)

    out_dir = os.path.join(ROOT, "docs", "软著")
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, "见报-源程序.docx")
    doc.save(out)

    print("源程序文档：%s" % out)
    print("源文件行数：%d，折行后 %d 行，文档 %d 页（每页 %d 行）"
          % (sum(per_file.values()), total, pages, LINES_PER_PAGE))
    for k, v in per_file.items():
        print("  %-14s %5d 行" % (k, v))


if __name__ == "__main__":
    sys.exit(main())
