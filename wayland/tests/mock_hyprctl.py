#!/usr/bin/env python3
"""Stateful hyprctl double, used only by test_scripts.py."""
import json
import os
from pathlib import Path
import re
import sys
import time

path = Path(os.environ["MOCK_STATE"])
state = json.loads(path.read_text())
args = sys.argv[1:]
state.setdefault("calls", []).append(args)
# Strip hyprctl flags (e.g. -j -a) like the real CLI does.
flags = {a for a in args if a.startswith("-") and not a.lstrip("-").isdigit()}
args = [a for a in args if a not in flags]
status = 0
reply = "ok"
if args[0] == "getoption":
    reply = json.dumps(state[args[1]])
elif args[0] == "clients":
    queries = state["queries"]
    item = queries.pop(0) if len(queries) > 1 else queries[0]
    if isinstance(item, dict) and "raw" in item:
        reply = item["raw"]
        status = item.get("status", 0)
        time.sleep(item.get("delay", 0))
    elif isinstance(item, list):
        # Like real Hyprland, unmapped clients are hidden unless -a is given.
        clients = item if "-a" in flags else [
            c for c in item
            if not isinstance(c, dict) or "mapped" not in c or c["mapped"]
        ]
        reply = json.dumps(clients)
    else:
        reply = json.dumps(item)
elif args[0] == "monitors":
    # Model special-workspace visibility on the focused monitor.
    item = state.get("monitors")
    if isinstance(item, dict) and "raw" in item:
        reply = item["raw"]
        status = item.get("status", 0)
        time.sleep(item.get("delay", 0))
    else:
        visible = state.get("special_visible", False)
        reply = json.dumps(item if item is not None else [{
            "id": 0, "focused": True,
            "activeWorkspace": {"id": 1, "name": "1"},
            "specialWorkspace": {"id": -5 if visible else 0,
                                 "name": "special:term" if visible else ""},
        }])
else:
    responses = state.get("responses", [])
    item = responses.pop(0) if responses else {}
    reply = item.get("reply", "ok")
    status = item.get("status", 0)
    # Lua config manager: gap writes arrive as eval hl.config(...), and
    # reads report the four-edge value in the "css" field.
    if args[0] == "eval" and "hl.config" in args[1] and reply == "ok" and status == 0:
        if not state.get("ignore_mutation"):
            match = re.search(r"gaps_(in|out)=(\d+)", args[1])
            if match:
                state[f"general:gaps_{match.group(1)}"] = {
                    "css": " ".join([match.group(2)] * 4)}
    # Model dispatch effects needed by the scratchpad visibility logic.
    if (args[0] == "dispatch" and "toggle_special" in args[1]
            and reply == "ok" and status == 0):
        state["special_visible"] = not state.get("special_visible", False)
path.write_text(json.dumps(state))
print(reply)
sys.exit(status)
