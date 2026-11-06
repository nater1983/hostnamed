#pragma once

#include <glib.h>

/* Linux hardware identity paths. */
/* DMI sysfs paths (Linux-specific) */
#ifndef RCL_DMI_CHASSIS_TYPE
#define RCL_DMI_CHASSIS_TYPE       "/sys/class/dmi/id/chassis_type"
#endif
#define RCL_DMI_CHASSIS_VENDOR     "/sys/class/dmi/id/chassis_vendor"
#ifndef RCL_DMI_PRODUCT_NAME
#define RCL_DMI_PRODUCT_NAME       "/sys/class/dmi/id/product_name"
#endif
#define RCL_DMI_PRODUCT_UUID       "/sys/class/dmi/id/product_uuid"
#define RCL_DMI_CHASSIS_SERIAL     "/sys/class/dmi/id/chassis_serial"
#define RCL_DMI_PRODUCT_SERIAL     "/sys/class/dmi/id/product_serial"
#define RCL_DMI_BOARD_SERIAL       "/sys/class/dmi/id/board_serial"
#define RCL_DMI_BIOS_VERSION       "/sys/class/dmi/id/bios_version"
#define RCL_DMI_BIOS_DATE          "/sys/class/dmi/id/bios_date"
#ifndef RCL_DMI_BOARD_VENDOR
#define RCL_DMI_BOARD_VENDOR       "/sys/class/dmi/id/board_vendor"
#endif
#ifndef RCL_DMI_BOARD_NAME
#define RCL_DMI_BOARD_NAME         "/sys/class/dmi/id/board_name"
#endif
#ifndef RCL_DMI_SYS_VENDOR
#define RCL_DMI_SYS_VENDOR         "/sys/class/dmi/id/sys_vendor"
#endif

/* Device-tree fallbacks for platforms without DMI (ARM, RISC-V, e.g. the
   Raspberry Pi).  Overridable for tests. */
#ifndef RCL_DT_MODEL
#define RCL_DT_MODEL               "/sys/firmware/devicetree/base/model"
#endif
#ifndef RCL_DT_COMPATIBLE
#define RCL_DT_COMPATIBLE          "/sys/firmware/devicetree/base/compatible"
#endif
#ifndef RCL_DT_SERIAL
#define RCL_DT_SERIAL              "/sys/firmware/devicetree/base/serial-number"
#endif
#ifndef RCL_DT_CHASSIS_TYPE
#define RCL_DT_CHASSIS_TYPE        "/sys/firmware/devicetree/base/chassis-type"
#endif



/* Chassis identification and icon mapping. */
const gchar *rcl_hardware_chassis_type_to_string (guint chassis_type);
const gchar *rcl_hardware_chassis_type_to_icon_name (guint chassis_type);
const gchar *rcl_hardware_chassis_to_icon_name (const gchar *chassis);
gboolean     rcl_hardware_chassis_is_known (const gchar *chassis);

/* DMI and device-tree access helpers. Returned strings are newly allocated. */
gchar       *rcl_hardware_read_dmi (const gchar *path);
gchar       *rcl_hardware_first_usable_dup (const gchar *a,
                                             const gchar *b,
                                             const gchar *c);
gchar       *rcl_hardware_read_dt_string (const gchar *path);
guint64      rcl_hardware_parse_firmware_date_usec (const gchar *date_str);
