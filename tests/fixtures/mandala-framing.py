"""Rebuild the synthetic framing clip. Requires Pillow and ffmpeg/libx264.

Only diagnostic geometric shapes; no performance footage or third-party art.
The first frame has four corner markers, an asymmetric house, trees and a sun.
"""
from pathlib import Path
from tempfile import TemporaryDirectory
import subprocess
from PIL import Image, ImageDraw

frames = []
for t in range(6):
    image = Image.new("RGB", (480, 300), "#294055")
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 196, 479, 299), fill="#8c794c")
    draw.ellipse((30 + t * 4, 26, 90 + t * 4, 86), fill="#e4c569")
    draw.polygon([(150, 143), (246, 63), (342, 143)], fill="#a64c40")
    draw.rectangle((167, 143, 325, 264), fill="#d8c5a1")
    draw.rectangle((235, 192, 269, 264), fill="#62453e")
    for x in (182, 283):
        draw.rectangle((x, 161, x + 26, 189), fill="#557d8b")
        draw.line((x + 13, 161, x + 13, 189), fill="#decf9a", width=3)
    draw.ellipse((246, 226, 251, 231), fill="#e4c569")
    for x, y, size in [(64, 213, 37), (409, 214, 47)]:
        draw.rectangle((x - 5, y - 3, x + 5, y + 47), fill="#694b39")
        draw.ellipse((x - size, y - size * 2, x + size, y), fill="#5e815d")
    draw.polygon([(235, 264), (269, 264), (307, 299), (204, 299)], fill="#bea484")
    for x, y, color in [(8, 8, "#dd6650"), (452, 8, "#71b898"),
                         (8, 272, "#709fc8"), (452, 272, "#e9cb68")]:
        draw.rectangle((x, y, x + 19, y + 19), fill=color)
    draw.text((176, 16), "FRAMING FIXTURE", fill="#e3dac9")
    frames.append(image)
with TemporaryDirectory() as directory:
    source = Path(directory) / "source.gif"
    frames[0].save(source, save_all=True, append_images=frames[1:],
                   duration=500, loop=0, optimize=False)
    subprocess.run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
                    "-i", str(source), "-an", "-c:v", "libx264", "-pix_fmt", "yuv420p",
                    "-movflags", "+faststart", str(Path(__file__).with_suffix(".mp4"))], check=True)
