#pragma once
#include <gio/gio.h>

gchar *read_first_line (const gchar *path);
gchar *read_key_value_file (const gchar *path, const gchar *key);
gboolean atomic_replace_file (const gchar *path, const gchar *contents,
                              gsize length, GError **error);
gboolean write_machine_info_key (const gchar *key, const gchar *value,
                                 GError **error);
