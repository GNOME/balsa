/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2013 Stuart Parmenter and others,
 *                         See the file AUTHORS for a list.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "toolbar-factory.h"

#include <string.h>
#include <glib/gi18n.h>

#include "balsa-app.h"
#include "balsa-icons.h"
#include "main-window.h"
#include "message-window.h"
#include "sendmsg-window.h"
#include "libbalsa-conf.h"

#include "toolbar-prefs.h"

/* Must be consistent with BalsaToolbarType enum: */
static const gchar *const balsa_toolbar_names[] =
    { "MainWindow", "ComposeWindow", "MessageWindow" };

/*
 * The BalsaToolbarModel class.
 */

struct BalsaToolbarModel_ {
    GObject object;

    GHashTable      *legal;
    GArray          *standard;
    GArray          *current;
    BalsaToolbarType type;
    GtkToolbarStyle  style;
#if HAVE_GNOME
    GSettings       *settings;
#endif /* HAVE_GNOME */
};

enum {
    CHANGED,
    LAST_SIGNAL
};

static GObjectClass* parent_class;
static guint model_signals[LAST_SIGNAL] = { 0 };

static void
balsa_toolbar_model_finalize(GObject * object)
{
    BalsaToolbarModel *model = BALSA_TOOLBAR_MODEL(object);

    if (model->legal) {
        g_hash_table_destroy(model->legal);
        model->legal = NULL;
    }
#if HAVE_GNOME
    if (model->settings) {
        g_object_unref(model->settings);
        model->settings = NULL;
    }
#endif /* HAVE_GNOME */
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
balsa_toolbar_model_class_init(BalsaToolbarModelClass* klass)
{
    GObjectClass *object_class = (GObjectClass *) klass;

    parent_class = g_type_class_peek_parent(klass);

    model_signals[CHANGED] =
        g_signal_new("changed", G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

    object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = balsa_toolbar_model_finalize;
}

static void
balsa_toolbar_model_init(BalsaToolbarModel * model)
{
    model->legal =
        g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
}

GType
balsa_toolbar_model_get_type()
{
    static GType balsa_toolbar_model_type = 0;

    if (!balsa_toolbar_model_type) {
        static const GTypeInfo balsa_toolbar_model_info = {
            sizeof(BalsaToolbarModelClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
            (GClassInitFunc) balsa_toolbar_model_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
            sizeof(BalsaToolbarModel),
            0,                  /* n_preallocs */
            (GInstanceInitFunc) balsa_toolbar_model_init,
        };

        balsa_toolbar_model_type =
            g_type_register_static(G_TYPE_OBJECT,
                                   "BalsaToolbarModel",
                                   &balsa_toolbar_model_info, 0);
    }

    return balsa_toolbar_model_type;
}

/* End of class boilerplate */

/* The descriptions must be SHORT */
button_data toolbar_buttons[]={
    {"",                         N_("Separator"),       FALSE},
    {"application-exit",         N_("Quit"),            FALSE},
    {BALSA_PIXMAP_RECEIVE,       N_("Check"),           TRUE},
    {BALSA_PIXMAP_COMPOSE,       N_("Compose"),         TRUE},
    {BALSA_PIXMAP_CONTINUE,      N_("Continue"),        FALSE},
    {BALSA_PIXMAP_REPLY,         N_("Reply"),           TRUE},
    {BALSA_PIXMAP_REPLY_ALL,     N_("Reply\nto all"),   FALSE},
    {BALSA_PIXMAP_REPLY_GROUP,   N_("Reply\nto group"), FALSE},
    {BALSA_PIXMAP_FORWARD,       N_("Forward"),         FALSE},
    {BALSA_PIXMAP_PREVIOUS,      N_("Previous"),        FALSE},
    {BALSA_PIXMAP_NEXT,          N_("Next"),            FALSE},
    {BALSA_PIXMAP_NEXT_UNREAD,   N_("Next\nunread"),    TRUE},
    {BALSA_PIXMAP_NEXT_FLAGGED,  N_("Next\nflagged"),   FALSE},
    {BALSA_PIXMAP_PREVIOUS_PART, N_("Previous\npart"),  FALSE},
    {BALSA_PIXMAP_NEXT_PART,     N_("Next\npart"),      FALSE},
    {"edit-delete",           N_("Trash /\nDelete"), FALSE},
    {BALSA_PIXMAP_POSTPONE,      N_("Postpone"),        FALSE},
    {"document-print",            N_("Print"),           FALSE},
    {BALSA_PIXMAP_REQUEST_MDN,   N_("Request\nMDN"),    FALSE},
    {BALSA_PIXMAP_SEND,          N_("Send"),            TRUE},
    {BALSA_PIXMAP_SEND_RECEIVE,  N_("Exchange"),        FALSE},
    {BALSA_PIXMAP_ATTACHMENT,    N_("Attach"),          TRUE},
    {"document-save",             N_("Save"),            TRUE},
    {BALSA_PIXMAP_IDENTITY,      N_("Identity"),        FALSE},
    {"tools-check-spelling",      N_("Spelling"),        TRUE},
    {"window-close-symbolic",    N_("Close"),           FALSE},
    {BALSA_PIXMAP_MARKED_NEW,    N_("Toggle\nnew"),     FALSE},
    {BALSA_PIXMAP_MARK_ALL,      N_("Mark all"),        FALSE},
    {BALSA_PIXMAP_SHOW_HEADERS,  N_("All\nheaders"),    FALSE},
    {"gtk-cancel",           N_("Reset\nFilter"),   FALSE},
    {BALSA_PIXMAP_SHOW_PREVIEW,  N_("Msg Preview"),     FALSE},
#ifdef HAVE_GPGME
    {BALSA_PIXMAP_GPG_SIGN,      N_("Sign"),            FALSE},
    {BALSA_PIXMAP_GPG_ENCRYPT,   N_("Encrypt"),         FALSE},
#endif
    {"edit-undo",             N_("Undo"),            FALSE},
    {"edit-redo",             N_("Redo"),            FALSE},
    {"edit-clear",            N_("Expunge"),         FALSE},
    {"list-remove",           N_("Empty\nTrash"),    FALSE},
    {"gtk-edit",             N_("Edit"),            FALSE},
};

const int toolbar_button_count =
    sizeof(toolbar_buttons) / sizeof(button_data);

/* Public methods. */
const gchar *
balsa_toolbar_button_text(gint button)
{
    return _(strcmp(toolbar_buttons[button].pixmap_id,
                    BALSA_PIXMAP_SEND) == 0
             && balsa_app.always_queue_sent_mail ?
             N_("Queue") : toolbar_buttons[button].button_text);
}

const gchar *
balsa_toolbar_sanitize_id(const gchar *id)
{
    gint button = get_toolbar_button_index(id);

    if (button >= 0)
	return toolbar_buttons[button].pixmap_id;
    else
	return NULL;
}

/* this should go to GTK because it modifies its internal structures. */
static void
balsa_toolbar_remove_all(GtkWidget * widget)
{
    GtkToolItem *item;

    while ((item = gtk_toolbar_get_nth_item((GtkToolbar *) widget, 0)))
        gtk_container_remove((GtkContainer *) widget, (GtkWidget *) item);
}

/* Load and save config
 */

static void
tm_load_model(BalsaToolbarModel * model)
{
    gchar *key;
    guint j;

    key = g_strconcat("toolbar-", balsa_toolbar_names[model->type], NULL);
    libbalsa_conf_push_group(key);
    g_free(key);

    model->style = libbalsa_conf_get_int_with_default("Style=-1", NULL);

    for (j = 0;; j++) {
        BalsaToolbarEntry entry;

        key = g_strdup_printf("Action%d", j);
        entry.action = libbalsa_conf_get_string(key);
        g_free(key);

        if (!entry.action)
            break;

        key = g_strdup_printf("Icon%d", j);
        entry.icon = libbalsa_conf_get_string(key);
        g_free(key);

        g_array_append_val(model->current, entry);
    }

    libbalsa_conf_pop_group();
}

static void
tm_save_model(BalsaToolbarModel * model)
{
    gchar *key;
    guint j;

    key = g_strconcat("toolbar-", balsa_toolbar_names[model->type], NULL);
    libbalsa_conf_remove_group(key);
    libbalsa_conf_push_group(key);
    g_free(key);

    if (model->style != (GtkToolbarStyle) (-1))
        libbalsa_conf_set_int("Style", model->style);


    for (j = 0; j < model->current->len; j++) {
        BalsaToolbarEntry *entry;

        entry = &g_array_index(model->current, BalsaToolbarEntry, j);
        key = g_strdup_printf("Action%d", j);
        libbalsa_conf_set_string(key, entry->action);
        g_free(key);
        key = g_strdup_printf("Icon%d", j);
        libbalsa_conf_set_string(key, entry->icon);
        g_free(key);
    }

    libbalsa_conf_pop_group();
}

#if HAVE_GNOME
/* GSettings change_cb
 */
static void
tm_gsettings_change_cb(GSettings   * settings,
                       const gchar * key,
                       gpointer      user_data)
{
    BalsaToolbarModel *model = user_data;

    if (!strcmp(key, "toolbar-style") &&
        model->style == (GtkToolbarStyle) (-1))
        balsa_toolbar_model_changed(model);
}
#endif /* HAVE_GNOME */

/* Create a BalsaToolbarModel structure.
 */
BalsaToolbarModel *
balsa_toolbar_model_new(BalsaToolbarType          type,
                        const BalsaToolbarEntry * entries,
                        guint                     n_entries)
{
    guint i;
    BalsaToolbarModel *model;

    model = g_object_new(BALSA_TYPE_TOOLBAR_MODEL, NULL);

    model->current = g_array_new(FALSE, FALSE, sizeof(BalsaToolbarEntry));
    model->standard = g_array_new(FALSE, FALSE, sizeof(BalsaToolbarEntry));
    for (i = 0; i < n_entries; i++) {
        BalsaToolbarEntry entry;

        entry.action = g_strdup(entries[i].action);
        entry.icon   = g_strdup(entries[i].icon);
        g_array_append_val(model->standard, entry);
    }

    model->type = type;
    balsa_toolbar_model_add_entries(model, entries, n_entries);
    tm_load_model(model);

#if HAVE_GNOME
    model->settings = g_settings_new("org.gnome.desktop.interface");
    g_signal_connect(model->settings, "changed",
                     G_CALLBACK(tm_gsettings_change_cb), model);
#endif /* HAVE_GNOME */

    return model;
}

/* balsa_toolbar_model_changed:
   Update all toolbars derived from the model.

   Called from toolbar-prefs.c after a change has been made to a toolbar
   layout.
*/
void
balsa_toolbar_model_changed(BalsaToolbarModel * model)
{
    g_signal_emit(model, model_signals[CHANGED], 0);
}

static void
tm_add_action(BalsaToolbarModel       * model,
              const BalsaToolbarEntry * entry)
{
    /* Check whether we have already seen this icon: */
    if (entry->icon && !g_hash_table_lookup(model->legal, entry->icon))
        g_hash_table_insert(model->legal, g_strdup(entry->icon),
                            g_strdup(entry->action));
}

void
balsa_toolbar_model_add_entries(BalsaToolbarModel       * model,
                                const BalsaToolbarEntry * entries,
                                guint                     n_entries)
{
    while (n_entries > 0) {
        tm_add_action(model, entries++);
        --n_entries;
    }
}

/* Return the legal icons.
 */
GHashTable *
balsa_toolbar_model_get_legal(BalsaToolbarModel * model)
{
    return model->legal;
}

/* Return the current icons.
 */
GArray *
balsa_toolbar_model_get_current(BalsaToolbarModel * model)
{
    return model->current->len > 0 ?  model->current : model->standard;
}

gboolean
balsa_toolbar_model_is_standard(BalsaToolbarModel * model)
{
    return model->current->len == 0;
}

/* Add an icon to the list of current icons in a BalsaToolbarModel.
 */
void
balsa_toolbar_model_insert_icon(BalsaToolbarModel * model, gchar * icon,
                                gint position)
{
    const gchar *real_button = balsa_toolbar_sanitize_id(icon);

    if (real_button) {
        BalsaToolbarEntry entry;

        entry.action = g_strdup(g_hash_table_lookup(model->legal, real_button));
        entry.icon   = g_strdup(real_button);
        if (position >= 0)
            g_array_insert_val(model->current, position, entry);
        else
            g_array_append_val(model->current, entry);
    } else
        g_warning(_("Unknown toolbar icon \"%s\""), icon);
}

/* Remove all icons from the BalsaToolbarModel.
 */
void
balsa_toolbar_model_clear(BalsaToolbarModel * model)
{
    guint j;

    for (j = 0; j < model->current->len; j++) {
        BalsaToolbarEntry *entry;

        entry = &g_array_index(model->current, BalsaToolbarEntry, j);
        g_free(entry->action);
        g_free(entry->icon);
    }
    g_array_set_size(model->current, 0);
}

/* Create a new instance of a toolbar
 */

static gboolean
tm_has_second_line(BalsaToolbarModel * model)
{
    GArray *current;
    guint j;

    /* Find out whether any button has 2 lines of text. */
    current = balsa_toolbar_model_get_current(model);
    for (j = 0; j < current->len; j++) {
        const gchar *icon;
        gint button;

        icon = g_array_index(current, BalsaToolbarEntry, j).icon;
        button = get_toolbar_button_index(icon);

        if (button >= 0 && strchr(balsa_toolbar_button_text(button), '\n'))
            return TRUE;
    }

    return FALSE;
}

static gint
tm_set_tool_item_label(GtkToolItem * tool_item, const gchar * stock_id,
                       gboolean make_two_line)
{
    gint button = get_toolbar_button_index(stock_id);
    const gchar *text;
    gchar *label;

    if (button < 0)
        return button;

    text = balsa_toolbar_button_text(button);
    if (balsa_app.toolbar_wrap_button_text) {
        /* Make sure all buttons have the same number of lines of
         * text (1 or 2), to keep icons aligned */
        label = make_two_line && !strchr(text, '\n') ?
            g_strconcat(text, "\n", NULL) : g_strdup(text);
    } else {
        gchar *p = label = g_strdup(text);
        while ((p = strchr(p, '\n')))
            *p++ = ' ';
    }

    gtk_tool_button_set_label(GTK_TOOL_BUTTON(tool_item), label);
    g_free(label);

    gtk_tool_item_set_is_important(tool_item,
                                   toolbar_buttons[button].is_important);

    if (strcmp(toolbar_buttons[button].pixmap_id, BALSA_PIXMAP_SEND) == 0
        && balsa_app.always_queue_sent_mail)
        gtk_tool_item_set_tooltip_text(tool_item,
                                       _("Queue this message for sending"));

    return button;
}

static GtkToolbarStyle tm_default_style(void);

static const struct {
    const gchar *text;
    const gchar *config_name;
    GtkToolbarStyle style;
} tm_toolbar_options[] = {
    {N_("Text Be_low Icons"),           "both",       GTK_TOOLBAR_BOTH},
    {N_("Priority Text Be_side Icons"), "both-horiz", GTK_TOOLBAR_BOTH_HORIZ},
    {NULL,                              "both_horiz", GTK_TOOLBAR_BOTH_HORIZ},
    {N_("_Icons Only"),                 "icons",      GTK_TOOLBAR_ICONS},
    {N_("_Text Only"),                  "text",       GTK_TOOLBAR_TEXT}
};

static GtkToolbarStyle
tm_default_style(void)
{
    GtkToolbarStyle default_style = GTK_TOOLBAR_BOTH;
#if HAVE_GNOME
    GSettings *settings;
    gchar *str;

    /* Get global setting */
    settings = g_settings_new("org.gnome.desktop.interface");
    str  = g_settings_get_string(settings, "toolbar-style");
    if (str) {
        guint i;

        for (i = 0; i < G_N_ELEMENTS(tm_toolbar_options); i++)
            if (strcmp(tm_toolbar_options[i].config_name, str) == 0) {
                default_style = tm_toolbar_options[i].style;
                break;
            }
        g_free(str);
    }
    g_object_unref(settings);
#endif /* HAVE_GNOME */

    return default_style;
}

static void
tm_set_style(GtkWidget * toolbar, BalsaToolbarModel * model)
{
    gtk_toolbar_set_style(GTK_TOOLBAR(toolbar),
                          model->style != (GtkToolbarStyle) (-1) ?
                          model->style : tm_default_style());
}

/* Populate a model
 */
#define BALSA_TOOLBAR_ACTION_MAP "balsa-toolbar-action-map"
static void
tm_populate(GtkWidget * toolbar, BalsaToolbarModel * model)
{
    gboolean style_is_both;
    gboolean make_two_line;
    GArray *current;
    guint j;
    GActionMap *action_map =
        g_object_get_data(G_OBJECT(toolbar), BALSA_TOOLBAR_ACTION_MAP);

    style_is_both = (model->style == GTK_TOOLBAR_BOTH
                     || (model->style == (GtkToolbarStyle) - 1
                         && tm_default_style() == GTK_TOOLBAR_BOTH));
    make_two_line = style_is_both && tm_has_second_line(model);

    current = balsa_toolbar_model_get_current(model);
    for (j = 0; j < current->len; j++) {
        BalsaToolbarEntry *entry;
        GtkToolItem *item;

        entry = &g_array_index(current, BalsaToolbarEntry, j);

        if (!*entry->action) {
            item = gtk_separator_tool_item_new();
        } else {
            GtkWidget *icon;
            GAction *action;
            const GVariantType *type;
            gchar *prefixed_action;

            icon = gtk_image_new_from_icon_name
                (balsa_icon_id(entry->icon), GTK_ICON_SIZE_SMALL_TOOLBAR);
            action = g_action_map_lookup_action(action_map, entry->action);
            if (action &&
                (type = g_action_get_state_type(action)) &&
                g_variant_type_equal(type, G_VARIANT_TYPE_BOOLEAN)) {
                item = gtk_toggle_tool_button_new();
                g_object_set(G_OBJECT(item), "icon-widget", icon,
                             "label", entry->action, NULL);
            } else {
                item = gtk_tool_button_new(icon, entry->action);
            }
            tm_set_tool_item_label(GTK_TOOL_ITEM(item), entry->icon,
                                   make_two_line);

            prefixed_action =
                g_strconcat(action ? "win." : "app.", entry->action, NULL);
            gtk_actionable_set_action_name(GTK_ACTIONABLE(item),
                                           prefixed_action);
            g_free(prefixed_action);
        }
        gtk_toolbar_insert((GtkToolbar *) toolbar, item, -1);
    }
    gtk_widget_show_all(toolbar);
}

/* Update a real toolbar when the model has changed.
 */
static void
tm_changed_cb(BalsaToolbarModel * model, GtkWidget * toolbar)
{
    balsa_toolbar_remove_all(toolbar);
    tm_populate(toolbar, model);
    tm_set_style(toolbar, model);
    tm_save_model(model);
}

typedef struct {
    BalsaToolbarModel *model;
    GtkWidget         *menu;
} toolbar_info;

static void
tm_toolbar_weak_notify(toolbar_info * info, GtkWidget * toolbar)
{
    g_signal_handlers_disconnect_by_data(info->model, toolbar);
    g_free(info);
}

#define BALSA_TOOLBAR_STYLE "balsa-toolbar-style"
static void
menu_item_toggled_cb(GtkCheckMenuItem * item, toolbar_info * info)
{
    if (gtk_check_menu_item_get_active(item)) {
        info->model->style =
            GPOINTER_TO_INT(g_object_get_data
                            (G_OBJECT(item), BALSA_TOOLBAR_STYLE));
        balsa_toolbar_model_changed(info->model);
        if (info->menu)
            gtk_menu_shell_deactivate(GTK_MENU_SHELL(info->menu));
    }
}

/* We want to destroy the popup menu after handling the "toggled"
 * signal; the "deactivate" signal is apparently emitted before
 * "toggled", so we have to use an idle callback. */
static gboolean
tm_popup_idle_cb(GtkWidget *menu)
{
    gtk_widget_destroy(menu);
    return FALSE;
}

static void
tm_popup_deactivated_cb(GtkWidget * menu, toolbar_info * info)
{
    if (info->menu) {
        g_idle_add((GSourceFunc) tm_popup_idle_cb, menu);
        info->menu = NULL;
    }
}

static gchar *
tm_remove_underscore(const gchar * text)
{
    gchar *p, *q, *r = g_strdup(text);

    for (p = q = r; *p; p++)
        if (*p != '_')
            *q++ = *p;
    *q = '\0';

    return r;
}

/* If the menu is popped up in response to a keystroke, center it
 * immediately below the toolbar.
 */
static void
tm_popup_position_func(GtkMenu * menu, gint * x, gint * y,
                       gboolean * push_in, gpointer user_data)
{
    GtkWidget *toolbar = GTK_WIDGET(user_data);
    GdkScreen *screen = gtk_widget_get_screen(toolbar);
    GtkRequisition req;
    gint monitor_num;
    GdkRectangle monitor;
    GtkAllocation allocation;

    g_return_if_fail(gtk_widget_get_window(toolbar));

    gdk_window_get_origin(gtk_widget_get_window(toolbar), x, y);

    gtk_widget_get_preferred_size(GTK_WIDGET(menu), NULL, &req);

    gtk_widget_get_allocation(toolbar, &allocation);
    *x += (allocation.width - req.width) / 2;
    *y += allocation.height;

    monitor_num = gdk_screen_get_monitor_at_point(screen, *x, *y);
    gtk_menu_set_monitor(menu, monitor_num);
    gdk_screen_get_monitor_geometry(screen, monitor_num, &monitor);

    *x = CLAMP(*x, monitor.x,
               monitor.x + MAX(0, monitor.width - req.width));
    *y = CLAMP(*y, monitor.y,
               monitor.y + MAX(0, monitor.height - req.height));

    *push_in = FALSE;
}

static gboolean
tm_do_popup_menu(GtkWidget * toolbar, GdkEventButton * event,
                 toolbar_info * info)
{
    GtkWidget *menu;
    int button, event_time;
    guint i;
    GSList *group = NULL;
    GtkToolbarStyle default_style = tm_default_style();

    if (info->menu)
        return FALSE;

    info->menu = menu = gtk_menu_new();
    g_signal_connect(menu, "deactivate",
                     G_CALLBACK(tm_popup_deactivated_cb), info);

    /* ... add menu items ... */
    for (i = 0; i < G_N_ELEMENTS(tm_toolbar_options); i++) {
        GtkWidget *item;

        if (!tm_toolbar_options[i].text)
            continue;

        item =
            gtk_radio_menu_item_new_with_mnemonic(group,
                                                  _(tm_toolbar_options[i].
                                                    text));
        group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));

        if (tm_toolbar_options[i].style == info->model->style)
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),
                                           TRUE);

        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        g_object_set_data(G_OBJECT(item), BALSA_TOOLBAR_STYLE,
                          GINT_TO_POINTER(tm_toolbar_options[i].style));
        g_signal_connect(item, "toggled", G_CALLBACK(menu_item_toggled_cb),
                         info);
    }

    for (i = 0; i < G_N_ELEMENTS(tm_toolbar_options); i++) {

        if (!tm_toolbar_options[i].text)
            continue;

        if (tm_toolbar_options[i].style == default_style) {
            gchar *option_text, *text;
            GtkWidget *item;

            gtk_menu_shell_append(GTK_MENU_SHELL(menu),
                                  gtk_separator_menu_item_new());

            option_text =
                tm_remove_underscore(_(tm_toolbar_options[i].text));
            text =
                g_strdup_printf(_("Use Desktop _Default (%s)"),
                                option_text);
            g_free(option_text);

            item = gtk_radio_menu_item_new_with_mnemonic(group, text);
            g_free(text);

            if (info->model->style == (GtkToolbarStyle) (-1))
                gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM
                                               (item), TRUE);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
            g_object_set_data(G_OBJECT(item), BALSA_TOOLBAR_STYLE,
                              GINT_TO_POINTER(-1));
            g_signal_connect(item, "toggled",
                             G_CALLBACK(menu_item_toggled_cb), info);
        }
    }

    if (gtk_widget_is_sensitive(toolbar)) {
        /* This is a real toolbar, not the template from the
         * toolbar-prefs dialog. */
        GtkWidget *item;

        gtk_menu_shell_append(GTK_MENU_SHELL(menu),
                              gtk_separator_menu_item_new());
        item =
            gtk_menu_item_new_with_mnemonic(_("_Customize Toolbars..."));
        g_signal_connect(item, "activate", G_CALLBACK(customize_dialog_cb),
                         gtk_widget_get_toplevel(toolbar));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

        /* Pass the model type to the customize widget, so that it can
         * show the appropriate notebook page. */
        g_object_set_data(G_OBJECT(item), BALSA_TOOLBAR_MODEL_TYPE,
                          GINT_TO_POINTER(info->model->type));
    }

    gtk_widget_show_all(menu);

    if (event) {
        button = event->button;
        event_time = event->time;
    } else {
        button = 0;
        event_time = gtk_get_current_event_time();
    }

    gtk_menu_attach_to_widget(GTK_MENU(menu), toolbar, NULL);
    if (button)
        gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, button,
                       event_time);
    else
        gtk_menu_popup(GTK_MENU(menu), NULL, NULL, tm_popup_position_func,
                       toolbar, button, event_time);

    return TRUE;
}

static gboolean
tm_button_press_cb(GtkWidget * toolbar, GdkEventButton * event,
                   toolbar_info * info)
{
    /* Ignore double-clicks and triple-clicks */
    if (event->button == 3 && event->type == GDK_BUTTON_PRESS)
        return tm_do_popup_menu(toolbar, event, info);

    return FALSE;
}

static gboolean
tm_popup_menu_cb(GtkWidget * toolbar, toolbar_info * info)
{
    return tm_do_popup_menu(toolbar, NULL, info);
}

static void
tm_realize_cb(GtkWidget * toolbar, BalsaToolbarModel * model)
{
    tm_set_style(toolbar, model);
}

GtkWidget *balsa_toolbar_new(BalsaToolbarModel * model,
                             GActionMap        * action_map)
{
    toolbar_info *info;
    GtkWidget *toolbar;

    info = g_new(toolbar_info, 1);
    info->model = model;
    info->menu = NULL;

    toolbar = gtk_toolbar_new();
    g_object_set_data_full(G_OBJECT(toolbar), BALSA_TOOLBAR_ACTION_MAP,
                           g_object_ref(action_map),
                           (GDestroyNotify) g_object_unref);
    tm_populate(toolbar, model);

    g_signal_connect(model, "changed", G_CALLBACK(tm_changed_cb), toolbar);
    g_signal_connect(toolbar, "realize", G_CALLBACK(tm_realize_cb), model);
    g_object_weak_ref(G_OBJECT(toolbar),
                      (GWeakNotify) tm_toolbar_weak_notify, info);

    g_signal_connect(toolbar, "button-press-event",
                     G_CALLBACK(tm_button_press_cb), info);
    g_signal_connect(toolbar, "popup-menu", G_CALLBACK(tm_popup_menu_cb),
                     info);

    gtk_widget_show_all(toolbar);

    return toolbar;
}
