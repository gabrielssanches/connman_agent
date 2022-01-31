#include <glib.h>
#include <glib/gprintf.h>
#include <gio/gio.h>
#include <stdio.h>

#define AGENT_PATH                               "/net/connman/BifrostWiFiAgent"
#define AGENT_INTERFACE                          "net.connman.Agent"

static GMainLoop *loop;

static void connman_agent_method_call(
    GDBusConnection *conn,
    const gchar *sender,
    const gchar *path,
    const gchar *interface,
    const gchar *method,
    GVariant *params,
    GDBusMethodInvocation *invocation,
    void *userdata
) {
    GVariant *p = g_dbus_method_invocation_get_parameters(invocation);
    const gchar *p_type = NULL;
    p_type = g_variant_get_type_string(p);
    g_debug("Agent method call: %s.%s()\n", interface, method);
    g_debug("Invocation parameters variant type is %s\n", p_type);

    // extract input params
    const gchar *req_path = NULL;
    const gchar *req_type = NULL;
    const gchar *req_err = NULL;
    if (g_strcmp0("(os)", p_type) == 0) {
        GVariant *req_obj = g_variant_get_child_value(p, 0);
        g_variant_get(req_obj, "&o", &req_path);

        req_obj = g_variant_get_child_value(p, 1);
        g_variant_get(req_obj, "&s", &req_err);
        g_debug("Object path:%s", req_path);
        g_debug("Error:%s", req_err);
    }
    if (g_strcmp0("(oa{sv})", p_type) == 0) {
        GVariant *req_obj = g_variant_get_child_value(p, 0);
        g_variant_get(req_obj, "&o", &req_path);

        GVariant *obj_array = g_variant_get_child_value(p, 1); 
        g_autoptr(GVariantIter) iter1 = NULL;
        g_variant_get(obj_array, "a{sv}", &iter1);
        const gchar *field;
        g_autoptr(GVariant) field_value = NULL;
        g_debug("Object path:%s", req_path);
        while (g_variant_iter_loop(iter1, "{&sv}", &field, &field_value)) {
            const gchar *field_type = g_variant_get_type_string(field_value);
            g_debug("%s[%s]\n", field, field_type);
            if (g_strcmp0("a{sv}", field_type) == 0) {
                g_autoptr(GVariantIter) iter2 = NULL;
                g_variant_get(field_value, "a{sv}", &iter2);
                const gchar *key;
                g_autoptr(GVariant) value = NULL;
                while (g_variant_iter_loop(iter2, "{&sv}", &key, &value)) {
                    gchar *value_str = NULL;
                    const gchar *value_type = g_variant_get_type_string(value);
                    if (g_strcmp0("s", value_type) == 0) {
                        g_variant_get(value, "s", &value_str);
                    } else {
                        g_warning("Unhandled type: %s", value_type);
                    }
                    if (g_strcmp0("Type", key) == 0) {
                        g_variant_get(value, "&s", &req_type);
                    }
                    g_debug("%s[%s] = %s", key, value_type, value_str);
                    g_free(value_str);
                }
            } else {
                g_warning("Unhandled type: %s", field_type);
            }
        }
    }

    if (g_strcmp0("RequestInput", method) == 0) {
        g_printf("Request Passphrase\n");
        GVariantBuilder builder;
        g_variant_builder_init (&builder, G_VARIANT_TYPE ("(a{sv})"));
        g_variant_builder_open (&builder, G_VARIANT_TYPE ("a{sv}"));
        g_variant_builder_open (&builder, G_VARIANT_TYPE ("{sv}"));
        g_variant_builder_add (&builder, "s", "Passphrase");
        g_variant_builder_add (&builder, "v", g_variant_new_string("passphrase_0"));
        g_variant_builder_close (&builder);
        g_variant_builder_open (&builder, G_VARIANT_TYPE ("{sv}"));
        g_variant_builder_add (&builder, "s", "Passphrase");
        g_variant_builder_add (&builder, "v", g_variant_new_string("passphrase_1"));
        g_variant_builder_close (&builder);
        g_variant_builder_close (&builder);
        g_dbus_method_invocation_return_value(invocation, g_variant_builder_end(&builder));
        return;
    } else if (g_strcmp0("ReportError", method) == 0) {
        g_warning("Error:%s", req_err);
    } else if (g_strcmp0("Release", method) == 0) {
        g_printf("Released\n");
        g_main_loop_quit(loop);
    } else if (g_strcmp0("Cancel", method) == 0) {
        g_printf("Canceled\n");
    } else {
        g_warning("Unknown method call: %s.%s()\n", interface, method);
    }
}

static const GDBusInterfaceVTable connman_agent_method_table = {
    .method_call = connman_agent_method_call,
};

static gboolean connman_dbus_manager_call_method(GDBusConnection *conn, const gchar *method, GVariant *param) {
    GVariant *result;
    GError *error = NULL;

    result = g_dbus_connection_call_sync(
        conn,
        "net.connman",
        "/",
        "net.connman.Manager",
        method,
        param,
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error
    );
    if (error != NULL) {
        g_print("Register %s: %s\n", AGENT_PATH, error->message);
        return FALSE;
    }

    g_variant_unref(result);
    return TRUE;
}

static guint connman_dbus_agent_register(GDBusConnection *conn) {
    GError *error = NULL;
    guint id = 0;
    GDBusNodeInfo *info = NULL;

    static const gchar connman_agent_introspection_xml[] =
        "<node name='/net/connman/BifrostWiFiAgent'>"
        "    <interface name='net.connman.Agent'>"
        "        <method name='Release'>"
        "        </method>"
        "        <method name='Cancel'>"
        "        </method>"
        "        <method name='RequestInput'>"
        "            <arg type='o' name='service' direction='in' />"
        "            <arg type='a{sv}' name='fields' direction='in' />"
        "            <arg type='a{sv}' name='fields' direction='out' />"
        "        </method>"
        "        <method name='ReportError'>"
        "            <arg type='o' name='peer' direction='in' />"
        "            <arg type='s' name='error' direction='in' />"
        "        </method>"
        "    </interface>"
        "</node>";

    info = g_dbus_node_info_new_for_xml(connman_agent_introspection_xml, &error);
    if (error) {
        g_printerr("Unable to create node: %s\n", error->message);
        g_clear_error(&error);
        return 0;
    }

    id = g_dbus_connection_register_object(
        conn,
        AGENT_PATH,
        info->interfaces[0],
        &connman_agent_method_table,
        NULL,
        NULL,
        &error
    );
    if (error) {
        g_printerr("Unable to create node: %s\n", error->message);
        g_clear_error(&error);
        return 0;
    }
    g_dbus_node_info_unref(info);
    //g_dbus_connection_unregister_object(conn, id);
    gboolean rc = connman_dbus_manager_call_method(conn, "RegisterAgent", g_variant_new("(o)", AGENT_PATH));
    if (!rc) {
        return 0;
    }
    return id;
}

static void cleanup_handler(int signo) {
    if (signo == SIGINT) {
        g_print("received SIGINT\n");
        g_main_loop_quit(loop);
    }
}

int main(int argc, char **argv) {
    int id;

    if (signal(SIGINT, cleanup_handler) == SIG_ERR) {
        g_print("can't catch SIGINT\n");
    }

    GDBusConnection *con;
    con = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
    if (con == NULL) {
        g_print("Not able to get connection to system bus\n");
        return 1;
    }

    loop = g_main_loop_new(NULL, FALSE);

    id = connman_dbus_agent_register(con);
    if (id == 0) {
        goto fail;
    }

    g_main_loop_run(loop);

fail:
    g_dbus_connection_unregister_object(con, id);
    g_object_unref(con);
    return 0;
}

