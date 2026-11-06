#pragma once
#include <gio/gio.h>

GDBusNodeInfo *rcl_dbus_introspection_new (GError **error);
guint rcl_dbus_register (GDBusConnection *connection, GDBusNodeInfo *introspection,
                         const GDBusInterfaceVTable *vtable, gpointer user_data,
                         GError **error);
