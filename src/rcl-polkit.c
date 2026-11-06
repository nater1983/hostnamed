#include "rcl-polkit.h"

typedef struct {
  GDBusMethodInvocation *invocation;
  RclPolkitAuthorizedFunc authorized;
  RclPolkitCleanupFunc cleanup;
  gpointer user_data;
} RclPolkitRequest;

static void
request_free (RclPolkitRequest *request)
{
  request->cleanup (request->user_data);
  g_object_unref (request->invocation);
  g_free (request);
}

static void
on_check_done (GObject *source, GAsyncResult *res, gpointer user_data)
{
  RclPolkitRequest *request = user_data;
  GError *error = NULL;
  PolkitAuthorizationResult *result =
    polkit_authority_check_authorization_finish (POLKIT_AUTHORITY (source), res, &error);

  if (!result) {
    g_warning ("PolKit authorization check failed: %s",
               error ? error->message : "unknown error");
    g_dbus_method_invocation_return_dbus_error (request->invocation,
      "org.freedesktop.DBus.Error.Failed", "Authorization service failed");
    g_clear_error (&error);
  } else {
    if (polkit_authorization_result_get_is_authorized (result)) {
      request->authorized (request->user_data);
    } else if (polkit_authorization_result_get_is_challenge (result)) {
      g_dbus_method_invocation_return_dbus_error (request->invocation,
        "org.freedesktop.DBus.Error.InteractiveAuthorizationRequired",
        "Authentication is required; retry interactively with a polkit authentication agent in the caller's session");
    } else {
      g_dbus_method_invocation_return_dbus_error (request->invocation,
        "org.freedesktop.DBus.Error.AccessDenied",
        "Not authorized to perform the requested action");
    }
    g_object_unref (result);
  }
  request_free (request);
}

void
rcl_polkit_check_async (PolkitAuthority **authority,
                        GDBusMethodInvocation *invocation,
                        const gchar *action_id,
                        gboolean interactive,
                        RclPolkitAuthorizedFunc authorized,
                        RclPolkitCleanupFunc cleanup,
                        gpointer user_data)
{
  PolkitSubject *subject;
  RclPolkitRequest *request;
  const gchar *sender;

  if (*authority == NULL) {
    GError *error = NULL;
    *authority = polkit_authority_get_sync (NULL, &error);
    if (*authority == NULL) {
      g_warning ("Could not acquire PolKit authority: %s",
                 error ? error->message : "unknown error");
      g_dbus_method_invocation_return_dbus_error (invocation,
        "org.freedesktop.DBus.Error.Failed", "Authorization service unavailable");
      g_clear_error (&error);
      cleanup (user_data);
      return;
    }
  }

  sender = g_dbus_method_invocation_get_sender (invocation);
  subject = polkit_system_bus_name_new (sender);
  request = g_new0 (RclPolkitRequest, 1);
  request->invocation = g_object_ref (invocation);
  request->authorized = authorized;
  request->cleanup = cleanup;
  request->user_data = user_data;

  polkit_authority_check_authorization (
    *authority, subject, action_id, NULL,
    interactive ? POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION
                : POLKIT_CHECK_AUTHORIZATION_FLAGS_NONE,
    NULL, on_check_done, request);
  g_object_unref (subject);
}
