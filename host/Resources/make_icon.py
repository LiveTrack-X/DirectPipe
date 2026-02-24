"""Generate DirectPipe app icon as PNG - unified with Stream Deck style."""
from PIL import Image, ImageDraw, ImageFilter
import math

SIZE = 512
img = Image.new('RGBA', (SIZE, SIZE), (0, 0, 0, 0))
draw = ImageDraw.Draw(img)

# Colors - matching Stream Deck directpipe.svg
BG_TOP = (26, 26, 46)       # #1a1a2e
BG_BOT = (22, 33, 62)       # #16213e
WHITE = (255, 255, 255)
CYAN = (79, 195, 247)       # #4fc3f7

# --- Background rounded rectangle with gradient ---
radius = 64
for y in range(SIZE):
    t = y / SIZE
    r = int(BG_TOP[0] + (BG_BOT[0] - BG_TOP[0]) * t)
    g = int(BG_TOP[1] + (BG_BOT[1] - BG_TOP[1]) * t)
    b = int(BG_TOP[2] + (BG_BOT[2] - BG_TOP[2]) * t)
    draw.line([(0, y), (SIZE, y)], fill=(r, g, b, 255))

# Mask to rounded rectangle
mask = Image.new('L', (SIZE, SIZE), 0)
mask_draw = ImageDraw.Draw(mask)
mask_draw.rounded_rectangle([(0, 0), (SIZE, SIZE)], radius=radius, fill=255)
img.putalpha(mask)
draw = ImageDraw.Draw(img)

# --- Microphone body (outlined oval) ---
mic_cx = 256
mic_top = 100
mic_w = 80
mic_h = 160
stroke = 20

# Draw outlined rounded rect (mic capsule)
draw.rounded_rectangle(
    [(mic_cx - mic_w//2, mic_top), (mic_cx + mic_w//2, mic_top + mic_h)],
    radius=mic_w//2,
    fill=None,
    outline=WHITE,
    width=stroke
)

# --- Microphone stand arc ---
# Draw arc from left side up and over to right side
arc_start_y = mic_top + mic_h - 20  # where arc connects to mic sides
arc_bottom_y = 320
arc_half_w = 80

# Use bezier-like approach with lines
segments = 40
for i in range(segments):
    t1 = i / segments
    t2 = (i + 1) / segments
    angle1 = math.pi * t1
    angle2 = math.pi * t2
    x1 = mic_cx + arc_half_w * math.cos(angle1)
    y1 = arc_bottom_y - (arc_bottom_y - arc_start_y) * math.sin(angle1)
    x2 = mic_cx + arc_half_w * math.cos(angle2)
    y2 = arc_bottom_y - (arc_bottom_y - arc_start_y) * math.sin(angle2)
    draw.line([(x1, y1), (x2, y2)], fill=WHITE, width=stroke)

# --- Stand line ---
draw.line([(mic_cx, 320), (mic_cx, 370)], fill=WHITE, width=stroke)

# --- Arrow / chain indicator ---
pts = [(220, 390), (256, 370), (292, 390)]
draw.line([pts[0], pts[1]], fill=WHITE, width=12)
draw.line([pts[1], pts[2]], fill=WHITE, width=12)

# --- Audio waveform bars (cyan) ---
bars = [
    (180, 430, 410),
    (216, 440, 400),
    (256, 445, 395),
    (296, 440, 400),
    (332, 430, 410),
]
for x, y_bot, y_top in bars:
    draw.line([(x, y_top), (x, y_bot)], fill=(*CYAN, 255), width=14)
    # Round caps
    draw.ellipse([(x-7, y_top-7), (x+7, y_top+7)], fill=(*CYAN, 255))
    draw.ellipse([(x-7, y_bot-7), (x+7, y_bot+7)], fill=(*CYAN, 255))

# --- Save ---
output_dir = 'C:/Users/livet/Desktop/DirectLoopMic/host/Resources'
img.save(f'{output_dir}/icon.png', 'PNG')
print(f"Icon saved: {SIZE}x{SIZE} PNG")

img_256 = img.resize((256, 256), Image.LANCZOS)
img_256.save(f'{output_dir}/icon_256.png', 'PNG')
print("Also saved 256x256 version")
