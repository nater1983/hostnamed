#pragma once
#include <gio/gio.h>

GDBusNodeInfo *rcl_dbus_introspection_new (GError **error);
guint rcl_dbus_register (GDBusConnection *connection, GDBusNodeInfo *introspection,
                         const GDBusInterfaceVTable *vtable, gpointer user_data,
                         GError **error);

/* Reject writes to the read-only hostname1 properties. */
gboolean rcl_dbus_handle_set_property (GDBusConnection *connection,
  const gchar *sender, const gchar *object_path, const gchar *interface_name,
  const gchar *property_name, GVariant *value, GError **error, gpointer user_data);
