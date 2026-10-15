/*
 * Copyright (C) 2026 Nathaniel Russell <naterussell83@gmail.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#define _GNU_SOURCE   /* for timegm(3) */
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#include <glib/gi18n-lib.h>
#include <polkit/polkit.h>

#include "rcl-hostname.h"

/* --------------------------------------------------------------------------
   The org.freedesktop.hostname1 introspection XML.

   Generated once at startup by g_dbus_node_info_new_for_xml() and kept for
   the lifetime of the process.  All method/property names here must match
   the handler and getter/setter functions below.
   -------------------------------------------------------------------------- */
static const gchar hostname1_introspection_xml[] =
  "<node>"
  "  <interface name='org.freedesktop.hostname1'>"
  /* ---- read-only properties ---- */
  "    <property name='Hostname'                    type='s' access='read'/>"
  "    <property name='StaticHostname'              type='s' access='read'/>"
  "    <property name='PrettyHostname'              type='s' access='read'/>"
  "    <property name='DefaultHostname'             type='s' access='read'/>"
  "    <property name='HostnameSource'              type='s' access='read'/>"
  "    <property name='IconName'                    type='s' access='read'/>"
  "    <property name='Chassis'                     type='s' access='read'/>"
  "    <property name='Deployment'                  type='s' access='read'/>"
  "    <property name='Location'                    type='s' access='read'/>"
  "    <property name='Tags'                         type='as' access='read'/>"
  "    <property name='KernelName'                  type='s' access='read'/>"
  "    <property name='KernelRelease'               type='s' access='read'/>"
  "    <property name='KernelVersion'               type='s' access='read'/>"
  "    <property name='OperatingSystemPrettyName'   type='s' access='read'/>"
  "    <property name='OperatingSystemCPEName'      type='s' access='read'/>"
  "    <property name='HomeURL'                      type='s' access='read'/>"
  "    <property name='HardwareVendor'              type='s' access='read'/>"
  "    <property name='HardwareModel'               type='s' access='read'/>"
  "    <property name='FirmwareVersion'             type='s' access='read'/>"
  "    <property name='FirmwareDate'                type='t' access='read'/>"
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
  "      <arg name='serial'      type='s' direction='out'/>"
  "    </method>"
  "    <method name='SetTags'>"
  "      <arg name='tags' type='as' direction='in'/>"
  "    </method>"
  "    <method name='AddAndRemoveTags'>"
  "      <arg name='add' type='as' direction='in'/>"
  "      <arg name='remove' type='as' direction='in'/>"
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

/* --------------------------------------------------------------------------
   Private data
   -------------------------------------------------------------------------- */
struct _RclHostnameDaemonPrivate
{
  GDBusConnection  *connection;
  GDBusNodeInfo    *introspection;
  guint             registration_id;
  gboolean          debug;

  /* Cached PolKit authority handle, fetched once at startup.  The blocking
     part of authorisation (the agent dialog) is driven asynchronously so it
     never stalls the single-threaded main loop. */
  PolkitAuthority  *authority;

  /* Cached property values.  Refreshed by rcl_daemon_sync_dbus_properties(). */
  gchar *hostname;
  gchar *static_hostname;
  gchar *pretty_hostname;
  gchar *default_hostname;
  gchar *hostname_source;
  gchar *icon_name;
  gchar *chassis;
  gchar *deployment;
  gchar *location;
  gchar **tags;
  gchar *kernel_name;
  gchar *kernel_release;
  gchar *kernel_version;
  gchar *os_pretty_name;
  gchar *os_cpe_name;
  gchar *os_home_url;
  gchar *hw_vendor;
  gchar *hw_model;
  gchar   *firmware_version;
  guint64  firmware_date_usec;   /* µs since Unix epoch; G_MAXUINT64 = unknown */
};

/* --------------------------------------------------------------------------
   GObject boilerplate
   -------------------------------------------------------------------------- */
G_DEFINE_TYPE_WITH_PRIVATE( RclHostnameDaemon, rcl_hostname_daemon, RCL_TYPE_DAEMON )

/* Also define the abstract RclDaemon base just enough that GObject is happy.
   In a real multi-daemon codebase this would live in its own rcl-daemon.c. */
G_DEFINE_TYPE( RclDaemon, rcl_daemon, G_TYPE_OBJECT )

static void rcl_daemon_class_init( RclDaemonClass *klass ) {}
static void rcl_daemon_init( RclDaemon *self ) {}

/* --------------------------------------------------------------------------
   Chassis helpers
   -------------------------------------------------------------------------- */

/*
 * Chassis handling.  One table drives everything: the SMBIOS chassis type
 * (SMBIOS spec 3.x, Table 17) maps to a Chassis string, and the Chassis
 * string maps to an icon name.  Keep this in sync with the case statements
 * in slack/hostnamectl.
 */
const gchar *
rcl_chassis_type_to_string( guint t )
{
  switch( t )
  {
    case  3: /* Desktop            */
    case  4: /* Low Profile Desktop*/
    case  5: /* Pizza Box          */
    case  6: /* Mini Tower         */
    case  7: /* Tower              */
    case 13: /* All In One         */
    case 15: /* Space-saving       */
    case 16: /* Lunch Box          */
    case 24: /* Sealed-case PC     */
    case 35: /* Mini PC            */
    case 36: /* Stick PC           */ return "desktop";
    case  8: /* Portable           */
    case  9: /* Laptop             */
    case 10: /* Notebook           */
    case 14: /* Sub Notebook       */ return "laptop";
    case 11: /* Hand Held          */ return "handset";
    case 17: /* Main Server        */
    case 23: /* Rack Mount Server  */
    case 25: /* Multi-system       */
    case 28: /* Blade              */
    case 29: /* Blade Enclosure    */ return "server";
    case 30: /* Tablet             */
    case 32: /* Detachable         */ return "tablet";
    case 31: /* Convertible        */ return "convertible";
    case 33: /* IoT Gateway        */
    case 34: /* Embedded PC        */ return "embedded";
    default:                         return "";
  }
}

gboolean
rcl_chassis_is_known( const gchar *c )
{
  static const gchar *const known[] =
    { "desktop", "laptop", "convertible", "server", "tablet", "handset",
      "watch", "embedded", "vm", "container", NULL };
  guint i;

  for( i = 0; c && known[i]; i++ )
    if( g_strcmp0( c, known[i] ) == 0 )
      return TRUE;
  return FALSE;
}

const gchar *
rcl_chassis_to_icon_name( const gchar *c )
{
  if( !c )                                return "computer";
  if( g_strcmp0( c, "desktop"     ) == 0 ) return "computer-desktop";
  if( g_strcmp0( c, "laptop"      ) == 0 ) return "computer-laptop";
  if( g_strcmp0( c, "convertible" ) == 0 ) return "computer-laptop";
  if( g_strcmp0( c, "server"      ) == 0 ) return "computer-server";
  if( g_strcmp0( c, "tablet"      ) == 0 ) return "computer-tablet";
  if( g_strcmp0( c, "handset"     ) == 0 ) return "phone";
  if( g_strcmp0( c, "watch"       ) == 0 ) return "watch";
  if( g_strcmp0( c, "vm"          ) == 0 ) return "computer-vm";
  if( g_strcmp0( c, "container"   ) == 0 ) return "container";
  return "computer";
}

const gchar *
rcl_chassis_type_to_icon_name( guint t )
{
  return rcl_chassis_to_icon_name( rcl_chassis_type_to_string( t ) );
}

/* --------------------------------------------------------------------------
   Small file-reading helpers
   -------------------------------------------------------------------------- */

/* Read a single-line text file (e.g. /etc/hostname), strip trailing newline.
   Returns a newly-allocated string, or NULL on error. */
static gchar *
read_first_line( const gchar *path )
{
  gchar *contents = NULL;
  gchar *nl;

  if( !g_file_get_contents( path, &contents, NULL, NULL ) )
    return NULL;

  nl = strchr( contents, '\n' );
  if( nl ) *nl = '\0';

  g_strstrip( contents );
  return contents;
}

/* Read a KEY=value file (like /etc/os-release or /etc/machine-info) and
   return the unquoted value for 'key', or NULL if not found. */
static gchar *
read_key_value_file( const gchar *path, const gchar *key )
{
  gchar  *contents = NULL;
  gchar **lines;
  gchar  *result   = NULL;
  gsize   i;
  gchar   prefix[256];

  if( !g_file_get_contents( path, &contents, NULL, NULL ) )
    return NULL;

  g_snprintf( prefix, sizeof(prefix), "%s=", key );
  lines = g_strsplit( contents, "\n", -1 );

  for( i = 0; lines[i] != NULL; i++ )
  {
    if( g_str_has_prefix( lines[i], prefix ) )
    {
      gchar *val = lines[i] + strlen( prefix );

      /* Strip surrounding double-quotes if present */
      if( val[0] == '"' )
      {
        gsize len = strlen( val );
        if( len >= 2 && val[len-1] == '"' )
        {
          result = g_strndup( val + 1, len - 2 );
        }
        else
        {
          result = g_strdup( val + 1 );
        }
      }
      else
      {
        result = g_strdup( val );
      }
      break;
    }
  }

  g_strfreev( lines );
  g_free( contents );
  return result;
}

/* Read a DMI sysfs file, stripping whitespace. Returns "" on error so
   callers never get NULL for DMI fields. */
static gchar *
read_dmi( const gchar *path )
{
  gchar *val = read_first_line( path );
  return val ? val : g_strdup( "" );
}

/* Firmware routinely leaves unfilled template strings in the DMI identity
   fields (very common on retail desktop boards, e.g. ASUS reports
   sys_vendor "System manufacturer" and product_name "System Product Name"
   while the real names live in board_vendor / board_name).  Treat those,
   and empty values, as "not set" so the next source is used. */
static gboolean
rcl_dmi_is_placeholder( const gchar *v )
{
  static const gchar *const junk[] = {
    "System manufacturer", "System Product Name", "System Version",
    "To Be Filled By O.E.M.", "To be filled by O.E.M.", "Default string",
    "Not Applicable", "Not Specified", "N/A", "None", "Unknown", "Undefined",
    "O.E.M.", "OEM", "Type1ProductConfigId", "All Series",
    "System Serial Number", "Chassis Serial Number", "Base Board Serial Number",
    "Serial Number", NULL
  };
  gchar   *t;
  gboolean bad = FALSE;
  guint    i;

  if( !v )
    return TRUE;
  t = g_strstrip( g_strdup( v ) );
  if( t[0] == '\0' )
    bad = TRUE;
  for( i = 0; !bad && junk[i]; i++ )
    if( g_ascii_strcasecmp( t, junk[i] ) == 0 )
      bad = TRUE;
  g_free( t );
  return bad;
}

/* First usable (non-empty, non-placeholder) of the given strings, as a
   newly-allocated string; "" if none is usable. */
static gchar *
first_usable_dup( const gchar *a, const gchar *b, const gchar *c )
{
  if( !rcl_dmi_is_placeholder( a ) ) return g_strstrip( g_strdup( a ) );
  if( !rcl_dmi_is_placeholder( b ) ) return g_strstrip( g_strdup( b ) );
  if( !rcl_dmi_is_placeholder( c ) ) return g_strstrip( g_strdup( c ) );
  return g_strdup( "" );
}

/* Device-tree string property (NUL-terminated); "" when absent. */
static gchar *
read_dt_string( const gchar *path )
{
  gchar *v = read_first_line( path );
  return v ? v : g_strdup( "" );
}

/* The device-tree "compatible" property is a NUL-separated list of
   "vendor,model" strings, most specific first.  Return the vendor prefix of
   the first entry (e.g. "raspberrypi" from "raspberrypi,5-model-b"), or "". */
static gchar *
dt_vendor_from_compatible( const gchar *first_entry )
{
  const gchar *comma;

  if( !first_entry || !( comma = strchr( first_entry, ',' ) ) || comma == first_entry )
    return g_strdup( "" );
  return g_strndup( first_entry, (gsize)( comma - first_entry ) );
}

/* --------------------------------------------------------------------------
   FirmwareDate parser

   /sys/class/dmi/id/bios_date contains MM/DD/YYYY.  The D-Bus property is
   spec'd as type 't' (uint64 µs since the Unix epoch, UTC).  Return
   G_MAXUINT64 when the date is absent or unparseable.
   -------------------------------------------------------------------------- */
static guint64
parse_firmware_date_usec (const gchar *date_str)
{
  int month = 0, day = 0, year = 0;
  struct tm tm;
  time_t t;

  if (!date_str || date_str[0] == '\0')
    return G_MAXUINT64;

  if (sscanf (date_str, "%d/%d/%d", &month, &day, &year) != 3)
    return G_MAXUINT64;

  memset (&tm, 0, sizeof (tm));
  tm.tm_year = year - 1900;
  tm.tm_mon  = month - 1;
  tm.tm_mday = day;

  t = timegm (&tm);          /* GNU extension; interprets tm as UTC */
  if (t == (time_t) -1)
    return G_MAXUINT64;

  return (guint64) t * G_USEC_PER_SEC;
}

static gboolean rcl_hostname_is_valid( const gchar *name );

/* --------------------------------------------------------------------------
   Property refresh
   -------------------------------------------------------------------------- */

/*
 * Populate (or refresh) all cached properties from the underlying sources,
 * then emit PropertiesChanged for anything that actually changed.
 */
void
rcl_daemon_sync_dbus_properties( RclHostnameDaemon *daemon )
{
  RclHostnameDaemonPrivate *priv = daemon->priv;
  struct utsname uts;
  guint chassis_num = 0;
  gchar *tmp;

  GVariantBuilder changed_props;
  gboolean any_changed = FALSE;

  g_variant_builder_init( &changed_props, G_VARIANT_TYPE("a{sv}") );

#define UPDATE_STR_PROP( field, new_val, dbus_name )              \
  do {                                                            \
    gchar *_nv = (new_val);                                       \
    if( g_strcmp0( priv->field, _nv ) != 0 )                     \
    {                                                             \
      g_free( priv->field );                                      \
      priv->field = _nv ? g_strdup(_nv) : g_strdup("");          \
      g_variant_builder_add( &changed_props, "{sv}",             \
                             dbus_name,                           \
                             g_variant_new_string(priv->field) );\
      any_changed = TRUE;                                         \
    }                                                             \
    g_free( _nv );                                                \
  } while(0)

  /* ---- transient hostname ---- */
  {
    gchar buf[256] = {0};
    if( gethostname( buf, sizeof(buf) - 1 ) == 0 )
      tmp = g_strdup( buf );
    else
      tmp = g_strdup( "" );
    UPDATE_STR_PROP( hostname, tmp, "Hostname" );
  }

  /* ---- static hostname ---- */
  UPDATE_STR_PROP( static_hostname,
                   read_first_line( RCL_HOSTNAME_FILE ),
                   "StaticHostname" );

  /* ---- /etc/machine-info fields ---- */
  UPDATE_STR_PROP( pretty_hostname,
                   read_key_value_file( RCL_MACHINE_INFO_FILE, "PRETTY_HOSTNAME" ),
                   "PrettyHostname" );

  UPDATE_STR_PROP( deployment,
                   read_key_value_file( RCL_MACHINE_INFO_FILE, "DEPLOYMENT" ),
                   "Deployment" );

  UPDATE_STR_PROP( location,
                   read_key_value_file( RCL_MACHINE_INFO_FILE, "LOCATION" ),
                   "Location" );

  {
    gchar *raw_tags = read_key_value_file(RCL_MACHINE_INFO_FILE, "TAGS");
    gchar **new_tags = (raw_tags && raw_tags[0]) ? g_strsplit(raw_tags, ":", -1) : g_new0(gchar *, 1);
    gboolean tags_changed = priv->tags == NULL;
    if(!tags_changed) {
      gsize ti = 0;
      while(priv->tags[ti] && new_tags[ti] &&
            g_strcmp0(priv->tags[ti], new_tags[ti]) == 0) ti++;
      tags_changed = (priv->tags[ti] != NULL || new_tags[ti] != NULL);
    }
    if(tags_changed) {
      g_strfreev(priv->tags); priv->tags = g_strdupv(new_tags);
      g_variant_builder_add(&changed_props, "{sv}", "Tags", g_variant_new_strv((const gchar * const *)priv->tags, -1));
      any_changed = TRUE;
    }
    g_strfreev(new_tags); g_free(raw_tags);
  }

  /* ---- chassis / icon ----
     Chassis: machine-info, then DMI chassis type, then the device-tree
     chassis-type property.  Icon: machine-info, else derived from the
     effective chassis string.  Each value is computed first and passed
     through UPDATE_STR_PROP exactly once, so an unchanged value never
     produces a PropertiesChanged entry. */
  {
    gchar *chassis_val = read_key_value_file( RCL_MACHINE_INFO_FILE, "CHASSIS" );
    gchar *icon_val    = read_key_value_file( RCL_MACHINE_INFO_FILE, "ICON_NAME" );

    if( chassis_val == NULL || chassis_val[0] == '\0' )
    {
      gchar *dmi_chassis_type = read_dmi( RCL_DMI_CHASSIS_TYPE );
      chassis_num = (guint) g_ascii_strtoull( dmi_chassis_type, NULL, 10 );
      g_free( dmi_chassis_type );

      g_free( chassis_val );
      chassis_val = g_strdup( rcl_chassis_type_to_string( chassis_num ) );

      if( chassis_val[0] == '\0' )
      {
        gchar *dt = read_dt_string( RCL_DT_CHASSIS_TYPE );
        if( rcl_chassis_is_known( dt ) )
        {
          g_free( chassis_val );
          chassis_val = dt;
        }
        else
          g_free( dt );
      }
    }

    if( icon_val == NULL || icon_val[0] == '\0' )
    {
      g_free( icon_val );
      icon_val = g_strdup( rcl_chassis_to_icon_name( chassis_val ) );
    }

    UPDATE_STR_PROP( chassis,   chassis_val, "Chassis" );
    UPDATE_STR_PROP( icon_name, icon_val,    "IconName" );
  }

  /* ---- DefaultHostname: os-release DEFAULT_HOSTNAME, else "localhost" ---- */
  {
    gchar *def = read_key_value_file( RCL_OS_RELEASE_FILE, "DEFAULT_HOSTNAME" );
    if( !def || !rcl_hostname_is_valid( def ) )
    {
      g_free( def );
      def = g_strdup( "localhost" );
    }
    UPDATE_STR_PROP( default_hostname, def, "DefaultHostname" );
  }

  /* ---- HostnameSource ----
     static    – a static hostname is configured
     transient – none configured, kernel hostname was set to something else
     default   – none configured, kernel hostname unset or equal to default */
  {
    const gchar *src;
    if( priv->static_hostname && priv->static_hostname[0] != '\0' )
      src = "static";
    else if( priv->hostname && priv->hostname[0] != '\0' &&
             g_strcmp0( priv->hostname, "(none)" ) != 0 &&
             g_strcmp0( priv->hostname, priv->default_hostname ) != 0 )
      src = "transient";
    else
      src = "default";
    UPDATE_STR_PROP( hostname_source, g_strdup(src), "HostnameSource" );
  }

  /* ---- kernel info from uname(2) ---- */
  if( uname( &uts ) == 0 )
  {
    UPDATE_STR_PROP( kernel_name,    g_strdup(uts.sysname),  "KernelName" );
    UPDATE_STR_PROP( kernel_release, g_strdup(uts.release),  "KernelRelease" );
    UPDATE_STR_PROP( kernel_version, g_strdup(uts.version),  "KernelVersion" );
  }

  /* ---- /etc/os-release ---- */
  UPDATE_STR_PROP( os_pretty_name,
                   read_key_value_file( RCL_OS_RELEASE_FILE, "PRETTY_NAME" ),
                   "OperatingSystemPrettyName" );

  UPDATE_STR_PROP( os_cpe_name,
                   read_key_value_file( RCL_OS_RELEASE_FILE, "CPE_NAME" ),
                   "OperatingSystemCPEName" );

  UPDATE_STR_PROP( os_home_url,
                   read_key_value_file( RCL_OS_RELEASE_FILE, "HOME_URL" ),
                   "OperatingSystemHomeURL" );

  /* ---- DMI hardware info ---- */
  /* Vendor: DMI system vendor, board vendor, then the device-tree
     compatible prefix.  Model: DMI product name, board name, then the
     device-tree model.  Placeholder strings are skipped at each step. */
  {
    gchar *sys_v   = read_dmi( RCL_DMI_SYS_VENDOR );
    gchar *board_v = read_dmi( RCL_DMI_BOARD_VENDOR );
    gchar *compat  = read_dt_string( RCL_DT_COMPATIBLE );
    gchar *dt_v    = dt_vendor_from_compatible( compat );
    gchar *prod    = read_dmi( RCL_DMI_PRODUCT_NAME );
    gchar *board_n = read_dmi( RCL_DMI_BOARD_NAME );
    gchar *dt_m    = read_dt_string( RCL_DT_MODEL );

    UPDATE_STR_PROP( hw_vendor, first_usable_dup( sys_v, board_v, dt_v ), "HardwareVendor" );
    UPDATE_STR_PROP( hw_model,  first_usable_dup( prod, board_n, dt_m ),  "HardwareModel" );

    g_free( sys_v ); g_free( board_v ); g_free( compat ); g_free( dt_v );
    g_free( prod );  g_free( board_n ); g_free( dt_m );
  }
  UPDATE_STR_PROP( firmware_version, read_dmi(RCL_DMI_BIOS_VERSION),  "FirmwareVersion" );

  /* FirmwareDate is type 't' (uint64 µs since epoch) per the spec */
  {
    gchar   *date_str = read_dmi (RCL_DMI_BIOS_DATE);
    guint64  new_usec = parse_firmware_date_usec (date_str);
    g_free (date_str);

    if (priv->firmware_date_usec != new_usec)
      {
        priv->firmware_date_usec = new_usec;
        g_variant_builder_add (&changed_props, "{sv}", "FirmwareDate",
                               g_variant_new_uint64 (new_usec));
        any_changed = TRUE;
      }
  }

#undef UPDATE_STR_PROP

  /* Emit PropertiesChanged only if something actually changed */
  if( any_changed && priv->connection && priv->registration_id > 0 )
  {
    GError *err = NULL;
    g_dbus_connection_emit_signal(
      priv->connection,
      NULL,
      "/org/freedesktop/hostname1",
      "org.freedesktop.DBus.Properties",
      "PropertiesChanged",
      g_variant_new( "(sa{sv}as)",
                     "org.freedesktop.hostname1",
                     &changed_props,
                     NULL ),
      &err );

    if( err )
    {
      g_warning( "PropertiesChanged emission failed: %s", err->message );
      g_error_free( err );
    }
  }
  else
  {
    g_variant_builder_clear( &changed_props );
  }
}

/* --------------------------------------------------------------------------
   Direct property cache update

   Forces priv-> for a given machine-info key to the value we just wrote to
   disk and emits PropertiesChanged for it immediately.  Used by the Set*
   method handlers right after a successful write, so the in-memory cache
   is guaranteed correct even if a concurrent inotify-triggered resync (or
   any other resync) interleaves and re-reads the file in a stale or
   torn state.
   -------------------------------------------------------------------------- */
static void
rcl_daemon_force_machine_info_property( RclHostnameDaemon *daemon,
                                        const gchar       *key,
                                        const gchar       *new_value )
{
  RclHostnameDaemonPrivate *priv = daemon->priv;
  gchar      **field      = NULL;
  const gchar *dbus_name  = NULL;

  if     ( g_strcmp0(key, "PRETTY_HOSTNAME") == 0 ) { field = &priv->pretty_hostname; dbus_name = "PrettyHostname"; }
  else if( g_strcmp0(key, "ICON_NAME")       == 0 ) { field = &priv->icon_name;       dbus_name = "IconName"; }
  else if( g_strcmp0(key, "CHASSIS")         == 0 ) { field = &priv->chassis;         dbus_name = "Chassis"; }
  else if( g_strcmp0(key, "DEPLOYMENT")      == 0 ) { field = &priv->deployment;      dbus_name = "Deployment"; }
  else if( g_strcmp0(key, "LOCATION")        == 0 ) { field = &priv->location;        dbus_name = "Location"; }

  if( !field )
    return;

  g_free( *field );
  *field = g_strdup( new_value ? new_value : "" );

  if( priv->connection && priv->registration_id > 0 )
  {
    GVariantBuilder changed_props;
    GError *err = NULL;

    g_variant_builder_init( &changed_props, G_VARIANT_TYPE("a{sv}") );
    g_variant_builder_add( &changed_props, "{sv}", dbus_name,
                           g_variant_new_string( *field ) );

    g_dbus_connection_emit_signal(
      priv->connection,
      NULL,
      "/org/freedesktop/hostname1",
      "org.freedesktop.DBus.Properties",
      "PropertiesChanged",
      g_variant_new( "(sa{sv}as)",
                     "org.freedesktop.hostname1",
                     &changed_props,
                     NULL ),
      &err );

    if( err )
    {
      g_warning( "PropertiesChanged emission failed for %s: %s",
                dbus_name, err->message );
      g_error_free( err );
    }
  }
}

/* --------------------------------------------------------------------------
   machine-info writer

   All Set* methods that touch /etc/machine-info funnel through here.
   The file is rewritten atomically (write to temp, rename).
   -------------------------------------------------------------------------- */

/* Forward declaration: the value validator is defined further down, but the
   writer below calls it as a defence-in-depth measure so a future second
   caller cannot reintroduce machine-info injection by forgetting to validate. */
static gboolean rcl_machine_info_value_is_safe( const gchar *value );

static gboolean
write_machine_info_key( const gchar  *key,
                        const gchar  *value,
                        GError      **error )
{
  gchar  *contents  = NULL;
  gchar **lines;
  gsize   i;
  GString *out;
  gboolean found = FALSE;
  gchar   *tmp_path;
  gboolean ok;

  /* Defence in depth: never write a value we have not vetted ourselves, even
     though every current caller already validates before calling us.  An
     empty/NULL value means "remove the key" and is allowed. */
  if( value && value[0] != '\0' && !rcl_machine_info_value_is_safe( value ) )
  {
    g_set_error( error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                 "Refusing to write unsafe value for %s", key );
    return FALSE;
  }

  /* Load existing file, tolerating absence */
  g_file_get_contents( RCL_MACHINE_INFO_FILE, &contents, NULL, NULL );
  lines = contents ? g_strsplit( contents, "\n", -1 )
                   : g_new0( gchar *, 1 );
  g_free( contents );

  out = g_string_new( NULL );
  gchar prefix[256];
  g_snprintf( prefix, sizeof(prefix), "%s=", key );

  for( i = 0; lines[i] != NULL; i++ )
  {
    if( lines[i][0] == '\0' ) continue; /* skip blank lines; we'll add a trailing newline */

    if( g_str_has_prefix( lines[i], prefix ) )
    {
      /* Replace or remove this key */
      if( value && value[0] != '\0' )
        g_string_append_printf( out, "%s=\"%s\"\n", key, value );
      found = TRUE;
    }
    else
    {
      g_string_append_printf( out, "%s\n", lines[i] );
    }
  }

  /* Key wasn't present yet – append it */
  if( !found && value && value[0] != '\0' )
    g_string_append_printf( out, "%s=\"%s\"\n", key, value );

  g_strfreev( lines );

  /* Write atomically */
  tmp_path = g_strdup( RCL_MACHINE_INFO_FILE ".tmp" );
  ok = g_file_set_contents( tmp_path, out->str, (gssize)out->len, error );

  /* g_file_set_contents() creates its temporary file with restrictive
     permissions.  Set the intended public configuration mode before rename. */
  if( ok && g_chmod( tmp_path, 0644 ) != 0 )
  {
    g_set_error( error, G_IO_ERROR, g_io_error_from_errno(errno),
                 "chmod(%s, 0644): %s", tmp_path, g_strerror(errno) );
    ok = FALSE;
  }

  if( ok )
  {
    if( g_rename( tmp_path, RCL_MACHINE_INFO_FILE ) != 0 )
    {
      g_set_error( error, G_IO_ERROR, g_io_error_from_errno(errno),
                   "rename(%s, %s): %s", tmp_path,
                   RCL_MACHINE_INFO_FILE, g_strerror(errno) );
      ok = FALSE;
    }
  }

  if( !ok )
    g_unlink( tmp_path );

  g_free( tmp_path );
  g_string_free( out, TRUE );
  return ok;
}

/* --------------------------------------------------------------------------
   Input validation
   -------------------------------------------------------------------------- */

static gboolean
rcl_hostname_is_valid( const gchar *name )
{
  gsize len, i, label_len = 0;

  if( !name )
    return FALSE;

  len = strlen( name );
  if( len == 0 || len > HOST_NAME_MAX )
    return FALSE;

  /* RFC 1123: labels of 1-63 [A-Za-z0-9-] characters, separated by single
     dots, not starting or ending with '-'.  No leading/trailing/double dot. */
  for( i = 0; i < len; i++ )
  {
    gchar c = name[i];

    if( c == '.' )
    {
      if( label_len == 0 || name[i - 1] == '-' )
        return FALSE;
      label_len = 0;
      continue;
    }

    if( !( g_ascii_isalnum( c ) || c == '-' ) )
      return FALSE;
    if( c == '-' && label_len == 0 )
      return FALSE;
    if( ++label_len > 63 )
      return FALSE;
  }

  return label_len > 0 && name[len - 1] != '-';
}

/* --------------------------------------------------------------------------
   Pretty-hostname sanitiser

   Some desktop shells (observed: GNOME Control Center's "Device Name"
   field) call SetPrettyHostname with the *same* dotted/FQDN-style string
   they also pass to SetStaticHostname, rather than a short human label.
   If the incoming pretty name contains a dot and matches that pattern,
   keep only the first label (e.g. "dair.example.net" -> "dair").  A
   pretty name the caller deliberately set with spaces/punctuation but no
   dot is left untouched.
   -------------------------------------------------------------------------- */
static gchar *
rcl_derive_pretty_hostname( const gchar *name )
{
  const gchar *dot;

  if( !name || name[0] == '\0' )
    return g_strdup( "" );

  dot = strchr( name, '.' );
  if( !dot )
    return g_strdup( name );

  /* Only treat it as an FQDN/static hostname (and keep the leading label)
     when the whole string is a valid hostname.  A deliberate human label
     such as "Bob's PC v2.0" is left untouched. */
  if( !rcl_hostname_is_valid( name ) )
    return g_strdup( name );

  return g_strndup( name, (gsize)(dot - name) );
}

static gboolean
rcl_machine_info_value_is_safe( const gchar *value )
{
  const gchar *p;

  if( !value )
    return FALSE;

  for( p = value; *p; p++ )
  {
    guchar c = (guchar) *p;
    if( c < 0x20 || c == 0x7f )
      return FALSE;
    if( c == '"' || c == '\\' )
      return FALSE;
  }
  return TRUE;
}

/* --------------------------------------------------------------------------
   JSON string escaping

   Used by the Describe method.  Not every field that ends up in the JSON
   blob is sanitised at its source: /etc/os-release fields, DMI strings
   (HardwareVendor/Model, FirmwareVersion) and the static hostname can all
   contain double-quotes, backslashes or control characters.  Embedding them
   raw with %s produces malformed JSON (and is an injection surface for
   firmware-controlled DMI strings), so every string value is escaped here
   per RFC 8259.  Returns a newly-allocated string (never NULL).  UTF-8
   multibyte sequences (bytes >= 0x80) are passed through unchanged, which
   is valid inside a JSON string.
   -------------------------------------------------------------------------- */
static gchar *
json_escape_string( const gchar *s )
{
  GString *out = g_string_new( NULL );

  if( !s )
    return g_string_free( out, FALSE );

  for( const gchar *p = s; *p; p++ )
  {
    guchar c = (guchar) *p;
    switch( c )
    {
      case '"':  g_string_append( out, "\\\"" ); break;
      case '\\': g_string_append( out, "\\\\" ); break;
      case '\b': g_string_append( out, "\\b" );  break;
      case '\f': g_string_append( out, "\\f" );  break;
      case '\n': g_string_append( out, "\\n" );  break;
      case '\r': g_string_append( out, "\\r" );  break;
      case '\t': g_string_append( out, "\\t" );  break;
      default:
        if( c < 0x20 )
          g_string_append_printf( out, "\\u%04x", c );
        else
          g_string_append_c( out, (gchar) c );
        break;
    }
  }

  return g_string_free( out, FALSE );
}

/* --------------------------------------------------------------------------
   PolKit authorisation (asynchronous)

   The authorisation check is performed asynchronously so the agent password
   dialog never blocks the single-threaded GMainLoop.  Each in-flight call
   carries an AuthCtx describing the operation to run once (and only if) it
   is authorised.  The operation handlers below complete the invocation
   exactly once each.
   -------------------------------------------------------------------------- */

typedef enum
{
  OP_SET_HOSTNAME,
  OP_SET_STATIC_HOSTNAME,
  OP_SET_MACHINE_INFO,
  OP_SET_TAGS,
  OP_ADD_REMOVE_TAGS,
  OP_GET_PRODUCT_UUID,
  OP_GET_HARDWARE_SERIAL,
} HostnameOp;

typedef struct
{
  RclHostnameDaemon     *daemon;       /* owned ref */
  GDBusMethodInvocation *invocation;   /* completed exactly once */
  HostnameOp             op;
  gchar                 *name;         /* owned; SET_* operand (may be NULL) */
  gchar                 *key;          /* owned; machine-info key (SET_MACHINE_INFO) */
  GVariant              *parameters;
} AuthCtx;

static void
auth_ctx_free( AuthCtx *ctx )
{
  if( !ctx )
    return;
  g_clear_object( &ctx->daemon );
  g_free( ctx->name );
  g_free( ctx->key );
  g_clear_pointer( &ctx->parameters, g_variant_unref );
  g_free( ctx );
}

/* ---- operation handlers: run only after a successful authorisation ---- */

static void
do_set_hostname( RclHostnameDaemon *daemon, const gchar *name,
                 GDBusMethodInvocation *invocation )
{
  if( name && name[0] != '\0' )
  {
    if( !rcl_hostname_is_valid( name ) )
    {
      g_dbus_method_invocation_return_error( invocation,
        G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Invalid hostname '%s'", name );
      return;
    }
    if( sethostname( name, strlen(name) ) != 0 )
    {
      g_dbus_method_invocation_return_error( invocation,
        G_IO_ERROR, g_io_error_from_errno(errno),
        "sethostname failed: %s", g_strerror(errno) );
      return;
    }
  }

  rcl_daemon_sync_dbus_properties( daemon );
  g_dbus_method_invocation_return_value( invocation, NULL );
}

static void
do_set_static_hostname( RclHostnameDaemon *daemon, const gchar *name,
                        GDBusMethodInvocation *invocation )
{
  if( name && name[0] != '\0' )
  {
    GError *error = NULL;
    gchar  *content;

    if( !rcl_hostname_is_valid( name ) )
    {
      g_dbus_method_invocation_return_error( invocation,
        G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
        "Invalid static hostname '%s'", name );
      return;
    }

    content = g_strdup_printf( "%s\n", name );
    if( !g_file_set_contents( RCL_HOSTNAME_FILE, content, -1, &error ) )
    {
      g_free( content );
      g_dbus_method_invocation_return_gerror( invocation, error );
      g_error_free( error );
      return;
    }
    g_free( content );

    /* Keep the static hostname file readable by unprivileged clients. */
    if( g_chmod( RCL_HOSTNAME_FILE, 0644 ) != 0 )
    {
      g_dbus_method_invocation_return_error( invocation,
        G_IO_ERROR, g_io_error_from_errno(errno),
        "chmod(%s, 0644) failed: %s",
        RCL_HOSTNAME_FILE, g_strerror(errno) );
      return;
    }

    if( sethostname( name, strlen(name) ) != 0 )
      g_warning( "sethostname(%s) failed: %s", name, g_strerror(errno) );
  }
  else
  {
    g_unlink( RCL_HOSTNAME_FILE );
  }

  rcl_daemon_sync_dbus_properties( daemon );
  g_dbus_method_invocation_return_value( invocation, NULL );
}

static void
do_set_machine_info( RclHostnameDaemon *daemon, const gchar *key,
                     const gchar *raw_name, GDBusMethodInvocation *invocation )
{
  GError      *error     = NULL;
  gchar       *sanitised = NULL;
  const gchar *name      = raw_name;

  /* GNOME's "Device Name" field sometimes sends the FQDN it also gave
     SetStaticHostname; reduce it to the first label for PrettyHostname. */
  if( g_strcmp0( key, "PRETTY_HOSTNAME" ) == 0 )
  {
    sanitised = rcl_derive_pretty_hostname( name );
    name = sanitised;
  }

  if( !rcl_machine_info_value_is_safe( name ) )
  {
    g_dbus_method_invocation_return_error( invocation,
      G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
      "Invalid value for %s: control characters and quotes are not allowed", key );
    g_free( sanitised );
    return;
  }

  if( !write_machine_info_key( key, name, &error ) )
  {
    g_dbus_method_invocation_return_gerror( invocation, error );
    g_error_free( error );
    g_free( sanitised );
    return;
  }

  /* Run the generic resync first, then force this specific field to exactly
     what we just wrote so a concurrent inotify-triggered resync reading a
     stale/interleaved file cannot clobber the authoritative value. */
  rcl_daemon_sync_dbus_properties( daemon );
  rcl_daemon_force_machine_info_property( daemon, key, name );

  g_free( sanitised );
  g_dbus_method_invocation_return_value( invocation, NULL );
}

/* Parse "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" into 16 raw bytes.  Returns
   FALSE for malformed input and for the all-zero / all-0xFF placeholder
   UUIDs firmware uses to mean "not set". */
static gboolean
parse_uuid_bytes( const gchar *str, guchar out[16] )
{
  gsize   n = 0;
  gint    hi = -1;
  guchar  or_all = 0, and_all = 0xff;

  for( ; *str; str++ )
  {
    gint v;

    if( *str == '-' )
      continue;
    v = g_ascii_xdigit_value( *str );
    if( v < 0 )
      return FALSE;
    if( hi < 0 )
      hi = v;
    else
    {
      if( n >= 16 )
        return FALSE;
      out[n] = (guchar)( ( hi << 4 ) | v );
      or_all  |= out[n];
      and_all &= out[n];
      n++;
      hi = -1;
    }
  }

  return n == 16 && hi < 0 && or_all != 0 && and_all != 0xff;
}

static void
do_get_product_uuid( GDBusMethodInvocation *invocation )
{
  gchar          *uuid_str = read_dmi( RCL_DMI_PRODUCT_UUID );
  guchar          raw[16];
  GVariantBuilder bytes;

  if( !parse_uuid_bytes( uuid_str, raw ) )
  {
    g_free( uuid_str );
    g_dbus_method_invocation_return_error( invocation,
      G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Product UUID is not available" );
    return;
  }
  g_free( uuid_str );

  g_variant_builder_init( &bytes, G_VARIANT_TYPE("ay") );
  for( gsize i = 0; i < 16; i++ )
    g_variant_builder_add( &bytes, "y", raw[i] );

  g_dbus_method_invocation_return_value( invocation,
    g_variant_new("(ay)", &bytes) );
}

static void
do_get_hardware_serial( GDBusMethodInvocation *invocation )
{
  gchar *prod_s  = read_dmi( RCL_DMI_PRODUCT_SERIAL );
  gchar *board_s = read_dmi( RCL_DMI_BOARD_SERIAL );
  gchar *chas_s  = read_dmi( RCL_DMI_CHASSIS_SERIAL );
  /* Retail boards commonly leave "Default string" / "System Serial Number"
     in some of these; use the first one that holds a real value. */
  gchar *serial  = first_usable_dup( prod_s, board_s, chas_s );

  g_free( prod_s ); g_free( board_s ); g_free( chas_s );

  /* No usable DMI serial (e.g. ARM boards such as the Raspberry Pi): use the
     device-tree serial-number property. */
  if( serial[0] == '\0' )
  {
    gchar *dt_s = read_dt_string( RCL_DT_SERIAL );
    g_free( serial );
    serial = first_usable_dup( dt_s, NULL, NULL );
    g_free( dt_s );
  }
  g_dbus_method_invocation_return_value( invocation,
    g_variant_new("(s)", serial) );
  g_free( serial );
}

/* Machine tags use the machine-info TAGS field as a colon-separated list. */
static gboolean machine_tag_valid(const gchar *tag)
{
  gsize n = tag ? strlen(tag) : 0;
  if(n == 0 || n > 255) return FALSE;
  for(const gchar *p=tag; *p; p++)
    if(!(g_ascii_isalnum(*p) || *p == '-' || *p == '.')) return FALSE;
  return TRUE;
}

static void do_set_tags(RclHostnameDaemon *daemon, GVariant *parameters,
                        gboolean add_remove, GDBusMethodInvocation *invocation)
{
  GVariant *addv=NULL, *removev=NULL;
  GVariantIter iter;
  const gchar *tag;
  GPtrArray *tags=g_ptr_array_new_with_free_func(g_free);
  gchar *existing=NULL, *joined=NULL;
  GError *error=NULL;

  if(add_remove) g_variant_get(parameters,"(@as@as)",&addv,&removev);
  else g_variant_get(parameters,"(@as)",&addv);

  if(add_remove) {
    existing=read_key_value_file(RCL_MACHINE_INFO_FILE,"TAGS");
    if(existing && *existing) {
      gchar **old=g_strsplit(existing,":",-1);
      for(gsize i=0;old[i];i++) if(machine_tag_valid(old[i])) g_ptr_array_add(tags,g_strdup(old[i]));
      g_strfreev(old);
    }
    g_variant_iter_init(&iter,removev);
    while(g_variant_iter_next(&iter,"&s",&tag)) {
      if(!machine_tag_valid(tag)) { g_dbus_method_invocation_return_error(invocation,G_DBUS_ERROR,G_DBUS_ERROR_INVALID_ARGS,"Invalid machine tag"); goto out; }
      for(guint i=0;i<tags->len;) {
        if(g_strcmp0(g_ptr_array_index(tags,i),tag)==0) g_ptr_array_remove_index(tags,i);
        else i++;
      }
    }
  }
  g_variant_iter_init(&iter,addv);
  while(g_variant_iter_next(&iter,"&s",&tag)) {
    if(!machine_tag_valid(tag)) { g_dbus_method_invocation_return_error(invocation,G_DBUS_ERROR,G_DBUS_ERROR_INVALID_ARGS,"Invalid machine tag"); goto out; }
    gboolean found=FALSE;
    for(guint i=0;i<tags->len;i++) if(g_strcmp0(g_ptr_array_index(tags,i),tag)==0) {found=TRUE;break;}
    if(!found) g_ptr_array_add(tags,g_strdup(tag));
  }
  for(guint i=0;i<tags->len;i++) for(guint j=i+1;j<tags->len;j++)
    if(g_strcmp0(g_ptr_array_index(tags,i),g_ptr_array_index(tags,j))>0) {
      gpointer tmp=g_ptr_array_index(tags,i); g_ptr_array_index(tags,i)=g_ptr_array_index(tags,j); g_ptr_array_index(tags,j)=tmp;
    }
  GString *s=g_string_new(NULL);
  for(guint i=0;i<tags->len;i++) { if(i) g_string_append_c(s,':'); g_string_append(s,g_ptr_array_index(tags,i)); }
  joined=g_string_free(s,FALSE);
  if(!write_machine_info_key("TAGS",joined,&error)) {
    g_dbus_method_invocation_return_gerror(invocation,error); g_clear_error(&error); goto out;
  }
  rcl_daemon_sync_dbus_properties(daemon);
  g_dbus_method_invocation_return_value(invocation,NULL);
out:
  g_free(existing); g_free(joined); g_ptr_array_unref(tags);
  g_clear_pointer(&addv,g_variant_unref); g_clear_pointer(&removev,g_variant_unref);
}

/* ---- async authorisation callback ---- */

static void
on_check_done( GObject *source, GAsyncResult *res, gpointer user_data )
{
  AuthCtx                   *ctx    = user_data;
  PolkitAuthorizationResult *result;
  GError                    *error  = NULL;

  result = polkit_authority_check_authorization_finish(
             POLKIT_AUTHORITY(source), res, &error );

  if( !result )
  {
    g_dbus_method_invocation_return_gerror( ctx->invocation, error );
    g_error_free( error );
    auth_ctx_free( ctx );
    return;
  }

  if( !polkit_authorization_result_get_is_authorized( result ) )
  {
    /* is_challenge: authentication is possible but the caller passed
       interactive=FALSE.  Tell the client to retry with interactive=TRUE
       (so a GUI can raise an agent dialog) rather than reporting a hard
       denial. */
    if( polkit_authorization_result_get_is_challenge( result ) )
      g_dbus_method_invocation_return_dbus_error( ctx->invocation,
        "org.freedesktop.DBus.Error.InteractiveAuthorizationRequired",
        "Authentication is required to perform the requested action" );
    else
      g_dbus_method_invocation_return_error( ctx->invocation,
        G_DBUS_ERROR, G_DBUS_ERROR_AUTH_FAILED,
        "Not authorised to perform the requested action" );
    g_object_unref( result );
    auth_ctx_free( ctx );
    return;
  }
  g_object_unref( result );

  switch( ctx->op )
  {
    case OP_SET_HOSTNAME:
      do_set_hostname( ctx->daemon, ctx->name, ctx->invocation );
      break;
    case OP_SET_STATIC_HOSTNAME:
      do_set_static_hostname( ctx->daemon, ctx->name, ctx->invocation );
      break;
    case OP_SET_MACHINE_INFO:
      do_set_machine_info( ctx->daemon, ctx->key, ctx->name, ctx->invocation );
      break;
    case OP_SET_TAGS:
      do_set_tags( ctx->daemon, ctx->parameters, FALSE, ctx->invocation );
      break;
    case OP_ADD_REMOVE_TAGS:
      do_set_tags( ctx->daemon, ctx->parameters, TRUE, ctx->invocation );
      break;
    case OP_GET_PRODUCT_UUID:
      do_get_product_uuid( ctx->invocation );
      break;
    case OP_GET_HARDWARE_SERIAL:
      do_get_hardware_serial( ctx->invocation );
      break;
  }

  auth_ctx_free( ctx );
}

/* ---- async authorisation starter (takes ownership of ctx) ---- */

static void
check_polkit_async( AuthCtx *ctx, const gchar *action_id, gboolean interactive )
{
  RclHostnameDaemonPrivate     *priv = ctx->daemon->priv;
  PolkitSubject                *subject;
  PolkitCheckAuthorizationFlags flags;
  const gchar                  *sender;

  /* Acquire the authority handle lazily if the startup fetch failed.  This
     is a fast, non-interactive call (it only connects to polkitd). */
  if( priv->authority == NULL )
  {
    GError *err = NULL;
    priv->authority = polkit_authority_get_sync( NULL, &err );
    if( priv->authority == NULL )
    {
      g_dbus_method_invocation_return_gerror( ctx->invocation, err );
      g_error_free( err );
      auth_ctx_free( ctx );
      return;
    }
  }

  sender  = g_dbus_method_invocation_get_sender( ctx->invocation );
  subject = polkit_system_bus_name_new( sender );
  flags   = interactive
              ? POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION
              : POLKIT_CHECK_AUTHORIZATION_FLAGS_NONE;

  polkit_authority_check_authorization(
    priv->authority, subject, action_id, NULL, flags,
    NULL /* cancellable */, on_check_done, ctx );

  g_object_unref( subject );
}

/* --------------------------------------------------------------------------
   D-Bus property getter
   -------------------------------------------------------------------------- */
static GVariant *
handle_get_property( GDBusConnection  *connection,
                     const gchar      *sender,
                     const gchar      *object_path,
                     const gchar      *interface_name,
                     const gchar      *property_name,
                     GError          **error,
                     gpointer          user_data )
{
  RclHostnameDaemon        *daemon = RCL_HOSTNAME_DAEMON( user_data );
  RclHostnameDaemonPrivate *priv   = daemon->priv;

#define RETURN_STR(field) \
  return g_variant_new_string( priv->field ? priv->field : "" )

  if( g_strcmp0(property_name, "Hostname"                  ) == 0 ) RETURN_STR(hostname);
  if( g_strcmp0(property_name, "StaticHostname"            ) == 0 ) RETURN_STR(static_hostname);
  if( g_strcmp0(property_name, "PrettyHostname"            ) == 0 ) RETURN_STR(pretty_hostname);
  if( g_strcmp0(property_name, "DefaultHostname"           ) == 0 ) RETURN_STR(default_hostname);
  if( g_strcmp0(property_name, "HostnameSource"            ) == 0 ) RETURN_STR(hostname_source);
  if( g_strcmp0(property_name, "IconName"                  ) == 0 ) RETURN_STR(icon_name);
  if( g_strcmp0(property_name, "Chassis"                   ) == 0 ) RETURN_STR(chassis);
  if( g_strcmp0(property_name, "Deployment"                ) == 0 ) RETURN_STR(deployment);
  if( g_strcmp0(property_name, "Location"                  ) == 0 ) RETURN_STR(location);
  if( g_strcmp0(property_name, "Tags") == 0 ) return g_variant_new_strv((const gchar * const *)priv->tags, -1);
  if( g_strcmp0(property_name, "KernelName"                ) == 0 ) RETURN_STR(kernel_name);
  if( g_strcmp0(property_name, "KernelRelease"             ) == 0 ) RETURN_STR(kernel_release);
  if( g_strcmp0(property_name, "KernelVersion"             ) == 0 ) RETURN_STR(kernel_version);
  if( g_strcmp0(property_name, "OperatingSystemPrettyName" ) == 0 ) RETURN_STR(os_pretty_name);
  if( g_strcmp0(property_name, "OperatingSystemCPEName"    ) == 0 ) RETURN_STR(os_cpe_name);
  if( g_strcmp0(property_name, "HomeURL" ) == 0 ) RETURN_STR(os_home_url);
  if( g_strcmp0(property_name, "HardwareVendor"            ) == 0 ) RETURN_STR(hw_vendor);
  if( g_strcmp0(property_name, "HardwareModel"             ) == 0 ) RETURN_STR(hw_model);
  if( g_strcmp0(property_name, "FirmwareVersion"           ) == 0 ) RETURN_STR(firmware_version);
  if( g_strcmp0(property_name, "FirmwareDate"              ) == 0 )
    return g_variant_new_uint64 (priv->firmware_date_usec);

#undef RETURN_STR

  g_set_error( error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
               "Unknown property '%s'", property_name );
  return NULL;
}

/* Properties are read-only via D-Bus; all changes go through Set* methods. */
static gboolean
handle_set_property( GDBusConnection  *connection,
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

/* --------------------------------------------------------------------------
   D-Bus method handler
   -------------------------------------------------------------------------- */
static void
handle_method_call( GDBusConnection       *connection,
                    const gchar           *sender,
                    const gchar           *object_path,
                    const gchar           *interface_name,
                    const gchar           *method_name,
                    GVariant              *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer               user_data )
{
  RclHostnameDaemon        *daemon = RCL_HOSTNAME_DAEMON( user_data );
  RclHostnameDaemonPrivate *priv   = daemon->priv;
  AuthCtx                  *ctx;

  /* ---- transient / static hostname ---- */
  if( g_strcmp0(method_name, "SetHostname") == 0 ||
      g_strcmp0(method_name, "SetStaticHostname") == 0 )
  {
    const gchar *name;
    gboolean     interactive;
    g_variant_get( parameters, "(&sb)", &name, &interactive );

    ctx = g_new0( AuthCtx, 1 );
    ctx->daemon     = g_object_ref( daemon );
    ctx->invocation = invocation;
    ctx->name       = g_strdup( name );

    if( g_strcmp0(method_name, "SetHostname") == 0 )
    {
      ctx->op = OP_SET_HOSTNAME;
      check_polkit_async( ctx, RCL_HOSTNAME_POLKIT_SET_HOSTNAME, interactive );
    }
    else
    {
      ctx->op = OP_SET_STATIC_HOSTNAME;
      check_polkit_async( ctx, RCL_HOSTNAME_POLKIT_SET_STATIC, interactive );
    }
    return;
  }

  /* ---- machine-info fields ---- */
  {
    const gchar *key = NULL;

    if     ( g_strcmp0(method_name, "SetPrettyHostname") == 0 ) key = "PRETTY_HOSTNAME";
    else if( g_strcmp0(method_name, "SetIconName")       == 0 ) key = "ICON_NAME";
    else if( g_strcmp0(method_name, "SetChassis")        == 0 ) key = "CHASSIS";
    else if( g_strcmp0(method_name, "SetDeployment")     == 0 ) key = "DEPLOYMENT";
    else if( g_strcmp0(method_name, "SetLocation")       == 0 ) key = "LOCATION";

    if( key )
    {
      const gchar *name;
      gboolean     interactive;
      g_variant_get( parameters, "(&sb)", &name, &interactive );

      ctx = g_new0( AuthCtx, 1 );
      ctx->daemon     = g_object_ref( daemon );
      ctx->invocation = invocation;
      ctx->op         = OP_SET_MACHINE_INFO;
      ctx->key        = g_strdup( key );
      ctx->name       = g_strdup( name );

      check_polkit_async( ctx, RCL_HOSTNAME_POLKIT_SET_MACHINE_INFO, interactive );
      return;
    }
  }

  /* ---- sensitive hardware reads ---- */
  if( g_strcmp0(method_name, "GetHardwareSerial") == 0 ) { do_get_hardware_serial(invocation); return; }
  if( g_strcmp0(method_name, "GetProductUUID") == 0 )
  {
    gboolean interactive;
    g_variant_get( parameters, "(b)", &interactive );

    ctx = g_new0( AuthCtx, 1 );
    ctx->daemon     = g_object_ref( daemon );
    ctx->invocation = invocation;

    if( g_strcmp0(method_name, "GetProductUUID") == 0 )
    {
      ctx->op = OP_GET_PRODUCT_UUID;
      check_polkit_async( ctx, RCL_HOSTNAME_POLKIT_GET_PRODUCT_UUID, interactive );
    }
    return;
  }

  if( g_strcmp0(method_name, "SetTags") == 0 || g_strcmp0(method_name, "AddAndRemoveTags") == 0 ) {
    ctx=g_new0(AuthCtx,1); ctx->daemon=g_object_ref(daemon); ctx->invocation=invocation;
    ctx->op = g_strcmp0(method_name,"SetTags")==0 ? OP_SET_TAGS : OP_ADD_REMOVE_TAGS;
    ctx->parameters=g_variant_ref(parameters);
    check_polkit_async(ctx,RCL_HOSTNAME_POLKIT_SET_MACHINE_INFO,FALSE); return;
  }
  if( g_strcmp0(method_name, "GetMachineInfo") == 0 ) {
    const gchar *field; gchar *value; g_variant_get(parameters,"(&s)",&field);
    if(!field[0] || !g_regex_match_simple("^[A-Z][A-Z0-9_]*$",field,0,0)) {
      g_dbus_method_invocation_return_error(invocation,G_DBUS_ERROR,G_DBUS_ERROR_INVALID_ARGS,"Invalid machine-info field name"); return;
    }
    value=read_key_value_file(RCL_MACHINE_INFO_FILE,field);
    if(!value) { g_dbus_method_invocation_return_error(invocation,G_DBUS_ERROR,G_DBUS_ERROR_INVALID_ARGS,"Machine-info field '%s' is not set",field); return; }
    g_dbus_method_invocation_return_value(invocation,g_variant_new("(s)",value)); g_free(value); return;
  }

  if( g_strcmp0(method_name, "Describe") == 0 )
  {
    GString *js = g_string_new( "{" );

    /* Every string value is JSON-escaped: fields sourced from os-release,
       DMI and the static hostname are not sanitised at their source and may
       contain quotes/backslashes/control characters. */
#define ADD_STR( jname, val )                                       \
    do {                                                            \
      gchar *_e = json_escape_string( (val) );                     \
      g_string_append_printf( js, "\"%s\":\"%s\",", (jname), _e );  \
      g_free( _e );                                                 \
    } while( 0 )

    ADD_STR( "Hostname",                  priv->hostname );
    ADD_STR( "StaticHostname",            priv->static_hostname );
    ADD_STR( "PrettyHostname",            priv->pretty_hostname );
    ADD_STR( "DefaultHostname",           priv->default_hostname );
    ADD_STR( "HostnameSource",            priv->hostname_source );
    ADD_STR( "IconName",                  priv->icon_name );
    ADD_STR( "Chassis",                   priv->chassis );
    ADD_STR( "Deployment",                priv->deployment );
    ADD_STR( "Location",                  priv->location );
    g_string_append(js, "\"Tags\":[");
    for(gsize ti=0; priv->tags && priv->tags[ti]; ti++) { gchar *e=json_escape_string(priv->tags[ti]); if(ti) g_string_append_c(js, ','); g_string_append_printf(js, "\"%s\"", e); g_free(e); }
    g_string_append(js, "],");
    ADD_STR( "KernelName",                priv->kernel_name );
    ADD_STR( "KernelRelease",             priv->kernel_release );
    ADD_STR( "KernelVersion",             priv->kernel_version );
    ADD_STR( "OperatingSystemPrettyName", priv->os_pretty_name );
    ADD_STR( "OperatingSystemCPEName",    priv->os_cpe_name );
    ADD_STR( "HomeURL",                  priv->os_home_url );
    ADD_STR( "HardwareVendor",            priv->hw_vendor );
    ADD_STR( "HardwareModel",             priv->hw_model );
    ADD_STR( "FirmwareVersion",           priv->firmware_version );

#undef ADD_STR

    /* FirmwareDate is numeric (type 't') and closes the object. */
    g_string_append_printf( js, "\"FirmwareDate\":%" G_GUINT64_FORMAT "}",
                            priv->firmware_date_usec );

    g_dbus_method_invocation_return_value( invocation,
      g_variant_new("(s)", js->str) );
    g_string_free( js, TRUE );
    return;
  }

  g_dbus_method_invocation_return_error( invocation,
    G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
    "Unknown method '%s'", method_name );
}

/* --------------------------------------------------------------------------
   D-Bus vtable
   -------------------------------------------------------------------------- */
static const GDBusInterfaceVTable interface_vtable = {
  handle_method_call,
  handle_get_property,
  handle_set_property,
};

/* --------------------------------------------------------------------------
   RclDaemon virtual method implementations
   -------------------------------------------------------------------------- */

RclDaemon *
rcl_daemon_new( void )
{
  return RCL_DAEMON( g_object_new(RCL_TYPE_HOSTNAME_DAEMON, NULL) );
}

gboolean
rcl_daemon_startup( RclDaemon *daemon, GDBusConnection *connection )
{
  RclHostnameDaemon        *self = RCL_HOSTNAME_DAEMON( daemon );
  RclHostnameDaemonPrivate *priv = self->priv;
  GDBusInterfaceInfo       *iface_info;
  GError *error = NULL;

  priv->connection    = g_object_ref( connection );
  priv->introspection = g_dbus_node_info_new_for_xml(
                          hostname1_introspection_xml, &error );
  if( !priv->introspection )
  {
    g_warning( "Failed to parse introspection XML: %s", error->message );
    g_error_free( error );
    return FALSE;
  }

  iface_info = g_dbus_node_info_lookup_interface(
                 priv->introspection, "org.freedesktop.hostname1" );

  priv->registration_id = g_dbus_connection_register_object(
    connection,
    "/org/freedesktop/hostname1",
    iface_info,
    &interface_vtable,
    self,
    NULL,
    &error );

  if( priv->registration_id == 0 )
  {
    g_warning( "Could not register D-Bus object: %s", error->message );
    g_error_free( error );
    return FALSE;
  }

  rcl_daemon_sync_dbus_properties( self );

  /* Acquire the PolKit authority once up front.  This is non-interactive and
     fast; if polkitd is not reachable yet we retry lazily on first use. */
  {
    GError *pk_error = NULL;
    priv->authority = polkit_authority_get_sync( NULL, &pk_error );
    if( priv->authority == NULL )
    {
      g_warning( "Could not acquire PolKit authority at startup: %s",
                 pk_error ? pk_error->message : "(unknown)" );
      g_clear_error( &pk_error );
    }
  }

  g_debug( "hostnamed D-Bus object registered at /org/freedesktop/hostname1" );
  return TRUE;
}

void
rcl_daemon_shutdown( RclDaemon *daemon )
{
  RclHostnameDaemon        *self = RCL_HOSTNAME_DAEMON( daemon );
  RclHostnameDaemonPrivate *priv = self->priv;

  if( priv->connection && priv->registration_id > 0 )
  {
    g_dbus_connection_unregister_object( priv->connection,
                                         priv->registration_id );
    priv->registration_id = 0;
  }

  g_clear_object( &priv->connection );

  if( priv->introspection )
  {
    g_dbus_node_info_unref( priv->introspection );
    priv->introspection = NULL;
  }
}

void
rcl_daemon_set_debug( RclDaemon *daemon, gboolean debug )
{
  RCL_HOSTNAME_DAEMON(daemon)->priv->debug = debug;
}

/* --------------------------------------------------------------------------
   GObject lifecycle
   -------------------------------------------------------------------------- */

static void
rcl_hostname_daemon_finalize( GObject *object )
{
  RclHostnameDaemonPrivate *priv = RCL_HOSTNAME_DAEMON(object)->priv;

  g_clear_object( &priv->authority );

  g_free( priv->hostname );
  g_free( priv->static_hostname );
  g_free( priv->pretty_hostname );
  g_free( priv->default_hostname );
  g_free( priv->hostname_source );
  g_free( priv->icon_name );
  g_free( priv->chassis );
  g_free( priv->deployment );
  g_free( priv->location );
  g_free( priv->kernel_name );
  g_free( priv->kernel_release );
  g_free( priv->kernel_version );
  g_free( priv->os_pretty_name );
  g_free( priv->os_cpe_name );
  g_free( priv->os_home_url );
  g_free( priv->hw_vendor );
  g_free( priv->hw_model );
  g_free( priv->firmware_version );
  /* firmware_date_usec is a guint64 scalar, no g_free needed */

  G_OBJECT_CLASS(rcl_hostname_daemon_parent_class)->finalize(object);
}

static void
rcl_hostname_daemon_class_init( RclHostnameDaemonClass *klass )
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  object_class->finalize = rcl_hostname_daemon_finalize;
}

static void
rcl_hostname_daemon_init( RclHostnameDaemon *self )
{
  self->priv = rcl_hostname_daemon_get_instance_private( self );
}
