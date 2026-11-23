"""Networked PIE probe: prints every soldier as each world sees it (dedicated server + each client).

Run during a Play As Client PIE session from the editor console:
    py "C:/Users/toony/Projects/Unreal/Scripts/Tests/net_probe.py"
Output goes to the Output Log (LogPython) and to Saved/Logs/net_probe.txt.
Pass a squad number to only list that soldier of each team:  py ".../net_probe.py" 1
"""
import sys
import unreal

only = int(sys.argv[1]) if len(sys.argv) > 1 else 0
lines = []
for w in unreal.EditorLevelLibrary.get_pie_worlds(True):
    if unreal.SystemLibrary.is_dedicated_server(w):
        tag = "SERVER"
    else:
        pc = unreal.GameplayStatics.get_player_controller(w, 0)
        tag = "CLIENT %s" % (str(pc.get_editor_property("player_team")).split(".")[-1].split(":")[0] if pc else "?")
    lines.append("%s t=%.1f" % (tag, unreal.GameplayStatics.get_time_seconds(w)))
    for x in sorted(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Soldier), key=lambda s: (str(s.get_editor_property("team")), s.get_squad_number())):
        if only and x.get_squad_number() != only:
            continue
        c = x.get_current_cell()
        l = x.get_actor_location()
        lines.append("   %-16s %s%d cell(%d,%d) loc(%d,%d) moving=%d dead=%d injured=%d mag=%d stress=%.0f" % (
            x.get_name(), str(x.get_editor_property("team")).split(".")[-1][:2], x.get_squad_number(), c.x, c.y, l.x, l.y,
            x.is_moving(), x.is_dead(), x.is_injured(), x.get_magazine_ammo(), x.get_stress()))
text = "\n".join(lines)
unreal.log(text)
with open(unreal.Paths.project_saved_dir() + "Logs/net_probe.txt", "w") as f:
    f.write(text + "\n")
