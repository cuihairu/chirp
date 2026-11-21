"""终端样式帧渲染 + ffmpeg 缝合 GIF（配置开关效果证据用）。

只放无状态的工具函数：把 (文本, 颜色) 行渲染成一张终端样式 PNG，再把 off/on
两帧缝成循环 GIF。取证内容的**真实**来自调用方——这里不负责编造文案。

用法：
  from switch_frames import render_frame, stitch_gif, BG, PROMPT, TEXT, ERROR
"""

import shutil
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
W, H = 900, 430
BG = "#1e1e28"
PROMPT = "#7ec699"
TEXT = "#e6e6e6"
ERROR = "#e57373"
# DejaVu 没有中日韩字形，渲出来是豆腐块——帧内文本一律用 ASCII。
COLUMNS = 104


def textwrap(text, width=COLUMNS):
    out = []
    for chunk in str(text).split("\n"):
        while len(chunk) > width:
            out.append(chunk[:width])
            chunk = chunk[width:]
        out.append(chunk)
    return out


def render_frame(lines, path, width=W, height=H):
    """lines: [(文本, 颜色), ...]；超长自动折行，超出画布的部分截断。"""
    img = Image.new("RGB", (width, height), BG)
    draw = ImageDraw.Draw(img)
    font = ImageFont.truetype(FONT, 15)
    x, y = 18, 16
    for text, color in lines:
        for row in textwrap(text):
            if y > height - 20:
                break
            draw.text((x, y), row, font=font, fill=color)
            y += 22
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path)
    return path


def stitch_gif(off_png, on_png, gif_path, framerate="0.6"):
    """把 off/on 两帧缝成循环 GIF；帧是交替播放的（0.6 帧/秒）。"""
    off_png, on_png, gif_path = map(Path, (off_png, on_png, gif_path))
    seq = off_png.parent / f".seq-{gif_path.stem}"
    shutil.rmtree(seq, ignore_errors=True)
    seq.mkdir()
    shutil.copyfile(off_png, seq / "01.png")
    shutil.copyfile(on_png, seq / "02.png")
    try:
        subprocess.run(
            [
                "ffmpeg", "-y", "-loglevel", "error", "-framerate", framerate,
                "-i", str(seq / "%02d.png"),
                "-filter_complex", "split[a][b];[a]palettegen[p];[b][p]paletteuse",
                "-loop", "0", str(gif_path),
            ],
            check=True,
        )
    finally:
        shutil.rmtree(seq, ignore_errors=True)
    return gif_path
