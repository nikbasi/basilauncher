"""Convert the renderer's .pgm screens to .png and build contact sheets."""
import sys
from pathlib import Path

from PIL import Image

out = Path(sys.argv[1])
names = (out / "screens.txt").read_text().split()
images = []
for name in names:
    img = Image.open(out / f"{name}.pgm").convert("L")
    img.save(out / f"{name}.png")
    images.append(img)
per = 4
for k in range(0, len(images), per):
    group = images[k : k + per]
    sheet = Image.new("L", (len(group) * 280, 490), 90)
    for i, img in enumerate(group):
        sheet.paste(img.resize((270, 480)), (i * 280 + 5, 5))
    sheet.save(out / f"sheet{k // per}.png")
print(f"{len(images)} screens -> {out}")
