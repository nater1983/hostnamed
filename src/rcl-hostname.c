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
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/file.h>
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
#include "rcl-hardware.h"
#include "rcl-polkit.h"
#include "rcl-dbus.h"
#include "rcl-config.h"

/* --------------------------------------------------------------------------
   The org.freedesktop.hostname1 introspection XML.

   Generated once at startup by g_dbus_node_info_new_for_xml() and kept for
   the lifetime of the process.  All method/property names here must match
   the handler and getter/setter functions below.
   -------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------
   Private data
   -------------------------------------------------------------------------- */
struct _RclHostnameDaemonPrivate
{
  GDBusConnection  *connection;
  GDBusNodeInfo    *introspection;
  guint             registration_id;
  guint64           registration_generation;
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
  gchar *os_fancy_name;
  gchar *os_cpe_name;
  guint64 os_support_end;
  gchar *os_home_url;
  gchar *os_image_id;
  gchar *os_image_version;
  gchar *hw_vendor;
  gchar *hw_model;
  gchar *hw_sku;
  gchar *hw_version;
  gchar *firmware_version;
  gchar *firmware_vendor;
  gchar *chassis_asset_tag;
  GVariant *machine_id;
  GVariant *boot_id;
  guint32 vsock_cid;
  gchar *hardware_chassis;
  guint64 firmware_date_usec;   /* µs since Unix epoch; G_MAXUINT64 = unknown */
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

/* File parsing and atomic writes are implemented in rcl-config.c. */
static gboolean rcl_hostname_is_valid( const gchar *name );

/* --------------------------------------------------------------------------
   Property refresh
   -------------------------------------------------------------------------- */

/*
 * Populate (or refresh) all cached properties from the underlying sources,
 * then emit PropertiesChanged for anything that actually changed.
 */
static void
rcl_daemon_sync_dbus_properties_internal( RclHostnameDaemon *daemon,
                                         gboolean refresh_hardware )
{
  RclHostnameDaemonPrivate *priv = daemon->priv;
  struct utsname uts;
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

  /* Only explicit full refreshes perform hardware discovery. */
  if (refresh_hardware) {
    RclHardwareSnapshot *hw = rcl_hardware_snapshot_new ();
    g_free (priv->hardware_chassis);
    priv->hardware_chassis = g_strdup (hw->chassis);
    UPDATE_STR_PROP (hw_vendor, g_strdup (hw->vendor), "HardwareVendor");
    UPDATE_STR_PROP (hw_model, g_strdup (hw->model), "HardwareModel");
    UPDATE_STR_PROP (hw_sku, g_strdup (hw->sku), "HardwareSKU");
    UPDATE_STR_PROP (hw_version, g_strdup (hw->version), "HardwareVersion");
    UPDATE_STR_PROP (firmware_version, g_strdup (hw->firmware_version), "FirmwareVersion");
    UPDATE_STR_PROP (firmware_vendor, g_strdup (hw->firmware_vendor), "FirmwareVendor");
    UPDATE_STR_PROP (chassis_asset_tag, g_strdup (hw->chassis_asset_tag), "ChassisAssetTag");
    if (priv->firmware_date_usec != hw->firmware_date_usec) {
      priv->firmware_date_usec = hw->firmware_date_usec;
      g_variant_builder_add (&changed_props, "{sv}", "FirmwareDate", g_variant_new_uint64 (hw->firmware_date_usec));
      any_changed = TRUE;
    }
    rcl_hardware_snapshot_free (hw);
  }

  /* Machine-info overrides cached hardware chassis. */
  {
    gchar *chassis_val = read_key_value_file (RCL_MACHINE_INFO_FILE, "CHASSIS");
    gchar *icon_val = read_key_value_file (RCL_MACHINE_INFO_FILE, "ICON_NAME");
    if (!chassis_val || !*chassis_val) {
      g_free (chassis_val);
      chassis_val = g_strdup (priv->hardware_chassis ? priv->hardware_chassis : "");
    }
    if (!icon_val || !*icon_val) {
      g_free (icon_val);
      icon_val = g_strdup (rcl_hardware_chassis_to_icon_name (chassis_val));
    }
    UPDATE_STR_PROP (chassis, chassis_val, "Chassis");
    UPDATE_STR_PROP (icon_name, icon_val, "IconName");
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

  UPDATE_STR_PROP( os_fancy_name,
                   read_key_value_file( RCL_OS_RELEASE_FILE, "FANCY_NAME" ),
                   "OperatingSystemFancyName" );

  UPDATE_STR_PROP( os_cpe_name,
                   read_key_value_file( RCL_OS_RELEASE_FILE, "CPE_NAME" ),
                   "OperatingSystemCPEName" );

  UPDATE_STR_PROP( os_home_url,
                   read_key_value_file( RCL_OS_RELEASE_FILE, "HOME_URL" ),
                   "HomeURL" );

  UPDATE_STR_PROP( os_image_id,
                   read_key_value_file( RCL_OS_RELEASE_FILE, "IMAGE_ID" ),
                   "OperatingSystemImageID" );

  UPDATE_STR_PROP( os_image_version,
                   read_key_value_file( RCL_OS_RELEASE_FILE, "IMAGE_VERSION" ),
                   "OperatingSystemImageVersion" );

  {
    gchar *support_end=read_key_value_file(RCL_OS_RELEASE_FILE,"SUPPORT_END");
    guint64 usec=G_MAXUINT64;
    if(support_end && support_end[0]) {
      struct tm tm={0}; int y=0,m=0,d=0;
      if(sscanf(support_end,"%4d-%2d-%2d",&y,&m,&d)==3 && m>=1 && m<=12 && d>=1 && d<=31) {
        tm.tm_year=y-1900; tm.tm_mon=m-1; tm.tm_mday=d;
        time_t t=timegm(&tm);
        if(t!=(time_t)-1) usec=(guint64)t*G_USEC_PER_SEC;
      }
    }
    g_free(support_end);
    if(priv->os_support_end!=usec) {
      priv->os_support_end=usec;
      g_variant_builder_add(&changed_props,"{sv}","OperatingSystemSupportEnd",g_variant_new_uint64(usec));
      any_changed=TRUE;
    }
  }

  {
    const gchar *paths[]={"/etc/machine-id","/proc/sys/kernel/random/boot_id"};
    GVariant **slots[]={&priv->machine_id,&priv->boot_id};
    const gchar *names[]={"MachineID","BootID"};
    for(guint bi=0;bi<2;bi++) {
      gchar *id=read_first_line(paths[bi]);
      guchar bytes[16]={0}; guint k=0;
      if(id) for(const gchar *p=id;*p && k<16;) {
        if(*p=='-') {p++;continue;}
        gint hi=g_ascii_xdigit_value(*p++);
        if(hi<0 || !*p) break;
        gint lo=g_ascii_xdigit_value(*p++);
        if(lo<0) break;
        bytes[k++]=(guchar)((hi<<4)|lo);
      }
      GVariant *nv=g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,bytes,16,sizeof(guchar));
      if(!*slots[bi] || !g_variant_equal(*slots[bi],nv)) {
        g_clear_pointer(slots[bi],g_variant_unref);
        *slots[bi]=g_variant_ref_sink(nv);
        g_variant_builder_add(&changed_props,"{sv}",names[bi],*slots[bi]);
        any_changed=TRUE;
      } else {
        g_variant_unref(nv);
      }
      g_free(id);
    }
  }

  {
    guint32 cid=G_MAXUINT32;
    gchar *cidstr=read_first_line("/sys/class/misc/vsock/dev");
    if(cidstr) { gchar *end=NULL; guint64 v=g_ascii_strtoull(cidstr,&end,10); if(end && *end=='\0' && v<=G_MAXUINT32) cid=(guint32)v; }
    g_free(cidstr);
    if(priv->vsock_cid!=cid) { priv->vsock_cid=cid; g_variant_builder_add(&changed_props,"{sv}","VSockCID",g_variant_new_uint32(cid)); any_changed=TRUE; }
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

void
rcl_daemon_sync_dbus_properties (RclHostnameDaemon *daemon)
{
  rcl_daemon_sync_dbus_properties_internal (daemon, TRUE);
}

void
rcl_daemon_sync_config_properties (RclHostnameDaemon *daemon)
{
  rcl_daemon_sync_dbus_properties_internal (daemon, FALSE);
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
rcl_machine_info_value_is_safe (const gchar *value)
{
  const guchar *p;

  if (value == NULL)
    return FALSE;

  for (p = (const guchar *) value; *p; p++)
    if (*p < 0x20 || *p == 0x7f)
      return FALSE;

  return g_utf8_validate (value, -1, NULL);
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
  guint64                registration_generation;
} AuthCtx;

static void
auth_ctx_free( AuthCtx *ctx )
{
  if( !ctx )
    return;
  g_clear_object( &ctx->daemon );
  g_clear_object( &ctx->invocation );
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
  if( name && name[0] != '\0' && !rcl_hostname_is_valid( name ) )
  {
    g_dbus_method_invocation_return_dbus_error( invocation,
      "org.freedesktop.DBus.Error.InvalidArgs", "Invalid hostname" );
    return;
  }

  /* An empty transient hostname explicitly clears the kernel hostname. */
  if( sethostname( name ? name : "", name ? strlen(name) : 0 ) != 0 )
  {
    {
      gchar *message = g_strdup_printf ("sethostname failed: %s", g_strerror (errno));
      g_dbus_method_invocation_return_dbus_error (invocation,
        "org.freedesktop.DBus.Error.Failed", message);
      g_free (message);
    }
    return;
  }

  rcl_daemon_sync_dbus_properties( daemon );
  g_dbus_method_invocation_return_value( invocation, NULL );
}

static void
do_set_static_hostname (RclHostnameDaemon *daemon, const gchar *name,
                        GDBusMethodInvocation *invocation)
{
  GError *error = NULL;
  const gchar *hostname = name ? name : "";

  if (hostname[0] != '\0' && !rcl_hostname_is_valid (hostname)) {
    g_dbus_method_invocation_return_dbus_error (invocation,
      "org.freedesktop.DBus.Error.InvalidArgs", "Invalid static hostname");
    return;
  }

  if (hostname[0] != '\0') {
    gchar *old_contents = NULL;
    gsize old_length = 0;
    gboolean old_exists = FALSE;
    gchar *content;
    gboolean written;

    /*
     * Snapshot the prior file so a kernel hostname failure does not leave
     * the persistent hostname changed while the D-Bus call reports failure.
     * Missing files are normal; other read failures must abort before write.
     */
    if (g_file_get_contents (RCL_HOSTNAME_FILE, &old_contents,
                             &old_length, &error)) {
      old_exists = TRUE;
    } else if (g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
      g_clear_error (&error);
    } else {
      g_dbus_method_invocation_return_dbus_error (invocation,
        "org.freedesktop.DBus.Error.Failed",
        error ? error->message : "Failed to read previous static hostname");
      g_clear_error (&error);
      return;
    }

    content = g_strdup_printf ("%s\n", hostname);
    written = atomic_replace_file (RCL_HOSTNAME_FILE, content,
                                  strlen (content), &error);
    g_free (content);
    if (!written) {
      /* A directory-fsync failure can occur after rename has taken effect.
       * Refresh even on error so the D-Bus cache reflects the actual file. */
      rcl_daemon_sync_dbus_properties (daemon);
      g_dbus_method_invocation_return_dbus_error (invocation,
        "org.freedesktop.DBus.Error.Failed",
        error ? error->message : "Failed to write static hostname");
      g_clear_error (&error);
      g_free (old_contents);
      return;
    }

    if (sethostname (hostname, strlen (hostname)) != 0) {
      int saved_errno = errno;
      GError *rollback_error = NULL;
      gboolean rolled_back;

      if (old_exists) {
        rolled_back = atomic_replace_file (RCL_HOSTNAME_FILE, old_contents,
                                           old_length, &rollback_error);
      } else {
        if (g_unlink (RCL_HOSTNAME_FILE) == 0)
          rolled_back = sync_parent_directory (RCL_HOSTNAME_FILE,
                                               &rollback_error);
        else if (errno == ENOENT)
          rolled_back = TRUE;
        else {
          rolled_back = FALSE;
          g_set_error (&rollback_error, G_IO_ERROR,
                       g_io_error_from_errno (errno),
                       "remove newly created %s: %s",
                       RCL_HOSTNAME_FILE, g_strerror (errno));
        }
      }

      if (!rolled_back) {
        g_warning ("Could not roll back %s after sethostname failure: %s",
                   RCL_HOSTNAME_FILE,
                   rollback_error ? rollback_error->message : "unknown error");
      }
      g_clear_error (&rollback_error);
      g_free (old_contents);

      /* Reflect the actual on-disk state even if rollback itself failed. */
      rcl_daemon_sync_dbus_properties (daemon);
      {
        gchar *message = g_strdup_printf ("sethostname(%s) failed: %s",
                                          hostname, g_strerror (saved_errno));
        g_dbus_method_invocation_return_dbus_error (invocation,
          "org.freedesktop.DBus.Error.Failed", message);
        g_free (message);
      }
      return;
    }

    g_free (old_contents);
  } else {
    if (g_unlink (RCL_HOSTNAME_FILE) == 0) {
      if (!sync_parent_directory (RCL_HOSTNAME_FILE, &error)) {
        rcl_daemon_sync_dbus_properties (daemon);
        g_dbus_method_invocation_return_dbus_error (invocation,
          "org.freedesktop.DBus.Error.Failed",
          error ? error->message : "Failed to sync static hostname directory");
        g_clear_error (&error);
        return;
      }
    } else if (errno != ENOENT) {
      {
        gchar *message = g_strdup_printf ("Cannot remove static hostname file %s: %s",
                                          RCL_HOSTNAME_FILE, g_strerror (errno));
        g_dbus_method_invocation_return_dbus_error (invocation,
          "org.freedesktop.DBus.Error.Failed", message);
        g_free (message);
      }
      return;
    }
  }

  rcl_daemon_sync_dbus_properties (daemon);
  g_dbus_method_invocation_return_value (invocation, NULL);
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
    g_dbus_method_invocation_return_dbus_error( invocation,
      "org.freedesktop.DBus.Error.InvalidArgs",
      "Invalid machine-info value: control characters are not allowed" );
    g_free( sanitised );
    return;
  }

  if( !write_machine_info_key( key, name, &error ) )
  {
    /* Keep the exported cache truthful if rename succeeded but directory
     * durability confirmation failed. */
    rcl_daemon_sync_dbus_properties( daemon );
    g_dbus_method_invocation_return_dbus_error( invocation,
      "org.freedesktop.DBus.Error.Failed",
      error ? error->message : "Failed to write machine information" );
    g_clear_error( &error );
    g_free( sanitised );
    return;
  }

  /* The file has been committed successfully. Refreshing the cache compares
     old and new values and emits exactly one PropertiesChanged signal only
     when the externally visible value actually changed. */
  rcl_daemon_sync_dbus_properties( daemon );

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

  /* DMI product UUIDs must use canonical 8-4-4-4-12 formatting. */
  if (strlen (str) != 36)
    return FALSE;
  for (gsize i = 0; i < 36; i++) {
    gboolean hyphen = (i == 8 || i == 13 || i == 18 || i == 23);
    if (hyphen != (str[i] == '-'))
      return FALSE;
  }
  return n == 16 && hi < 0 && or_all != 0 && and_all != 0xff;
}

static void
do_get_product_uuid( GDBusMethodInvocation *invocation )
{
  gchar          *uuid_str = rcl_hardware_read_dmi( RCL_DMI_PRODUCT_UUID );
  guchar          raw[16];
  GVariantBuilder bytes;

  if( !parse_uuid_bytes( uuid_str, raw ) )
  {
    g_free( uuid_str );
    g_dbus_method_invocation_return_dbus_error( invocation,
      "org.freedesktop.DBus.Error.NotSupported", "Product UUID is not available" );
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
  gchar *prod_s  = rcl_hardware_read_dmi( RCL_DMI_PRODUCT_SERIAL );
  gchar *board_s = rcl_hardware_read_dmi( RCL_DMI_BOARD_SERIAL );
  gchar *chas_s  = rcl_hardware_read_dmi( RCL_DMI_CHASSIS_SERIAL );
  /* Retail boards commonly leave "Default string" / "System Serial Number"
     in some of these; use the first one that holds a real value. */
  gchar *serial  = rcl_hardware_first_usable_dup( prod_s, board_s, chas_s );

  g_free( prod_s ); g_free( board_s ); g_free( chas_s );

  /* No usable DMI serial (e.g. ARM boards such as the Raspberry Pi): use the
     device-tree serial-number property. */
  if( serial[0] == '\0' )
  {
    gchar *dt_s = rcl_hardware_read_dt_string( RCL_DT_SERIAL );
    g_free( serial );
    serial = rcl_hardware_first_usable_dup( dt_s, NULL, NULL );
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

  gboolean interactive;

  if(add_remove) g_variant_get(parameters,"(@as@asb)",&addv,&removev,&interactive);
  else g_variant_get(parameters,"(@asb)",&addv,&interactive);
  (void) interactive;

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
    rcl_daemon_sync_dbus_properties(daemon);
    g_dbus_method_invocation_return_dbus_error(invocation,
      "org.freedesktop.DBus.Error.Failed",
      error ? error->message : "Failed to write machine tags");
    g_clear_error(&error); goto out;
  }
  rcl_daemon_sync_dbus_properties(daemon);
  g_dbus_method_invocation_return_value(invocation,NULL);
out:
  g_free(existing); g_free(joined); g_ptr_array_unref(tags);
  g_clear_pointer(&addv,g_variant_unref); g_clear_pointer(&removev,g_variant_unref);
}

/* Execute the requested operation only after Polkit authorizes it. */
static void
on_polkit_authorized (gpointer user_data)
{
  AuthCtx *ctx = user_data;
  RclHostnameDaemonPrivate *priv = ctx->daemon->priv;

  /* An authorization result may arrive after bus loss or a restart.
   * Never execute a privileged operation for an obsolete D-Bus request. */
  if (priv->registration_id == 0 ||
      priv->registration_generation != ctx->registration_generation ||
      priv->connection != g_dbus_method_invocation_get_connection (ctx->invocation) ||
      g_dbus_connection_is_closed (priv->connection))
  {
    g_dbus_method_invocation_return_dbus_error (
      ctx->invocation, "org.freedesktop.DBus.Error.Disconnected",
      "D-Bus connection changed while authorization was pending");
    return;
  }

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

}

static void
check_polkit_async (AuthCtx *ctx, const gchar *action_id, gboolean interactive)
{
  /* Bind the request to this registration, even if a later startup reuses
   * the same GDBusConnection and numeric registration ID. */
  ctx->registration_generation = ctx->daemon->priv->registration_generation;
  rcl_polkit_check_async (&ctx->daemon->priv->authority, ctx->invocation,
                          action_id, interactive, on_polkit_authorized,
                          (RclPolkitCleanupFunc) auth_ctx_free, ctx);
}

/* --------------------------------------------------------------------------
   D-Bus property getter
   -------------------------------------------------------------------------- */
GVariant *
rcl_hostname_get_property_value( const gchar *property_name,
                                 GError **error, gpointer user_data )
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
  if( g_strcmp0(property_name, "OperatingSystemFancyName"  ) == 0 ) RETURN_STR(os_fancy_name);
  if( g_strcmp0(property_name, "OperatingSystemCPEName"    ) == 0 ) RETURN_STR(os_cpe_name);
  if( g_strcmp0(property_name, "OperatingSystemSupportEnd" ) == 0 ) return g_variant_new_uint64(priv->os_support_end);
  if( g_strcmp0(property_name, "HomeURL"                   ) == 0 ) RETURN_STR(os_home_url);
  if( g_strcmp0(property_name, "OperatingSystemImageID"    ) == 0 ) RETURN_STR(os_image_id);
  if( g_strcmp0(property_name, "OperatingSystemImageVersion") == 0 ) RETURN_STR(os_image_version);
  if( g_strcmp0(property_name, "HardwareVendor"            ) == 0 ) RETURN_STR(hw_vendor);
  if( g_strcmp0(property_name, "HardwareModel"             ) == 0 ) RETURN_STR(hw_model);
  if( g_strcmp0(property_name, "HardwareSKU"               ) == 0 ) RETURN_STR(hw_sku);
  if( g_strcmp0(property_name, "HardwareVersion"           ) == 0 ) RETURN_STR(hw_version);
  if( g_strcmp0(property_name, "FirmwareVersion"           ) == 0 ) RETURN_STR(firmware_version);
  if( g_strcmp0(property_name, "FirmwareVendor"            ) == 0 ) RETURN_STR(firmware_vendor);
  if( g_strcmp0(property_name, "ChassisAssetTag"            ) == 0 ) RETURN_STR(chassis_asset_tag);
  if( g_strcmp0(property_name, "MachineID") == 0 ) return priv->machine_id ? g_variant_ref(priv->machine_id) : g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,NULL,0,1);
  if( g_strcmp0(property_name, "BootID") == 0 ) return priv->boot_id ? g_variant_ref(priv->boot_id) : g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,NULL,0,1);
  if( g_strcmp0(property_name, "VSockCID") == 0 ) return g_variant_new_uint32(priv->vsock_cid);
  if( g_strcmp0(property_name, "FirmwareDate"              ) == 0 )
    return g_variant_new_uint64 (priv->firmware_date_usec);

#undef RETURN_STR

  g_set_error( error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
               "Unknown property '%s'", property_name );
  return NULL;
}

/* --------------------------------------------------------------------------
   D-Bus method handler
   -------------------------------------------------------------------------- */
void
rcl_hostname_dispatch_method( GDBusConnection       *connection,
                    const gchar           *sender,
                    const gchar           *object_path,
                    const gchar           *interface_name,
                    RclHostnameMethod      method,
                    GVariant              *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer               user_data )
{
  RclHostnameDaemon        *daemon = RCL_HOSTNAME_DAEMON( user_data );
  RclHostnameDaemonPrivate *priv   = daemon->priv;
  AuthCtx                  *ctx;

  /* ---- transient / static hostname ---- */
  if( method == RCL_METHOD_SET_HOSTNAME ||
      method == RCL_METHOD_SET_STATIC_HOSTNAME )
  {
    const gchar *name;
    gboolean     interactive;
    g_variant_get( parameters, "(&sb)", &name, &interactive );

    ctx = g_new0( AuthCtx, 1 );
    ctx->daemon     = g_object_ref( daemon );
    ctx->invocation = g_object_ref (invocation);
    ctx->name       = g_strdup( name );

    if( method == RCL_METHOD_SET_HOSTNAME )
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

    if     ( method == RCL_METHOD_SET_PRETTY_HOSTNAME ) key = "PRETTY_HOSTNAME";
    else if( method == RCL_METHOD_SET_ICON_NAME ) key = "ICON_NAME";
    else if( method == RCL_METHOD_SET_CHASSIS ) key = "CHASSIS";
    else if( method == RCL_METHOD_SET_DEPLOYMENT ) key = "DEPLOYMENT";
    else if( method == RCL_METHOD_SET_LOCATION ) key = "LOCATION";

    if( key )
    {
      const gchar *name;
      gboolean     interactive;
      g_variant_get( parameters, "(&sb)", &name, &interactive );

      ctx = g_new0( AuthCtx, 1 );
      ctx->daemon     = g_object_ref( daemon );
      ctx->invocation = g_object_ref (invocation);
      ctx->op         = OP_SET_MACHINE_INFO;
      ctx->key        = g_strdup( key );
      ctx->name       = g_strdup( name );

      check_polkit_async( ctx,
        g_strcmp0(key, "PRETTY_HOSTNAME") == 0
          ? RCL_HOSTNAME_POLKIT_SET_STATIC
          : RCL_HOSTNAME_POLKIT_SET_MACHINE_INFO,
        interactive );
      return;
    }
  }

  /* ---- sensitive hardware reads ---- */
  if( method == RCL_METHOD_GET_HARDWARE_SERIAL )
  {
    gboolean interactive;

    g_variant_get(parameters, "(b)", &interactive);

    ctx = g_new0(AuthCtx, 1);
    ctx->daemon = g_object_ref(daemon);
    ctx->invocation = g_object_ref (invocation);
    ctx->op = OP_GET_HARDWARE_SERIAL;
    check_polkit_async(ctx, RCL_HOSTNAME_POLKIT_GET_HARDWARE_SERIAL, interactive);
    return;
  }
  if( method == RCL_METHOD_GET_PRODUCT_UUID )
  {
    gboolean interactive;
    g_variant_get( parameters, "(b)", &interactive );

    ctx = g_new0( AuthCtx, 1 );
    ctx->daemon     = g_object_ref( daemon );
    ctx->invocation = g_object_ref (invocation);

    if( method == RCL_METHOD_GET_PRODUCT_UUID )
    {
      ctx->op = OP_GET_PRODUCT_UUID;
      check_polkit_async( ctx, RCL_HOSTNAME_POLKIT_GET_PRODUCT_UUID, interactive );
    }
    return;
  }

  if( method == RCL_METHOD_SET_TAGS || method == RCL_METHOD_ADD_AND_REMOVE_TAGS ) {
    gboolean interactive;

    g_variant_get_child(parameters, g_variant_n_children(parameters) - 1,
                        "b", &interactive);

    ctx=g_new0(AuthCtx,1); ctx->daemon=g_object_ref(daemon); ctx->invocation=g_object_ref(invocation);
    ctx->op = method == RCL_METHOD_SET_TAGS ? OP_SET_TAGS : OP_ADD_REMOVE_TAGS;
    ctx->parameters=g_variant_ref(parameters);
    check_polkit_async(ctx,RCL_HOSTNAME_POLKIT_SET_MACHINE_INFO,interactive); return;
  }
  if( method == RCL_METHOD_GET_MACHINE_INFO ) {
    const gchar *field; gchar *value; g_variant_get(parameters,"(&s)",&field);
    if(!field[0] || !g_regex_match_simple("^[A-Z][A-Z0-9_]*$",field,0,0)) {
      g_dbus_method_invocation_return_error(invocation,G_DBUS_ERROR,G_DBUS_ERROR_INVALID_ARGS,"Invalid machine-info field name"); return;
    }
    value=read_key_value_file(RCL_MACHINE_INFO_FILE,field);
    if(!value) { g_dbus_method_invocation_return_error(invocation,G_DBUS_ERROR,G_DBUS_ERROR_INVALID_ARGS,"Machine-info field '%s' is not set",field); return; }
    g_dbus_method_invocation_return_value(invocation,g_variant_new("(s)",value)); g_free(value); return;
  }

  if( method == RCL_METHOD_DESCRIBE )
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
    ADD_STR( "OperatingSystemFancyName",  priv->os_fancy_name );
    ADD_STR( "OperatingSystemCPEName",    priv->os_cpe_name );
    ADD_STR( "HomeURL",                   priv->os_home_url );
    ADD_STR( "OperatingSystemImageID",    priv->os_image_id );
    ADD_STR( "OperatingSystemImageVersion", priv->os_image_version );
    ADD_STR( "HardwareVendor",            priv->hw_vendor );
    ADD_STR( "HardwareModel",             priv->hw_model );
    ADD_STR( "HardwareSKU",               priv->hw_sku );
    ADD_STR( "HardwareVersion",           priv->hw_version );
    ADD_STR( "FirmwareVersion",           priv->firmware_version );
    ADD_STR( "FirmwareVendor",             priv->firmware_vendor );
    ADD_STR( "ChassisAssetTag",            priv->chassis_asset_tag );

#undef ADD_STR

    /* Numeric and byte-array properties finish the JSON object. */
    g_string_append_printf(js, "\"OperatingSystemSupportEnd\":%" G_GUINT64_FORMAT ",", priv->os_support_end);
    g_string_append(js, "\"MachineID\":[");
    if(priv->machine_id) { gsize n=0; const guint8 *p=g_variant_get_fixed_array(priv->machine_id,&n,1); for(gsize i=0;i<n;i++) { if(i) g_string_append_c(js,','); g_string_append_printf(js,"%u",p[i]); } }
    g_string_append(js, "],\"BootID\":[");
    if(priv->boot_id) { gsize n=0; const guint8 *p=g_variant_get_fixed_array(priv->boot_id,&n,1); for(gsize i=0;i<n;i++) { if(i) g_string_append_c(js,','); g_string_append_printf(js,"%u",p[i]); } }
    g_string_append_printf(js, "],\"VSockCID\":%u,\"FirmwareDate\":%" G_GUINT64_FORMAT "}",
                           priv->vsock_cid, priv->firmware_date_usec);

    g_dbus_method_invocation_return_value( invocation,
      g_variant_new("(s)", js->str) );
    g_string_free( js, TRUE );
    return;
  }

  g_dbus_method_invocation_return_error_literal( invocation,
    G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown hostname1 method" );
}

/* --------------------------------------------------------------------------
   D-Bus vtable
   -------------------------------------------------------------------------- */
static const GDBusInterfaceVTable interface_vtable = {
  rcl_dbus_handle_method_call,
  rcl_dbus_handle_get_property,
  rcl_dbus_handle_set_property,
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
  GError *error = NULL;

  /* Release any prior or partially initialized bus state before acquiring
   * new references. This also handles repeated startup after a bus restart. */
  rcl_daemon_shutdown (daemon);

  priv->connection    = g_object_ref( connection );
  priv->introspection = rcl_dbus_introspection_new( &error );
  if( !priv->introspection )
  {
    g_warning( "Failed to parse introspection XML: %s",
               error ? error->message : "(unknown error)" );
    g_clear_error( &error );
    rcl_daemon_shutdown (daemon);
    return FALSE;
  }

  priv->registration_id = rcl_dbus_register(
    connection, priv->introspection, &interface_vtable, self, &error );

  if( priv->registration_id == 0 )
  {
    g_warning( "Could not register D-Bus object: %s",
               error ? error->message : "(unknown error)" );
    g_clear_error( &error );
    rcl_daemon_shutdown (daemon);
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

  g_info( "hostnamed D-Bus object registered at /org/freedesktop/hostname1" );
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

  /* Invalidate every outstanding authorization from this registration. */
  priv->registration_generation++;
  g_clear_object( &priv->connection );
  g_clear_object( &priv->authority );

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

  /* Finalization must also release bus resources if startup failed or the
   * caller omitted an explicit shutdown. Shutdown is safe to call twice. */
  rcl_daemon_shutdown (RCL_DAEMON (object));

  g_free( priv->hostname );
  g_free( priv->static_hostname );
  g_free( priv->pretty_hostname );
  g_free( priv->default_hostname );
  g_free( priv->hostname_source );
  g_free( priv->icon_name );
  g_free( priv->chassis );
  g_free( priv->deployment );
  g_free( priv->location );
  g_strfreev( priv->tags );
  g_free( priv->kernel_name );
  g_free( priv->kernel_release );
  g_free( priv->kernel_version );
  g_free( priv->os_pretty_name );
  g_free( priv->os_fancy_name );
  g_free( priv->os_cpe_name );
  g_free( priv->os_home_url );
  g_free( priv->os_image_id );
  g_free( priv->os_image_version );
  g_free( priv->hw_vendor );
  g_free( priv->hw_model );
  g_free( priv->hw_sku );
  g_free( priv->hw_version );
  g_free( priv->firmware_version );
  g_free( priv->firmware_vendor );
  g_free( priv->chassis_asset_tag );
  g_free( priv->hardware_chassis );
  g_clear_pointer( &priv->machine_id, g_variant_unref );
  g_clear_pointer( &priv->boot_id, g_variant_unref );
  /* Scalar properties need no heap cleanup. */

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
