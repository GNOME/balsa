/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "balsa-message.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/utsname.h>

#include "balsa-app.h"
#include "balsa-icons.h"
#include "mime.h"
#include "misc.h"
#include "html.h"
#include <glib/gi18n.h>
#include "balsa-mime-widget.h"
#include "balsa-mime-widget-callbacks.h"
#include "balsa-mime-widget-message.h"
#include "balsa-mime-widget-image.h"
#include "balsa-mime-widget-text.h"
#include "balsa-mime-widget-crypto.h"
#include "libbalsa-gpgme.h"
#include "autocrypt.h"
#include "dkim.h"

#include <gdk/gdkkeysyms.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "send.h"
#include "quote-color.h"
#include "sendmsg-window.h"
#include "libbalsa-vfs.h"

#include "gmime-part-rfc2440.h"

#  define FORWARD_SEARCH(iter, text, match_begin, match_end)            \
    gtk_text_iter_forward_search((iter), (text),                        \
    GTK_TEXT_SEARCH_CASE_INSENSITIVE, (match_begin), (match_end), NULL)
#  define BACKWARD_SEARCH(iter, text, match_begin, match_end)           \
    gtk_text_iter_backward_search((iter), (text),                       \
    GTK_TEXT_SEARCH_CASE_INSENSITIVE, (match_begin), (match_end), NULL)

enum {
    SELECT_PART,
    LAST_SIGNAL,
};

enum {
    PART_INFO_COLUMN = 0,
    PART_NUM_COLUMN,
    MIME_ICON_COLUMN,
    MIME_TYPE_COLUMN,
    NUM_COLUMNS
};

G_DECLARE_FINAL_TYPE(BalsaPartInfo, balsa_part_info, BALSA, PART_INFO, GObject)

struct _BalsaPartInfo {
    GObject parent_object;

    LibBalsaMessageBody *body;

    /* MIME widget */
    BalsaMimeWidget *mime_widget;

    /* The contect menu; referenced */
    GtkWidget *popup_menu;

    /* the path in the tree view */
    GtkTreePath *path;
};

#define TYPE_BALSA_PART_INFO (balsa_part_info_get_type())

static gint balsa_message_signals[LAST_SIGNAL];

/* widget */
static void balsa_message_destroy(GObject * object);

static void display_headers(BalsaMessage * balsa_message);
static void display_content(BalsaMessage * balsa_message);

static LibBalsaMessageBody *add_part(BalsaMessage *balsa_message, BalsaPartInfo *info,
                                     GtkWidget * container);
static LibBalsaMessageBody *add_multipart(BalsaMessage * balsa_message,
                                          LibBalsaMessageBody * parent,
                                          GtkWidget * container);
static void select_part(BalsaMessage * balsa_message, BalsaPartInfo *info);
static void tree_activate_row_cb(GtkTreeView *treeview, GtkTreePath *arg1,
                                 GtkTreeViewColumn *arg2, gpointer user_data);
static gboolean tree_menu_popup_key_cb(GtkWidget *widget, gpointer user_data);
static void tree_button_press_cb(GtkGestureMultiPress *multi_press_gesture,
                                 gint                  n_press,
                                 gdouble               x,
                                 gdouble               y,
                                 gpointer              user_data);

static void part_info_init(BalsaMessage * balsa_message, BalsaPartInfo * info);
static void part_context_save_all_cb(GtkWidget * menu_item, GList * info_list);
static void part_context_dump_all_cb(GtkMenuItem *self, gpointer user_data);
static void part_create_menu (BalsaPartInfo* info);

/* stuff needed for sending Message Disposition Notifications */
static void handle_mdn_request(GtkWindow *parent, LibBalsaMessage *message,
                               LibBalsaMessageHeaders *headers);
static LibBalsaMessage *create_mdn_reply (const LibBalsaIdentity *mdn_ident,
                                          LibBalsaMessage *for_msg,
                                          gboolean manual);
static GtkWidget* create_mdn_dialog (GtkWindow *parent, gchar *sender,
				     gchar *mdn_to_address,
                                     LibBalsaMessage *send_msg,
                                     LibBalsaIdentity *mdn_ident);
static void mdn_dialog_response(GtkWidget * dialog, gint response,
                                gpointer user_data);

static BalsaPartInfo* balsa_part_info_new(LibBalsaMessageBody* body);
static void balsa_part_info_dispose(GObject * object);
static void balsa_part_info_finalize(GObject * object);

static guint bm_scan_signatures(LibBalsaMessageBody *body,
							     LibBalsaMessage * message);
static GdkPixbuf * get_crypto_content_icon(LibBalsaMessageBody * body,
					   const gchar * content_type,
					   gchar ** icon_title);

G_DEFINE_TYPE(BalsaPartInfo, balsa_part_info, G_TYPE_OBJECT)

static void
balsa_part_info_class_init(BalsaPartInfoClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = balsa_part_info_dispose;
    object_class->finalize = balsa_part_info_finalize;
}

struct _BalsaMessage {
        GtkBox parent;

        GtkWidget *stack;
        GtkWidget *switcher;

        /* Top-level MIME widget */
        BalsaMimeWidget *bm_widget;

	/* header-related information */
	ShownHeaders shown_headers;

	/* Widgets to hold content */
	GtkWidget *scroll;

        /* Widget to hold structure tree */
        GtkWidget *treeview;
        gint info_count;
        GList *save_all_list;
        GtkWidget *save_all_popup;

	gboolean wrap_text;

        BalsaPartInfo *current_part;
        GtkWidget *parts_popup;
        gboolean force_inline;

	LibBalsaMessage *message;

        BalsaMessageFocusState focus_state;

        /* Find-in-message stuff */
        GtkWidget  *find_bar;
        GtkWidget  *find_entry;
        GtkWidget  *find_next;
        GtkWidget  *find_prev;
        GtkWidget  *find_sep;
        GtkWidget  *find_label;
        GtkTextIter find_iter;
        gboolean    find_forward;
        GtkEventController *find_key_controller;

        /* Widget to hold Faces */
        GtkWidget *face_box;

#ifdef HAVE_HTML_WIDGET
        gpointer html_find_info;
#endif				/* HAVE_HTML_WIDGET */

        GtkWidget *attach_button;
};

G_DEFINE_TYPE(BalsaMessage, balsa_message, GTK_TYPE_BOX)

static void
balsa_message_class_init(BalsaMessageClass * klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS(klass);

    balsa_message_signals[SELECT_PART] =
        g_signal_new("select-part",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL, NULL,
                     NULL,
                     G_TYPE_NONE, 0);

    object_class->dispose = balsa_message_destroy;
}

/* Helpers for balsa_message_init. */

static void
balsa_headers_attachments_popup(GtkButton * button, BalsaMessage * balsa_message)
{
    if (balsa_message->parts_popup) {
        gtk_menu_popup_at_widget(GTK_MENU(balsa_message->parts_popup),
                                 GTK_WIDGET(balsa_message->attach_button),
                                 GDK_GRAVITY_CENTER, GDK_GRAVITY_CENTER,
                                 NULL);
    }
}


/* Note: this function returns a NULL-terminated array of buttons for a top-level headers widget.  Currently, we return just a
 * single item (the button for showing the menu for switching between attachments) so we /could/ change the return type to
 * GtkWidget *. */
static GtkWidget **
bm_header_tl_buttons(BalsaMessage * balsa_message)
{
    GPtrArray *array;
    GtkWidget *button;
    GtkEventController *key_controller;

    array = g_ptr_array_new();

    balsa_message->attach_button = button =
        gtk_button_new_from_icon_name(balsa_icon_id(BALSA_PIXMAP_ATTACHMENT),
                                      GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(button,
			        _("Select message part to display"));

    key_controller = gtk_event_controller_key_new(button);
    g_signal_connect(key_controller, "focus-in",
		     G_CALLBACK(balsa_mime_widget_limit_focus), balsa_message);
    g_signal_connect(key_controller, "focus-out",
		     G_CALLBACK(balsa_mime_widget_unlimit_focus), balsa_message);
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    g_signal_connect(button, "clicked",
		     G_CALLBACK(balsa_headers_attachments_popup), balsa_message);

    key_controller = gtk_event_controller_key_new(button);
    g_signal_connect(key_controller, "key-pressed",
		     G_CALLBACK(balsa_mime_widget_key_pressed), balsa_message);

    g_ptr_array_add(array, button);

    g_ptr_array_add(array, NULL);

    return (GtkWidget **) g_ptr_array_free(array, FALSE);
}


/*
 * Callbacks and helpers for the find bar.
 */

typedef enum {
    BM_FIND_STATUS_INIT,
    BM_FIND_STATUS_FOUND,
    BM_FIND_STATUS_WRAPPED,
    BM_FIND_STATUS_NOT_FOUND
} BalsaMessageFindStatus;

static void
bm_find_set_status(BalsaMessage * balsa_message, BalsaMessageFindStatus status)
{
    const gchar *text = "";
    gboolean sensitive = FALSE;

    switch (status) {
        default:
        case BM_FIND_STATUS_INIT:
            break;
        case BM_FIND_STATUS_FOUND:
            /* The widget returned "found"; if it really found a string,
             * we sensitize the "next" and "previous" buttons, but if
             * the find-entry was empty, we desensitize them. */
            if (gtk_entry_get_text(GTK_ENTRY(balsa_message->find_entry))[0])
                sensitive = TRUE;
            break;
        case BM_FIND_STATUS_WRAPPED:
            text = _("Wrapped");
            sensitive = TRUE;
            break;
        case BM_FIND_STATUS_NOT_FOUND:
            text = _("Not found");
            break;
    }

    gtk_label_set_text(GTK_LABEL(balsa_message->find_label), text);
    gtk_separator_tool_item_set_draw(GTK_SEPARATOR_TOOL_ITEM
                                     (balsa_message->find_sep), text[0] != '\0');
    gtk_widget_set_sensitive(balsa_message->find_prev, sensitive);
    gtk_widget_set_sensitive(balsa_message->find_next, sensitive);
}

static void
bm_find_scroll_to_rectangle(BalsaMessage * balsa_message,
                            GtkWidget    * widget,
                            GdkRectangle * rectangle)
{
    gint x, y;
    GtkAdjustment *adj;
    GtkScrolledWindow *scroll = GTK_SCROLLED_WINDOW(balsa_message->scroll);

    gtk_widget_translate_coordinates(widget,
                                     GTK_WIDGET(balsa_message->bm_widget),
                                     rectangle->x, rectangle->y,
                                     &x, &y);

    adj = gtk_scrolled_window_get_hadjustment(scroll);
    gtk_adjustment_clamp_page(adj, x, x + rectangle->width);
    adj = gtk_scrolled_window_get_vadjustment(scroll);
    gtk_adjustment_clamp_page(adj, y, y + rectangle->height);
}

static void
bm_find_scroll_to_selection(BalsaMessage * balsa_message,
                            GtkTextView  * text_view,
                            GtkTextIter  * begin_iter,
                            GtkTextIter  * end_iter)
{
    GdkRectangle begin_location, end_location;

    gtk_text_view_get_iter_location(text_view, begin_iter,
                                    &begin_location);
    gtk_text_view_get_iter_location(text_view, end_iter,
                                    &end_location);
    end_location.width = 0;
    gdk_rectangle_union(&begin_location, &end_location, &begin_location);
    gtk_text_view_buffer_to_window_coords(text_view,
                                          GTK_TEXT_WINDOW_WIDGET,
                                          begin_location.x,
                                          begin_location.y,
                                          &begin_location.x,
                                          &begin_location.y);

    bm_find_scroll_to_rectangle(balsa_message, GTK_WIDGET(text_view), &begin_location);
}

#ifdef HAVE_HTML_WIDGET
typedef struct {
    BalsaMessage *balsa_message;
    GtkWidget    *widget;
    gboolean      continuing;
    gboolean      wrapping;
} BalsaMessageFindInfo;
#define BALSA_MESSAGE_FIND_INFO "BalsaMessageFindInfo"

static void
bm_find_cb(const gchar * text, gboolean found, gpointer data)
{
    BalsaMessageFindInfo *info = data;

    if (!found && info->continuing) {
        info->wrapping = TRUE;
        libbalsa_html_search(info->widget, text, info->balsa_message->find_forward,
                             TRUE, bm_find_cb, info);
        return;
    }

    if (found && *text) {
        GdkRectangle selection_bounds;
        if (libbalsa_html_get_selection_bounds(info->widget,
                                               &selection_bounds))
            bm_find_scroll_to_rectangle(info->balsa_message, info->widget,
                                        &selection_bounds);
    }

    if (info->wrapping) {
        info->wrapping = FALSE;
        bm_find_set_status(info->balsa_message, BM_FIND_STATUS_WRAPPED);
    } else
        bm_find_set_status(info->balsa_message, found ? BM_FIND_STATUS_FOUND :
                                             BM_FIND_STATUS_NOT_FOUND);
}
#endif                          /* HAVE_HTML_WIDGET */

static void
bm_find_entry_changed_cb(GtkEditable * editable, gpointer data)
{
    BalsaMessage *balsa_message = data;
    const gchar *text;
    BalsaMimeWidget *mime_widget;
    GtkWidget *widget;

    if (balsa_message->current_part == NULL)
        return;

    mime_widget = balsa_message->current_part->mime_widget;
    widget = balsa_mime_widget_text_get_text_widget(BALSA_MIME_WIDGET_TEXT(mime_widget));
    text = gtk_entry_get_text(GTK_ENTRY(editable));

    if (GTK_IS_TEXT_VIEW(widget)) {
        gboolean found = FALSE;
        GtkTextView *text_view = GTK_TEXT_VIEW(widget);
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
        GtkTextIter match_begin, match_end;

        if (balsa_message->find_forward) {
            found = FORWARD_SEARCH(&balsa_message->find_iter, text,
                                   &match_begin, &match_end);
            if (!found) {
                /* Silently wrap to the top. */
                gtk_text_buffer_get_start_iter(buffer, &balsa_message->find_iter);
                found = FORWARD_SEARCH(&balsa_message->find_iter, text,
                                       &match_begin, &match_end);
            }
        } else {
            found = BACKWARD_SEARCH(&balsa_message->find_iter, text,
                                    &match_begin, &match_end);
            if (!found) {
                /* Silently wrap to the bottom. */
                gtk_text_buffer_get_end_iter(buffer, &balsa_message->find_iter);
                found = BACKWARD_SEARCH(&balsa_message->find_iter, text,
                                        &match_begin, &match_end);
            }
        }

        if (found) {
            gtk_text_buffer_select_range(buffer, &match_begin, &match_end);
            bm_find_scroll_to_selection(balsa_message, text_view,
                                        &match_begin, &match_end);
            balsa_message->find_iter = match_begin;
        }

        bm_find_set_status(balsa_message, found ? BM_FIND_STATUS_FOUND :
                                       BM_FIND_STATUS_NOT_FOUND);
#ifdef HAVE_HTML_WIDGET
    } else if (libbalsa_html_can_search(widget)) {
        BalsaMessageFindInfo *info;

        if (!(info = balsa_message->html_find_info)) {
            balsa_message->html_find_info = info = g_new(BalsaMessageFindInfo, 1);
            info->balsa_message = balsa_message;
        }
        info->widget = widget;
        info->continuing = FALSE;
        info->wrapping = FALSE;

        libbalsa_html_search(widget, text, balsa_message->find_forward, TRUE,
                             bm_find_cb, info);
#endif                          /* HAVE_HTML_WIDGET */
    } else
        g_assert_not_reached();
}

static void
bm_find_again(BalsaMessage * balsa_message, gboolean find_forward)
{
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(balsa_message->find_entry));
    BalsaMimeWidget *mime_widget = balsa_message->current_part->mime_widget;
    GtkWidget *widget = balsa_mime_widget_text_get_text_widget(BALSA_MIME_WIDGET_TEXT(mime_widget));
    gboolean found;

    balsa_message->find_forward = find_forward;

    if (GTK_IS_TEXT_VIEW(widget)) {
        GtkTextView *text_view = GTK_TEXT_VIEW(widget);
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
        GtkTextIter match_begin, match_end;

        if (find_forward) {
            gtk_text_iter_forward_char(&balsa_message->find_iter);
            found = FORWARD_SEARCH(&balsa_message->find_iter, text,
                                   &match_begin, &match_end);
            if (!found) {
                gtk_text_buffer_get_start_iter(buffer, &balsa_message->find_iter);
                FORWARD_SEARCH(&balsa_message->find_iter, text, &match_begin,
                               &match_end);
            }
        } else {
            gtk_text_iter_backward_char(&balsa_message->find_iter);
            found = BACKWARD_SEARCH(&balsa_message->find_iter, text,
                                    &match_begin, &match_end);
            if (!found) {
                gtk_text_buffer_get_end_iter(buffer, &balsa_message->find_iter);
                BACKWARD_SEARCH(&balsa_message->find_iter, text, &match_begin,
                                &match_end);
            }
        }

        gtk_text_buffer_select_range(buffer, &match_begin, &match_end);
        bm_find_scroll_to_selection(balsa_message, text_view,
                                    &match_begin, &match_end);
        balsa_message->find_iter = match_begin;

        bm_find_set_status(balsa_message, found ?
                           BM_FIND_STATUS_FOUND : BM_FIND_STATUS_WRAPPED);
#ifdef HAVE_HTML_WIDGET
    } else if (libbalsa_html_can_search(widget)) {
        BalsaMessageFindInfo *info = balsa_message->html_find_info;

        info->continuing = TRUE;
        libbalsa_html_search(widget, text, find_forward, FALSE,
                             bm_find_cb, info);
#endif                          /* HAVE_HTML_WIDGET */
    } else
        g_assert_not_reached();
}

static void
bm_find_prev_cb(GtkToolButton * prev_button, gpointer data)
{
    bm_find_again((BalsaMessage *) data, FALSE);
}

static void
bm_find_next_cb(GtkToolButton * prev_button, gpointer data)
{
    bm_find_again((BalsaMessage *) data, TRUE);
}

static GtkWidget *
bm_find_bar_new(BalsaMessage * balsa_message)
{
    GtkWidget *toolbar;
    GtkWidget *hbox;
    GtkWidget *toplevel;
    GtkToolItem *tool_item;
    GtkWidget *image;
    GtkWidget *search_bar;

    toolbar = gtk_toolbar_new();
    gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_BOTH_HORIZ);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_add(GTK_CONTAINER(hbox), gtk_label_new(_("Find:")));
    balsa_message->find_entry = gtk_search_entry_new();
    g_signal_connect(balsa_message->find_entry, "search-changed",
                     G_CALLBACK(bm_find_entry_changed_cb), balsa_message);
    gtk_container_add(GTK_CONTAINER(hbox), balsa_message->find_entry);

    toplevel = gtk_widget_get_toplevel(GTK_WIDGET(balsa_message));
    balsa_message->find_key_controller = gtk_event_controller_key_new(toplevel);
    gtk_event_controller_set_propagation_phase(balsa_message->find_key_controller,
                                               GTK_PHASE_CAPTURE);

    tool_item = gtk_tool_item_new();
    gtk_container_add(GTK_CONTAINER(tool_item), hbox);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), tool_item, -1);

    image = gtk_image_new_from_icon_name("pan-up-symbolic", GTK_ICON_SIZE_BUTTON);
    tool_item = gtk_tool_button_new(image, _("Previous"));
    balsa_message->find_prev = GTK_WIDGET(tool_item);
    gtk_tool_item_set_is_important(tool_item, TRUE);
    g_signal_connect(tool_item, "clicked", G_CALLBACK(bm_find_prev_cb), balsa_message);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), tool_item, -1);

    image = gtk_image_new_from_icon_name("pan-down-symbolic", GTK_ICON_SIZE_BUTTON);
    tool_item = gtk_tool_button_new(image, _("Next"));
    balsa_message->find_next = GTK_WIDGET(tool_item);
    gtk_tool_item_set_is_important(tool_item, TRUE);
    g_signal_connect(tool_item, "clicked", G_CALLBACK(bm_find_next_cb), balsa_message);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), tool_item, -1);

    balsa_message->find_sep = GTK_WIDGET(gtk_separator_tool_item_new());
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), GTK_TOOL_ITEM(balsa_message->find_sep), -1);

    balsa_message->find_label = gtk_label_new("");
    tool_item = gtk_tool_item_new();
    gtk_container_add(GTK_CONTAINER(tool_item), balsa_message->find_label);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), tool_item, -1);

    search_bar = gtk_search_bar_new();
    gtk_search_bar_set_search_mode(GTK_SEARCH_BAR(search_bar), FALSE);
    gtk_search_bar_set_show_close_button(GTK_SEARCH_BAR(search_bar), TRUE);
    gtk_search_bar_connect_entry(GTK_SEARCH_BAR(search_bar),
                                 GTK_ENTRY(balsa_message->find_entry));
    gtk_container_add(GTK_CONTAINER(search_bar), toolbar);

    return search_bar;
}

static void bm_disable_find_entry(BalsaMessage * balsa_message);

static gboolean
bm_find_pass_to_entry(GtkEventControllerKey *key_controller,
                      guint                  keyval,
                      guint                  keycode,
                      GdkModifierType        state,
                      gpointer               user_data)
{
    BalsaMessage *balsa_message = user_data;
    gboolean res = TRUE;

    switch (keyval) {
    case GDK_KEY_Escape:
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
        bm_disable_find_entry(balsa_message);
        return res;
    case GDK_KEY_g:
        if ((state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK)) == GDK_CONTROL_MASK &&
            gtk_widget_get_sensitive(balsa_message->find_next)) {
            bm_find_again(balsa_message, balsa_message->find_forward);
            return res;
        }
        /* else fall through */
    default:
        break;
    }

    res = FALSE;
    if (gtk_widget_has_focus(balsa_message->find_entry)) {
        GtkSearchEntry *search_entry = GTK_SEARCH_ENTRY(balsa_message->find_entry);
        GdkEvent *event = gtk_get_current_event();
        res = gtk_search_entry_handle_event(search_entry, event);
    }

    return res;
}

static void
bm_disable_find_entry(BalsaMessage * balsa_message)
{
    g_signal_handlers_disconnect_by_func(balsa_message->find_key_controller,
                                         G_CALLBACK(bm_find_pass_to_entry),
                                         balsa_message);
    gtk_search_bar_set_search_mode(GTK_SEARCH_BAR(balsa_message->find_bar), FALSE);
}

/*
 * End of callbacks and helpers for the find bar.
 */

static void
balsa_message_init(BalsaMessage * balsa_message)
{
    GtkStack  *stack;
    GtkWidget *vbox;
    GtkWidget *scroll;
    GtkWidget *viewport;
    GtkWidget **buttons;
    GtkTreeStore *model;
    GtkCellRenderer *renderer;
    GtkTreeSelection *selection;
    GtkEventController *key_controller;
    GtkGesture *gesture;

    balsa_message->switcher = gtk_stack_switcher_new();
    gtk_container_add(GTK_CONTAINER(balsa_message), balsa_message->switcher);

    balsa_message->stack = gtk_stack_new();
    stack = GTK_STACK(balsa_message->stack);
    gtk_stack_set_transition_type(stack,
                                  GTK_STACK_TRANSITION_TYPE_SLIDE_UP_DOWN);
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(balsa_message->switcher), stack);

    gtk_widget_set_vexpand(balsa_message->stack, TRUE);
    gtk_widget_set_valign(balsa_message->stack, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(balsa_message), balsa_message->stack);

    /* Box to hold the scrolled window and the find bar */
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_stack_add_titled(stack, vbox, "content", _("Content"));

    /* scrolled window for the contents */
    balsa_message->scroll = scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);

    key_controller = gtk_event_controller_key_new(scroll);
    g_signal_connect(key_controller, "key-pressed",
		     G_CALLBACK(balsa_mime_widget_key_pressed), balsa_message);

    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_valign(scroll, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(vbox), scroll);

    /* Widget to hold headers */
    buttons = bm_header_tl_buttons(balsa_message);
    balsa_message->bm_widget = balsa_mime_widget_new_message_tl(balsa_message, buttons);
    g_free(buttons);

    /* Widget to hold message */
    key_controller = gtk_event_controller_key_new(GTK_WIDGET(balsa_message->bm_widget));
    g_signal_connect(key_controller, "focus-in",
                     G_CALLBACK(balsa_mime_widget_limit_focus), balsa_message);
    g_signal_connect(key_controller, "focus-out",
                     G_CALLBACK(balsa_mime_widget_unlimit_focus), balsa_message);

    /* If we do not add the widget to a viewport, GtkContainer would
     * provide one, but it would also set it up to scroll on grab-focus,
     * which has been really annoying for a long time :-( */
    viewport = gtk_viewport_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(viewport), GTK_WIDGET(balsa_message->bm_widget));
    gtk_container_add(GTK_CONTAINER(balsa_message->scroll), viewport);

    /* structure view */
    model = gtk_tree_store_new (NUM_COLUMNS,
                                TYPE_BALSA_PART_INFO,
				G_TYPE_STRING,
                                GDK_TYPE_PIXBUF,
                                G_TYPE_STRING);
    balsa_message->treeview = gtk_tree_view_new_with_model (GTK_TREE_MODEL(model));
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW (balsa_message->treeview));
    g_signal_connect(balsa_message->treeview, "row-activated",
                     G_CALLBACK(tree_activate_row_cb), balsa_message);

    gesture = gtk_gesture_multi_press_new(balsa_message->treeview);
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
    g_signal_connect(gesture, "pressed",
                     G_CALLBACK(tree_button_press_cb), balsa_message);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(gesture), GTK_PHASE_CAPTURE);

    g_signal_connect(balsa_message->treeview, "popup-menu",
                     G_CALLBACK(tree_menu_popup_key_cb), balsa_message);
    g_object_unref(model);
    gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);
    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (balsa_message->treeview), FALSE);

    /* column for the part number */
    renderer = gtk_cell_renderer_text_new ();
    g_object_set (renderer, "xalign", 0.0, NULL);
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (balsa_message->treeview),
                                                 -1, NULL,
                                                 renderer, "text",
                                                 PART_NUM_COLUMN,
                                                 NULL);

    /* column for type icon */
    renderer = gtk_cell_renderer_pixbuf_new ();
    g_object_set (renderer, "xalign", 0.0, NULL);
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (balsa_message->treeview),
                                                 -1, NULL,
                                                 renderer, "pixbuf",
                                                 MIME_ICON_COLUMN,
                                                 NULL);

    /* column for mime type */
    renderer = gtk_cell_renderer_text_new ();
    g_object_set (renderer, "xalign", 0.0, NULL);
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (balsa_message->treeview),
                                                 -1, NULL,
                                                 renderer, "text",
                                                 MIME_TYPE_COLUMN,
                                                 NULL);

    /* scrolled window for the tree view */
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);

    gtk_tree_view_set_expander_column
	(GTK_TREE_VIEW (balsa_message->treeview), gtk_tree_view_get_column
	 (GTK_TREE_VIEW (balsa_message->treeview), MIME_ICON_COLUMN - 1));

    gtk_stack_add_titled(stack, scroll, "parts", _("Message parts"));
    gtk_container_add(GTK_CONTAINER(scroll), balsa_message->treeview);

    balsa_message->current_part = NULL;
    balsa_message->message = NULL;
    balsa_message->info_count = 0;
    balsa_message->save_all_list = NULL;
    balsa_message->save_all_popup = NULL;

    balsa_message->wrap_text = balsa_app.browse_wrap;
    balsa_message->shown_headers = balsa_app.shown_headers;

    /* Find-in-message search bar, initially hidden. */
    balsa_message->find_bar = bm_find_bar_new(balsa_message);
    gtk_container_add(GTK_CONTAINER(vbox), balsa_message->find_bar);

    gtk_widget_show_all(GTK_WIDGET(balsa_message));
}

static void
balsa_message_destroy(GObject * object)
{
    BalsaMessage* balsa_message = BALSA_MESSAGE(object);

    if (balsa_message->treeview) {
        balsa_message_set(balsa_message, NULL, 0);
        gtk_widget_destroy(balsa_message->treeview);
        balsa_message->treeview = NULL;
    }

    g_list_free(balsa_message->save_all_list);
    balsa_message->save_all_list = NULL;

    g_clear_object(&balsa_message->save_all_popup);
    g_clear_object(&balsa_message->parts_popup);
    g_clear_object(&balsa_message->face_box);

#ifdef HAVE_HTML_WIDGET
    if (balsa_message->html_find_info) {
        g_free(balsa_message->html_find_info);
        balsa_message->html_find_info = NULL;
    }
#endif                          /* HAVE_HTML_WIDGET */

    G_OBJECT_CLASS(balsa_message_parent_class)->dispose(object);
}

GtkWidget *
balsa_message_new(void)
{
    BalsaMessage *balsa_message;

    balsa_message = g_object_new(BALSA_TYPE_MESSAGE,
                      "orientation", GTK_ORIENTATION_VERTICAL,
                      NULL);

    return GTK_WIDGET(balsa_message);
}

/* Returns a BalsaPartInfo with a reference (g_object_unref when done). */
static BalsaPartInfo *
tree_next_valid_part_info(GtkTreeModel * model, GtkTreeIter * iter)
{
    BalsaPartInfo *info = NULL;

    do {
        GtkTreeIter child;

        /* check if there is a valid info */
        gtk_tree_model_get(model, iter, PART_INFO_COLUMN, &info, -1);
        if (info)
            return info;

        /* if there are children, check the childs */
        if (gtk_tree_model_iter_children (model, &child, iter))
            if ((info = tree_next_valid_part_info(model, &child)))
                return info;

        /* switch to the next iter on the same level */
        if (!gtk_tree_model_iter_next(model, iter))
            return NULL;
    } while (1);
    /* never reached */
    return NULL;
}

static void
tree_activate_row_cb(GtkTreeView *treeview, GtkTreePath *arg1,
                     GtkTreeViewColumn *arg2, gpointer user_data)
{
    BalsaMessage * balsa_message = (BalsaMessage *)user_data;
    GtkTreeModel * model = gtk_tree_view_get_model(treeview);
    GtkTreeIter sel_iter;
    BalsaPartInfo *info = NULL;

    g_return_if_fail(balsa_message);

    /* get the info of the activated part */
    if (!gtk_tree_model_get_iter(model, &sel_iter, arg1))
        return;
    gtk_tree_model_get(model, &sel_iter, PART_INFO_COLUMN, &info, -1);

    /* if it's not displayable (== no info), get the next one... */
    if (!info) {
        info = tree_next_valid_part_info(model, &sel_iter);

        if (!info) {
            gtk_tree_model_get_iter_first(model, &sel_iter);
            gtk_tree_model_get(model, &sel_iter, PART_INFO_COLUMN, &info, -1);
            if (!info)
                info = tree_next_valid_part_info(model, &sel_iter);
        }
    }

    gtk_stack_set_visible_child_name(GTK_STACK(balsa_message->stack), "content");
    select_part(balsa_message, info);
    if (info)
        g_object_unref(info);
}

static void
collect_selected_info(GtkTreeModel * model, GtkTreePath * path,
                      GtkTreeIter * iter, gpointer data)
{
    GList **info_list = (GList **)data;
    BalsaPartInfo *info;

    gtk_tree_model_get(model, iter, PART_INFO_COLUMN, &info, -1);
    if (info) {
        g_object_unref(info);
        *info_list = g_list_append(*info_list, info);
    }
}

static void
add_save_view_menu_item(GtkWidget *menu, const gchar *label, GCallback callback, gpointer user_data)
{
	GAppInfo *app_info;

	app_info = g_app_info_get_default_for_type("inode/directory", FALSE);
	if (app_info != NULL) {
		GtkWidget *menu_item;

		menu_item = gtk_menu_item_new_with_label(label);
		g_object_set_data_full(G_OBJECT(menu_item), BALSA_MIME_WIDGET_CB_APPINFO, app_info, g_object_unref);
		g_signal_connect(menu_item, "activate", callback, user_data);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
	}
}

static void
tree_mult_selection_popup(BalsaMessage     *balsa_message,
                          const GdkEvent   *event,
                          GtkTreeSelection *selection)
{
    gint selected;

    /* destroy left-over select list and popup... */
    g_list_free(balsa_message->save_all_list);
    balsa_message->save_all_list = NULL;
    if (balsa_message->save_all_popup) {
        g_object_unref(balsa_message->save_all_popup);
        balsa_message->save_all_popup = NULL;
    }

    /* collect all selected info blocks */
    gtk_tree_selection_selected_foreach(selection,
                                        collect_selected_info,
                                        &balsa_message->save_all_list);

    /* For a single part, display it's popup, for multiple parts a "save all"
     * popup. If nothing with an info block is selected, do nothing */
    selected = g_list_length(balsa_message->save_all_list);
    if (selected == 1) {
        BalsaPartInfo *info = BALSA_PART_INFO(balsa_message->save_all_list->data);
        if (info->popup_menu) {
            if (event != NULL) {
                gtk_menu_popup_at_pointer(GTK_MENU(info->popup_menu), event);
            } else {
                gtk_menu_popup_at_widget(GTK_MENU(info->popup_menu),
                                         GTK_WIDGET(balsa_message),
                                         GDK_GRAVITY_CENTER, GDK_GRAVITY_CENTER,
                                         NULL);
            }
        }
        g_list_free(balsa_message->save_all_list);
        balsa_message->save_all_list = NULL;
    } else if (selected > 1) {
        GtkWidget *menu_item;

        balsa_message->save_all_popup = gtk_menu_new ();
        g_object_ref_sink(balsa_message->save_all_popup);
        menu_item =
            gtk_menu_item_new_with_label (_("Save selected as…"));
        g_signal_connect (menu_item, "activate",
                          G_CALLBACK (part_context_save_all_cb),
                          (gpointer) balsa_message->save_all_list);
        gtk_menu_shell_append (GTK_MENU_SHELL (balsa_message->save_all_popup), menu_item);
        menu_item =
            gtk_menu_item_new_with_label (_("Save selected to folder…"));
        g_signal_connect (menu_item, "activate",
                          G_CALLBACK (part_context_dump_all_cb),
                          (gpointer) balsa_message);
        gtk_menu_shell_append (GTK_MENU_SHELL (balsa_message->save_all_popup), menu_item);
        /* Translators: save all items to folder and open the folder in standard file manager app */
        add_save_view_menu_item(balsa_message->save_all_popup,
                                _("Save selected to folder and browse…"),
                                G_CALLBACK(part_context_dump_all_cb), balsa_message);
        gtk_widget_show_all(balsa_message->save_all_popup);
        if (event != NULL) {
            gtk_menu_popup_at_pointer(GTK_MENU(balsa_message->save_all_popup), event);
        } else {
            gtk_menu_popup_at_widget(GTK_MENU(balsa_message->save_all_popup),
                                     GTK_WIDGET(balsa_message),
                                     GDK_GRAVITY_CENTER, GDK_GRAVITY_CENTER,
                                     NULL);
        }
    }
}

static gboolean
tree_menu_popup_key_cb(GtkWidget *widget, gpointer user_data)
{
    BalsaMessage * balsa_message = (BalsaMessage *)user_data;

    g_return_val_if_fail(balsa_message, FALSE);
    tree_mult_selection_popup(balsa_message, NULL,
                              gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)));
    return TRUE;
}

static void
tree_button_press_cb(GtkGestureMultiPress *multi_press_gesture,
                     gint                  n_press,
                     gdouble               x,
                     gdouble               y,
                     gpointer              user_data)
{
    BalsaMessage *balsa_message = (BalsaMessage *) user_data;
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(multi_press_gesture));
    GtkTreeView *tree_view = GTK_TREE_VIEW(widget);
    GtkTreePath *path;
    GtkGesture *gesture;
    GdkEventSequence *sequence;
    const GdkEvent *event;
    gint bx;
    gint by;

    gesture  = GTK_GESTURE(multi_press_gesture);
    sequence = gtk_gesture_single_get_current_sequence(GTK_GESTURE_SINGLE(multi_press_gesture));
    event    = gtk_gesture_get_last_event(gesture, sequence);

    if (!gdk_event_triggers_context_menu(event) ||
        gdk_event_get_window(event) != gtk_tree_view_get_bin_window(tree_view))
        return;

    gtk_tree_view_convert_widget_to_bin_window_coords(tree_view, (gint) x, (gint) y,
                                                      &bx, &by);

    /* If the part which received the click is already selected, don't change
     * the selection and check if more than on part is selected. Pop up the
     * "save all" menu in this case and the "normal" popup otherwise.
     * If the receiving part is not selected, select (only) this part and pop
     * up its menu.
     */
    if (gtk_tree_view_get_path_at_pos(tree_view, bx, by,
                                      &path, NULL, NULL, NULL)) {
        GtkTreeIter iter;
        GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
        GtkTreeModel *model = gtk_tree_view_get_model(tree_view);

        if (!gtk_tree_selection_path_is_selected(selection, path)) {
            BalsaPartInfo *info = NULL;

            gtk_tree_selection_unselect_all(selection);
            gtk_tree_selection_select_path(selection, path);
            gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree_view), path, NULL,
                                     FALSE);
            if (gtk_tree_model_get_iter (model, &iter, path)) {
                gtk_tree_model_get(model, &iter, PART_INFO_COLUMN, &info, -1);
                if (info) {
                    if (info->popup_menu) {
                        gtk_menu_popup_at_pointer(GTK_MENU(info->popup_menu),
                                                  (GdkEvent *) event);
                    }
                    g_object_unref(info);
                }
            }
        } else
            tree_mult_selection_popup(balsa_message, event, selection);
        gtk_tree_path_free(path);
    }
}

/* balsa_message_set:
   returns TRUE on success, FALSE on failure (message content could not be
   accessed).

   if msgno == 0, clears the display and returns TRUE
*/

/* Helpers:
 */

gchar *
balsa_message_sender_to_gchar(InternetAddressList * list, gint which)
{
    InternetAddress *ia;

    if (!list)
	return g_strdup(_("(No sender)"));
    if (which < 0)
	return internet_address_list_to_string(list, NULL, FALSE);
    ia = internet_address_list_get_address (list, which);
    return internet_address_to_string(ia, NULL, FALSE);
}

static void
bm_clear_tree(BalsaMessage * balsa_message)
{
    GtkTreeModel *model;

    g_return_if_fail(balsa_message != NULL);

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(balsa_message->treeview));
    gtk_tree_store_clear(GTK_TREE_STORE(model));
    balsa_message->info_count = 0;
}

gboolean
balsa_message_set(BalsaMessage * balsa_message, LibBalsaMailbox * mailbox, guint msgno)
{
    gboolean is_new;
    GtkTreeIter iter;
    BalsaPartInfo *info;
    gboolean has_focus;
    LibBalsaMessage *message;
    guint prot_state;

    g_return_val_if_fail(balsa_message != NULL, FALSE);
    has_focus = balsa_message->focus_state != BALSA_MESSAGE_FOCUS_STATE_NO;

    bm_disable_find_entry(balsa_message);
    bm_clear_tree(balsa_message);
    select_part(balsa_message, NULL);
    if (balsa_message->message != NULL) {
        libbalsa_message_body_unref(balsa_message->message);
        g_object_unref(balsa_message->message);
        balsa_message->message = NULL;
    }

    if (mailbox == NULL || msgno == 0) {
        gtk_widget_hide(balsa_message->switcher);
        gtk_stack_set_visible_child_name(GTK_STACK(balsa_message->stack), "content");
        return TRUE;
    }

    balsa_message->message = message = libbalsa_mailbox_get_message(mailbox, msgno);
    /* We must not use msgno from now on: an asynchronous expunge may
       arrive (in particular between the body_ref() and set_flags()
       actions) and change the message numbering. Asynchronous
       expunges will update the LibBalsaMailbox::message data but no
       message numbers stored in random integer variables. */
    if (!message) {
	balsa_information(LIBBALSA_INFORMATION_WARNING,
                          _("Could not access message %u "
                            "in mailbox “%s”."),
			  msgno, libbalsa_mailbox_get_name(mailbox));
        return FALSE;
    }

    is_new = LIBBALSA_MESSAGE_IS_UNREAD(message);
    if(!libbalsa_message_body_ref(message, TRUE)) {
	libbalsa_mailbox_check(mailbox);
        g_object_unref(balsa_message->message);
        balsa_message->message = NULL;
	balsa_information(LIBBALSA_INFORMATION_WARNING,
                          _("Could not access message %u "
                            "in mailbox “%s”."),
			  libbalsa_message_get_msgno(message),
                          libbalsa_mailbox_get_name(mailbox));
        return FALSE;
    }

    balsa_message_perform_crypto(message,
				 libbalsa_mailbox_get_crypto_mode(mailbox),
				 FALSE, 1);
    /* calculate the signature summary state if not set earlier */
    prot_state = libbalsa_message_get_crypt_mode(message);
    if (prot_state == LIBBALSA_PROTECT_NONE) {
        guint new_prot_state =
            bm_scan_signatures(libbalsa_message_get_body_list(message), message);
        /* update the icon if necessary */
        if (prot_state != new_prot_state)
            libbalsa_message_set_crypt_mode(message, new_prot_state);
    }

    /* DKIM */
    if (balsa_app.enable_dkim_checks != 0) {
        libbalsa_dkim_message(message);
    }

    /* may update the icon */
    libbalsa_mailbox_msgno_update_attach(mailbox, libbalsa_message_get_msgno(message), message);

    display_headers(balsa_message);
    display_content(balsa_message);
    gtk_widget_show(GTK_WIDGET(balsa_message));

    if (balsa_message->info_count > 1)
        gtk_widget_show(balsa_message->switcher);
    else
        gtk_widget_hide(balsa_message->switcher);
    gtk_stack_set_visible_child_name(GTK_STACK(balsa_message->stack), "content");

    /*
     * At this point we check if (a) a message was new (its not new
     * any longer) and (b) a Disposition-Notification-To header line is
     * present.
     *
     */
    if (is_new) {
        LibBalsaMessageHeaders *headers = libbalsa_message_get_headers(message);

        if (headers != NULL && headers->dispnotify_to != NULL)
            handle_mdn_request(balsa_get_parent_window(GTK_WIDGET(balsa_message)), message, headers);
    }

#ifdef ENABLE_AUTOCRYPT
    /* check for Autocrypt information if the message is new only */
    if (is_new && balsa_autocrypt_in_use()) {
    	GError *error = NULL;

    	autocrypt_from_message(message, &error);
    	if (error != NULL) {
    		libbalsa_information(LIBBALSA_INFORMATION_ERROR, _("Autocrypt error: %s"), error->message);
    	}
    	g_clear_error(&error);
    }
#endif

    if (!gtk_tree_model_get_iter_first (gtk_tree_view_get_model(GTK_TREE_VIEW(balsa_message->treeview)),
                                        &iter))
        /* Not possible? */
        return TRUE;

    info =
        tree_next_valid_part_info(gtk_tree_view_get_model(GTK_TREE_VIEW(balsa_message->treeview)),
                                  &iter);
    select_part(balsa_message, info);
    if (info)
        g_object_unref(info);
    /*
     * emit read message
     */
    if (is_new && !libbalsa_mailbox_get_readonly(mailbox))
        libbalsa_mailbox_msgno_change_flags(mailbox,
                                            libbalsa_message_get_msgno(message),
                                            0, LIBBALSA_MESSAGE_FLAG_NEW);

    /* restore keyboard focus to the content, if it was there before */
    if (has_focus)
        balsa_message_grab_focus(balsa_message);

    return TRUE;
}

void
balsa_message_save_current_part(BalsaMessage * balsa_message)
{
    g_return_if_fail(balsa_message != NULL);

    if (balsa_message->current_part)
	balsa_mime_widget_ctx_menu_save(GTK_WIDGET(balsa_message), balsa_message->current_part->body);
}

static gboolean
bm_set_embedded_hdr(GtkTreeModel * model, GtkTreePath * path,
                               GtkTreeIter *iter, gpointer data)
{
    BalsaPartInfo *info = NULL;
    BalsaMessage * balsa_message = BALSA_MESSAGE(data);

    gtk_tree_model_get(model, iter, PART_INFO_COLUMN, &info, -1);
    if (info) {
 	if (info->body && info->body->embhdrs && info->mime_widget)
 	    balsa_mime_widget_message_set_headers_d(balsa_message, info->mime_widget,
                                                    info->body->embhdrs,
                                                    info->body->parts,
                                                    info->body->embhdrs->subject);
	g_object_unref(info);
    }

    return FALSE;
}

void
balsa_message_set_displayed_headers(BalsaMessage * balsa_message,
                                    ShownHeaders sh)
{
    g_return_if_fail(balsa_message != NULL);
    g_return_if_fail(sh >= HEADERS_NONE && sh <= HEADERS_ALL);

    if (balsa_message->shown_headers == sh)
        return;

    balsa_message->shown_headers = sh;

    if (balsa_message->message != NULL) {
        if (sh == HEADERS_ALL) {
            libbalsa_mailbox_set_msg_headers(libbalsa_message_get_mailbox(balsa_message->message),
                                             balsa_message->message);
        }
        display_headers(balsa_message);
        gtk_tree_model_foreach
            (gtk_tree_view_get_model(GTK_TREE_VIEW(balsa_message->treeview)),
             bm_set_embedded_hdr, balsa_message);
	if (balsa_message->attach_button != NULL) {
	    if (balsa_message->info_count > 1)
		gtk_widget_show_all(balsa_message->attach_button);
	    else
		gtk_widget_hide(balsa_message->attach_button);
	}
    }
}

void
balsa_message_set_wrap(BalsaMessage * balsa_message, gboolean wrap)
{
    g_return_if_fail(balsa_message != NULL);

    balsa_message->wrap_text = wrap;

    /* This is easier than reformating all the widgets... */
    if (balsa_message->message != NULL) {
        LibBalsaMessage *msg = balsa_message->message;
        balsa_message_set(balsa_message,
                          libbalsa_message_get_mailbox(msg),
                          libbalsa_message_get_msgno(msg));
    }
}


static void
display_headers(BalsaMessage * balsa_message)
{
    balsa_mime_widget_message_set_headers_d(balsa_message, balsa_message->bm_widget,
                                            libbalsa_message_get_headers(balsa_message->message),
                                            libbalsa_message_get_body_list(balsa_message->message),
                                            LIBBALSA_MESSAGE_GET_SUBJECT(balsa_message->message));
}


static void
part_info_init(BalsaMessage * balsa_message, BalsaPartInfo * info)
{
    g_return_if_fail(balsa_message != NULL);
    g_return_if_fail(info != NULL);
    g_return_if_fail(info->body != NULL);

    info->mime_widget =
        g_object_ref_sink(balsa_mime_widget_new(balsa_message, info->body, info->popup_menu));
}


static inline gchar *
mpart_content_name(const gchar *content_type)
{
    if (strcmp(content_type, "multipart/mixed") == 0)
        return g_strdup(_("mixed parts"));
    else if (strcmp(content_type, "multipart/alternative") == 0)
        return g_strdup(_("alternative parts"));
    else if (strcmp(content_type, "multipart/signed") == 0)
        return g_strdup(_("signed parts"));
    else if (strcmp(content_type, "multipart/encrypted") == 0)
        return g_strdup(_("encrypted parts"));
    else if (strcmp(content_type, "message/rfc822") == 0)
        return g_strdup(_("RFC822 message"));
    else
        return g_strdup_printf(_("“%s” parts"),
                               strchr(content_type, '/') + 1);
}

static void
atattchments_menu_cb(GtkWidget * widget, BalsaPartInfo *info)
{
    BalsaMessage * balsa_message = g_object_get_data(G_OBJECT(widget), "balsa-message");

    g_return_if_fail(balsa_message);
    g_return_if_fail(info);

    gtk_stack_set_visible_child_name(GTK_STACK(balsa_message->stack), "content");
    select_part(balsa_message, info);
}

static void
add_to_attachments_popup(GtkMenuShell * menu, const gchar * item,
			 BalsaMessage * balsa_message, BalsaPartInfo *info)
{
    GtkWidget * menuitem = gtk_menu_item_new_with_label (item);

    g_object_set_data(G_OBJECT(menuitem), "balsa-message", balsa_message);
    g_signal_connect(menuitem, "activate",
		     G_CALLBACK (atattchments_menu_cb),
		     (gpointer) info);
    gtk_menu_shell_append(menu, menuitem);
}

static void
toggle_all_inline_cb(GtkCheckMenuItem * item, BalsaPartInfo *info)
{
    BalsaMessage * balsa_message = g_object_get_data(G_OBJECT(item), "balsa-message");

    g_return_if_fail(balsa_message);
    g_return_if_fail(info);

    balsa_message->force_inline = gtk_check_menu_item_get_active(item);

    gtk_stack_set_visible_child_name(GTK_STACK(balsa_message->stack), "content");
    select_part(balsa_message, info);
}

static void
add_toggle_inline_menu_item(GtkMenuShell * menu, BalsaMessage * balsa_message,
			    BalsaPartInfo *info)
{
    GtkWidget * menuitem =
	gtk_check_menu_item_new_with_label (_("force inline for all parts"));

    g_object_set_data(G_OBJECT(menuitem), "balsa-message", balsa_message);
    g_signal_connect(menuitem, "activate",
		     G_CALLBACK (toggle_all_inline_cb),
		     (gpointer) info);
    gtk_menu_shell_append(menu, menuitem);

    /* Clear force-inline to be consistent with initial FALSE state of
     * check-menu-item. */
    balsa_message->force_inline = FALSE;
}

static void
display_part(BalsaMessage * balsa_message, LibBalsaMessageBody * body,
             GtkTreeModel * model, GtkTreeIter * iter, gchar * part_id)
{
    BalsaPartInfo *info = NULL;
    gchar *content_type = libbalsa_message_body_get_mime_type(body);
    gchar *icon_title = NULL;
    gboolean is_multipart=libbalsa_message_body_is_multipart(body);
    GdkPixbuf *content_icon;
    gchar *content_desc;

    content_desc = libbalsa_vfs_content_description(content_type);

    if(!is_multipart ||
       strcmp(content_type, "message/rfc822") == 0 ||
       strcmp(content_type, "multipart/signed") == 0 ||
       strcmp(content_type, "multipart/encrypted") == 0 ||
       strcmp(content_type, "multipart/mixed") == 0 ||
       strcmp(content_type, "multipart/alternative") == 0) {

        info = balsa_part_info_new(body);
        balsa_message->info_count++;

        if (strcmp(content_type, "message/rfc822") == 0 &&
            body->embhdrs) {
            gchar *from = balsa_message_sender_to_gchar(body->embhdrs->from, -1);
            gchar *subj = g_strdup(body->embhdrs->subject);
            libbalsa_utf8_sanitize(&from, balsa_app.convert_unknown_8bit, NULL);
            libbalsa_utf8_sanitize(&subj, balsa_app.convert_unknown_8bit, NULL);
            icon_title =
                g_strdup_printf(_("RFC822 message (from %s, subject “%s”)"),
                                from, subj);
            g_free(from);
            g_free(subj);
        } else if (is_multipart) {
            icon_title = mpart_content_name(content_type);
	    if (!strcmp(part_id, "1")) {
		add_toggle_inline_menu_item(GTK_MENU_SHELL(balsa_message->parts_popup),
					    balsa_message, info);
		gtk_menu_shell_append(GTK_MENU_SHELL(balsa_message->parts_popup),
				      gtk_separator_menu_item_new ());
		add_to_attachments_popup(GTK_MENU_SHELL(balsa_message->parts_popup),
					 _("complete message"),
					 balsa_message, info);
		gtk_menu_shell_append(GTK_MENU_SHELL(balsa_message->parts_popup),
				      gtk_separator_menu_item_new ());
	    }
        } else if (body->filename) {
            gchar * filename = g_strdup(body->filename);
	    gchar * menu_label;

            libbalsa_utf8_sanitize(&filename, balsa_app.convert_unknown_8bit,
                                   NULL);
            icon_title =
                g_strdup_printf("%s (%s)", filename, content_desc);

	    /* this should neither be a message nor multipart, so add it to the
	       attachments popup */
	    menu_label =
		g_strdup_printf(_("part %s: %s (file %s)"), part_id,
				content_desc, filename);
	    add_to_attachments_popup(GTK_MENU_SHELL(balsa_message->parts_popup),
				     menu_label, balsa_message, info);
	    g_free(menu_label);
            g_free(filename);
        } else {
	    gchar * menu_label;

            icon_title = g_strdup_printf("%s", content_desc);
	    menu_label =
		g_strdup_printf(_("part %s: %s"), part_id, content_desc);
	    add_to_attachments_popup(GTK_MENU_SHELL(balsa_message->parts_popup),
				     menu_label, balsa_message, info);
	    g_free(menu_label);
	}

        part_create_menu (info);
        info->path = gtk_tree_model_get_path(model, iter);

        /* add to the tree view */
        content_icon =
	    get_crypto_content_icon(body, content_type, &icon_title);
	if (info->body->was_encrypted) {
	    gchar * new_title =
		g_strconcat(_("encrypted: "), icon_title, NULL);
	    g_free(icon_title);
	    icon_title = new_title;
	}
        if (!content_icon)
	    content_icon =
		libbalsa_icon_finder(GTK_WIDGET(balsa_message),
                                     content_type, NULL, NULL,
				     GTK_ICON_SIZE_LARGE_TOOLBAR);
        gtk_tree_store_set (GTK_TREE_STORE(model), iter,
                            PART_INFO_COLUMN, info,
			    PART_NUM_COLUMN, part_id,
                            MIME_ICON_COLUMN, content_icon,
                            MIME_TYPE_COLUMN, icon_title, -1);

        g_object_unref(info);
        g_free(icon_title);
    } else {
	content_icon =
	    libbalsa_icon_finder(GTK_WIDGET(balsa_message),
                                 content_type, NULL, NULL,
				 GTK_ICON_SIZE_LARGE_TOOLBAR);
        gtk_tree_store_set (GTK_TREE_STORE(model), iter,
                            PART_INFO_COLUMN, NULL,
			    PART_NUM_COLUMN, part_id,
                            MIME_ICON_COLUMN, content_icon,
                            MIME_TYPE_COLUMN, content_desc, -1);
    }

    if (content_icon)
	g_object_unref(content_icon);
    g_free(content_desc);
    g_free(content_type);
}

static void
display_parts(BalsaMessage * balsa_message, LibBalsaMessageBody * body,
              GtkTreeIter * parent, gchar * prefix)
{
    GtkTreeModel *model =
        gtk_tree_view_get_model(GTK_TREE_VIEW(balsa_message->treeview));
    GtkTreeIter iter;
    gint part_in_level = 1;

    while (body) {
	gchar * part_id;

	if (prefix)
	    part_id = g_strdup_printf("%s.%d", prefix, part_in_level);
	else
	    part_id = g_strdup_printf("%d", part_in_level);
        gtk_tree_store_append(GTK_TREE_STORE(model), &iter, parent);
        display_part(balsa_message, body, model, &iter, part_id);
        display_parts(balsa_message, body->parts, &iter, part_id);
        body = body->next;
	part_in_level++;
	g_free(part_id);
    }
}

/* Display the image in a "Face:" header, if any. */
static void
display_face(BalsaMessage * balsa_message)
{
    GtkWidget *face_box;

    face_box = balsa_message->face_box;
    gtk_widget_hide(face_box);
    gtk_container_foreach(GTK_CONTAINER(face_box),
                          (GtkCallback) gtk_widget_destroy, NULL);

    if (balsa_message->message != NULL) {
        const gchar *face;
        GError *err = NULL;
        GtkWidget *image = NULL;

        face = libbalsa_message_get_user_header(balsa_message->message, "Face");
        if (face != NULL) {
            image = libbalsa_get_image_from_face_header(face, &err);
        } else {
#if HAVE_COMPFACE
            const gchar *x_face;

            x_face = libbalsa_message_get_user_header(balsa_message->message, "X-Face");
            if (x_face != NULL)
                image = libbalsa_get_image_from_x_face_header(x_face, &err);
#endif                          /* HAVE_COMPFACE */
        }

        if (err != NULL) {
            balsa_information(LIBBALSA_INFORMATION_WARNING,
                    /* Translators: please do not translate Face. */
                              _("Error loading Face: %s"), err->message);
            g_error_free(err);
        }

        if (image != NULL) {
            gtk_container_add(GTK_CONTAINER(face_box), image);
            gtk_widget_show_all(face_box);
        }
    }
}

static void
display_content(BalsaMessage * balsa_message)
{
    bm_clear_tree(balsa_message);
    if (balsa_message->parts_popup)
	g_object_unref(balsa_message->parts_popup);
    balsa_message->parts_popup = gtk_menu_new();
    g_object_ref_sink(balsa_message->parts_popup);
    display_parts(balsa_message, libbalsa_message_get_body_list(balsa_message->message), NULL, NULL);
    if (balsa_message->info_count > 1) {
 	gtk_widget_show_all(balsa_message->parts_popup);
 	gtk_widget_show_all(balsa_message->attach_button);
    } else {
 	gtk_widget_hide(balsa_message->attach_button);
    }
    display_face(balsa_message);
    gtk_tree_view_columns_autosize(GTK_TREE_VIEW(balsa_message->treeview));
    gtk_tree_view_expand_all(GTK_TREE_VIEW(balsa_message->treeview));
}

void
balsa_message_copy_part(GObject      *source_object,
                        GAsyncResult *res,
                        gpointer      data)
{
    LibBalsaMailbox *mailbox = balsa_mblist_mru_menu_finish(res);
    LibBalsaMessageBody *part = data;
    GMimeStream *stream;
    GError *err = NULL;

    if (mailbox == NULL) /* copy was canceled */
        return;

    stream = libbalsa_message_body_get_stream(part, &err);

    if (!stream) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("Reading embedded message failed: %s"),
			     err ? err->message : "?");
	g_clear_error(&err);
	return;
    }

    if (!libbalsa_mailbox_add_message(mailbox, stream, 0, &err)) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("Appending message to %s failed: %s"),
			     libbalsa_mailbox_get_name(mailbox),
			     err ? err->message : "?");
	g_clear_error(&err);
    }
    g_object_unref(stream);
}

static void
part_create_menu (BalsaPartInfo* info)
/* Remarks: Will add items in the following order:
            1) Default application according to GnomeVFS.
            2) GNOME MIME/GnomeVFS key values that don't match default
               application or anything on the shortlist.
            3) GnomeVFS shortlist applications, with the default one (sometimes
               included on shortlist, sometimes not) excluded. */
{
    GtkWidget* menu_item;
    gchar* content_type;

    info->popup_menu = gtk_menu_new ();
    g_object_ref_sink(info->popup_menu);

    content_type = libbalsa_message_body_get_mime_type (info->body);
    libbalsa_vfs_fill_menu_by_content_type(GTK_MENU(info->popup_menu),
					   content_type,
					   G_CALLBACK (balsa_mime_widget_ctx_menu_cb),
					   (gpointer)info->body);

    menu_item = gtk_menu_item_new_with_mnemonic (_("_Save…"));
    g_signal_connect (menu_item, "activate",
                      G_CALLBACK (balsa_mime_widget_ctx_menu_save), (gpointer) info->body);
    gtk_menu_shell_append (GTK_MENU_SHELL (info->popup_menu), menu_item);

    if (strcmp(content_type, "message/rfc822") == 0) {
        GtkWidget *submenu;

        menu_item =
            gtk_menu_item_new_with_mnemonic(_("_Copy to folder…"));
        gtk_menu_shell_append(GTK_MENU_SHELL(info->popup_menu), menu_item);

        submenu =
            balsa_mblist_mru_menu(GTK_WINDOW(gtk_widget_get_toplevel(info->popup_menu)),
                                  &balsa_app.folder_mru,
                                  balsa_message_copy_part,
                                  info->body);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item), submenu);
    } else {
    	/* Translators: save to folder and open the folder in standard file manager app */
    	add_save_view_menu_item(info->popup_menu, _("Save and open folder…"),
    		G_CALLBACK(balsa_mime_widget_ctx_menu_save), info->body);
    }

    gtk_widget_show_all (info->popup_menu);
    g_free (content_type);
}

static void
balsa_part_info_init(BalsaPartInfo *info)
{
    info->body = NULL;
    info->mime_widget = NULL;
    info->popup_menu = NULL;
    info->path = NULL;
}

static BalsaPartInfo*
balsa_part_info_new(LibBalsaMessageBody* body)
{
    BalsaPartInfo * info = g_object_new(TYPE_BALSA_PART_INFO, NULL);

    info->body = body;

    return info;
}

static void
balsa_part_info_dispose(GObject * object)
{
    BalsaPartInfo *info = (BalsaPartInfo *) object;

    g_clear_object(&info->mime_widget);
    g_clear_object(&info->popup_menu);

    G_OBJECT_CLASS(balsa_part_info_parent_class)->dispose(object);
}

static void
balsa_part_info_finalize(GObject * object)
{
    BalsaPartInfo *info = (BalsaPartInfo *) object;

    gtk_tree_path_free(info->path);

    G_OBJECT_CLASS(balsa_part_info_parent_class)->finalize(object);
}

static void
part_context_save_all_cb(GtkWidget * menu_item, GList * info_list)
{
    while (info_list) {
	balsa_mime_widget_ctx_menu_save(menu_item,
                                        BALSA_PART_INFO(info_list->data)->body);
        info_list = g_list_next(info_list);
    }
}

/*
 * Let the user select a folder and save all message parts form info_list in
 * this folder, either with their name (if defined) or as localised "content-
 * type message part". The function protects files from being overwritten by
 * appending "(1)", "(2)", ... to the name to make it unique.
 * Sets balsa_app::save_dir to the selected folder.
 */
typedef struct {
    GtkWidget *menu_item;
    GList     *info_list;
} part_context_dump_all_data_t;

static void part_context_dump_all_cb_response(GtkDialog *dialog,
                                              gint       response_id,
                                              gpointer   user_data);

static void
part_context_dump_all_cb(GtkMenuItem *self,
                         gpointer     user_data)
{
    BalsaMessage *balsa_message = user_data;
    GList *info_list = balsa_message->save_all_list;
    GtkWidget *dump_dialog;
    part_context_dump_all_data_t *data;

    g_return_if_fail(info_list);

    dump_dialog =
        gtk_file_chooser_dialog_new(_("Select folder for saving selected parts"),
                                    balsa_get_parent_window(GTK_WIDGET(balsa_message)),
                                    GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                                    _("_OK"),     GTK_RESPONSE_OK,
                                    NULL);

    gtk_window_set_modal(GTK_WINDOW(dump_dialog), TRUE);

    gtk_dialog_set_default_response(GTK_DIALOG(dump_dialog),
                                    GTK_RESPONSE_CANCEL);
    gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(dump_dialog),
                                    libbalsa_vfs_local_only());
    if (balsa_app.save_dir)
        gtk_file_chooser_set_current_folder_uri(GTK_FILE_CHOOSER(dump_dialog),
                                                balsa_app.save_dir);

    data = g_new(part_context_dump_all_data_t, 1);
    data->menu_item = GTK_WIDGET(self);
    data->info_list = info_list;

    g_signal_connect(dump_dialog, "response",
                     G_CALLBACK(part_context_dump_all_cb_response), data);
    gtk_widget_show_all(dump_dialog);
}

static void
part_context_dump_all_cb_response(GtkDialog *dump_dialog,
                                  gint       response_id,
                                  gpointer   user_data)
{
    if (response_id == GTK_RESPONSE_OK) {
	gchar *dir_name =
            gtk_file_chooser_get_uri(GTK_FILE_CHOOSER(dump_dialog));
        part_context_dump_all_data_t *data = user_data;
        GList *info_list;
        GFile * dir_file;

        g_debug("store to URI: %s", dir_name);
        dir_file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dump_dialog));

	/* remember the folder */
	g_free(balsa_app.save_dir);
	balsa_app.save_dir = dir_name;

	/* save all parts without further user interaction */
        for (info_list = data->info_list; info_list != NULL; info_list = info_list->next) {
	    BalsaPartInfo *info = BALSA_PART_INFO(info_list->data);
            GFile *save_file;
            char *save_path;
            char *save_path_utf8;
	    gboolean result;
            GFileOutputStream *stream;
            GError *err = NULL;
            ssize_t bytes_written;

	    if (info->body->filename) {
		save_file = g_file_get_child(dir_file, info->body->filename);
            } else {
		gchar *cont_type =
		    libbalsa_message_body_get_mime_type(info->body);
		gchar *p;

		/* be sure to have no '/' in the file name */
		g_strdelimit(cont_type, G_DIR_SEPARATOR_S, '-');
		p = g_strdup_printf(_("%s message part"), cont_type);
		g_free(cont_type);
		save_file = g_file_get_child(dir_file, p);
		g_free(p);
	    }
            save_path = g_file_get_path(save_file);
            save_path_utf8 = g_filename_to_utf8(save_path, -1, NULL, NULL, NULL);
            g_debug("store to file: %s", save_path_utf8);

            /* We don't use stream, but holding a reference to it keeps
             * a file descriptor open and blocks any other process from
             * opening the file. */
            stream = g_file_create(save_file, G_FILE_CREATE_PRIVATE, NULL, &err);

	    /* don't overwrite existing files, append (1), (2), ... instead */
	    if (stream == NULL) {
		gint n = 1;
                char *base_path;

                if (!g_error_matches(err, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
                    balsa_information(LIBBALSA_INFORMATION_ERROR,
                                      _("Could not save %s: %s"), save_path_utf8,
                                      err->message);
                    g_clear_error(&err);

                    /* Give up on this part */
                    g_free(save_path);
                    g_free(save_path_utf8);
                    g_object_unref(save_file);
                    continue;
                }

                base_path = save_path;
                save_path = NULL;
		do {
                    g_free(save_path);
                    save_path = g_strdup_printf("%s (%d)", base_path, n++);
                    g_object_unref(save_file);
                    save_file = g_file_new_for_path(save_path);
                    stream = g_file_create(save_file, G_FILE_CREATE_PRIVATE, NULL, NULL);
		} while (stream == NULL);
                g_free(base_path);
	    }
            g_free(save_path_utf8);
            save_path_utf8 = g_filename_to_utf8(save_path, -1, NULL, NULL, NULL);
            g_debug("store to file: %s", save_path_utf8);

	    /* try to save the file */
            result =
                libbalsa_message_body_save_gio(info->body, save_file,
                                               info->body->body_type ==
                                               LIBBALSA_MESSAGE_BODY_TYPE_TEXT,
                                               &bytes_written, &err);

            /* Now safe to drop our reference. */
            g_object_unref(stream);

	    if (!result) {
		balsa_information(LIBBALSA_INFORMATION_ERROR,
				  _("Could not save %s: %s"),
				  save_path_utf8,
                                  err && err->message ?
                                  err->message : _("Unknown error"));
                g_clear_error(&err);
            } else if (bytes_written == 0) {
                /* We could leave the empty file, but that would be
                 * inconsistent with not saving an empty part when only
                 * one file is selected. */
		balsa_information(LIBBALSA_INFORMATION_WARNING,
				  _("Empty part was not saved to %s"),
				  save_path_utf8);
                g_file_delete(save_file, NULL, NULL);
            }
            g_free(save_path_utf8);
            g_object_unref(save_file);
	}
        balsa_mime_widget_view_save_dir(data->menu_item);
	g_object_unref(dir_file);
    }

    gtk_widget_destroy(GTK_WIDGET(dump_dialog));
    g_free(user_data);
}


typedef struct _selFirst_T {
    GtkTreeIter sel_iter;
    gboolean found;
} selFirst_T;

static void
tree_selection_get_first(GtkTreeModel * model, GtkTreePath * path,
                         GtkTreeIter * iter, gpointer data)
{
    selFirst_T *sel = (selFirst_T *)data;

    if (!sel->found) {
        sel->found = TRUE;
        memcpy (&sel->sel_iter, iter, sizeof(GtkTreeIter));
    }
}

static BalsaPartInfo *
bm_next_part_info(BalsaMessage * balsa_message)
{
    selFirst_T sel;
    GtkTreeView *gtv;
    GtkTreeModel *model;

    g_return_val_if_fail(balsa_message != NULL, NULL);
    g_return_val_if_fail(balsa_message->treeview != NULL, NULL);

    gtv = GTK_TREE_VIEW(balsa_message->treeview);
    model = gtk_tree_view_get_model(gtv);

    /* get the info of the first selected part */
    sel.found = FALSE;
    gtk_tree_selection_selected_foreach(gtk_tree_view_get_selection(gtv),
                                        tree_selection_get_first, &sel);
    if (!sel.found) {
        /* return the first part if nothing is selected */
        if (!gtk_tree_model_get_iter_first(model, &sel.sel_iter))
            return NULL;
    } else {
        GtkTreeIter iter;

        /* If the first selected iter has a child, select it, otherwise
         * take next on the same or higher level.  If there is no next,
         * return NULL */
        if (!gtk_tree_model_iter_children (model, &iter, &sel.sel_iter)) {
            GtkTreeIter tmp_iter;

            tmp_iter = iter = sel.sel_iter;
            while (!gtk_tree_model_iter_next (model, &iter)) {
                if (!gtk_tree_model_iter_parent(model, &iter, &tmp_iter))
                    return NULL;
	        tmp_iter = iter;
            }
        }
        sel.sel_iter = iter;
    }

    return tree_next_valid_part_info(model, &sel.sel_iter);
}

void
balsa_message_next_part(BalsaMessage * balsa_message)
{
    BalsaPartInfo *info;
    GtkTreeView *gtv;

    if (!(info = bm_next_part_info(balsa_message)))
	return;

    gtv = GTK_TREE_VIEW(balsa_message->treeview);
    gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(gtv));
    select_part(balsa_message, info);
    g_object_unref(info);
}

gboolean
balsa_message_has_next_part(BalsaMessage * balsa_message)
{
    BalsaPartInfo *info;

    if (balsa_message && balsa_message->treeview
        && (info = bm_next_part_info(balsa_message))) {
        g_object_unref(info);
        return TRUE;
    }

    return FALSE;
}

static BalsaPartInfo *
bm_previous_part_info(BalsaMessage * balsa_message)
{
    selFirst_T sel;
    GtkTreeView *gtv;
    GtkTreeModel *model;
    BalsaPartInfo *info;

    g_return_val_if_fail(balsa_message != NULL, NULL);
    g_return_val_if_fail(balsa_message->treeview != NULL, NULL);

    gtv = GTK_TREE_VIEW(balsa_message->treeview);
    model = gtk_tree_view_get_model(gtv);

    /* get the info of the first selected part */
    sel.found = FALSE;
    gtk_tree_selection_selected_foreach(gtk_tree_view_get_selection(gtv),
                                        tree_selection_get_first, &sel);
    if (!sel.found) {
        /* return the first part if nothing is selected */
        if (!gtk_tree_model_get_iter_first(model, &sel.sel_iter))
            return NULL;
        gtk_tree_model_get(model, &sel.sel_iter, PART_INFO_COLUMN, &info, -1);
    } else {
        GtkTreePath * path = gtk_tree_model_get_path(model, &sel.sel_iter);

        /* find the previous element with a valid info block */
        do {
            if (!gtk_tree_path_prev (path)) {
                if (gtk_tree_path_get_depth (path) <= 1) {
                    gtk_tree_path_free(path);
                    return NULL;
                }
                gtk_tree_path_up(path);
            }
            gtk_tree_model_get_iter(model, &sel.sel_iter, path);
            gtk_tree_model_get(model, &sel.sel_iter, PART_INFO_COLUMN,
                               &info, -1);
        } while (!info);
        gtk_tree_path_free(path);
    }

    return info;
}

void
balsa_message_previous_part(BalsaMessage * balsa_message)
{
    BalsaPartInfo *info;
    GtkTreeView *gtv;

    if (!(info = bm_previous_part_info(balsa_message)))
	return;

    gtv = GTK_TREE_VIEW(balsa_message->treeview);
    gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(gtv));
    select_part(balsa_message, info);
    g_object_unref(info);
}

gboolean
balsa_message_has_previous_part(BalsaMessage * balsa_message)
{
    BalsaPartInfo *info;

    if (balsa_message && balsa_message->treeview
        && (info = bm_previous_part_info(balsa_message))) {
        g_object_unref(info);
        return TRUE;
    }

    return FALSE;
}

#ifdef HAVE_HTML_WIDGET
static gboolean
libbalsa_can_display(LibBalsaMessageBody *part, InternetAddressList *from, gpointer key)
{
	gchar *content_type = libbalsa_message_body_get_mime_type(part);
	gboolean res;

	if (strcmp(content_type, "multipart/related") == 0) {
		res = (part->parts != NULL) ? libbalsa_can_display(part->parts, from, key) : FALSE;
	} else {
		switch (libbalsa_message_body_get_mp_alt_selection(part, key)) {
		case LIBBALSA_MP_ALT_AUTO:
			res = (!balsa_app.display_alt_plain || libbalsa_html_get_prefer_html(from)) &&
				(libbalsa_html_type(content_type) != LIBBALSA_HTML_TYPE_NONE);
			break;
		case LIBBALSA_MP_ALT_PLAIN:
			res = FALSE;
			break;
		case LIBBALSA_MP_ALT_HTML:
			res = (libbalsa_html_type(content_type) != LIBBALSA_HTML_TYPE_NONE);
			break;
		default:
			g_assert_not_reached();		/* paranoid check... */
		}
	}

	g_free(content_type);
	return res;
}
#endif                          /* HAVE_HTML_WIDGET */

/** Determines whether given part can be displayed. We display plain
   text, parts html/rtf parts unless it has been disabled in the
   preferences. We make sure the process is correctly recursive, to
   display properly messages of following structure:

   multiplart/alternative
     text/plain "A"
     multipart/related
       text/plain "B"
       image/jpeg "C"

   In the case as above, B & C should be displayed.
*/
static LibBalsaMessageBody*
preferred_part(LibBalsaMessageBody *parts, InternetAddressList *from, gpointer key)
{
    LibBalsaMessageBody *body, *preferred = parts;

    for (body = parts; body; body = body->next) {
        gchar *content_type;

        content_type = libbalsa_message_body_get_mime_type(body);

        if (strcmp(content_type, "text/plain") == 0 ||
            strcmp(content_type, "text/calendar") == 0)
            preferred = body;
#ifdef HAVE_HTML_WIDGET
        else if (libbalsa_can_display(body, from, key))
            preferred = body;
#endif                          /* HAVE_HTML_WIDGET */

        g_free(content_type);
    }

    return preferred;
}

typedef struct _treeSearchT {
    const LibBalsaMessageBody *body;
    BalsaPartInfo *info;
} treeSearchT;

static gboolean
treeSearch_Func(GtkTreeModel * model, GtkTreePath *path,
                GtkTreeIter * iter, gpointer data)
{
    treeSearchT *search = (treeSearchT *)data;
    BalsaPartInfo *info = NULL;

    gtk_tree_model_get(model, iter, PART_INFO_COLUMN, &info, -1);
    if (info) {
        if (info->body == search->body) {
            search->info = info;
            return TRUE;
        } else
            g_object_unref(info);
    }

    return FALSE;
}

static BalsaPartInfo *
part_info_from_body(BalsaMessage *balsa_message, const LibBalsaMessageBody *body)
{
    treeSearchT search;

    search.body = body;
    search.info = NULL;

    gtk_tree_model_foreach
        (gtk_tree_view_get_model(GTK_TREE_VIEW(balsa_message->treeview)),
         treeSearch_Func, &search);
    return search.info;
}


static LibBalsaMessageBody *
add_body(BalsaMessage * balsa_message, LibBalsaMessageBody * body,
         GtkWidget * container)
{
    if(body) {
        BalsaPartInfo *info = part_info_from_body(balsa_message, body);

        if (info) {
	    body = add_part(balsa_message, info, container);
            g_object_unref(info);
        } else
	    body = add_multipart(balsa_message, body, container);
    }

    return body;
}

static LibBalsaMessageBody *
add_multipart_digest(BalsaMessage * balsa_message, LibBalsaMessageBody * body,
                     GtkWidget * container)
{
    LibBalsaMessageBody *retval = NULL;
    /* Add all parts */
    retval = add_body(balsa_message, body, container);
    for (body = body->next; body; body = body->next)
        add_body(balsa_message, body, container);

    return retval;
}

static LibBalsaMessageBody *
add_multipart_mixed(BalsaMessage * balsa_message, LibBalsaMessageBody * body,
                    GtkWidget * container)
{
    LibBalsaMessageBody * retval = NULL;
    /* Add first (main) part + anything else with
       Content-Disposition: inline */
    if (body) {
        retval = add_body(balsa_message, body, container);
        for (body = body->next; body; body = body->next) {
	    GMimeContentType *type =
		g_mime_content_type_parse(libbalsa_parser_options(), body->content_type);

            if (libbalsa_message_body_is_inline(body) ||
		balsa_message->force_inline ||
                libbalsa_message_body_is_multipart(body) ||
		g_mime_content_type_is_type(type, "application", "pgp-signature") ||
		(balsa_app.has_smime &&
		 (g_mime_content_type_is_type(type, "application", "pkcs7-signature") ||
		  g_mime_content_type_is_type(type, "application", "x-pkcs7-signature"))))
                add_body(balsa_message, body, container);
	    g_object_unref(type);
        }
    }

    return retval;
}

static LibBalsaMessageBody *
add_multipart(BalsaMessage *balsa_message, LibBalsaMessageBody *body,
              GtkWidget * container)
/* This function handles multiparts as specified by RFC2046 5.1 and
 * message/rfc822 types. */
{
    GMimeContentType *type;

    if (!body->parts)
	return body;

    type = g_mime_content_type_parse(libbalsa_parser_options(), body->content_type);

    if (g_mime_content_type_is_type(type, "multipart", "related")) {
        /* add the compound object root part */
        body = add_body(balsa_message, libbalsa_message_body_mp_related_root(body), container);
    } else if (g_mime_content_type_is_type(type, "multipart", "alternative")) {
        InternetAddressList *from = NULL;

        from = libbalsa_message_get_headers(balsa_message->message)->from;
        /* Add the most suitable part. */
        body = add_body(balsa_message, preferred_part(body->parts, from, balsa_message), container);
    } else if (g_mime_content_type_is_type(type, "multipart", "digest")) {
	body = add_multipart_digest(balsa_message, body->parts, container);
    } else { /* default to multipart/mixed */
	body = add_multipart_mixed(balsa_message, body->parts, container);
    }
    g_object_unref(type);

    return body;
}

static LibBalsaMessageBody *
add_part(BalsaMessage * balsa_message, BalsaPartInfo * info, GtkWidget * container)
{
    GtkTreeSelection *selection;
    LibBalsaMessageBody *body;
    GtkWidget *info_container;

    if (info == NULL)
	return NULL;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(balsa_message->treeview));

    if (info->path != NULL &&
	!gtk_tree_selection_path_is_selected(selection, info->path))
	gtk_tree_selection_select_path(selection, info->path);

    if (info->mime_widget == NULL)
	part_info_init(balsa_message, info);

    gtk_container_add(GTK_CONTAINER(container), GTK_WIDGET(info->mime_widget));

    info_container = balsa_mime_widget_get_container(info->mime_widget);
    body = add_multipart(balsa_message, info->body,
                         info_container != NULL ?
                         info_container : container);

    return body;
}


static gboolean
gtk_tree_hide_func(GtkTreeModel * model, GtkTreePath * path,
                   GtkTreeIter * iter, gpointer data)
{
    BalsaPartInfo *info;

    gtk_tree_model_get(model, iter, PART_INFO_COLUMN, &info, -1);

    if (info != NULL) {
        if (info->mime_widget != NULL) {
            GtkWidget *widget, *parent;

            widget = GTK_WIDGET(info->mime_widget);
            if ((parent = gtk_widget_get_parent(widget)) != NULL)
                gtk_container_remove(GTK_CONTAINER(parent), widget);
        }

        g_object_unref(info);
    }

    return FALSE;
}

static void
hide_all_parts(BalsaMessage * balsa_message)
{
    if (balsa_message->current_part) {
	gtk_tree_model_foreach(gtk_tree_view_get_model
			       (GTK_TREE_VIEW(balsa_message->treeview)),
			       gtk_tree_hide_func, balsa_message);
	gtk_tree_selection_unselect_all(gtk_tree_view_get_selection
					(GTK_TREE_VIEW(balsa_message->treeview)));
        g_object_unref(balsa_message->current_part);
	balsa_message->current_part = NULL;
    }

    gtk_container_foreach(GTK_CONTAINER(balsa_mime_widget_get_container(balsa_message->bm_widget)),
                          (GtkCallback) gtk_widget_destroy, NULL);
}

/*
 * If part == -1 then change to no part
 * must release selection before hiding a text widget.
 */
static void
select_part(BalsaMessage * balsa_message, BalsaPartInfo *info)
{
    LibBalsaMessageBody *body;

    hide_all_parts(balsa_message);
    bm_disable_find_entry(balsa_message);

    body = add_part(balsa_message, info,
                    balsa_mime_widget_get_container(balsa_message->bm_widget));
    balsa_message->current_part = part_info_from_body(balsa_message, body);
    libbalsa_message_body_set_mp_alt_selection(body, balsa_message);

    g_signal_emit(balsa_message, balsa_message_signals[SELECT_PART], 0);

    if (body != NULL) {
        GtkScrolledWindow *scroll = GTK_SCROLLED_WINDOW(balsa_message->scroll);
        GtkAdjustment *adj;

        adj = gtk_scrolled_window_get_hadjustment(scroll);
        gtk_adjustment_set_value(adj, 0);
        adj = gtk_scrolled_window_get_vadjustment(scroll);
        gtk_adjustment_set_value(adj, 0);
    }
}

GtkWidget *
balsa_message_current_part_widget(BalsaMessage * balsa_message)
{
    if (balsa_message && balsa_message->current_part &&
	balsa_message->current_part->mime_widget)
	return GTK_WIDGET(balsa_message->current_part->mime_widget);
    else
	return NULL;
}

GtkWindow *
balsa_get_parent_window(GtkWidget * widget)
{
    if (widget) {
        GtkWidget *toplevel = gtk_widget_get_toplevel(widget);

        if (gtk_widget_is_toplevel(toplevel) && GTK_IS_WINDOW(toplevel))
            return GTK_WINDOW(toplevel);
    }

    return GTK_WINDOW(balsa_app.main_window);
}


/*
 * This function informs the caller if the currently selected part
 * supports selection/copying etc.
 */
gboolean
balsa_message_can_select(BalsaMessage * balsa_message)
{
    BalsaMimeWidget *mime_widget;
    GtkWidget *widget;

    g_return_val_if_fail(BALSA_IS_MESSAGE(balsa_message), FALSE);

    if (balsa_message->current_part == NULL)
        return FALSE;

    mime_widget = balsa_message->current_part->mime_widget;
    if (!BALSA_IS_MIME_WIDGET_TEXT(mime_widget))
        return FALSE;

    widget = balsa_mime_widget_text_get_text_widget(BALSA_MIME_WIDGET_TEXT(mime_widget));

    return GTK_IS_EDITABLE(widget) || GTK_IS_TEXT_VIEW(widget)
#ifdef    HAVE_HTML_WIDGET
        || libbalsa_html_can_select(widget)
#endif /* HAVE_HTML_WIDGET */
        ;
}

gboolean
balsa_message_grab_focus(BalsaMessage * balsa_message)
{
    GtkWidget *widget;

    g_return_val_if_fail(balsa_message != NULL, FALSE);
    g_return_val_if_fail(balsa_message->current_part != NULL, FALSE);

    widget = GTK_WIDGET(balsa_message->current_part->mime_widget);
    g_return_val_if_fail(widget != NULL, FALSE);

    gtk_widget_set_can_focus(widget, TRUE);
    gtk_widget_grab_focus(widget);

    return TRUE;
}

static InternetAddress *
bm_get_mailbox(InternetAddressList * list)
{
    InternetAddress *ia;

    if (!list)
	return NULL;

    ia = internet_address_list_get_address (list, 0);
    if (!ia)
	return NULL;

    while (ia && INTERNET_ADDRESS_IS_GROUP (ia))
	ia = internet_address_list_get_address (INTERNET_ADDRESS_GROUP (ia)->members, 0);
    if (!ia)
	return NULL;

    return ia;
}

static void
handle_mdn_request(GtkWindow *parent,
                   LibBalsaMessage *message,
                   LibBalsaMessageHeaders *headers)
{
    const gchar *return_path;
    gboolean suspicious = TRUE;
    InternetAddressList *list;
    InternetAddress *dn;
    BalsaMDNReply action;
    LibBalsaMessage *mdn;
    LibBalsaIdentity *mdn_ident = NULL;
    gint i, len;

    /* according to RFC 8098 sect. 2.1 a MDN request shall be considered suspicious if the Return-Path differs from the
     * Disposition-Notification-To address */
    return_path = libbalsa_message_get_user_header(message, "Return-Path");
    dn = internet_address_list_get_address(headers->dispnotify_to, 0);
    if (return_path != NULL) {
    	InternetAddressList *return_ia;

    	return_ia = internet_address_list_parse(NULL, return_path);
    	if (return_ia != NULL) {
    		suspicious = !libbalsa_ia_rfc2821_equal(dn, internet_address_list_get_address(return_ia, 0));
    		g_object_unref(return_ia);
    	}
    }

    /* Try to find "my" identity first in the to, then in the cc list */
    list = headers->to_list;
    len = list ? internet_address_list_length(list) : 0;
    for (i = 0; i < len && !mdn_ident; i++) {
        GList * id_list;

        for (id_list = balsa_app.identities; !mdn_ident && id_list;
             id_list = id_list->next) {
            LibBalsaIdentity *ident = LIBBALSA_IDENTITY(id_list->data);
            InternetAddress *ia;

            ia = libbalsa_identity_get_address(ident);
            if (libbalsa_ia_rfc2821_equal(ia, bm_get_mailbox(list)))
                mdn_ident = ident;
        }
    }

    list = headers->cc_list;
    for (i = 0; i < len && !mdn_ident; i++) {
        GList * id_list;

        for (id_list = balsa_app.identities; !mdn_ident && id_list;
             id_list = id_list->next) {
            LibBalsaIdentity *ident = LIBBALSA_IDENTITY(id_list->data);
            InternetAddress *ia;

            ia = libbalsa_identity_get_address(ident);
            if (libbalsa_ia_rfc2821_equal(ia, bm_get_mailbox(list)))
                mdn_ident = ident;
        }
    }


    /* Now we decide from the settings of balsa_app.mdn_reply_[not]clean what
       to do...
    */
    if (suspicious || !mdn_ident)
        action = balsa_app.mdn_reply_notclean;
    else
        action = balsa_app.mdn_reply_clean;
    if (action == BALSA_MDN_REPLY_NEVER)
        return;

    /* fall back to the current identity if the requested one is empty */
    if (!mdn_ident)
        mdn_ident = balsa_app.current_ident;

    /* We *may* send a reply, so let's create a message for that... */
    mdn = create_mdn_reply (mdn_ident, message, action == BALSA_MDN_REPLY_ASKME);

    /* if the user wants to be asked, display a dialog, otherwise send... */
    if (action == BALSA_MDN_REPLY_ASKME) {
        gchar *sender;
        gchar *reply_to;
        sender = internet_address_list_to_string(headers->from, NULL, FALSE);
        reply_to =
            internet_address_list_to_string (headers->dispnotify_to, NULL,
		                             FALSE);
        gtk_widget_show_all (create_mdn_dialog (parent, sender, reply_to, mdn,
                                                mdn_ident));
        g_free (reply_to);
        g_free (sender);
    } else {
	GError * error = NULL;
	LibBalsaMsgCreateResult result;

        result = libbalsa_message_send(mdn, balsa_app.outbox, NULL,
				       balsa_find_sentbox_by_url,
				       libbalsa_identity_get_smtp_server(mdn_ident),
					   balsa_app.send_progress_dialog,
                                       parent,
				       TRUE, &error);
	if (result != LIBBALSA_MESSAGE_CREATE_OK)
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("Sending the disposition notification failed: %s"),
				 error ? error->message : "?");
	g_error_free(error);
        g_object_unref(mdn);
    }
}

static LibBalsaMessage *create_mdn_reply (const LibBalsaIdentity *mdn_ident,
                                          LibBalsaMessage *for_msg,
                                          gboolean manual)
{
    LibBalsaMessage *message;
    LibBalsaMessageHeaders *headers;
    LibBalsaMessageHeaders *for_msg_headers;
    LibBalsaMessageBody *body;
    gchar *date, *dummy;
    GString *report;
    gchar **params;
    struct utsname uts_name;
    const gchar *original_rcpt;

    /* create a message with the header set from the incoming message */
    message = libbalsa_message_new();
    headers = libbalsa_message_get_headers(message);
    headers->from = internet_address_list_new();
    internet_address_list_add(headers->from,
                              libbalsa_identity_get_address(balsa_app.current_ident));
    headers->date = time(NULL);
    libbalsa_message_set_subject(message, "Message Disposition Notification");
    headers->to_list = internet_address_list_new ();
    for_msg_headers = libbalsa_message_get_headers(for_msg);
    internet_address_list_append(headers->to_list, for_msg_headers->dispnotify_to);

    /* RFC 2298 requests this mime type... */
    libbalsa_message_set_subtype(message, "report");
    params = g_new(gchar *, 3);
    params[0] = g_strdup("report-type");
    params[1] = g_strdup("disposition-notification");
    params[2] = NULL;
    libbalsa_message_add_parameters(message, params);

    /* the first part of the body is an informational note */
    body = libbalsa_message_body_new(message);
    date = libbalsa_message_date_to_utf8(for_msg, balsa_app.date_string);
    dummy = internet_address_list_to_string(for_msg_headers->to_list, NULL, FALSE);
    body->buffer = g_strdup_printf(
        "The message sent on %s to %s with subject “%s” has been displayed.\n"
        "There is no guarantee that the message has been read or understood.\n\n",
        date, dummy, LIBBALSA_MESSAGE_GET_SUBJECT(for_msg));
    g_free (date);
    g_free (dummy);
    libbalsa_utf8_sanitize(&body->buffer, balsa_app.convert_unknown_8bit, NULL);
    if (balsa_app.wordwrap)
        libbalsa_wrap_string(body->buffer, balsa_app.wraplength);
    dummy = (gchar *)g_mime_charset_best(body->buffer, strlen(body->buffer));
    body->charset = g_strdup(dummy ? dummy : "us-ascii");
    libbalsa_message_append_part(message, body);

    /* the second part is a rfc3798 compliant
       message/disposition-notification */
    body = libbalsa_message_body_new(message);
    report = g_string_new("");
    uname(&uts_name);
    g_string_printf(report, "Reporting-UA: %s; " PACKAGE " " VERSION "\n",
		    uts_name.nodename);
    /* see rfc 3798, sections 2.3 and 3.2.3 */
    if ((original_rcpt =
	 libbalsa_message_get_user_header(for_msg, "original-recipient")))
	g_string_append_printf(report, "Original-Recipient: %s\n",
			       original_rcpt);
    g_string_append_printf(report, "Final-Recipient: rfc822; %s\n",
                           INTERNET_ADDRESS_MAILBOX
                           (libbalsa_identity_get_address
                            (balsa_app.current_ident))->addr);
    if (libbalsa_message_get_message_id(for_msg) != NULL)
        g_string_append_printf(report, "Original-Message-ID: <%s>\n",
                               libbalsa_message_get_message_id(for_msg));
    g_string_append_printf(report,
			   "Disposition: %s-action/MDN-sent-%sly; displayed",
			   manual ? "manual" : "automatic",
			   manual ? "manual" : "automatical");
    body->buffer = g_string_free(report, FALSE);
    body->content_type = g_strdup("message/disposition-notification");
    body->charset = g_strdup ("US-ASCII");
    libbalsa_message_append_part(message, body);

    return message;
}

static GtkWidget *
create_mdn_dialog(GtkWindow *parent, gchar * sender, gchar * mdn_to_address,
                  LibBalsaMessage * send_msg, LibBalsaIdentity *mdn_ident)
{
    GtkWidget *mdn_dialog;

    mdn_dialog =
        gtk_message_dialog_new(parent,
                               GTK_DIALOG_DESTROY_WITH_PARENT,
                               GTK_MESSAGE_QUESTION,
                               GTK_BUTTONS_YES_NO,
                               _("The sender of this mail, %s, "
                                 "requested \n"
                                 "a Message Disposition Notification "
                                 "(MDN) to be returned to “%s”.\n"
                                 "Do you want to send "
                                 "this notification?"),
                               sender, mdn_to_address);
    gtk_window_set_title(GTK_WINDOW(mdn_dialog), _("Reply to MDN?"));
    g_object_set_data(G_OBJECT(mdn_dialog), "balsa-send-msg", send_msg);
    g_object_set_data(G_OBJECT(mdn_dialog), "mdn-ident",
                      g_object_ref(mdn_ident));
    g_signal_connect(mdn_dialog, "response",
                     G_CALLBACK(mdn_dialog_response), NULL);

    return mdn_dialog;
}

static void
mdn_dialog_response(GtkWidget * dialog, gint response, gpointer user_data)
{
    LibBalsaMessage *send_msg =
        LIBBALSA_MESSAGE(g_object_get_data(G_OBJECT(dialog),
                                           "balsa-send-msg"));
    LibBalsaIdentity *mdn_ident =
        LIBBALSA_IDENTITY(g_object_get_data(G_OBJECT(dialog), "mdn-ident"));
    GError * error = NULL;
    LibBalsaMsgCreateResult result;

    g_return_if_fail(send_msg != NULL);
    g_return_if_fail(mdn_ident != NULL);

    if (response == GTK_RESPONSE_YES) {
        result =
            libbalsa_message_send(send_msg, balsa_app.outbox, NULL,
                                  balsa_find_sentbox_by_url,
				  libbalsa_identity_get_smtp_server(mdn_ident),
                                  balsa_app.send_progress_dialog,
                                  gtk_window_get_transient_for
                                  ((GtkWindow *) dialog),
                                  TRUE, &error);
        if (result != LIBBALSA_MESSAGE_CREATE_OK)
            libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                                 _("Sending the disposition notification failed: %s"),
                                 error ? error->message : "?");
        if (error)
            g_error_free(error);
    }
    g_object_unref(send_msg);
    g_object_unref(mdn_ident);
    gtk_widget_destroy(dialog);
}

#ifdef HAVE_HTML_WIDGET
/* Does the current part support zoom? */
gboolean
balsa_message_can_zoom(BalsaMessage * balsa_message)
{
    return balsa_message->current_part
        && libbalsa_html_can_zoom(GTK_WIDGET(balsa_message->current_part->mime_widget));
}

/* Zoom an html item. */
void
balsa_message_zoom(BalsaMessage * balsa_message, gint in_out)
{
    gint zoom;

    if (!balsa_message_can_zoom(balsa_message))
        return;

    zoom =
       GPOINTER_TO_INT(g_object_get_data
                       (G_OBJECT(balsa_message->message), BALSA_MESSAGE_ZOOM_KEY));
     if (in_out)
       zoom += in_out;
     else
       zoom = 0;
     g_object_set_data(G_OBJECT(balsa_message->message), BALSA_MESSAGE_ZOOM_KEY,
                     GINT_TO_POINTER(zoom));

     libbalsa_html_zoom(GTK_WIDGET(balsa_message->current_part->mime_widget), in_out);

}
#endif /* HAVE_HTML_WIDGET */


/*
 * collected GPG(ME) helper stuff to make the source more readable
 */


/*
 * Calculate and return a "worst case" summary of all checked signatures in a
 * message.
 */
static guint
bm_scan_signatures(LibBalsaMessageBody *body,
                              LibBalsaMessage * message)
{
    guint result = LIBBALSA_PROTECT_NONE;

    g_return_val_if_fail(libbalsa_message_get_headers(message) != NULL, result);

    while (body) {
	guint this_part_state =
	    libbalsa_message_body_signature_state(body);

	/* remember: greater means worse... */
	if (this_part_state > result)
	    result = this_part_state;

        /* scan embedded messages */
        if (body->parts) {
            guint sub_result =
                bm_scan_signatures(body->parts, message);

            if (sub_result > result)
                result = sub_result;
        }

	body = body->next;
    }

    return result;
}


/*
 * Check if body (of type content_type) is signed and/or encrypted and return
 * the proper icon in this case, and NULL otherwise. If the part is signed,
 * replace *icon-title by the signature status.
 */
static GdkPixbuf *
get_crypto_content_icon(LibBalsaMessageBody * body, const gchar * content_type,
			gchar ** icon_title)
{
    GdkPixbuf *icon;
    gchar * new_title;
    const gchar * icon_name;

    if ((libbalsa_message_body_protect_mode(body) &
         (LIBBALSA_PROTECT_ENCRYPT | LIBBALSA_PROTECT_ERROR)) ==
        LIBBALSA_PROTECT_ENCRYPT)
        return NULL;

    icon_name = balsa_mime_widget_signature_icon_name(libbalsa_message_body_signature_state(body));
    if (!icon_name)
        return NULL;
    icon =
        gtk_icon_theme_load_icon(gtk_icon_theme_get_default(), icon_name,
                                 GTK_ICON_SIZE_LARGE_TOOLBAR, 0, NULL);
    if (!icon_title)
        return icon;

    if (*icon_title &&
	strcmp(content_type, "application/pgp-signature") != 0 &&
	strcmp(content_type, "application/pkcs7-signature") != 0 &&
	strcmp(content_type, "application/x-pkcs7-signature") != 0) {
    	gchar *info_str;

    	info_str = g_mime_gpgme_sigstat_info(body->sig_info, TRUE);
    	new_title = g_strconcat(*icon_title, "; ", info_str, NULL);
    	g_free(info_str);
    } else {
    	new_title = g_mime_gpgme_sigstat_info(body->sig_info, TRUE);
    }

    if (*icon_title)
	g_free(*icon_title);
    *icon_title = new_title;

    return icon;
}


/* helper functions for libbalsa_msg_perform_crypto below */

/* this is a helper structure to simplify passing a set of parameters */
typedef struct {
    LibBalsaChkCryptoMode chk_mode;      /* current check mode */
    gboolean no_mp_signed;               /* don't check multipart/signed */
    guint max_ref;                       /* maximum allowed ref count */
    gchar * sender;                      /* shortcut to sender */
    gchar * subject;                     /* shortcut to subject */
} chk_crypto_t;


/*
 * check if body is a multipart/encrypted (RFC 3156) or a signed or encrypted
 * application/pkcs7-mime type, try to decrypt it and return the new body
 * chain.
 */
static LibBalsaMessageBody *
libbalsa_msg_try_decrypt(LibBalsaMessage * message, LibBalsaMessageBody * body,
			 chk_crypto_t * chk_crypto)
{
    gchar * mime_type;
    LibBalsaMessageBody * this_body;

    /* Check for multipart/encrypted or application/pkcs7-mime parts and
       replace this_body by it's decrypted content. Remember that there may be
       weird cases where such parts are nested, so do it in a loop. */
    this_body = body;
    mime_type = libbalsa_message_body_get_mime_type(this_body);
    while (strcmp(mime_type, "multipart/encrypted") == 0 ||
	   strcmp(mime_type, "application/pkcs7-mime") == 0 ||
	   strcmp(mime_type, "application/x-pkcs7-mime") == 0) {
	guint encrres;

	/* FIXME: not checking for body_ref > 1 (or > 2 when re-checking, which
	 * adds an extra ref) leads to a crash if we have both the encrypted and
	 * the unencrypted version open as the body chain of the first one will be
	 * unref'd. */
	if (libbalsa_message_get_body_ref(message) > chk_crypto->max_ref) {
	    if (chk_crypto->chk_mode == LB_MAILBOX_CHK_CRYPT_ALWAYS) {
		libbalsa_information
		    (LIBBALSA_INFORMATION_ERROR,
		     _("The decryption cannot be performed because this message "
		       "is displayed more than once.\n"
		       "Please close the other instances of this message and try "
		       "again."));
		/* downgrade the check mode to avoid multiple errors popping up */
		chk_crypto->chk_mode = LB_MAILBOX_CHK_CRYPT_MAYBE;
	    }
	    return body;
	}

	/* check if the gmime part is present and force loading it if we are
	   in "always" mode */
	if (!this_body->mime_part) {
            GError *err = NULL;
	    if (chk_crypto->chk_mode != LB_MAILBOX_CHK_CRYPT_ALWAYS)
		return this_body;

	    /* force loading the missing part */
	    if(!libbalsa_mailbox_get_message_part(message, this_body, &err)) {
		libbalsa_information
		    (LIBBALSA_INFORMATION_ERROR,
                     _("Parsing a message part failed: %s"),
                     err ? err->message : _("Possible disk space problem."));
                g_clear_error(&err);
                /* There is nothing more we can do... */
                return body;
            }
	}

	encrres = libbalsa_message_body_protect_mode(this_body);

	if (encrres & LIBBALSA_PROTECT_ENCRYPT) {
	    if (encrres & LIBBALSA_PROTECT_ERROR) {
		libbalsa_information
		    (chk_crypto->chk_mode == LB_MAILBOX_CHK_CRYPT_ALWAYS ?
		     LIBBALSA_INFORMATION_ERROR : LIBBALSA_INFORMATION_MESSAGE,
                     _("The message sent by %s with subject “%s” contains "
                       "an encrypted part, but its structure is invalid."),
		     chk_crypto->sender, chk_crypto->subject);
            } else if (encrres & LIBBALSA_PROTECT_RFC3156) {
                if (!balsa_app.has_openpgp)
                    libbalsa_information
                        (chk_crypto->chk_mode == LB_MAILBOX_CHK_CRYPT_ALWAYS ?
			 LIBBALSA_INFORMATION_WARNING : LIBBALSA_INFORMATION_MESSAGE,
                         _("The message sent by %s with subject “%s” "
                           "contains a PGP encrypted part, but this "
                           "crypto protocol is not available."),
                         chk_crypto->sender, chk_crypto->subject);
                else
                    this_body =
                        libbalsa_body_decrypt(this_body, GPGME_PROTOCOL_OpenPGP);
            } else if (encrres & LIBBALSA_PROTECT_SMIME) {
                if (!balsa_app.has_smime)
                    libbalsa_information
                        (chk_crypto->chk_mode == LB_MAILBOX_CHK_CRYPT_ALWAYS ?
			 LIBBALSA_INFORMATION_WARNING : LIBBALSA_INFORMATION_MESSAGE,
                         _("The message sent by %s with subject “%s” "
                           "contains a S/MIME encrypted part, but this "
                           "crypto protocol is not available."),
                         chk_crypto->sender, chk_crypto->subject);
                else
                    this_body =
                        libbalsa_body_decrypt(this_body, GPGME_PROTOCOL_CMS);
            }
        }

	/* has been decrypted? - eject if not, otherwise... */
	if (!this_body->was_encrypted)
	    return this_body;

	/* ...check decrypted content again... */
	g_free(mime_type);
	mime_type = libbalsa_message_body_get_mime_type(this_body);
    }
    g_free(mime_type);

    return this_body;
}


/*
 * Treatment of a multipart/signed body with protocol protocol.
 */
#define BALSA_MESSAGE_SIGNED_NOTIFIED "balsa-message-signed-notified"
static void
libbalsa_msg_try_mp_signed(LibBalsaMessage * message, LibBalsaMessageBody *body,
			   chk_crypto_t * chk_crypto)
{
    guint signres;

    if (chk_crypto->no_mp_signed)
	return;

    /* check if the gmime part is present and force loading it if we are in
       "always" mode */
    if (!body->mime_part) {
        GError *err = NULL;
	if (chk_crypto->chk_mode != LB_MAILBOX_CHK_CRYPT_ALWAYS)
	    return;

	/* force loading the missing part */
        if(!libbalsa_mailbox_get_message_part(message, body, &err)) {
            libbalsa_information
                (LIBBALSA_INFORMATION_ERROR,
                 _("Parsing a message part failed: %s"),
                 err ? err->message : _("Possible disk space problem."));
            g_clear_error(&err);
            /* There is nothing more we can do... */
            return;
        }
    }

    /* check which type of protection we've got */
    signres = libbalsa_message_body_protect_mode(body);
    if (!(signres & LIBBALSA_PROTECT_SIGN))
	return;

    /* eject if the structure is broken */
    if (signres & LIBBALSA_PROTECT_ERROR) {
	libbalsa_information
	    (chk_crypto->chk_mode == LB_MAILBOX_CHK_CRYPT_ALWAYS ?
	     LIBBALSA_INFORMATION_ERROR : LIBBALSA_INFORMATION_MESSAGE,
	     _("The message sent by %s with subject “%s” contains a signed "
	       "part, but its structure is invalid. The signature, if there "
	       "is any, cannot be checked."),
	     chk_crypto->sender, chk_crypto->subject);
	return;
    }

    /* check for an unsupported protocol */
    if (((signres & LIBBALSA_PROTECT_RFC3156) && !balsa_app.has_openpgp) ||
	((signres & LIBBALSA_PROTECT_SMIME) && !balsa_app.has_smime)) {
	libbalsa_information
	    (chk_crypto->chk_mode == LB_MAILBOX_CHK_CRYPT_ALWAYS ?
	     LIBBALSA_INFORMATION_WARNING : LIBBALSA_INFORMATION_MESSAGE,
	     _("The message sent by %s with subject “%s” contains a %s "
	       "signed part, but this crypto protocol is not available."),
	     chk_crypto->sender, chk_crypto->subject,
	     (signres & LIBBALSA_PROTECT_RFC3156) ? _("PGP") : _("S/MIME"));
	return;
    }

    /* force creating the protection info */
    if (body->parts->next->sig_info) {
	g_object_unref(body->parts->next->sig_info);
	body->parts->next->sig_info = NULL;
    }
    if (!libbalsa_body_check_signature(body,
				       (signres & LIBBALSA_PROTECT_RFC3156) ?
				       GPGME_PROTOCOL_OpenPGP : GPGME_PROTOCOL_CMS))
	return;

    /* evaluate the result */
    if (g_object_get_data(G_OBJECT(message), BALSA_MESSAGE_SIGNED_NOTIFIED))
        return;
    g_object_set_data(G_OBJECT(message), BALSA_MESSAGE_SIGNED_NOTIFIED,
                      GUINT_TO_POINTER(TRUE));

    if (body->parts->next->sig_info) {
	switch (libbalsa_message_body_signature_state(body->parts->next)) {
	case LIBBALSA_PROTECT_SIGN_GOOD:
	    g_debug("Detected a good signature");
	    break;
	case LIBBALSA_PROTECT_SIGN_NOTRUST:
	    if (g_mime_gpgme_sigstat_protocol(body->parts->next->sig_info) == GPGME_PROTOCOL_CMS)
		libbalsa_information_may_hide
		    (LIBBALSA_INFORMATION_MESSAGE, "SIG_NOTRUST",
		     _("Detected a good signature with insufficient "
		       "validity"));
	    else
		libbalsa_information_may_hide
		    (LIBBALSA_INFORMATION_MESSAGE, "SIG_NOTRUST",
		     _("Detected a good signature with insufficient "
		       "validity/trust"));
	    break;
	case LIBBALSA_PROTECT_SIGN_BAD: {
		gchar *status;

		status = libbalsa_gpgme_sig_stat_to_gchar(g_mime_gpgme_sigstat_status(body->parts->next->sig_info));
		libbalsa_information
		(chk_crypto->chk_mode == LB_MAILBOX_CHK_CRYPT_ALWAYS ?
		 LIBBALSA_INFORMATION_ERROR : LIBBALSA_INFORMATION_MESSAGE,
		 _("Checking the signature of the message sent by %s with "
		   "subject “%s” returned:\n%s"),
		 chk_crypto->sender, chk_crypto->subject, status);
		g_free(status);
	    break;
	}
	default:
	    break;
        }
    } else
	libbalsa_information
	    (chk_crypto->chk_mode == LB_MAILBOX_CHK_CRYPT_ALWAYS ?
	     LIBBALSA_INFORMATION_ERROR : LIBBALSA_INFORMATION_MESSAGE,
	     _("Checking the signature of the message sent by %s with subject "
	       "“%s” failed with an error!"),
	     chk_crypto->sender, chk_crypto->subject);
}


/*
 * Check if body is has some RFC 2440 protection and try to decrypt and/or
 * verify it.
 */
static void
libbalsa_msg_part_2440(LibBalsaMessage * message, LibBalsaMessageBody * body,
		       chk_crypto_t * chk_crypto)
{
    LibBalsaMailbox *mailbox;
    gpgme_error_t sig_res;
    GMimePartRfc2440Mode rfc2440mode;

    /* multiparts or complete messages can not be RFC 2440 protected */
    if (body->body_type == LIBBALSA_MESSAGE_BODY_TYPE_MESSAGE ||
	body->body_type == LIBBALSA_MESSAGE_BODY_TYPE_MULTIPART)
	return;

    if (!body->mime_part) {
        GError *err = NULL;
	/* if the user requested to always check everything or if the part is
	   text (and therefore likely to be displayed anyway) force loading
	   the missing part */
	if (chk_crypto->chk_mode == LB_MAILBOX_CHK_CRYPT_MAYBE &&
	    body->body_type != LIBBALSA_MESSAGE_BODY_TYPE_TEXT)
	    return;

        if(!libbalsa_mailbox_get_message_part(message, body, &err)) {
            libbalsa_information
                (LIBBALSA_INFORMATION_ERROR,
                 _("Parsing a message part failed: %s"),
                 err ? err->message : _("Possible disk space problem."));
            g_clear_error(&err);
            return;
        }
    }

    /* check if this is a RFC2440 part */
    if (!GMIME_IS_PART(body->mime_part))
	return;

    mailbox = libbalsa_message_get_mailbox(body->message);
    libbalsa_mailbox_lock_store(mailbox);
    rfc2440mode = g_mime_part_check_rfc2440(GMIME_PART(body->mime_part));
    libbalsa_mailbox_unlock_store(mailbox);

    /* if not, or if we have more than one instance of this message open, eject
       (see remark for libbalsa_msg_try_decrypt above) - remember that
       libbalsa_rfc2440_verify would also replace the stream by the "decrypted"
       (i.e. RFC2440 stuff removed) one! */
    if (rfc2440mode == GMIME_PART_RFC2440_NONE)
	return;
    if (libbalsa_message_get_body_ref(message) > chk_crypto->max_ref) {
	if (chk_crypto->chk_mode == LB_MAILBOX_CHK_CRYPT_ALWAYS) {
            libbalsa_information
                (LIBBALSA_INFORMATION_ERROR, "%s\n%s",
		 rfc2440mode == GMIME_PART_RFC2440_ENCRYPTED ?
                 _("The decryption cannot be performed because "
		   "this message is displayed more than once.") :
                 _("The signature check and removal of the OpenPGP armor "
		   "cannot be performed because "
		   "this message is displayed more than once."),
                 _("Please close the other instances of this message "
		   "and try again."));
	    /* downgrade the check mode to avoid multiple errors popping up */
	    chk_crypto->chk_mode = LB_MAILBOX_CHK_CRYPT_MAYBE;
	}
        return;
    }

    /* do the rfc2440 stuff */
    libbalsa_mailbox_lock_store(mailbox);
    if (rfc2440mode == GMIME_PART_RFC2440_SIGNED)
        sig_res =
            libbalsa_rfc2440_verify(GMIME_PART(body->mime_part),
				    &body->sig_info);
    else {
        sig_res =
            libbalsa_rfc2440_decrypt(GMIME_PART(body->mime_part), &body->sig_info);
	body->was_encrypted = (body->sig_info || sig_res == GPG_ERR_NO_ERROR);
	if (sig_res == GPG_ERR_NO_ERROR) {
	    /* decrypting may change the charset, so be sure to use the one
	       GMimePart reports */
	    g_free(body->charset);
	    body->charset = NULL;
	}
    }
    libbalsa_mailbox_unlock_store(mailbox);

    if (body->sig_info && sig_res == GPG_ERR_NO_ERROR) {
        if ((g_mime_gpgme_sigstat_summary(body->sig_info) & GPGME_SIGSUM_VALID) == GPGME_SIGSUM_VALID) {
        	g_debug("%s: detected a good signature", __func__);
        } else {
            libbalsa_information_may_hide
		(LIBBALSA_INFORMATION_MESSAGE, "SIG_NOTRUST",
		 _("Detected a good signature with insufficient "
		   "validity/trust"));
        }
    } else if (sig_res != GPG_ERR_NO_ERROR && sig_res != GPG_ERR_CANCELED) {
    	gchar *status;

    	status = libbalsa_gpgme_sig_stat_to_gchar(sig_res);
    	libbalsa_information(chk_crypto->chk_mode == LB_MAILBOX_CHK_CRYPT_ALWAYS ?
				LIBBALSA_INFORMATION_ERROR : LIBBALSA_INFORMATION_MESSAGE,
				_("Checking the signature of the message sent by %s with "
				  "subject “%s” returned:\n%s"),
				  chk_crypto->sender, chk_crypto->subject, status);
    	g_free(status);
    }
}


/*
 * Scan a body chain for RFC 2440, RFC 2633 and RFC 3156 encrypted and/or
 * signed parts. The function is called recursively for nested stuff.
 */
static LibBalsaMessageBody *
libbalsa_msg_perform_crypto_real(LibBalsaMessage * message,
				 LibBalsaMessageBody * body,
				 chk_crypto_t * chk_crypto)
{
    gchar * mime_type;
    LibBalsaMessageBody * chk_body;

    /* First try do decrypt RFC 3156 multipart/encrypted or any RFC 2633
       application/pkcs7-mime stuff. */
    body = libbalsa_msg_try_decrypt(message, body, chk_crypto);

    /* Check for multipart/signed and check the signature. */
    mime_type = libbalsa_message_body_get_mime_type(body);
    if (strcmp(mime_type, "multipart/signed") == 0)
	libbalsa_msg_try_mp_signed(message, body, chk_crypto);
    g_free(mime_type);

    /* loop over the parts, checking for RFC 2440 stuff, but ignore
       application/octet-stream which might be a detached encrypted part
       as well as all detached signatures */
    chk_body = body;
    while (chk_body) {
	mime_type = libbalsa_message_body_get_mime_type(chk_body);

	if (strcmp(mime_type, "application/octet-stream") != 0 &&
	    strcmp(mime_type, "application/pkcs7-signature") != 0 &&
	    strcmp(mime_type, "application/x-pkcs7-signature") != 0 &&
	    strcmp(mime_type, "application/pgp-signature") != 0)
	    libbalsa_msg_part_2440(message, chk_body, chk_crypto);
	g_free(mime_type);

	/* check nested stuff */
	if (chk_body->parts)
	    chk_body->parts =
		libbalsa_msg_perform_crypto_real(message, chk_body->parts,
						 chk_crypto);

	chk_body = chk_body->next;
    }

    return body;
}


/*
 * Launches a scan of message with chk_mode and a maximum reference of max_ref.
 * If no_mp_signed is TRUE, multipart/signed parts are ignored, i.e. only
 * encrypted (either "really" encrypted or armored) parts are used. This is
 * useful e.g. for replying.
 */
void
balsa_message_perform_crypto(LibBalsaMessage * message,
			     LibBalsaChkCryptoMode chk_mode,
			     gboolean no_mp_signed, guint max_ref)
{
    LibBalsaMessageBody *body_list;
    chk_crypto_t chk_crypto;

    body_list = libbalsa_message_get_body_list(message);
    if (body_list == NULL)
	return;

    /* check if the user requested to ignore any crypto stuff */
    if (chk_mode == LB_MAILBOX_CHK_CRYPT_NEVER)
	return;

    /* set up... */
    chk_crypto.chk_mode = chk_mode;
    chk_crypto.no_mp_signed = no_mp_signed;
    chk_crypto.max_ref = max_ref;
    chk_crypto.sender =
        balsa_message_sender_to_gchar(libbalsa_message_get_headers(message)->from, -1);
    chk_crypto.subject = g_strdup(LIBBALSA_MESSAGE_GET_SUBJECT(message));
    libbalsa_utf8_sanitize(&chk_crypto.subject, balsa_app.convert_unknown_8bit,
			   NULL);

    /* do the real work */
    body_list = libbalsa_msg_perform_crypto_real(message, body_list, &chk_crypto);
    libbalsa_message_set_body_list(message, body_list);

    /* clean up */
    g_free(chk_crypto.subject);
    g_free(chk_crypto.sender);
}


/*
 * Recheck crypto status of a message.
 * It works roughly like balsa_message_set, but with less overhead and with
 * "check always" mode. Note that this routine adds a temporary reference to
 * the message.
 */
void
balsa_message_recheck_crypto(BalsaMessage *balsa_message)
{
    LibBalsaMessageBody *body_list;
    guint prot_state;
    LibBalsaMessage * message;
    GtkTreeIter iter;
    BalsaPartInfo * info;
    gboolean has_focus = balsa_message->focus_state != BALSA_MESSAGE_FOCUS_STATE_NO;

    message = g_object_ref(balsa_message->message);

    select_part(balsa_message, NULL);
    bm_clear_tree(balsa_message);

    if (!libbalsa_message_body_ref(message, TRUE)) {
	g_object_unref(message);
        return;
    }

    g_object_set_data(G_OBJECT(message), BALSA_MESSAGE_SIGNED_NOTIFIED, NULL);
    balsa_message_perform_crypto(message, LB_MAILBOX_CHK_CRYPT_ALWAYS, FALSE, 2);

    /* calculate the signature summary state */
    body_list = libbalsa_message_get_body_list(message);
    prot_state = bm_scan_signatures(body_list, message);

    /* update the icon if necessary */
    libbalsa_message_set_crypt_mode(message, prot_state);

    /* may update the icon */
    libbalsa_mailbox_msgno_update_attach(libbalsa_message_get_mailbox(balsa_message->message),
					 libbalsa_message_get_msgno(balsa_message->message),
                                         balsa_message->message);

    display_headers(balsa_message);
    display_content(balsa_message);

    if (balsa_message->info_count > 1)
        gtk_widget_show(balsa_message->switcher);
    else
        gtk_widget_hide(balsa_message->switcher);
    gtk_stack_set_visible_child_name(GTK_STACK(balsa_message->stack), "content");

    if (!gtk_tree_model_get_iter_first (gtk_tree_view_get_model(GTK_TREE_VIEW(balsa_message->treeview)),
                                        &iter)) {
	libbalsa_message_body_unref(message);
	g_object_unref(message);
        return;
    }

    info =
        tree_next_valid_part_info(gtk_tree_view_get_model(GTK_TREE_VIEW(balsa_message->treeview)),
                                  &iter);
    select_part(balsa_message, info);
    if (info)
        g_object_unref(info);

    /* restore keyboard focus to the content, if it was there before */
    if (has_focus)
        balsa_message_grab_focus(balsa_message);

    libbalsa_message_body_unref(message);
    g_object_unref(message);
}


/*
 * Public method for find-in-message.
 */

void
balsa_message_find_in_message(BalsaMessage * balsa_message)
{
    BalsaMimeWidget *mime_widget;
    GtkWidget *w;

    if (balsa_message->current_part == NULL ||
        balsa_message->current_part->mime_widget == NULL)
        return;

    mime_widget = balsa_message->current_part->mime_widget;
    if (!BALSA_IS_MIME_WIDGET_TEXT(mime_widget))
        return;

    w = balsa_mime_widget_text_get_text_widget((BalsaMimeWidgetText *) mime_widget);
    if (GTK_IS_TEXT_VIEW(w)
#ifdef HAVE_HTML_WIDGET
        || libbalsa_html_can_search(w)
#endif                          /* HAVE_HTML_WIDGET */
            ) {
        if (GTK_IS_TEXT_VIEW(w)) {
            GtkTextView *text_view = (GtkTextView *) w;
            GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);

            gtk_text_buffer_get_start_iter(buffer, &balsa_message->find_iter);
        }

        balsa_message->find_forward = TRUE;
        gtk_entry_set_text(GTK_ENTRY(balsa_message->find_entry), "");

        g_signal_connect(balsa_message->find_key_controller, "key-pressed",
                         G_CALLBACK(bm_find_pass_to_entry), balsa_message);

        bm_find_set_status(balsa_message, BM_FIND_STATUS_INIT);

        gtk_search_bar_set_search_mode(GTK_SEARCH_BAR(balsa_message->find_bar), TRUE);
        if (gtk_widget_get_window(balsa_message->find_entry))
            gtk_widget_grab_focus(balsa_message->find_entry);
    }
}

/*
 * Getters
 */

gboolean
balsa_message_get_wrap_text(BalsaMessage *balsa_message)
{
    g_return_val_if_fail(BALSA_IS_MESSAGE(balsa_message), FALSE);

    return balsa_message->wrap_text;
}

BalsaMessageFocusState
balsa_message_get_focus_state(BalsaMessage *balsa_message)
{
    g_return_val_if_fail(BALSA_IS_MESSAGE(balsa_message), 0);

    return balsa_message->focus_state;
}

GtkScrolledWindow *
balsa_message_get_scroll(BalsaMessage *balsa_message)
{
    g_return_val_if_fail(BALSA_IS_MESSAGE(balsa_message), NULL);

    return GTK_SCROLLED_WINDOW(balsa_message->scroll);
}

BalsaMimeWidget *
balsa_message_get_bm_widget(BalsaMessage *balsa_message)
{
    g_return_val_if_fail(BALSA_IS_MESSAGE(balsa_message), NULL);

    return balsa_message->bm_widget;
}

LibBalsaMessage *
balsa_message_get_message(BalsaMessage *balsa_message)
{
    g_return_val_if_fail(BALSA_IS_MESSAGE(balsa_message), NULL);

    return balsa_message->message;
}

ShownHeaders
balsa_message_get_shown_headers(BalsaMessage *balsa_message)
{
    g_return_val_if_fail(BALSA_IS_MESSAGE(balsa_message), 0);

    return balsa_message->shown_headers;
}

GtkWidget *
balsa_message_get_face_box(BalsaMessage *balsa_message)
{
    g_return_val_if_fail(BALSA_IS_MESSAGE(balsa_message), NULL);

    return balsa_message->face_box;
}

GtkWidget *
balsa_message_get_tree_view(BalsaMessage *balsa_message)
{
    g_return_val_if_fail(BALSA_IS_MESSAGE(balsa_message), NULL);

    return balsa_message->treeview;
}

/*
 * Setters
 */

void
balsa_message_set_focus_state(BalsaMessage *balsa_message,
                              BalsaMessageFocusState focus_state)
{
    g_return_if_fail(BALSA_IS_MESSAGE(balsa_message));

    balsa_message->focus_state = focus_state;
}

void
balsa_message_set_face_box(BalsaMessage *balsa_message,
                           GtkWidget * face_box)
{
    g_return_if_fail(BALSA_IS_MESSAGE(balsa_message));

    g_set_object(&balsa_message->face_box, face_box);
}
