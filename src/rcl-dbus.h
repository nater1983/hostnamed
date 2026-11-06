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

GVariant *rcl_dbus_handle_get_property (GDBusConnection *connection,
  const gchar *sender, const gchar *object_path, const gchar *interface_name,
  const gchar *property_name, GError **error, gpointer user_data);
void rcl_dbus_handle_method_call (GDBusConnection *connection,
  const gchar *sender, const gchar *object_path, const gchar *interface_name,
  const gchar *method_name, GVariant *parameters,
  GDBusMethodInvocation *invocation, gpointer user_data);
