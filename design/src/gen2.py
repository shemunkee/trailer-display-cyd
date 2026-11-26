import json, os
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "boards")

BG = "#000000"; CARD = "#111318"; LINE = "#23262f"; TXT = "#F2F2F5"; MUTED = "#9AA0B4"
PINK = "#F0457E"; BLUE = "#7C88F7"; GREEN = "#3FCF8E"; AMBER = "#F5A524"; RED = "#FF5A5F"; ORANGE = "#FF8A3D"

# One type scale and one spacing rule for every screen
PAD = 12      # outer padding on every screen
GAP = 8       # gap between blocks
CP = 8        # padding inside cards
F_LABEL = 10  # small caps labels
F_BODY = 12   # row text
F_VALUE = 18  # tile values
ORDER = ["Overview", "Battery", "Solar", "AC", "Tanks", "Alarms", "Settings"]

ICONS = {
 "Overview": '<path d="M3 11l9-7 9 7v9a1 1 0 0 1-1 1h-5v-6h-6v6H4a1 1 0 0 1-1-1z"></path>',
 "Battery": '<rect x="2" y="7" width="17" height="10" rx="2"></rect><path d="M22 11v2M6 10v4M10 10v4"></path>',
 "Solar": '<circle cx="12" cy="12" r="4"></circle><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"></path>',
 "AC": '<path d="M9 2v6M15 2v6M6 8h12v3a6 6 0 0 1-12 0zM12 17v5"></path>',
 "Tanks": '<path d="M8 8h8a3 3 0 0 1 3 3v7a3 3 0 0 1-3 3H8a3 3 0 0 1-3-3v-7a3 3 0 0 1 3-3z"></path><path d="M8.5 8V4.5h7V8"></path><path d="M10.5 4.5V6.5h3V4.5"></path><path d="M5 14.5h14"></path>',
 "Alarms": '<path d="M6 8a6 6 0 0 1 12 0c0 7 3 9 3 9H3s3-2 3-9M10.3 21a1.94 1.94 0 0 0 3.4 0"></path>',
 "Settings": '<path d="M4 21v-7M4 10V3M12 21v-9M12 8V3M20 21v-5M20 12V3M1 14h6M9 8h6M17 16h6"></path>',
}
NAMES = {"Tanks": "Tanks", "Overview": "Overview", "Battery": "Battery", "Solar": "Solar", "AC": "AC and Quattro", "Alarms": "Alarms", "Settings": "Settings"}

def tabbar(idx):
    btns = []
    for i, k in enumerate(ORDER):
        on = i == idx
        col = TXT if on else MUTED
        bg = CARD if on else "transparent"
        bar = f'<span style="position: absolute; left: 10px; right: 10px; top: 0; height: 2px; border-radius: 1px; background: {BLUE}"></span>' if on else ""
        dot = f'<span style="position: absolute; top: 5px; right: 9px; width: 7px; height: 7px; border-radius: 4px; background: {RED}"></span>' if k == "Alarms" else ""
        btns.append(f'<button aria-label="{NAMES[k]}" style="position: relative; flex: 1; height: 32px; border: 0; border-radius: 6px; padding: 0; background: {bg}; display: flex; align-items: center; justify-content: center">{bar}<svg aria-hidden="true" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="{col}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">{ICONS[k]}</svg>{dot}</button>')
    return f'<div style="height: 40px; flex-shrink: 0; box-sizing: border-box; padding: 4px {PAD}px; display: flex; gap: 3px; border-top: 1px solid {LINE}">{"".join(btns)}</div>'

def page(name, title, body, idx=None, full=False):
    inner = body if full else f'''<div style="width: 320px; height: 240px; background: {BG}; color: {TXT}; font-family: Inter, system-ui, sans-serif; font-variant-numeric: tabular-nums; display: flex; flex-direction: column; overflow: hidden">
<div style="flex: 1; min-height: 0; padding: {PAD}px; box-sizing: border-box; display: flex; flex-direction: column; gap: {GAP}px">
{body}
</div>
{tabbar(idx)}
</div>'''
    html = f'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>{title} screen</title>
<script src="./support.js"></script>
</head>
<body>
<x-dc>
<helmet>
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;600;700&amp;display=swap" rel="stylesheet">
<style>
body{{margin:0}}
</style>
</helmet>
{inner}
</x-dc>
<script type="text/x-dc" data-dc-script data-props='{{"$preview":{{"width":320,"height":240}}}}'>
class Component extends DCLogic {{
renderVals() {{
return {{}};
}}
}}
</script>
</body>
</html>
'''
    with open(os.path.join(ROOT, name + ".dc.html"), "w") as f:
        f.write(html)

def label(t, color=MUTED):
    return f'<div style="font-size: {F_LABEL}px; font-weight: 600; letter-spacing: 0.06em; color: {color}">{t}</div>'

def card(content, extra=""):
    return f'<div style="background: {CARD}; border-radius: 8px; padding: {CP}px; box-sizing: border-box; display: flex; flex-direction: column; gap: 2px; {extra}">{content}</div>'

def tile(lbl, value, sub, color=TXT, vsize=F_VALUE):
    return card(f'''{label(lbl)}
<div style="font-size: {vsize}px; font-weight: 700; line-height: 1.2; color: {color}; white-space: nowrap">{value}</div>
<div style="font-size: {F_LABEL}px; color: {MUTED}; white-space: nowrap">{sub}</div>''', "flex: 1; justify-content: center")

def row(lbl, value, color=TXT, last=False):
    border = "" if last else f"border-bottom: 1px solid {LINE};"
    return f'''<div style="display: flex; justify-content: space-between; align-items: center; flex: 1; {border}">
<div style="font-size: {F_BODY}px; color: {MUTED}">{lbl}</div>
<div style="font-size: {F_BODY + 2}px; font-weight: 600; color: {color}">{value}</div>
</div>'''

# Overview
def ring(soc, watts, volts, color):
    return f'''<div style="width: 88px; height: 88px; border-radius: 44px; border: 6px solid {color}; box-sizing: border-box; display: flex; flex-direction: column; align-items: center; justify-content: center">
<div style="font-size: 24px; font-weight: 700; line-height: 1">{soc}</div>
<div style="font-size: {F_LABEL}px; font-weight: 600; color: {color}; margin-top: 2px">{watts}</div>
<div style="font-size: {F_LABEL}px; color: {MUTED}">{volts}</div>
</div>'''

def tank_bars(name, level, low=20):
    n = 0 if level <= 0 else min(5, -(-level // 20))      # 1 bar = 1 to 20%, 5 bars = 81 to 100%
    color = AMBER if level <= low else GREEN
    segs = "".join(f'<span style="width: 11px; height: 9px; border-radius: 2px; background: {color if i < n else LINE}"></span>' for i in range(5))
    return f'''<div style="display: flex; align-items: center; gap: 6px">
<span style="font-size: 11px; font-weight: 600; color: {TXT}">{name}</span>
<span style="display: flex; gap: 2px">{segs}</span></div>'''

def tank_strip(tanks):
    icon = '<svg aria-hidden="true" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="#9AA0B4" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M8 8h8a3 3 0 0 1 3 3v7a3 3 0 0 1-3 3H8a3 3 0 0 1-3-3v-7a3 3 0 0 1 3-3z"></path><path d="M8.5 8V4.5h7V8"></path><path d="M10.5 4.5V6.5h3V4.5"></path><path d="M5 14.5h14"></path></svg>'
    items = "".join(tank_bars(n, l) for n, l in tanks)
    return f'''<div style="height: 26px; flex-shrink: 0; background: {CARD}; border-radius: 8px; padding: 0 10px; display: flex; align-items: center; gap: 14px">{icon}{items}</div>'''

def overview(src, solar, rng, loads, quattro, tanks=None):
    col = f"width: 96px; flex-shrink: 0; display: flex; flex-direction: column; gap: {GAP}px; align-self: stretch"
    top = f'''<div style="flex: 1; min-height: 0; display: flex; align-items: center; gap: {GAP}px">
<div style="{col}">{src}{solar}</div>
<div style="flex: 1; display: flex; justify-content: center">{rng}</div>
<div style="{col}">{loads}{quattro}</div>
</div>'''
    return top + (tank_strip(tanks) if tanks else "")

page("Main", "Overview", overview(
    tile("SHORE", "0 W", "Off · last used", MUTED),
    tile("SOLAR", "640 W", "2.4 kWh today", PINK),
    ring("86%", "+412 W", "53.2 V", GREEN),
    tile("AC LOADS", "228 W", "1.9 A", BLUE),
    tile("QUATTRO", "Inverting", "30 A limit", TXT, 14), [("Driver", 96), ("Passenger", 18)]), 0)
page("MainGen", "Overview", overview(
    tile("GENERATOR", "2.1 kW", "Running", ORANGE),
    tile("SOLAR", "180 W", "2.4 kWh today", PINK),
    ring("86%", "+1.6 kW", "54.1 V", GREEN),
    tile("AC LOADS", "640 W", "5.3 A", BLUE),
    tile("QUATTRO", "Charging", "Passthru", TXT, 14), [("Driver", 96), ("Passenger", 18)]), 0)

# Battery
battery = f'''<div style="flex: 1; display: flex; gap: {GAP}px">
{card(f"""{label("SOC")}
<div style="font-size: 36px; font-weight: 700; line-height: 1.1">86%</div>
<div style="height: 8px; border-radius: 4px; background: {LINE}; overflow: hidden; margin: 6px 0"><div style="width: 86%; height: 8px; background: {GREEN}"></div></div>
<div style="font-size: {F_BODY}px; font-weight: 600; color: {GREEN}">Charging</div>""", "width: 100px; justify-content: center")}
<div style="flex: 1; display: flex; flex-direction: column">
{row("Voltage", "53.2 V")}
{row("Current", "+7.8 A", GREEN)}
{row("Power", "+412 W", GREEN)}
{row("Time to go", "Charging", MUTED)}
{row("Consumed", "−32 Ah", TXT, True)}
</div>
</div>'''
page("Battery", "Battery", battery, 1)

# Solar
def kv(k, v):
    return f'<div style="display: flex; justify-content: space-between; font-size: {F_BODY}px"><span style="color: {MUTED}">{k}</span><span>{v}</span></div>'
def mppt(name, w, v, state, y):
    return card(f'''{label(name.upper())}
<div style="font-size: 22px; font-weight: 700; color: {PINK}; margin-bottom: 4px">{w}</div>
{kv("PV", v)}{kv("State", state)}{kv("Today", y)}''', "flex: 1; gap: 4px")
solar = f'''<div style="display: flex; justify-content: space-between; align-items: baseline">
<div style="font-size: {F_BODY}px; color: {MUTED}">Total solar</div>
<div style="font-size: 20px; font-weight: 700; color: {PINK}">640 W</div>
</div>
<div style="flex: 1; display: flex; gap: {GAP}px">
{mppt("Driver", "352 W", "71.4 V", "Bulk", "1.3 kWh")}
{mppt("Passenger", "288 W", "69.8 V", "Bulk", "1.1 kWh")}
</div>'''
page("Solar", "Solar", solar, 2)

# AC
def ac_card(lbl, big, color, rows):
    return card(f'''{label(lbl)}
<div style="font-size: {F_VALUE}px; font-weight: 700; color: {color}; margin-bottom: 2px">{big}</div>
<div style="flex: 1; display: flex; flex-direction: column">{rows}</div>''', "flex: 1")
ac = f'''<div style="flex: 1; display: flex; gap: {GAP}px">
{ac_card("SHORE IN", "Off", MUTED, row("Voltage", "0 V", MUTED) + row("Power", "0 W", MUTED) + row("Limit", "30 A", TXT, True))}
{ac_card("AC OUT", "228 W", BLUE, row("Voltage", "120 V") + row("Current", "1.9 A") + row("Freq", "60 Hz", TXT, True))}
</div>
{card(f"""<div style="display: flex; justify-content: space-between; align-items: center">
<div style="font-size: {F_BODY}px; color: {MUTED}">Quattro state</div>
<div style="font-size: {F_BODY + 2}px; font-weight: 700">Inverting</div>
</div>""")}'''
page("AC", "AC / Quattro", ac, 3)

# Alarms
def alarm(dev, msg, t, color, level):
    return card(f'''<div style="display: flex; gap: {GAP}px">
<div style="width: 4px; border-radius: 2px; background: {color}"></div>
<div style="flex: 1; display: flex; flex-direction: column; gap: 2px">
{label(level, color)}
<div style="font-size: 14px; font-weight: 600">{msg}</div>
<div style="font-size: {F_LABEL}px; color: {MUTED}">{dev} · since {t}</div>
</div>
</div>''')
alarms = f'''{alarm("Quattro", "Overload", "2:32 PM", RED, "ALARM")}
{alarm("SmartShunt", "Low state of charge", "1:05 PM", AMBER, "WARNING")}
<div style="flex: 1; display: flex; align-items: flex-end; justify-content: center">
<div style="font-size: {F_LABEL}px; color: {MUTED}">Read only. Clear alarms on the Cerbo.</div>
</div>'''
page("Alarms", "Alarms", alarms, 5)


# Settings
def btn(l, sym):
    return f'<button aria-label="{l}" style="width: 40px; height: 30px; border-radius: 6px; border: 1px solid {LINE}; background: #1a1d24; color: {TXT}; font-size: 18px; font-weight: 700; padding: 0">{sym}</button>'
def srow(lbl, right):
    return card(f'''<div style="display: flex; align-items: center; justify-content: space-between; height: 30px">
<div style="font-size: 13px; font-weight: 600">{lbl}</div>
<div style="display: flex; align-items: center; gap: 4px">{right}</div>
</div>''', "padding: 4px 4px 4px 10px; flex: 1; justify-content: center")
def stepper(lbl, value):
    return srow(lbl, f'{btn("Decrease " + lbl, "−")}<div style="width: 40px; text-align: center; font-size: 14px; font-weight: 700">{value}</div>{btn("Increase " + lbl, "+")}')
segs = "".join(
    f'<button style="width: 40px; height: 30px; border: 0; border-radius: 6px; padding: 0; font-family: inherit; font-size: {F_BODY}px; font-weight: 700; background: {BLUE if s == "30s" else "#1a1d24"}; color: {"#000" if s == "30s" else TXT}">{s}</button>'
    for s in ["5s", "10s", "30s", "60s", "90s"])
net = f'''<div style="display: flex; flex-direction: column; align-items: flex-end; padding-right: 4px"><div style="font-size: {F_BODY}px; font-weight: 600; color: {GREEN}">Local</div><div style="font-size: {F_LABEL}px; color: {MUTED}">Cloud if no hotspot</div></div><svg aria-hidden="true" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="{MUTED}" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M9 6l6 6-6 6"></path></svg>'''
settings = f'''{stepper("Low SOC", "30%")}
{srow("Snooze", segs)}
{stepper("Brightness", "70%")}
{srow("Data source", net)}'''
page("Settings", "Settings", settings, 6)
print("ok")

# Setup screen with QR codes (sample pattern; firmware draws the real codes)
import random
def fake_qr(seed, label):
    r = random.Random(seed); n = 21; m = 4
    cells = []
    def finder(x, y):
        out = [f'<rect x="{x*m}" y="{y*m}" width="{7*m}" height="{7*m}" fill="#000"></rect>',
               f'<rect x="{(x+1)*m}" y="{(y+1)*m}" width="{5*m}" height="{5*m}" fill="#fff"></rect>',
               f'<rect x="{(x+2)*m}" y="{(y+2)*m}" width="{3*m}" height="{3*m}" fill="#000"></rect>']
        return "".join(out)
    def in_finder(i, j):
        return (i < 8 and j < 8) or (i < 8 and j > n - 9) or (i > n - 9 and j < 8)
    for i in range(n):
        for j in range(n):
            if not in_finder(i, j) and r.random() < 0.5:
                cells.append(f'<rect x="{j*m}" y="{i*m}" width="{m}" height="{m}" fill="#000"></rect>')
    body = "".join(cells) + finder(0, 0) + finder(n - 7, 0) + finder(0, n - 7)
    size = n * m
    return f'<div style="background: #ffffff; padding: 6px; border-radius: 4px; line-height: 0"><svg role="img" aria-label="{label}" width="{size}" height="{size}" viewBox="0 0 {size} {size}">{body}</svg></div>'

def step(num, title, qr, line1, line2):
    return card(f'''{label(f"{num}  {title}")}
<div style="display: flex; justify-content: center; margin: 4px 0">{qr}</div>
<div style="font-size: {F_BODY}px; font-weight: 600; text-align: center">{line1}</div>
<div style="font-size: {F_LABEL}px; color: {MUTED}; text-align: center">{line2}</div>''', "flex: 1")

setup = f'''<div style="width: 320px; height: 240px; background: {BG}; color: {TXT}; font-family: Inter, system-ui, sans-serif; padding: {PAD}px; box-sizing: border-box; display: flex; flex-direction: column; gap: {GAP}px">
<div style="display: flex; justify-content: space-between; align-items: baseline">
<div style="font-size: 16px; font-weight: 700">Setup: Trailer</div>
<div style="font-size: {F_LABEL}px; color: {MUTED}">Scan with your phone</div>
</div>
<div style="flex: 1; display: flex; gap: {GAP}px">
{step(1, "JOIN WIFI", fake_qr(7, "QR code to join the CYD setup network"), "Victron_CYD_Trailer", "Password k7m2x9qp")}
{step(2, "OPEN SETUP", fake_qr(11, "QR code to open the setup page"), "192.168.4.1", "Opens on its own on most phones")}
</div>
<div style="font-size: {F_LABEL}px; color: {MUTED}; text-align: center">Reopen anytime: Settings, Data source, Run setup</div>
</div>'''
page("Setup", "Setup", setup, full=True)
print("setup ok")
