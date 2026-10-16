#!/usr/bin/env python3
"""Passively record Riot/League X11 window lifetimes, geometry and child stacking (requires python-xlib)."""
import argparse
from datetime import datetime
import json
from pathlib import Path
import select
import time
from Xlib import X, display, error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--duration', type=float, default=3600)
    args = parser.parse_args()
    connection = display.Display()
    root = connection.screen().root
    watched, known = set(), {}
    connection.set_error_handler(lambda exc, request: None)  # Windows may disappear between queries.
    atoms = {name: connection.intern_atom(name) for name in (
        '_NET_WM_PID', '_NET_WM_NAME', '_NET_WM_STATE', '_NET_WM_WINDOW_TYPE')}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('a', buffering=1) as output:
        def emit(record):
            output.write(json.dumps({'time': datetime.now().astimezone().isoformat(), **record}) + '\n')
        def property_value(window, name):
            value = window.get_full_property(atoms[name], X.AnyPropertyType)
            if value is None:
                return None
            if name == '_NET_WM_NAME':
                return bytes(value.value).decode(errors='replace')
            return list(map(int, value.value))
        def scan(window, event, inherited=False, depth=0):
            if depth > 64:
                return
            try:
                if window.id not in watched:
                    window.change_attributes(event_mask=X.SubstructureNotifyMask | X.StructureNotifyMask | X.PropertyChangeMask)
                    watched.add(window.id)
                tree = window.query_tree()
                cls = window.get_wm_class()
                relevant = inherited or any(word in str(cls).lower() for word in ('riot', 'league'))
                if relevant:
                    attrs, geom = window.get_attributes(), window.get_geometry()
                    point = root.translate_coords(window, 0, 0)
                    state = {
                        'window': hex(window.id), 'parent': hex(tree.parent.id),
                        'class': cls, 'title': property_value(window, '_NET_WM_NAME') or window.get_wm_name(),
                        'pid': property_value(window, '_NET_WM_PID'),
                        'size': [geom.width, geom.height], 'root_position': [point.x, point.y],
                        'mapped': attrs.map_state, 'override_redirect': bool(attrs.override_redirect),
                        'state': [connection.get_atom_name(n) for n in property_value(window, '_NET_WM_STATE') or []],
                        'type': [connection.get_atom_name(n) for n in property_value(window, '_NET_WM_WINDOW_TYPE') or []],
                        'children_bottom_to_top': [hex(child.id) for child in tree.children],
                    }
                    if known.get(window.id) != state or event in ('MapNotify', 'UnmapNotify'):
                        emit({'event': event, **state})
                    known[window.id] = state
                for child in tree.children:
                    scan(child, event, relevant, depth + 1)
            except (error.XError, OSError, AttributeError, ValueError):
                pass
        emit({'event': 'trace-start', 'display': connection.get_display_name()})
        scan(root, 'initial')
        connection.flush()
        deadline = time.monotonic() + args.duration
        while time.monotonic() < deadline:
            if not connection.pending_events():
                select.select([connection.fileno()], [], [], min(1, max(0, deadline - time.monotonic())))
            while connection.pending_events():
                event = connection.next_event()
                window = getattr(event, 'window', None)
                if window is None:
                    continue
                if event.type == X.DestroyNotify:
                    if window.id in known:
                        emit({'event': 'DestroyNotify', **known.pop(window.id)})
                    watched.discard(window.id)
                else:
                    # Recover the owning top-level so new descendants inherit the Riot/League filter.
                    ancestor = window
                    try:
                        for _ in range(64):
                            parent = ancestor.query_tree().parent
                            if not hasattr(parent, 'id') or parent.id == root.id or parent.id == 0:
                                break
                            ancestor = parent
                        scan(ancestor, type(event).__name__)
                    except error.XError:
                        pass
            connection.flush()
        emit({'event': 'trace-end'})
    connection.close()


if __name__ == '__main__':
    main()
