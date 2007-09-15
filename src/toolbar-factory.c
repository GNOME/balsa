/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2001 Stuart Parmenter and others,
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

#include "config.h"

#include <string.h>
#include <gnome.h>
#include "i18n.h"

#include "balsa-app.h"
#include "balsa-icons.h"
#include "main-window.h"
#include "message-window.h"
#include "sendmsg-window.h"

#include "toolbar-prefs.h"
#include "toolbar-factory.h"

/*
 * The BalsaToolbarModel class.
 */

struct BalsaToolbarModel_ {
    GObject     object;
    GHashTable *legal;
    GSList     *standard;
    GSList    **current;
};

enum {
    CHANGED,
    LAST_SIGNAL
};

static GtkObjectClass* parent_class;
static guint model_signals[LAST_SIGNAL] = { 0 };

static void
balsa_toolbar_model_finalize(GObject * object)
{
    BalsaToolbarModel *model = BALSA_TOOLBAR_MODEL(object);
    g_hash_table_destroy(model->legal);
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
        g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_free);
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
    {"", N_("Separator")},
    {BALSA_PIXMAP_RECEIVE, N_("Check")},
    {BALSA_PIXMAP_COMPOSE, N_("Compose")},
    {BALSA_PIXMAP_CONTINUE, N_("Continue")},
    {BALSA_PIXMAP_REPLY, N_("Reply")},
    {BALSA_PIXMAP_REPLY_ALL, N_("Reply\nto all")},
    {BALSA_PIXMAP_REPLY_GROUP, N_("Reply\nto group")},
    {BALSA_PIXMAP_FORWARD, N_("Forward")},
    {BALSA_PIXMAP_PREVIOUS, N_("Previous")},
    {BALSA_PIXMAP_NEXT, N_("Next")},
    {BALSA_PIXMAP_NEXT_UNREAD, N_("Next\nunread")},
    {BALSA_PIXMAP_NEXT_FLAGGED, N_("Next\nflagged")},
    {BALSA_PIXMAP_PREVIOUS_PART, N_("Previous\npart")},
    {BALSA_PIXMAP_NEXT_PART, N_("Next\npart")},
    {GTK_STOCK_DELETE, N_("Trash /\nDelete")},
    {BALSA_PIXMAP_POSTPONE, N_("Postpone")},
    {GTK_STOCK_PRINT, N_("Print")},
    {BALSA_PIXMAP_SEND, N_("Send")},
    {BALSA_PIXMAP_SEND_RECEIVE, N_("Exchange")},
    {BALSA_PIXMAP_ATTACHMENT, N_("Attach")},
    {GTK_STOCK_SAVE, N_("Save")},
    {BALSA_PIXMAP_IDENTITY, N_("Identity")},
    {GTK_STOCK_SPELL_CHECK, N_("Spelling")},
    {GTK_STOCK_CLOSE, N_("Close")},
    {BALSA_PIXMAP_MARKED_NEW, N_("Toggle\nnew")},
    {BALSA_PIXMAP_MARK_ALL, N_("Mark all")},
    {BALSA_PIXMAP_SHOW_HEADERS, N_("All\nheaders")},
    {BALSA_PIXMAP_TRASH_EMPTY, N_("Empty Trash")},
    {GTK_STOCK_CANCEL, N_("Close")},
    {BALSA_PIXMAP_SHOW_PREVIEW, N_("Msg Preview")},
#ifdef HAVE_GPGME
    {BALSA_PIXMAP_GPG_SIGN, N_("Sign")},
    {BALSA_PIXMAP_GPG_ENCRYPT, N_("Encrypt")},
#endif
    {GTK_STOCK_UNDO, N_("Undo")},
    {GTK_STOCK_REDO, N_("Redo")},
    {GTK_STOCK_CLEAR, N_("Expunge\nDeleted")},
};

const int toolbar_button_count =
    sizeof(toolbar_buttons) / sizeof(button_data);

/* Public methods. */
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
void
balsa_toolbar_remove_all(GtkWidget * widget)
{
    GList *child, *children;
    
    children = gtk_container_get_children(GTK_CONTAINER(widget));
    for (child = children; child; child = child->next)
        gtk_widget_destroy(child->data);
    g_list_free(children);
}

/* Create a BalsaToolbarModel structure.
 */
BalsaToolbarModel *
balsa_toolbar_model_new(GSList * standard, GSList ** current)
{
    BalsaToolbarModel *model = g_object_new(BALSA_TYPE_TOOLBAR_MODEL, NULL);

    model->standard = standard;
    model->current = current;

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

typedef struct {
    const gchar *name;
    const gchar *tooltip;
} ActionInfo;

static void
tm_add_action(BalsaToolbarModel * model, const gchar * stock_id,
              const gchar * name, const gchar * tooltip)
{
    /* Check whether we have already seen this icon: */
    if (stock_id && !g_hash_table_lookup(model->legal, stock_id)) {
        ActionInfo *info = g_new(ActionInfo, 1);
        info->name = name;
        info->tooltip = tooltip;
        g_hash_table_insert(model->legal, (gchar *) stock_id, info);
    }
}

void
balsa_toolbar_model_add_actions(BalsaToolbarModel * model,
                                const GtkActionEntry * entries,
                                guint n_entries)
{
    guint i;

    for (i = 0; i < n_entries; i++)
        tm_add_action(model, entries[i].stock_id, entries[i].name,
                      entries[i].tooltip);
}

void
balsa_toolbar_model_add_toggle_actions(BalsaToolbarModel * model,
                                       const GtkToggleActionEntry *
                                       entries, guint n_entries)
{
    guint i;

    for (i = 0; i < n_entries; i++)
        tm_add_action(model, entries[i].stock_id, entries[i].name,
                      entries[i].tooltip);
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
GSList *
balsa_toolbar_model_get_current(BalsaToolbarModel * model)
{
    return *model->current ? *model->current : model->standard;
}

gboolean
balsa_toolbar_model_is_standard(BalsaToolbarModel * model)
{
    return *model->current == NULL;
}

/* Add an icon to the list of current icons in a BalsaToolbarModel.
 */
void
balsa_toolbar_model_insert_icon(BalsaToolbarModel * model, gchar * icon,
                                gint position)
{
    const gchar* real_button = balsa_toolbar_sanitize_id(icon);

    if (real_button)
        *model->current =
            g_slist_insert(*model->current, g_strdup(real_button), position);
    else
        g_warning(_("Unknown toolbar icon \"%s\""), icon);
}

/* Remove all icons from the BalsaToolbarModel.
 */
void 
balsa_toolbar_model_clear(BalsaToolbarModel * model)
{
    g_slist_foreach(*model->current, (GFunc) g_free, NULL);
    g_slist_free(*model->current);
    *model->current = NULL;
}

/* Create a new instance of a toolbar
 */

static gboolean
tm_has_second_line(BalsaToolbarModel * model)
{
    GSList *list;

    /* Find out whether any button has 2 lines of text. */
    for (list = balsa_toolbar_model_get_current(model); list; list = list->next) {
        const gchar *icon = list->data;
        gint button = get_toolbar_button_index(icon);

        if (button < 0) {
            g_warning("button '%s' not found. ABORT!\n", icon);
            continue;
        }

        if (strchr(_(toolbar_buttons[button].button_text), '\n'))
            return TRUE;
    }

    return FALSE;
}

static gint
tm_set_tool_item_label(GtkToolButton * tool_item, const gchar * stock_id,
                       gboolean has_second_line)
{
    gint button = get_toolbar_button_index(stock_id);
    gchar *tmp, *text;

    if (button < 0)
        return button;

    tmp = _(toolbar_buttons[button].button_text);
    if (strcmp(toolbar_buttons[button].pixmap_id, BALSA_PIXMAP_SEND) == 0
        && balsa_app.always_queue_sent_mail)
        tmp = _("Queue");
    if (balsa_app.toolbar_wrap_button_text) {
        /* Make sure all buttons have the same number of lines of
         * text (1 or 2), to keep icons aligned */
        text = has_second_line && !strchr(tmp, '\n') ?
            g_strconcat(tmp, "\n", NULL) : g_strdup(tmp);
    } else {
        text = tmp = g_strdup(tmp);
        while ((tmp = strchr(tmp, '\n')))
            *tmp++ = ' ';
    }

    gtk_tool_button_set_label(tool_item, text);
    g_free(text);

    return button;
}

static void
tm_populate(BalsaToolbarModel * model, GtkUIManager * ui_manager,
            GArray * merge_ids)
{
    gboolean has_second_line = tm_has_second_line(model);
    GSList *list;

    for (list = balsa_toolbar_model_get_current(model); list;
         list = list->next) {
        const gchar *stock_id = list->data;
        guint merge_id = gtk_ui_manager_new_merge_id(ui_manager);

        g_array_append_val(merge_ids, merge_id);

        if (!*stock_id) {
#if GTK_CHECK_VERSION(2, 11, 6)
            gtk_ui_manager_add_ui(ui_manager, merge_id, "/Toolbar",
                                  NULL, NULL, GTK_UI_MANAGER_SEPARATOR,
                                  FALSE);
#else                           /* GTK_CHECK_VERSION(2, 11, 6) */
            gchar *name = g_strdup_printf("Separator%d", merge_id);
            gtk_ui_manager_add_ui(ui_manager, merge_id, "/Toolbar",
                                  name, NULL, GTK_UI_MANAGER_SEPARATOR,
                                  FALSE);
            g_free(name);
#endif                          /* GTK_CHECK_VERSION(2, 11, 6) */
        } else {
            gchar *path;
            GtkWidget *tool_item;
            ActionInfo *info;

            info = g_hash_table_lookup(model->legal, stock_id);
            if (!info) {
                g_warning("no info for stock_id \"%s\"", stock_id);
                continue;
            }
            gtk_ui_manager_add_ui(ui_manager, merge_id, "/Toolbar",
                                  info->name, info->name,
                                  GTK_UI_MANAGER_AUTO, FALSE);
            /* Replace the long menu-item label with the short
             * tool-button label: */
            path = g_strconcat("/Toolbar/", info->name, NULL);
            tool_item = gtk_ui_manager_get_widget(ui_manager, path);
            g_free(path);
            tm_set_tool_item_label(GTK_TOOL_BUTTON(tool_item), stock_id, has_second_line);
        }
    }
}

#define BALSA_TOOLBAR_MERGE_IDS "balsa-toolbar-merge-ids"
static void
bt_free_merge_ids(GArray * merge_ids)
{
     g_array_free(merge_ids, TRUE);
}

/* Update a real toolbar when the model has changed.
 */
static void
tm_changed_cb(BalsaToolbarModel * model, GtkUIManager * ui_manager)
{
    GArray *merge_ids =
        g_object_get_data(G_OBJECT(ui_manager), BALSA_TOOLBAR_MERGE_IDS);
    guint i;

    for (i = 0; i < merge_ids->len; i++) {
        guint merge_id = g_array_index(merge_ids, guint, i);
        gtk_ui_manager_remove_ui(ui_manager, merge_id);
    }
    merge_ids->len = 0;

    tm_populate(model, ui_manager, merge_ids);
}

typedef struct {
    BalsaToolbarModel * model;
    GtkUIManager * ui_manager;
} toolbar_info;

static void
tm_toolbar_weak_notify(toolbar_info * info, GtkWidget * toolbar)
{
    g_signal_handlers_disconnect_by_func(info->model, tm_changed_cb,
                                         info->ui_manager);
    g_object_unref(info->ui_manager);
    g_free(info);
}

GtkWidget *
balsa_toolbar_new(BalsaToolbarModel * model, GtkUIManager * ui_manager)
{
    GtkWidget *toolbar;
    toolbar_info *info;
    GArray *merge_ids = g_array_new(FALSE, FALSE, sizeof(guint));

    g_object_set_data_full(G_OBJECT(ui_manager), BALSA_TOOLBAR_MERGE_IDS,
                           merge_ids, (GDestroyNotify) bt_free_merge_ids);

    tm_populate(model, ui_manager, merge_ids);
    toolbar = gtk_ui_manager_get_widget(ui_manager, "/Toolbar");

    g_signal_connect(model, "changed", G_CALLBACK(tm_changed_cb),
                     ui_manager);

    info = g_new(toolbar_info, 1);
    info->model = model;
    info->ui_manager = g_object_ref(ui_manager);
    g_object_weak_ref(G_OBJECT(toolbar),
                      (GWeakNotify) tm_toolbar_weak_notify, info);

    return toolbar;
}
