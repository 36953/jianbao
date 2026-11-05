"""生成应用图标：红色圆角方块 + 白色报纸图形，多尺寸打包为 ICO。"""
from PIL import Image, ImageDraw

RED = (184, 34, 34, 255)
WHITE = (255, 255, 255, 255)

S = 1024
im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(im)

# 圆角底板
d.rounded_rectangle([0, 0, S - 1, S - 1], radius=int(S * 0.22), fill=RED)

# 白色报纸页
px0, py0, px1, py1 = int(S * 0.235), int(S * 0.165), int(S * 0.765), int(S * 0.835)
d.rounded_rectangle([px0, py0, px1, py1], radius=int(S * 0.032), fill=WHITE)

# 报头粗条
d.rectangle([px0 + int(S * 0.075), py0 + int(S * 0.068),
             px1 - int(S * 0.075), py0 + int(S * 0.118)], fill=RED)
# 正文细线
mx = int(S * 0.075)
fullW = (px1 - mx) - (px0 + mx)
y = py0 + int(S * 0.163)
widths = [1.0, 1.0, 1.0, 1.0, 1.0, 0.42]
for w in widths:
    d.rectangle([px0 + mx, y, px0 + mx + int(fullW * w), y + int(S * 0.034)], fill=RED)
    y += int(S * 0.076)

im.save("app_icon_1024.png")

sizes = [256, 128, 64, 48, 32, 24, 16]
imgs = [im.resize((s, s), Image.LANCZOS) for s in sizes]
imgs[0].save("app.ico", format="ICO", sizes=[(s, s) for s in sizes])
print("app.ico", imgs[0].size, sizes)
