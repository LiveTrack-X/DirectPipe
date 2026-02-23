"""Generate DirectPipe app icon as PNG using Pillow."""
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import math

SIZE = 512
img = Image.new('RGBA', (SIZE, SIZE), (0, 0, 0, 0))
draw = ImageDraw.Draw(img)

# Colors (matching app dark theme)
BG_DARK = (30, 30, 46, 255)     # #1E1E2E
ACCENT = (108, 99, 255)         # #6C63FF
ACCENT_LIGHT = (150, 142, 255)  # lighter accent
ACCENT_DIM = (80, 72, 200)      # dimmer accent
BORDER = (58, 56, 96)           # #3A3860

# --- Background rounded rectangle ---
radius = 96
draw.rounded_rectangle(
    [(16, 16), (496, 496)],
    radius=radius,
    fill=BG_DARK,
    outline=BORDER,
    width=3
)

# --- Very subtle top gradient (just a hint of lighter) ---
grad_img = Image.new('RGBA', (SIZE, SIZE), (0, 0, 0, 0))
grad_draw = ImageDraw.Draw(grad_img)
for i in range(240):
    alpha = max(0, int(12 * (1.0 - i / 240.0)))
    grad_draw.line([(16, 16 + i), (496, 16 + i)], fill=(80, 75, 140, alpha))

# Mask gradient to rounded rect area
mask = Image.new('L', (SIZE, SIZE), 0)
mask_draw = ImageDraw.Draw(mask)
mask_draw.rounded_rectangle([(16, 16), (496, 496)], radius=radius, fill=255)
grad_img.putalpha(Image.composite(grad_img.split()[3], Image.new('L', (SIZE, SIZE), 0), mask))
img = Image.alpha_composite(img, grad_img)
draw = ImageDraw.Draw(img)

# Redraw border
draw.rounded_rectangle(
    [(16, 16), (496, 496)],
    radius=radius,
    fill=None,
    outline=BORDER,
    width=3
)

# --- Microphone capsule ---
mic_cx = 256  # center x
mic_y = 85
mic_w = 124
mic_h = 190
mic_r = 62

mic_left = mic_cx - mic_w // 2
mic_right = mic_cx + mic_w // 2

# Glow behind mic
glow_img = Image.new('RGBA', (SIZE, SIZE), (0, 0, 0, 0))
glow_draw = ImageDraw.Draw(glow_img)
glow_draw.rounded_rectangle(
    [(mic_left - 20, mic_y - 20), (mic_right + 20, mic_y + mic_h + 20)],
    radius=mic_r + 20,
    fill=(108, 99, 255, 35)
)
glow_img = glow_img.filter(ImageFilter.GaussianBlur(radius=25))
img = Image.alpha_composite(img, glow_img)
draw = ImageDraw.Draw(img)

# Mic body - gradient fill (lighter at top)
draw.rounded_rectangle(
    [(mic_left, mic_y), (mic_right, mic_y + mic_h)],
    radius=mic_r,
    fill=ACCENT
)

# Light reflection on top-left of mic capsule
for i in range(25):
    alpha = max(0, int(45 * (1.0 - i / 25.0)))
    x = mic_left + 18 + i
    draw.line([(x, mic_y + 40), (x, mic_y + mic_h - 50)],
              fill=(180, 175, 255, alpha))

# Mic grille lines (horizontal)
for y_offset in range(0, 120, 24):
    y = mic_y + 48 + y_offset
    if y < mic_y + mic_h - 25:
        half_w = int(math.sqrt(max(0, mic_r**2 - ((y - mic_y - mic_h/2)**2))) * 0.7)
        draw.line(
            [(mic_cx - half_w, y), (mic_cx + half_w, y)],
            fill=(30, 30, 46, 90),
            width=3
        )

# --- Microphone arc holder ---
arc_w = 200
arc_bbox = [(mic_cx - arc_w//2, 185), (mic_cx + arc_w//2, 345)]
draw.arc(arc_bbox, start=0, end=180, fill=ACCENT_LIGHT, width=11)

# --- Microphone stand ---
draw.line([(mic_cx, 335), (mic_cx, 380)], fill=ACCENT_LIGHT, width=11)
# Stand base
draw.line([(mic_cx - 42, 380), (mic_cx + 42, 380)], fill=ACCENT_LIGHT, width=9)

# --- Signal flow chevrons >>> (pipe/routing concept) ---
chevron_y = 435
chevron_h = 26

positions_colors = [
    (168, ACCENT_LIGHT, 240),
    (228, ACCENT, 210),
    (288, ACCENT_DIM, 170),
    (348, (60, 54, 160), 120),
]

for cx, color, alpha in positions_colors:
    r, g, b = color
    # Draw thicker chevron with rounded appearance
    pts = [
        (cx - 16, chevron_y - chevron_h),
        (cx + 16, chevron_y),
        (cx - 16, chevron_y + chevron_h)
    ]
    draw.line(pts, fill=(r, g, b, alpha), width=8, joint="curve")

# --- Save PNG ---
img.save('C:/Users/livet/Desktop/DirectLoopMic/host/Resources/icon.png', 'PNG')
print(f"Icon saved: {SIZE}x{SIZE} PNG")

# Also create a 256x256 version for smaller contexts
img_256 = img.resize((256, 256), Image.LANCZOS)
img_256.save('C:/Users/livet/Desktop/DirectLoopMic/host/Resources/icon_256.png', 'PNG')
print("Also saved 256x256 version")
