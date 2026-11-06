#include "config.h"
#include "rcl-config.h"
#include "rcl-hostname.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/file.h>
#include <unistd.h>
#include <glib/gstdio.h>

/* --------------------------------------------------------------------------
   Small file-reading helpers
   -------------------------------------------------------------------------- */

/* Read a single-line text file (e.g. /etc/hostname), strip trailing newline.
   Returns a newly-allocated string, or NULL on error. */
gchar *
read_first_line ( const gchar *path )
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

/*
 * Parse one KEY=value assignment without evaluating it as shell code.
 * os-release and machine-info are data files: variable expansion,
 * command substitution and shell execution are deliberately unsupported.
 * Malformed assignments are ignored.
 */
static gchar *
parse_key_value_line (const gchar *line, const gchar *key)
{
  const gchar *p = line;
  GString *value;
  gchar quote = '\0';

  while (g_ascii_isspace (*p))
    p++;

  if (*p == '\0' || *p == '#')
    return NULL;

  const gchar *key_start = p;
  while (g_ascii_isalnum (*p) || *p == '_')
    p++;

  if ((gsize) (p - key_start) != strlen (key) ||
      strncmp (key_start, key, strlen (key)) != 0 || *p != '=')
    return NULL;

  p++;
  while (*p == ' ' || *p == '\t')
    p++;

  if (*p == '"' || *p == '\'')
    quote = *p++;

  value = g_string_new (NULL);

  if (quote != '\0')
  {
    gboolean closed = FALSE;

    while (*p)
    {
      if (*p == quote)
      {
        p++;
        closed = TRUE;
        break;
      }

      /* os-release only defines these four double-quoted escapes.
       * Preserve a backslash before any other character literally. */
      if (*p == '\\' && quote == '"' && p[1] != '\0' &&
          (p[1] == '"' || p[1] == '\\' || p[1] == '$' || p[1] == 0x60))
        p++;

      g_string_append_c (value, *p++);
    }

    if (!closed)
      goto malformed;

    while (*p == ' ' || *p == '\t' || *p == '\r')
      p++;

    if (*p != '\0' && *p != '#')
      goto malformed;
  }
  else
  {
    gboolean trailing_space = FALSE;

    while (*p)
    {
      if (*p == '\\')
      {
        if (p[1] == '\0' || trailing_space)
          goto malformed;
        p++;
        g_string_append_c (value, *p++);
        continue;
      }

      if (g_ascii_isspace (*p))
      {
        trailing_space = TRUE;
        p++;
        continue;
      }

      if (trailing_space)
      {
        if (*p == '#')
          break;
        goto malformed;
      }

      g_string_append_c (value, *p++);
    }
  }

  return g_string_free (value, FALSE);

malformed:
  g_string_free (value, TRUE);
  return NULL;
}

/* Read a KEY=value file (e.g. /etc/os-release or /etc/machine-info).
 * Never source or execute it. The last valid occurrence of a duplicate key
 * wins, matching hostnamed's established behavior. */
gchar *
read_key_value_file (const gchar *path, const gchar *key)
{
  gchar *contents = NULL;
  gchar **lines;
  gchar *result = NULL;

  if (!g_file_get_contents (path, &contents, NULL, NULL))
    return NULL;

  lines = g_strsplit (contents, "\n", -1);

  for (gsize i = 0; lines[i] != NULL; i++)
  {
    gchar *value = parse_key_value_line (lines[i], key);

    if (value != NULL)
    {
      g_free (result);
      result = value;
    }
  }

  g_strfreev (lines);
  g_free (contents);
  return result;
}




/* Make a directory-entry change durable after unlink/rename. */
static gboolean
sync_parent_directory (const gchar *path, GError **error)
{
  gchar *dir = g_path_get_dirname (path);
  int dirfd = open (dir, O_RDONLY | O_DIRECTORY);
  int saved_errno = 0;

  if (dirfd < 0)
    saved_errno = errno;
  else if (fsync (dirfd) != 0)
    saved_errno = errno;

  if (dirfd >= 0 && close (dirfd) != 0 && saved_errno == 0)
    saved_errno = errno;

  if (saved_errno != 0) {
    g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved_errno),
                 "sync directory %s for %s: %s",
                 dir, path, g_strerror (saved_errno));
    g_free (dir);
    return FALSE;
  }

  g_free (dir);
  return TRUE;
}

/* Replace a privileged system configuration file atomically. The resulting
 * file is always owned by root:root; retain an existing regular file's mode
 * (without special bits), or use 0644 for a new file. The destination is not
 * touched unless the complete temporary file has been written and synced. */
static gboolean
atomic_replace_file_mode (const gchar *path, const gchar *contents, gsize length,
                          mode_t required_mode, GError **error)
{
  gchar *tmp_path = g_strdup_printf ("%s.XXXXXX", path);
  struct stat st;
  gboolean existed;
  int fd;
  /*
   * Do not follow a symlink at a privileged configuration path. Replacing
   * the symlink itself would be surprising, while following it could change
   * a different file than the administrator configured.
   */
  if (g_lstat (path, &st) == 0) {
    if (!S_ISREG (st.st_mode)) {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "Refusing to replace non-regular configuration file %s",
                   path);
      g_free (tmp_path);
      return FALSE;
    }
    existed = TRUE;
  } else if (errno == ENOENT) {
    existed = FALSE;
  } else {
    g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                 "lstat(%s): %s", path, g_strerror (errno));
    g_free (tmp_path);
    return FALSE;
  }
  fd = g_mkstemp (tmp_path);
  gboolean ok = FALSE;
  gsize written = 0;

  if (fd < 0) {
    g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                 "create temporary file for %s: %s", path, g_strerror (errno));
    goto out;
  }

  /* Never preserve an untrusted owner on a root-managed /etc file. */
  if (fchown (fd, 0, 0) != 0) {
    g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                 "set root ownership for %s: %s", path, g_strerror (errno));
    goto close_out;
  }
  /* Preserve ordinary rwx permissions, but never carry setuid/setgid
   * or sticky bits onto a newly written configuration file. */
  mode_t safe_mode = required_mode != 0 ? required_mode :
                     (existed ? (st.st_mode & 0777) : 0644);
  safe_mode &= ~0022;
  if (fchmod (fd, safe_mode) != 0) {
    g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                 "set permissions for %s: %s", path, g_strerror (errno));
    goto close_out;
  }

  while (written < length) {
    ssize_t n = write (fd, contents + written, length - written);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0) {
      int saved_errno = n == 0 ? EIO : errno;
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved_errno),
                   "write temporary file for %s: %s",
                   path, g_strerror (saved_errno));
      goto close_out;
    }
    written += (gsize) n;
  }

  if (fsync (fd) != 0) {
    g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                 "sync temporary file for %s: %s", path, g_strerror (errno));
    goto close_out;
  }
  if (close (fd) != 0) {
    fd = -1;
    g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                 "close temporary file for %s: %s", path, g_strerror (errno));
    goto out;
  }
  fd = -1;

  if (g_rename (tmp_path, path) != 0) {
    g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                 "rename temporary file to %s: %s", path, g_strerror (errno));
    goto out;
  }

  /* Persist the rename along with the file contents. */
  if (!sync_parent_directory (path, error))
    goto out;

  ok = TRUE;
  goto out;

close_out:
  if (fd >= 0)
    close (fd);
out:
  if (!ok)
    g_unlink (tmp_path);
  g_free (tmp_path);
  return ok;
}

/* Preserve the historical call shape for files whose mode may be retained. */
gboolean
atomic_replace_file (const gchar *path, const gchar *contents, gsize length,
                     GError **error)
{
  return atomic_replace_file_mode (path, contents, length, 0, error);
}

/* --------------------------------------------------------------------------
   machine-info writer

   All Set* methods that touch /etc/machine-info funnel through here.
   The file is rewritten atomically (write to temp, rename).
   -------------------------------------------------------------------------- */

/* Forward declaration: the value validator is defined further down, but the
   writer below calls it as a defence-in-depth measure so a future second
   caller cannot reintroduce machine-info injection by forgetting to validate. */
static gboolean rcl_machine_info_value_is_safe (const gchar *value);
static gchar *rcl_machine_info_quote_value (const gchar *value);

/* Serialize daemon writers and reject a symlink or untrusted lock file. */
static int
lock_machine_info_file (GError **error)
{
  gchar *path = g_strconcat (RCL_MACHINE_INFO_FILE, ".lock", NULL);
  int fd = open (path, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
  struct stat st;
  int saved_errno;

  if (fd < 0) {
    saved_errno = errno;
    g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved_errno),
                 "open machine-info lock %s: %s", path, g_strerror (saved_errno));
    g_free (path);
    return -1;
  }
  if (fstat (fd, &st) != 0 || !S_ISREG (st.st_mode) || st.st_uid != 0) {
    saved_errno = errno != 0 ? errno : EPERM;
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                 "machine-info lock %s must be a root-owned regular file", path);
    close (fd);
    g_free (path);
    (void) saved_errno;
    return -1;
  }
  if (fchmod (fd, 0600) != 0 || flock (fd, LOCK_EX) != 0) {
    saved_errno = errno;
    g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved_errno),
                 "secure or lock %s: %s", path, g_strerror (saved_errno));
    close (fd);
    g_free (path);
    return -1;
  }
  g_free (path);
  return fd;
}

gboolean
write_machine_info_key (const gchar *key, const gchar *value, GError **error)
{
  gchar *contents = NULL;
  gchar **lines;
  GString *out;
  gboolean found = FALSE;
  GError *read_error = NULL;
  int lock_fd;

  /* Empty or NULL values remove the key. Refuse to rewrite the file if it
   * exists but cannot be read: otherwise unrelated settings could be lost. */
  if (value && value[0] != '\0' && !rcl_machine_info_value_is_safe (value)) {
    g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                 "Refusing to write unsafe value for %s", key);
    return FALSE;
  }

  lock_fd = lock_machine_info_file (error);
  if (lock_fd < 0)
    return FALSE;

  if (!g_file_get_contents (RCL_MACHINE_INFO_FILE, &contents, NULL, &read_error)) {
    if (!g_error_matches (read_error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
      g_propagate_prefixed_error (error, read_error,
                                  "Cannot read %s: ", RCL_MACHINE_INFO_FILE);
      close (lock_fd);
      return FALSE;
    }
    g_clear_error (&read_error);
  }

  lines = contents ? g_strsplit (contents, "\n", -1) : g_new0 (gchar *, 1);
  g_free (contents);
  out = g_string_new (NULL);
  gchar *quoted_value = (value && value[0] != '\0')
                      ? rcl_machine_info_quote_value (value) : NULL;

  for (gsize i = 0; lines[i] != NULL; i++) {
    gchar *existing_value = parse_key_value_line (lines[i], key);

    if (existing_value != NULL) {
      /* Collapse duplicate valid assignments into one canonical entry. */
      if (!found && quoted_value)
        g_string_append_printf (out, "%s=%s\n", key, quoted_value);
      found = TRUE;
      g_free (existing_value);
    } else if (lines[i][0] != '\0') {
      /* Preserve comments, malformed lines, and unrelated entries. */
      g_string_append_printf (out, "%s\n", lines[i]);
    }
  }

  if (!found && quoted_value)
    g_string_append_printf (out, "%s=%s\n", key, quoted_value);

  g_free (quoted_value);
  g_strfreev (lines);

  gboolean ok = atomic_replace_file_mode (RCL_MACHINE_INFO_FILE, out->str,
                                          out->len, 0644, error);
  g_string_free (out, TRUE);
  close (lock_fd);
  return ok;
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


/* Serialize a value as a machine-info double-quoted string. */
static gchar *
rcl_machine_info_quote_value (const gchar *value)
{
  GString *quoted = g_string_new ("\"");

  for (const gchar *p = value; *p; p++)
  {
    if (*p == '\\' || *p == '"')
      g_string_append_c (quoted, '\\');
    g_string_append_c (quoted, *p);
  }

  g_string_append_c (quoted, '"');
  return g_string_free (quoted, FALSE);
}

