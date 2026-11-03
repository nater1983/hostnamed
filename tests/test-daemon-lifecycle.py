#!/usr/bin/env python3
"""Guard D-Bus name ownership and reconnect lifecycle decisions."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


start = source.index("rcl_main_name_lost(GDBusConnection")
end = source.index("\n}\n", start) + 3
handler = source[start:end]

require("rcl_daemon_shutdown(state->daemon);" in handler,
        "name loss must unregister the old D-Bus object")
require("if (state->exiting)" in handler,
        "shutdown must not schedule a reconnect")
require("if (!state->has_name)" in handler,
        "initial name acquisition failure must remain fatal")
require("connection != NULL && !g_dbus_connection_is_closed(connection)" in handler,
        "a live bus connection must identify an ownership takeover")
require("g_main_loop_quit(state->loop);" in handler,
        "an intentional takeover must stop the old daemon")
require("state->reconnect_id = g_timeout_add(RECONNECT_DELAY_MS" in handler,
        "a disconnected bus must schedule a delayed reconnect")
require("state->replace" not in handler,
        "--replace must not determine recovery after a bus disconnect")

reconnect_start = source.index("rcl_main_reconnect_cb(gpointer")
reconnect_end = source.index("\n}\n", reconnect_start) + 3
reconnect = source[reconnect_start:reconnect_end]
require("g_bus_unown_name(state->watch_id);" in reconnect,
        "reconnect must release the stale name watch")
require("rcl_acquire_name(state);" in reconnect,
        "reconnect must create a fresh ownership attempt")

print("daemon lifecycle invariants: PASS")
