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


# Startup must release any previous or partially initialized bus state before
# taking ownership of a new connection, including after a bus restart.
daemon_source = (Path(sys.argv[1]).parent / "rcl-hostname.c").read_text(encoding="utf-8")
startup_start = daemon_source.index("rcl_daemon_startup( RclDaemon *daemon")
startup_end = daemon_source.index("\n}\n", startup_start) + 3
startup = daemon_source[startup_start:startup_end]
require(startup.index("rcl_daemon_shutdown (daemon);") <
        startup.index("priv->connection    = g_object_ref( connection );"),
        "startup must clear prior bus resources before acquiring a connection")

shutdown_start = daemon_source.index("rcl_daemon_shutdown( RclDaemon *daemon")
shutdown_end = daemon_source.index("\n}\n", shutdown_start) + 3
shutdown = daemon_source[shutdown_start:shutdown_end]
for fragment in ("g_dbus_connection_unregister_object", "priv->registration_id = 0",
                 "g_clear_object( &priv->connection )",
                 "g_clear_object( &priv->authority )",
                 "priv->introspection = NULL"):
    require(fragment in shutdown, f"shutdown must release resource: {fragment}")

finalizer_start = daemon_source.index("rcl_hostname_daemon_finalize( GObject *object")
finalizer_end = daemon_source.index("\n}\n", finalizer_start) + 3
finalizer = daemon_source[finalizer_start:finalizer_end]
require("rcl_daemon_shutdown (RCL_DAEMON (object));" in finalizer,
        "finalizer must release partially initialized bus resources")
require("g_clear_object( &priv->authority )" not in finalizer,
        "finalizer must not duplicate shutdown-owned Polkit cleanup")

print("daemon lifecycle invariants: PASS")
