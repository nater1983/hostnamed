#include "rcl-dbus.h"
#include "rcl-hostname.h"

static const gchar hostname1_introspection_xml[] =
  "<node>"
  "  <interface name='org.freedesktop.hostname1'>"
  /* ---- read-only properties ---- */
  "    <property name='Hostname'                    type='s' access='read'/>"
  "    <property name='StaticHostname'              type='s' access='read'/>"
  "    <property name='PrettyHostname'              type='s' access='read'/>"
  "    <property name='DefaultHostname'             type='s' access='read'><annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const'/></property>"
  "    <property name='HostnameSource'              type='s' access='read'/>"
  "    <property name='IconName'                    type='s' access='read'/>"
  "    <property name='Chassis'                     type='s' access='read'/>"
  "    <property name='Deployment'                  type='s' access='read'/>"
  "    <property name='Location'                    type='s' access='read'/>"
  "    <property name='Tags'                         type='as' access='read'/>"
  "    <property name='KernelName'                  type='s' access='read'><annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const'/></property>"
  "    <property name='KernelRelease'               type='s' access='read'><annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const'/></property>"
  "    <property name='KernelVersion'               type='s' access='read'><annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const'/></property>"
  "    <property name='OperatingSystemPrettyName'   type='s' access='read'/>"
  "    <property name='OperatingSystemFancyName'    type='s' access='read'/>"
  "    <property name='OperatingSystemCPEName'      type='s' access='read'/>"
  "    <property name='OperatingSystemSupportEnd'   type='t' access='read'/>"
  "    <property name='HomeURL'                     type='s' access='read'/>"
  "    <property name='OperatingSystemImageID'      type='s' access='read'/>"
  "    <property name='OperatingSystemImageVersion' type='s' access='read'/>"
  "    <property name='HardwareVendor'              type='s' access='read'><annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const'/></property>"
  "    <property name='HardwareModel'               type='s' access='read'><annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const'/></property>"
  "    <property name='HardwareSKU'                 type='s' access='read'><annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const'/></property>"
  "    <property name='HardwareVersion'             type='s' access='read'><annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const'/></property>"
  "    <property name='FirmwareVersion'             type='s' access='read'><annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const'/></property>"
  "    <property name='FirmwareVendor'              type='s' access='read'><annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const'/></property>"
  "    <property name='FirmwareDate'                type='t' access='read'><annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const'/></property>"
  "    <property name='MachineID'                   type='ay' access='read'><annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const'/></property>"
  "    <property name='BootID'                      type='ay' access='read'><annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const'/></property>"
  "    <property name='VSockCID'                    type='u' access='read'><annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const'/></property>"
  "    <property name='ChassisAssetTag'             type='s' access='read'><annotation name='org.freedesktop.DBus.Property.EmitsChangedSignal' value='const'/></property>"
  /* ---- methods ---- */
  "    <method name='SetHostname'>"
  "      <arg name='name'        type='s' direction='in'/>"
  "      <arg name='interactive' type='b' direction='in'/>"
  "    </method>"
  "    <method name='SetStaticHostname'>"
  "      <arg name='name'        type='s' direction='in'/>"
  "      <arg name='interactive' type='b' direction='in'/>"
  "    </method>"
  "    <method name='SetPrettyHostname'>"
  "      <arg name='name'        type='s' direction='in'/>"
  "      <arg name='interactive' type='b' direction='in'/>"
  "    </method>"
  "    <method name='SetIconName'>"
  "      <arg name='name'        type='s' direction='in'/>"
  "      <arg name='interactive' type='b' direction='in'/>"
  "    </method>"
  "    <method name='SetChassis'>"
  "      <arg name='name'        type='s' direction='in'/>"
  "      <arg name='interactive' type='b' direction='in'/>"
  "    </method>"
  "    <method name='SetDeployment'>"
  "      <arg name='name'        type='s' direction='in'/>"
  "      <arg name='interactive' type='b' direction='in'/>"
  "    </method>"
  "    <method name='SetLocation'>"
  "      <arg name='name'        type='s' direction='in'/>"
  "      <arg name='interactive' type='b' direction='in'/>"
  "    </method>"
  "    <method name='GetProductUUID'>"
  "      <arg name='interactive' type='b' direction='in'/>"
  "      <arg name='uuid'        type='ay' direction='out'/>"
  "    </method>"
  "    <method name='GetHardwareSerial'>"
  "      <arg name='interactive' type='b' direction='in'/>"
  "      <arg name='serial'      type='s' direction='out'/>"
  "    </method>"
  "    <method name='SetTags'>"
  "      <arg name='tags' type='as' direction='in'/>"
  "      <arg name='interactive' type='b' direction='in'/>"
  "    </method>"
  "    <method name='AddAndRemoveTags'>"
  "      <arg name='add' type='as' direction='in'/>"
  "      <arg name='remove' type='as' direction='in'/>"
  "      <arg name='interactive' type='b' direction='in'/>"
  "    </method>"
  "    <method name='GetMachineInfo'>"
  "      <arg name='field' type='s' direction='in'/>"
  "      <arg name='value' type='s' direction='out'/>"
  "    </method>"
  "    <method name='Describe'>"
  "      <arg name='json' type='s' direction='out'/>"
  "    </method>"
  "  </interface>"
  "</node>";

GDBusNodeInfo *
rcl_dbus_introspection_new (GError **error)
{
  return g_dbus_node_info_new_for_xml (hostname1_introspection_xml, error);
}

guint
rcl_dbus_register (GDBusConnection *connection, GDBusNodeInfo *introspection,
                   const GDBusInterfaceVTable *vtable, gpointer user_data,
                   GError **error)
{
  GDBusInterfaceInfo *iface = g_dbus_node_info_lookup_interface (
    introspection, "org.freedesktop.hostname1");
  return g_dbus_connection_register_object (connection,
    "/org/freedesktop/hostname1", iface, vtable, user_data, NULL, error);
}

/* Properties are read-only via D-Bus; all changes go through Set* methods. */
gboolean
rcl_dbus_handle_set_property( GDBusConnection  *connection,
                     const gchar      *sender,
                     const gchar      *object_path,
                     const gchar      *interface_name,
                     const gchar      *property_name,
                     GVariant         *value,
                     GError          **error,
                     gpointer          user_data )
{
  g_set_error( error, G_DBUS_ERROR, G_DBUS_ERROR_PROPERTY_READ_ONLY,
               "Property '%s' is read-only; use the Set* methods", property_name );
  return FALSE;
}


/* Keep the D-Bus callbacks separate from daemon state and operations. */
GVariant *
rcl_dbus_handle_get_property (GDBusConnection *connection,
  const gchar *sender, const gchar *object_path, const gchar *interface_name,
  const gchar *property_name, GError **error, gpointer user_data)
{
  return rcl_hostname_get_property_value (property_name, error, user_data);
}

void
rcl_dbus_handle_method_call (GDBusConnection *connection,
  const gchar *sender, const gchar *object_path, const gchar *interface_name,
  const gchar *method_name, GVariant *parameters,
  GDBusMethodInvocation *invocation, gpointer user_data)
{
  rcl_hostname_dispatch_method (connection, sender, object_path, interface_name,
                                method_name, parameters, invocation, user_data);
}
