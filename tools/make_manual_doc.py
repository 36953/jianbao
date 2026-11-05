#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""生成软件著作权登记用的「软件说明书」。

用法：python tools/make_manual_doc.py
产物：docs/软著/见报-软件说明书.docx
"""
import json
import os
import sys

from docx import Document
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_BREAK, WD_TAB_ALIGNMENT, WD_TAB_LEADER
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Pt, RGBColor

SOFTWARE = "见报电子报纸下载器"
VERSION = "V1.1.1"
OWNER = "李强"
DATE_CN = "2026 年 11 月"

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHOTS = os.path.join(ROOT, "docs", "shots")

BODY_FONT = "宋体"
HEAD_FONT = "黑体"
BODY_SIZE = Pt(10.5)


def set_run(run, font=BODY_FONT, size=BODY_SIZE, bold=False, color=None):
    run.font.name = font
    run.font.size = size
    run.font.bold = bold
    if color is not None:
        run.font.color.rgb = color
    rpr = run._element.get_or_add_rPr()
    rfonts = rpr.find(qn("w:rFonts"))
    if rfonts is None:
        rfonts = OxmlElement("w:rFonts")
        rpr.append(rfonts)
    rfonts.set(qn("w:ascii"), "Times New Roman" if font == BODY_FONT else font)
    rfonts.set(qn("w:hAnsi"), "Times New Roman" if font == BODY_FONT else font)
    rfonts.set(qn("w:eastAsia"), font)


def para(doc, text="", size=BODY_SIZE, bold=False, align=WD_ALIGN_PARAGRAPH.JUSTIFY,
         first_line=True, space_after=6, line=1.5):
    p = doc.add_paragraph()
    p.alignment = align
    pf = p.paragraph_format
    pf.space_before = Pt(0)
    pf.space_after = Pt(space_after)
    pf.line_spacing = line
    if first_line:
        pf.first_line_indent = Pt(size.pt * 2)
    if text:
        set_run(p.add_run(text), size=size, bold=bold)
    return p


def heading(doc, text, level=1):
    sizes = {1: Pt(16), 2: Pt(13), 3: Pt(11.5)}
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.LEFT
    pf = p.paragraph_format
    pf.space_before = Pt(16 if level == 1 else 10)
    pf.space_after = Pt(8 if level == 1 else 5)
    pf.line_spacing = 1.3
    pf.keep_with_next = True
    set_run(p.add_run(text), font=HEAD_FONT, size=sizes[level], bold=True)
    return p


def bullet(doc, text):
    p = doc.add_paragraph()
    pf = p.paragraph_format
    pf.left_indent = Pt(21)
    pf.space_before = Pt(0)
    pf.space_after = Pt(3)
    pf.line_spacing = 1.5
    set_run(p.add_run("· " + text))
    return p


def image(doc, name, caption, width_cm=15.4):
    path = os.path.join(SHOTS, name)
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(6)
    p.paragraph_format.space_after = Pt(2)
    p.add_run().add_picture(path, width=Cm(width_cm))
    c = doc.add_paragraph()
    c.alignment = WD_ALIGN_PARAGRAPH.CENTER
    c.paragraph_format.space_before = Pt(0)
    c.paragraph_format.space_after = Pt(10)
    set_run(c.add_run(caption), size=Pt(9), color=RGBColor(0x55, 0x55, 0x55))


def table(doc, rows, widths=None, header=True):
    t = doc.add_table(rows=len(rows), cols=len(rows[0]))
    t.style = "Table Grid"
    t.alignment = WD_TABLE_ALIGNMENT.CENTER
    for i, row in enumerate(rows):
        for j, cell_text in enumerate(row):
            cell = t.cell(i, j)
            cell.text = ""
            p = cell.paragraphs[0]
            p.paragraph_format.space_before = Pt(2)
            p.paragraph_format.space_after = Pt(2)
            p.paragraph_format.line_spacing = 1.2
            # 表格尽量不跨页：除最后一行外都与下一行同页
            p.paragraph_format.keep_with_next = (i < len(rows) - 1)
            set_run(p.add_run(str(cell_text)), size=Pt(9.5),
                    bold=(header and i == 0))
            if widths:
                cell.width = Cm(widths[j])
        # 行内禁止拆分
        tr_pr = t.rows[i]._tr.get_or_add_trPr()
        cant = OxmlElement("w:cantSplit")
        tr_pr.append(cant)
        if header and i == 0:
            th = OxmlElement("w:tblHeader")
            th.set(qn("w:val"), "true")
            tr_pr.append(th)
    doc.add_paragraph().paragraph_format.space_after = Pt(2)
    return t


def load_toc_pages():
    """目录页码由第二遍生成时回填（先出稿 → 导出 PDF 定位标题页 → 回填重排）。"""
    path = os.path.join(ROOT, "docs", "软著", "toc_pages.json")
    if not os.path.exists(path):
        return {}
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def main():
    toc_pages = load_toc_pages()
    doc = Document()

    sec = doc.sections[0]
    sec.page_width = Cm(21.0)
    sec.page_height = Cm(29.7)
    sec.top_margin = Cm(2.5)
    sec.bottom_margin = Cm(2.3)
    sec.left_margin = Cm(2.6)
    sec.right_margin = Cm(2.4)

    normal = doc.styles["Normal"]
    normal.font.name = BODY_FONT
    normal.font.size = BODY_SIZE
    normal.element.rPr.rFonts.set(qn("w:eastAsia"), BODY_FONT)

    # 页脚页码
    footer_p = sec.footer.paragraphs[0]
    footer_p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    set_run(footer_p.add_run("— "), size=Pt(9))
    r = footer_p.add_run()
    begin = OxmlElement("w:fldChar"); begin.set(qn("w:fldCharType"), "begin")
    instr = OxmlElement("w:instrText"); instr.set(qn("xml:space"), "preserve"); instr.text = " PAGE "
    sep = OxmlElement("w:fldChar"); sep.set(qn("w:fldCharType"), "separate")
    t_ = OxmlElement("w:t"); t_.text = "1"
    end = OxmlElement("w:fldChar"); end.set(qn("w:fldCharType"), "end")
    for el in (begin, instr, sep, t_, end):
        r._r.append(el)
    set_run(footer_p.add_run(" —"), size=Pt(9))

    # ---------------- 封面（首页不显示页码）
    sec.different_first_page_header_footer = True

    for _ in range(6):
        e = doc.add_paragraph(); e.paragraph_format.space_after = Pt(0)

    p = doc.add_paragraph(); p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_after = Pt(10)
    set_run(p.add_run("计算机软件著作权登记"), font=HEAD_FONT, size=Pt(18), bold=True)

    p = doc.add_paragraph(); p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_after = Pt(0)
    set_run(p.add_run("软 件 说 明 书"), font=HEAD_FONT, size=Pt(28), bold=True)

    for _ in range(5):
        e = doc.add_paragraph(); e.paragraph_format.space_after = Pt(0)

    p = doc.add_paragraph(); p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_after = Pt(10)
    set_run(p.add_run(SOFTWARE), font=HEAD_FONT, size=Pt(22), bold=True)

    p = doc.add_paragraph(); p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_after = Pt(0)
    set_run(p.add_run("版本号  %s" % VERSION), size=Pt(13))

    for _ in range(12):
        e = doc.add_paragraph(); e.paragraph_format.space_after = Pt(0)

    for label, value in (("著作权人", OWNER), ("编制日期", DATE_CN)):
        p = doc.add_paragraph(); p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        p.paragraph_format.space_after = Pt(8)
        set_run(p.add_run("%s：%s" % (label, value)), size=Pt(12))

    doc.add_page_break()

    # ---------------- 目录
    heading(doc, "目　录", 1)
    toc = [
        "一、软件概述",
        "二、运行环境",
        "三、安装与启动",
        "四、界面说明",
        "五、功能操作",
        "六、命令行方式",
        "七、技术实现",
        "八、常见问题",
        "九、声明",
    ]
    for item in toc:
        p = doc.add_paragraph()
        p.paragraph_format.space_after = Pt(8)
        p.paragraph_format.line_spacing = 1.5
        p.paragraph_format.tab_stops.add_tab_stop(
            Cm(15.4), WD_TAB_ALIGNMENT.RIGHT, WD_TAB_LEADER.DOTS)
        text = item
        if toc_pages and item in toc_pages:
            text = "%s\t%d" % (item, toc_pages[item])
        set_run(p.add_run(text), size=Pt(12))
    doc.add_page_break()

    # ---------------- 一、概述
    heading(doc, "一、软件概述", 1)
    heading(doc, "1.1 软件简介", 2)
    para(doc, "「见报」是一款运行于 Windows 桌面环境的电子报纸下载与合并工具。用户选择报纸、"
              "日期与版面后，程序从对应报社的数字报站点抓取各版的官方 PDF，按版序合并为一份"
              "完整的当日报纸，便于离线阅读、打印存档与教学使用。")
    para(doc, "程序使用 C++ 编写，基于 Win32 窗口与 GDI+ 自行绘制界面，网络层使用系统自带的 "
              "WinHTTP，PDF 合并为自研实现，不依赖任何第三方运行库。全部功能编译为单个可执行文件，"
              "复制到任意 Windows 电脑即可直接运行。")

    heading(doc, "1.2 主要功能", 2)
    for text in [
        "多报纸来源：内置人民日报、经济日报、法治日报、大众日报四家数字报，界面左栏一键切换。",
        "按日期浏览：日期步进器与日历弹层逐日翻看，支持「最近一期」快速跳转；程序启动时自动探测并载入最新一期。",
        "版面列表：以卡片网格呈现当日全部版面，卡片内绘制仿真报纸缩略图，并显示从网页真实解析的版名与头条标题。",
        "版面检索：支持按版名、版号、头条标题即时过滤；支持全选与逐版点选。",
        "下载与合并：多线程并发下载各版 PDF，可整份合并为单一 PDF，也可关闭合并保留分版文件。",
        "任务面板：实时显示已选版面、输出文件名、下载进度、已下载体积与保存位置。",
        "下载记录：自动记录每次下载的报纸、日期、文件名、版面数与体积，可一键打开所在目录。",
        "偏好设置：可配置保存目录、下载后是否自动打开目录、是否自动合并。",
        "命令行模式：同一可执行文件支持按参数批量下载，便于定时任务与脚本调用。",
    ]:
        bullet(doc, text)

    heading(doc, "1.3 适用对象", 2)
    para(doc, "需要长期保存报纸电子版的读者、教师与学生、资料室与图书馆工作人员，"
              "以及需要按日期批量获取版面 PDF 的研究与教学场景。")

    # ---------------- 二、运行环境
    heading(doc, "二、运行环境", 1)
    heading(doc, "2.1 硬件环境", 2)
    table(doc, [
        ["项目", "要求"],
        ["处理器", "x86 / x64 架构，1 GHz 及以上（程序为 64 位可执行文件）"],
        ["内存", "2 GB 及以上（整份报纸合并时峰值约 300 MB）"],
        ["硬盘", "至少 500 MB 可用空间（单期报纸约 15–150 MB）"],
        ["显示器", "分辨率 1280×860 及以上，支持 100%–200% 缩放"],
        ["网络", "可访问报社数字报站点的互联网连接"],
    ], widths=[3.0, 12.0])

    heading(doc, "2.2 软件环境", 2)
    table(doc, [
        ["项目", "说明"],
        ["操作系统", "Windows 10 / Windows 11（64 位）；Windows 8.1 亦可运行"],
        ["运行库", "无需额外安装；仅使用系统自带的 WinHTTP、GDI+、DWM"],
        ["开发语言", "C++17"],
        ["开发工具", "zig c++（LLVM/Clang 工具链）交叉编译至 x86_64-windows-gnu"],
        ["程序体积", "单个可执行文件，约 900 KB"],
    ], widths=[3.0, 12.0])

    heading(doc, "2.3 网络说明", 2)
    para(doc, "程序直接访问各报社公开的数字报站点，不需要注册或登录。若单位网络配置了代理，"
              "需在系统「Internet 选项」中设置，程序通过 WinHTTP 自动继承系统代理设置。")

    # ---------------- 三、安装与启动
    heading(doc, "三、安装与启动", 1)
    heading(doc, "3.1 获取与安装", 2)
    para(doc, "本软件为绿色免安装程序：将「见报.exe」复制到任意目录即可使用，"
              "不写入注册表，不安装服务。卸载时直接删除该文件，"
              "并在需要时删除配置目录「%APPDATA%\\见报」即可。")

    heading(doc, "3.2 启动", 2)
    para(doc, "双击「见报.exe」进入图形界面。程序启动后会自动探测当前报纸来源的可下载最新一期，"
              "并载入该日的全部版面；网络较慢时界面中栏会显示加载状态。")
    para(doc, "若需以命令行方式运行，打开「命令提示符」，进入程序所在目录后带参数执行，详见第六章。")

    heading(doc, "3.3 首次运行提示", 2)
    para(doc, "程序未做数字签名，首次运行时 Windows 可能弹出「已保护你的电脑」提示，"
              "点击「更多信息」→「仍要运行」即可。")

    # ---------------- 四、界面说明
    heading(doc, "四、界面说明", 1)
    para(doc, "主界面采用三栏布局，窗口无系统边框，标题栏、按钮与图标均由程序自行绘制，"
              "整体为浅米色纸质风格，强调色为报刊红。")
    image(doc, "download.png", "图 1  主界面（已选中 3 个版面）")

    heading(doc, "4.1 标题栏", 2)
    para(doc, "位于窗口最上方，高 48 像素。左侧为品牌标记与软件名「见报」，"
              "右侧三个按钮依次为：偏好设置、最小化、关闭。标题栏空白处可拖动窗口，"
              "窗口四角与边缘可拖拽调整大小。")

    heading(doc, "4.2 左栏（导航与来源）", 2)
    para(doc, "宽 210 像素，自上而下分三段：")
    bullet(doc, "工作台：报纸下载、下载记录两个入口，下载记录右侧显示记录条数。")
    bullet(doc, "报纸来源：列出已接入的四家报纸，当前来源以红色竖条、红色图标底板与「当前」标记表示；"
                "点击其他条目即切换来源，中栏立即按同一日期重新载入该报纸的版面。")
    bullet(doc, "底部：偏好设置、帮助与关于，以及运行状态圆点与版本号。")

    heading(doc, "4.3 中栏（版面区）", 2)
    para(doc, "自上而下为页头、工具栏与版面区：")
    bullet(doc, "页头：西文小标（如 DAZHONG DAILY）、报纸中文名与副标题，右侧为日期步进器。")
    bullet(doc, "工具栏：全选复选框、版面计数、搜索框、卡片/列表视图切换。")
    bullet(doc, "版面区：卡片网格或列表，每张卡片含仿真报纸缩略图、版名与 PDF 标记；"
                "选中时卡片描红并显示勾选角标。")

    heading(doc, "4.4 右栏（下载任务面板）", 2)
    para(doc, "宽 286 像素，显示当前任务：当日日期、已选版面列表（可逐条移除）、"
              "输出文件名预览、文件格式、保存位置、下载进度与已下载体积，底部为下载按钮。")

    # ---------------- 五、功能操作
    heading(doc, "五、功能操作", 1)

    heading(doc, "5.1 切换报纸来源", 2)
    para(doc, "点击左栏「报纸来源」中的任一条目。切换后程序会清空已选版面，"
              "并按当前日期重新抓取该报纸的版面内容。若该日无报纸（休刊或超出存档范围），"
              "中栏会给出提示，并可点击「最近一期」跳到该报纸可下载的最新日期。")

    heading(doc, "5.2 选择日期", 2)
    para(doc, "点击日期栏左右箭头按天前后翻看；点击日期本身打开日历弹层，"
              "可直接选择某一天。日历中提供「今天」与「最近一期」两个快捷按钮。")
    image(doc, "calendar.png", "图 2  日历弹层")

    heading(doc, "5.3 选择版面", 2)
    para(doc, "在卡片上点击一次即选中，再点一次取消；卡片选中后描红并出现勾选角标，"
              "同时该版面加入右栏「已选版面」列表。点击工具栏的全选复选框可一次选中全部版面。")
    para(doc, "点击工具栏右侧的视图切换按钮可在卡片视图与列表视图之间切换；"
              "列表视图每行显示一个版面，信息更紧凑，便于快速浏览整份报纸的版面构成。")
    image(doc, "list.png", "图 3  列表视图")

    heading(doc, "5.4 搜索版面", 2)
    para(doc, "在工具栏搜索框中输入关键字，版面区即时按版名、版号与头条标题过滤，"
              "计数区显示「共 N 个版面 · 匹配 M」。清空搜索框即恢复全部版面。")

    heading(doc, "5.5 下载与合并", 2)
    para(doc, "确认右栏的版面列表与输出文件名后，点击底部「下载 N 个版面」按钮。"
              "下载过程中右栏显示进度条、当前版面与已下载体积，所选卡片保持选中。")
    image(doc, "running.png", "图 4  下载进行中")
    para(doc, "下载完成后，成品保存到「保存位置」所示目录下的日期子目录中，"
              "并弹出完成提示；若在偏好设置中开启「下载后打开目录」，程序会自动打开资源管理器。"
              "若在偏好设置中关闭「自动合并多个版面」，程序会保留各版 PDF 而不合并。")

    heading(doc, "5.6 下载记录", 2)
    para(doc, "点击左栏「下载记录」进入记录页，按时间倒序列出每次下载的报纸名、日期、"
              "文件名、版面数与文件体积，点击「打开目录」可直接定位到成品所在位置。")
    image(doc, "history.png", "图 5  下载记录")

    heading(doc, "5.7 偏好设置", 2)
    para(doc, "点击标题栏的设置按钮或左栏「偏好设置」打开设置弹窗，可修改：")
    bullet(doc, "保存位置：点击「浏览」选择目录，默认位于「下载\\见报」。")
    bullet(doc, "下载后打开目录：开关，默认开启。")
    bullet(doc, "自动合并多个版面：开关，默认开启；关闭后保留逐版 PDF。")
    para(doc, "修改后点击「保存」生效并写入配置文件，下次启动自动恢复。")
    image(doc, "settings.png", "图 6  偏好设置")

    # ---------------- 六、命令行
    heading(doc, "六、命令行方式", 1)
    para(doc, "同一个可执行文件支持命令行模式，不带参数时打开图形界面，带参数时按参数执行并退出。"
              "常用参数如下：")
    table(doc, [
        ["参数", "说明"],
        ["--source ID", "指定报纸来源：rmrb 人民日报、ce 经济日报、fzrb 法治日报、dzrb 大众日报"],
        ["--sources", "列出全部已接入的报纸来源"],
        ["--date YYYY-MM-DD", "指定日期"],
        ["--latest", "自动探测并下载最新一期"],
        ["--pages 1,2,5-8", "只下载指定版面，支持逗号与区间"],
        ["--info", "只列出当日各版名称，不下载"],
        ["--out DIR", "指定输出目录"],
        ["--keep", "保留分版临时文件，便于排查问题"],
        ["--help", "显示帮助"],
    ], widths=[4.0, 11.0])
    para(doc, "示例：")
    para(doc, "见报.exe --source dzrb --latest --out D:\\报纸", first_line=False)
    para(doc, "见报.exe --source fzrb --date 2026-11-05 --pages 1,2,3", first_line=False)

    # ---------------- 七、技术实现
    heading(doc, "七、技术实现", 1)

    heading(doc, "7.1 程序结构", 2)
    table(doc, [
        ["模块", "职责"],
        ["core.h", "公共声明：日期、字符串、HTTP、来源表、站点解析与合并接口"],
        ["util.cpp", "UTF-8/UTF-16 转换、公历换算、文件操作、WinHTTP 封装"],
        ["sources.cpp", "四家报纸的来源表、版面导航解析、PDF 直链解析、头条提取、最新日期探测"],
        ["job.cpp", "下载与合并任务，图形界面与命令行共用"],
        ["pdfmerge.cpp", "自研 PDF 合并器"],
        ["ui_theme.h", "界面设计令牌（配色、尺寸）"],
        ["ui_draw.cpp", "矢量图标、字体缓存、绘图图元、仿真报纸缩略图"],
        ["ui_paint.cpp", "整帧绘制：标题栏、左栏、版面区、任务面板、记录页、弹层"],
        ["ui_app.cpp", "窗口过程、事件循环、工作线程、配置与记录持久化、命中判定"],
        ["main.cpp", "程序入口，区分图形界面与命令行模式"],
    ], widths=[3.2, 11.8])

    heading(doc, "7.2 界面绘制", 2)
    para(doc, "界面不使用系统标准控件，而是整帧绘制到内存位图后一次性呈现，避免闪烁；"
              "每帧重建矩形命中区并按逆序判定，使日历、设置弹窗、提示条等浮层优先响应。"
              "窗口采用无边框样式，自行处理非客户区计算与命中测试，并调用系统 API 取得圆角。"
              "程序按显示器 DPI 缩放，所有尺寸与字号均以 96 DPI 为基准等比换算。")

    heading(doc, "7.3 版面解析", 2)
    para(doc, "四家报纸分属三种数字报体系。人民日报新版式与经济日报采用同一套版面路径规则，"
              "但 PDF 目录名不同，且 PDF 文件名带随机标识，必须从版面页 HTML 中解析；"
              "法治日报与大众日报的 PDF 路径可由日期与版号直接推导。程序按来源分别处理，"
              "并对相对链接按 RFC 3986 规则回退目录层级。")
    para(doc, "部分版面的头条标题可从页面文章列表中提取，用于在缩略图上展示；"
              "若页面不提供文章列表，则只绘制版面占位线条，不虚构标题。")

    heading(doc, "7.4 PDF 合并", 2)
    para(doc, "合并器不依赖任何第三方库，采用「对象级搬运」的方式实现：")
    bullet(doc, "线性扫描每个源文件的全部对象，遇到流数据时先按 /Length 跳过二进制区；"
                "由于大量 PDF 使用间接长度引用（形如 /Length 49 0 R），程序会预先扫出"
                "全部裸整数对象建立数值表，再解析长度；长度不可信时回退搜索 endstream 与 endobj 定位。")
    bullet(doc, "为每个源文件分配对象号区间并整体重编号，只改写字典区中的间接引用，"
                "流数据整段原样照抄。")
    bullet(doc, "把每个页对象的父节点指向新建的页面树，重建交叉引用表与文件尾，并写入标题等文档信息。")

    heading(doc, "7.5 可靠性与容错", 2)
    para(doc, "程序在网络层实现了超时与重试；对 404、403 等状态分别给出中文提示，"
              "并可自动探测该报纸当前最新可下载日期。下载完成后校验文件头，避免保存损坏文件。")
    para(doc, "合并前会做引用完整性校验：逐个检查被引用的对象是否都已正确解析，"
              "一旦发现缺失立即中止并报出文件名与对象号，宁可报错也不产出缺图缺页的残损文件。")
    para(doc, "在界面上一律采用后台线程执行网络与合并任务，主线程只负责绘制，"
              "并用请求序号丢弃过期结果，避免快速切换日期或来源时出现串图。")

    # ---------------- 八、常见问题
    heading(doc, "八、常见问题", 1)
    faqs = [
        ("某天列不出版面", "该日期可能尚未出版或该报纸当天休刊。可点击日历中的「最近一期」跳到该报纸当前可下载的最新日期。"),
        ("卡片上的头条标题为空", "个别报纸的旧版式页面不提供文章列表，此时缩略图只绘制版面占位线条，不影响版名、下载与合并。"),
        ("下载速度慢", "报纸 PDF 体积较大，整份约 15–150 MB，个别广告版单版可达 10 MB 以上，速度取决于网络状况。程序默认 4 线程并发并自动重试。"),
        ("提示「PDF 结构异常，已停止合并」", "某一版的 PDF 内部对象不完整，继续合并会得到残损文件。可先重试一次；仍失败时关闭「自动合并多个版面」，改为保存逐版 PDF。"),
        ("界面显示模糊", "程序支持高 DPI 缩放，调整系统「显示设置」中的缩放比例后重新打开即可。"),
    ]
    for q, a in faqs:
        p = doc.add_paragraph()
        p.paragraph_format.space_before = Pt(4)
        p.paragraph_format.space_after = Pt(2)
        set_run(p.add_run("问：" + q), bold=True)
        para(doc, "答：" + a)

    # ---------------- 九、声明
    heading(doc, "九、声明", 1)
    para(doc, "本软件仅抓取各报社数字报站点公开的版面 PDF，用于个人学习、教学与研究。"
              "报纸内容的著作权归各报社所有，请勿用于商业传播或再分发；"
              "软件本身不存储、不分发任何报纸内容。")
    para(doc, "软件代码以 MIT 许可开源，著作权人保留署名权。")

    out_dir = os.path.join(ROOT, "docs", "软著")
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, "见报-软件说明书.docx")
    doc.save(out)
    print("软件说明书：%s" % out)


if __name__ == "__main__":
    sys.exit(main())
