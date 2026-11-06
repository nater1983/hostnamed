#define _GNU_SOURCE  /* for timegm(3) */

/*
 * Hardware discovery helpers for hostnamed.
 *
 * This module owns chassis mapping, DMI/device-tree string handling,
 * placeholder filtering, and firmware-date parsing.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <gio/gio.h>
#include <glib.h>

#include "rcl-hardware.h"

static gchar *
hardware_read_first_line (const gchar *path)
{
  gchar *contents = NULL;
  gchar *nl;

  if (!g_file_get_contents (path, &contents, NULL, NULL))
    return NULL;

  nl = strchr (contents, '\n');
  if (nl)
    *nl = '\0';

  g_strstrip (contents);
  return contents;
}

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
rcl_hardware_chassis_type_to_string( guint t )
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
rcl_hardware_chassis_is_known( const gchar *c )
{
  static const gchar *const known[] =
    { "desktop", "laptop", "convertible", "server", "tablet", "handset",
      "watch", "embedded", NULL };
  guint i;

  for( i = 0; c && known[i]; i++ )
    if( g_strcmp0( c, known[i] ) == 0 )
      return TRUE;
  return FALSE;
}

const gchar *
rcl_hardware_chassis_to_icon_name( const gchar *c )
{
  if( !c )                                return "computer";
  if( g_strcmp0( c, "desktop"     ) == 0 ) return "computer-desktop";
  if( g_strcmp0( c, "laptop"      ) == 0 ) return "computer-laptop";
  if( g_strcmp0( c, "convertible" ) == 0 ) return "computer-laptop";
  if( g_strcmp0( c, "server"      ) == 0 ) return "computer-server";
  if( g_strcmp0( c, "tablet"      ) == 0 ) return "computer-tablet";
  if( g_strcmp0( c, "handset"     ) == 0 ) return "phone";
  if( g_strcmp0( c, "watch"       ) == 0 ) return "watch";
  return "computer";
}

const gchar *
rcl_hardware_chassis_type_to_icon_name( guint t )
{
  return rcl_hardware_chassis_to_icon_name( rcl_hardware_chassis_type_to_string( t ) );
}


/* Read a DMI sysfs file, stripping whitespace. Returns "" on error so
   callers never get NULL for DMI fields. */
gchar *
rcl_hardware_read_dmi( const gchar *path )
{
  gchar *val = hardware_read_first_line( path );
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

  /* Exact known placeholders plus common firmware-template patterns. */
  for( i = 0; !bad && junk[i]; i++ )
    if( g_ascii_strcasecmp( t, junk[i] ) == 0 )
      bad = TRUE;

  if( !bad )
  {
    gchar *lower = g_ascii_strdown( t, -1 );

    if( strstr( lower, "fill" ) ||
        strstr( lower, "default" ) ||
        strstr( lower, "system manufacturer" ) ||
        strstr( lower, "system product name" ) ||
        strstr( lower, "not specified" ) ||
        strstr( lower, "not applicable" ) ||
        strstr( lower, "not set" ) ||
        strstr( lower, "not defined" ) ||
        strstr( lower, "unspecified" ) ||
        strstr( lower, "placeholder" ) ||
        strstr( lower, "unknown" ) ||
        strstr( lower, "undefined" ) ||
        strstr( lower, "invalid" ) ||
        strstr( lower, "o.e.m." ) ||
        strstr( lower, "type1productconfigid" ) ||
        strstr( lower, "productconfigid" ) ||
        strstr( lower, "serial number" ) ||
        g_str_has_prefix( lower, "oem" ) ||
        ( lower[0] == '<' && lower[strlen(lower) - 1] == '>' ) )
      bad = TRUE;

    g_free( lower );
  }

  g_free( t );
  return bad;
}

/* First usable (non-empty, non-placeholder) of the given strings, as a
   newly-allocated string; "" if none is usable. */
gchar *
rcl_hardware_first_usable_dup( const gchar *a, const gchar *b, const gchar *c )
{
  if( !rcl_dmi_is_placeholder( a ) ) return g_strstrip( g_strdup( a ) );
  if( !rcl_dmi_is_placeholder( b ) ) return g_strstrip( g_strdup( b ) );
  if( !rcl_dmi_is_placeholder( c ) ) return g_strstrip( g_strdup( c ) );
  return g_strdup( "" );
}

/* Device-tree string property (NUL-terminated); "" when absent. */
gchar *
rcl_hardware_read_dt_string( const gchar *path )
{
  gchar *v = hardware_read_first_line( path );
  return v ? v : g_strdup( "" );
}

/* --------------------------------------------------------------------------
   FirmwareDate parser

   /sys/class/dmi/id/bios_date contains MM/DD/YYYY.  The D-Bus property is
   spec'd as type 't' (uint64 µs since the Unix epoch, UTC).  Return
   G_MAXUINT64 when the date is absent or unparseable.
   -------------------------------------------------------------------------- */
guint64
rcl_hardware_parse_firmware_date_usec(const gchar *date_str)
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

