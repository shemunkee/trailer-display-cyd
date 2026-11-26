import runpy, os
g = runpy.run_path(os.path.join(os.path.dirname(os.path.abspath(__file__)), "gen_tanks.py"))
TXT, MUTED, CARD, LINE, BLUE, BG = (g[k] for k in ["TXT","MUTED","CARD","LINE","BLUE","BG"])
ORDER = ["Overview", "Battery", "Solar", "AC", "Tanks", "Alarms", "Settings"]
base = dict(g["ICONS"])
opts = [
 ("A", "Propane bottle with collar", '<path d="M8 8h8a3 3 0 0 1 3 3v7a3 3 0 0 1-3 3H8a3 3 0 0 1-3-3v-7a3 3 0 0 1 3-3z"></path><path d="M8.5 8V4.5h7V8"></path><path d="M10.5 4.5V6.5h3V4.5"></path><path d="M5 14.5h14"></path>'),
 ("B", "Bottle with level line", '<rect x="6" y="7" width="12" height="14" rx="4"></rect><path d="M10 7V5h4v2M12 5V3"></path><path d="M6 14h12"></path><path d="M8 17.5h8" opacity="0.6"></path>'),
 ("C", "Bottle with flame", '<rect x="5" y="7" width="14" height="14" rx="5"></rect><path d="M9.5 4h5M12 4v3"></path><path d="M12 10.5c1.6 1.4 2.3 2.6 2.3 3.8a2.3 2.3 0 0 1-4.6 0c0-.9.5-1.6 1-2.1.3.7.7 1 1.2 1.1-.3-1-.2-1.9.1-2.8z"></path>'),
]
def bar(icon):
    icons = dict(base); icons["Tanks"] = icon
    out = []
    for k in ORDER:
        on = k == "Tanks"; col = TXT if on else MUTED; bg = CARD if on else "transparent"
        top = f'<span style="position: absolute; left: 10px; right: 10px; top: 0; height: 2px; border-radius: 1px; background: {BLUE}"></span>' if on else ""
        out.append(f'<button aria-label="{k}" style="position: relative; flex: 1; height: 32px; border: 0; border-radius: 6px; padding: 0; background: {bg}; display: flex; align-items: center; justify-content: center">{top}<svg aria-hidden="true" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="{col}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">{icons[k]}</svg></button>')
    return f'<div style="width: 320px; height: 40px; box-sizing: border-box; padding: 4px 12px; display: flex; gap: 3px; border-top: 1px solid {LINE}; background: {BG}">{"".join(out)}</div>'
rows = ""
for letter, name, icon in opts:
    rows += f'''<div style="display: flex; align-items: center; gap: 20px">
<div style="width: 72px; height: 72px; border-radius: 12px; background: {CARD}; display: flex; align-items: center; justify-content: center; flex-shrink: 0"><svg role="img" aria-label="Option {letter} tank icon" width="44" height="44" viewBox="0 0 24 24" fill="none" stroke="{TXT}" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">{icon}</svg></div>
<div style="display: flex; flex-direction: column; gap: 6px">
<div style="font-size: 13px; font-weight: 700; color: {TXT}">Option {letter}: {name}</div>
{bar(icon)}
</div></div>'''
inner = f'''<div style="width: 460px; height: 360px; background: #000; color: {TXT}; font-family: Inter, system-ui, sans-serif; padding: 20px; box-sizing: border-box; display: flex; flex-direction: column; gap: 22px">
<div style="font-size: 12px; color: {MUTED}">Bottom bar at real size (320 wide), Tanks selected. Large icon shows the detail.</div>
{rows}
</div>'''
g["g"]["page"]("TankIcons", "Tank icon options", inner, full=True) if "g" in g else None
