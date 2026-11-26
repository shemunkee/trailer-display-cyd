import runpy, sys, os
g = runpy.run_path(os.path.join(os.path.dirname(os.path.abspath(__file__)), "gen2.py"))
ORDER = ["Overview", "Battery", "Solar", "AC", "Tanks", "Alarms", "Settings"]
ICONS = dict(g["ICONS"]); ICONS["Tanks"] = '<path d="M8 8h8a3 3 0 0 1 3 3v7a3 3 0 0 1-3 3H8a3 3 0 0 1-3-3v-7a3 3 0 0 1 3-3z"></path><path d="M8.5 8V4.5h7V8"></path><path d="M10.5 4.5V6.5h3V4.5"></path><path d="M5 14.5h14"></path>'
NAMES = dict(g["NAMES"]); NAMES["Tanks"] = "Tanks"
CARD, LINE, TXT, MUTED, GREEN, AMBER, BLUE, RED, BG = (g[k] for k in ["CARD","LINE","TXT","MUTED","GREEN","AMBER","BLUE","RED","BG"])
PAD, GAP, CP, F_LABEL, F_BODY = (g[k] for k in ["PAD","GAP","CP","F_LABEL","F_BODY"])
def tabbar(idx):
    btns = []
    for i, k in enumerate(ORDER):
        on = i == idx; col = TXT if on else MUTED; bg = CARD if on else "transparent"
        bar = f'<span style="position: absolute; left: 10px; right: 10px; top: 0; height: 2px; border-radius: 1px; background: {BLUE}"></span>' if on else ""
        dot = f'<span style="position: absolute; top: 5px; right: 9px; width: 7px; height: 7px; border-radius: 4px; background: {AMBER}"></span>' if k == "Alarms" else ""
        btns.append(f'<button aria-label="{NAMES[k]}" style="position: relative; flex: 1; height: 32px; border: 0; border-radius: 6px; padding: 0; background: {bg}; display: flex; align-items: center; justify-content: center">{bar}{dot}<svg aria-hidden="true" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="{col}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">{ICONS[k]}</svg></button>')
    return f'<div style="height: 40px; flex-shrink: 0; box-sizing: border-box; padding: 4px {PAD}px; display: flex; gap: 3px; border-top: 1px solid {LINE}">{"".join(btns)}</div>'
def tank(name, pct, rem, status, color):
    fill = round(104 * pct / 100)
    return f'''<div style="flex: 1; background: {CARD}; border-radius: 8px; padding: {CP}px; box-sizing: border-box; display: flex; flex-direction: column; gap: 6px">
<div style="font-size: {F_LABEL}px; font-weight: 600; letter-spacing: 0.06em; color: {MUTED}">{name}</div>
<div style="flex: 1; display: flex; gap: 10px; align-items: stretch">
<div style="width: 22px; border-radius: 9px; border: 2px solid {LINE}; box-sizing: border-box; display: flex; flex-direction: column; justify-content: flex-end; overflow: hidden"><div style="height: {fill}px; background: {color}"></div></div>
<div style="flex: 1; display: flex; flex-direction: column; justify-content: space-between">
<div style="font-size: 30px; font-weight: 700; line-height: 1; color: {color}">{pct}%</div>
<div style="display: flex; flex-direction: column; gap: 4px">
<div style="display: flex; justify-content: space-between; font-size: {F_BODY}px"><span style="color: {MUTED}">Left</span><span>{rem}</span></div>
<div style="display: flex; justify-content: space-between; font-size: {F_BODY}px"><span style="color: {MUTED}">Type</span><span>LPG</span></div>
<div style="display: flex; justify-content: space-between; font-size: {F_BODY}px"><span style="color: {MUTED}">Status</span><span style="color: {color}">{status}</span></div>
</div></div></div></div>'''
body = f'''<div style="flex: 1; display: flex; gap: {GAP}px">
{tank("DRIVER SIDE", 96, "19.2 lb", "OK", GREEN)}
{tank("PASSENGER SIDE", 18, "3.6 lb", "Low", AMBER)}
</div>'''
inner = f'''<div style="width: 320px; height: 240px; background: {BG}; color: {TXT}; font-family: Inter, system-ui, sans-serif; font-variant-numeric: tabular-nums; display: flex; flex-direction: column; overflow: hidden">
<div style="flex: 1; min-height: 0; padding: {PAD}px; box-sizing: border-box; display: flex; flex-direction: column; gap: {GAP}px">
{body}
</div>
{tabbar(4)}
</div>'''
g["page"]("Tanks", "Tanks", inner, full=True)
print("tanks ok")
