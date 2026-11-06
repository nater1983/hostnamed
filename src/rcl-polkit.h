#pragma once
#include <gio/gio.h>
#include <polkit/polkit.h>

typedef void (*RclPolkitAuthorizedFunc) (gpointer user_data);
typedef void (*RclPolkitCleanupFunc) (gpointer user_data);

void rcl_polkit_check_async (PolkitAuthority **authority,
                            GDBusMethodInvocation *invocation,
                            const gchar *action_id,
                            gboolean interactive,
                            RclPolkitAuthorizedFunc authorized,
                            RclPolkitCleanupFunc cleanup,
                            gpointer user_data);
