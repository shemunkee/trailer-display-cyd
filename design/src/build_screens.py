"""Builds ../screens.html: every CYD screen mockup on one standalone page.

Run: python build_screens.py
Edit a screen by changing the generators (gen2.py, gen_tanks.py, gen_toolbar.py),
running them to rewrite boards/*.dc.html, then running this script.
"""
import base64, json, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
BOARDS = os.path.join(HERE, "boards")
OUT = os.path.join(HERE, "..", "screens.html")

# Order and notes shown on the page
ORDER = [
    ("Main.dc.html", "Overview", "Default screen after boot."),
    ("MainGen.dc.html", "Overview, generator running", "Shore or generator tile shows whichever source is live, defaulting to the last one used."),
    ("Battery.dc.html", "Battery", ""),
    ("Solar.dc.html", "Solar", "One row per MPPT, named from CustomName (Driver, Passenger)."),
    ("AC.dc.html", "AC and Quattro", ""),
    ("Tanks.dc.html", "Tanks", "Percent and pounds per Mopeka tank. Amber at or below 20%."),
    ("Alarms.dc.html", "Alarms", ""),
    ("Settings.dc.html", "Settings", ""),
    ("Snooze.dc.html", "Snooze logo", "Shown for 3 s before the screen sleeps."),
    ("Setup.dc.html", "Setup", "First boot, or Settings > Run setup."),
    ("TankIcons.dc.html", "Tank icon options", "Decided 2026-11-24: option A."),
]

def inner(path):
    html = open(path, encoding="utf-8").read()
    m = re.search(r"</helmet>(.*)</x-dc>", html, re.S)
    body = m.group(1).strip()
    def embed(mm):
        f = os.path.join(BOARDS, mm.group(1))
        data = base64.b64encode(open(f, "rb").read()).decode()
        return f'src="data:image/png;base64,{data}"'
    return re.sub(r'src="([^"/:]+\.png)"', embed, body)

canvas = json.load(open(os.path.join(BOARDS, "canvas.json")))["boards"]
cards = []
for fname, title, note in ORDER:
    b = canvas[fname]
    n = f'<p class="note">{note}</p>' if note else ""
    cards.append(f'''<figure>
<figcaption>{title}</figcaption>
<div class="screen" style="width:{b["w"]}px;height:{b["h"]}px">{inner(os.path.join(BOARDS, fname))}</div>
{n}
<p class="src">src/boards/{fname}</p>
</figure>''')

page = f'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>CYD Victron display screens</title>
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;600;700&display=swap" rel="stylesheet">
<style>
body{{margin:0;padding:24px;background:#1a1c22;color:#e6e6ea;font-family:Inter,system-ui,sans-serif}}
h1{{font-size:20px;margin:0 0 4px}}
.lead{{color:#9AA0B4;font-size:13px;margin:0 0 20px;max-width:720px}}
.grid{{display:flex;flex-wrap:wrap;gap:28px}}
figure{{margin:0}}
figcaption{{font-size:13px;font-weight:600;margin-bottom:6px}}
.screen{{overflow:hidden;border-radius:4px;box-shadow:0 0 0 1px #33363f;zoom:var(--z,1)}}
.note{{font-size:12px;color:#9AA0B4;margin:6px 0 0;max-width:320px}}
.src{{font-size:11px;color:#5d6273;margin:2px 0 0}}
label{{font-size:12px;color:#9AA0B4}}
</style>
</head>
<body>
<h1>CYD Victron display screens</h1>
<p class="lead">Mockups at the real 320 x 240 size of the ESP32-2432S028R. SPEC.md is the source of truth for behavior; these show layout.
<label><input type="checkbox" onchange="document.body.style.setProperty('--z', this.checked ? 2 : 1)"> Show at 2x</label></p>
<div class="grid">
{"".join(cards)}
</div>
</body>
</html>
'''
open(OUT, "w", encoding="utf-8").write(page)
print("wrote", os.path.normpath(OUT))
