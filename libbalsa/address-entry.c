/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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

#include <gtk/gtk.h>
#include <string.h>

/*
 * LibBalsa includes.
 */
#include "address-entry.h"

#if !NEW_ADDRESS_ENTRY_WIDGET
/*
 * A subclass of gtkentry to support alias completion.
 */

#include <gdk/gdkkeysyms.h>

/* LibBalsaAddressEntry is typedef'd in address-entry.h, but the
 * structure is declared here to keep it opaque */
struct _LibBalsaAddressEntry {
    GtkEntry parent;
    gpointer dummy;             /* GtkEntry needs more space? */

    GList *active;              /* A GList of email addresses.
                                 * Caution! active may not
                                 * point to the start of the list. */
    gint focus;			/* Used to keep track of the validity of
				   the 'input' variable. */
    gchar *domain;		/* The domain to add if the user omits one. */

    /*
     * Function to find matches.  User defined.
     */
    void (* find_match)  (emailData *addy, gboolean fast_check);
};

/*
 * Global variable.  We need this for access to parent methods.
 */
static GtkEntryClass *parent_class = NULL;


/*
 * Function prototypes all GtkObjects need.
 */
static void libbalsa_address_entry_class_init(LibBalsaAddressEntryClass *klass);
static void libbalsa_address_entry_init(LibBalsaAddressEntry *ab);
static void libbalsa_address_entry_destroy(GtkObject * object);

/*
 * Other function prototypes.
 */
static void libbalsa_inputData_free(LibBalsaAddressEntry * address_entry);
static emailData *libbalsa_emailData_new(const gchar *str, gint n);
static void libbalsa_emailData_set_user(emailData * addy, gchar * str);
static void libbalsa_emailData_free(emailData * addy);
static gint libbalsa_address_entry_button_press(GtkWidget *,
                                                GdkEventButton *);
static void libbalsa_address_entry_received(GtkWidget * widget,
                                            GtkSelectionData * data,
                                            guint time);
static gint libbalsa_address_entry_key_press(GtkWidget *, GdkEventKey *);
static void libbalsa_address_entry_show(LibBalsaAddressEntry * entry);
static void libbalsa_force_no_match(emailData *);
static void libbalsa_address_entry_clear_match(LibBalsaAddressEntry *);
static GList *libbalsa_strsplit(const gchar *, gchar);
static gint libbalsa_address_entry_focus_out(GtkWidget * widget,
                                             GdkEventFocus * event);
static gboolean libbalsa_get_selection_bounds(GtkEditable * editable,
                                              gint * start, gint * end);
static gboolean libbalsa_get_editable(GtkEditable * editable);
static GList * libbalsa_delete_link(GList *list, GList *link);


/*
 * Functions mapped to by keys.
 */
static void libbalsa_move_forward_word		(LibBalsaAddressEntry *);
static void libbalsa_move_backward_word		(LibBalsaAddressEntry *);
static void libbalsa_move_backward_character	(LibBalsaAddressEntry *);
static void libbalsa_move_forward_character	(LibBalsaAddressEntry *);
static void libbalsa_keystroke_home		(LibBalsaAddressEntry *);
static void libbalsa_keystroke_end		(LibBalsaAddressEntry *);
static void libbalsa_delete_forward_character	(LibBalsaAddressEntry *);
static void libbalsa_delete_backward_character	(LibBalsaAddressEntry *);
static void libbalsa_delete_forward_word	(LibBalsaAddressEntry *);
static void libbalsa_delete_backward_word	(LibBalsaAddressEntry *);
static void libbalsa_delete_line		(LibBalsaAddressEntry *);
static void libbalsa_delete_to_line_end		(LibBalsaAddressEntry *);
static void libbalsa_force_expand               (LibBalsaAddressEntry *);
static void libbalsa_paste_clipboard		(GtkEntry *);
static void libbalsa_cut_clipboard		(GtkEntry *);

/*
 * Map the Control keys to relevant functions.
 */
typedef void (*LibBalsaTextFun) (GtkEditable * editable,
                                                  guint32 time);
static const LibBalsaTextFun control_keys[26] =
{
    (LibBalsaTextFun)libbalsa_keystroke_home,		/* a */
    (LibBalsaTextFun)libbalsa_move_backward_character,	/* b */
    NULL,						/* c */
    (LibBalsaTextFun)libbalsa_delete_forward_character,	/* d */
    (LibBalsaTextFun)libbalsa_keystroke_end,		/* e */
    (LibBalsaTextFun)libbalsa_move_forward_character,	/* f */
    NULL,						/* g */
    (LibBalsaTextFun)libbalsa_delete_backward_character,/* h */
    NULL,						/* i */
    NULL,						/* j */
    (LibBalsaTextFun)libbalsa_delete_to_line_end,	/* k */
    NULL,						/* l */
    NULL,						/* m */
    NULL,						/* n */
    NULL,						/* o */
    NULL,						/* p */
    NULL,						/* q */
    (LibBalsaTextFun)libbalsa_force_expand,		/* r */
    NULL,						/* s */
    NULL,						/* t */
    (LibBalsaTextFun)libbalsa_delete_line,		/* u */
    NULL,		                                /* v */
    (LibBalsaTextFun)libbalsa_delete_backward_word,	/* w */
    NULL,						/* x */
    NULL,						/* y */
    NULL,						/* z */
};


/*
 * Map the Alt keys to relevant functions.
 */
static const LibBalsaTextFun alt_keys[26] =
{
    NULL,						/* a */
    (LibBalsaTextFun)libbalsa_move_backward_word,	/* b */
    NULL,						/* c */
    (LibBalsaTextFun)libbalsa_delete_forward_word,	/* d */
    NULL,						/* e */
    (LibBalsaTextFun)libbalsa_move_forward_word,	/* f */
    NULL,						/* g */
    NULL,						/* h */
    NULL,						/* i */
    NULL,						/* j */
    NULL,						/* k */
    NULL,						/* l */
    NULL,						/* m */
    NULL,						/* n */
    NULL,						/* o */
    NULL,						/* p */
    NULL,						/* q */
    NULL,						/* r */
    NULL,						/* s */
    NULL,						/* t */
    NULL,						/* u */
    NULL,						/* v */
    NULL,						/* w */
    NULL,						/* x */
    NULL,						/* y */
    NULL,						/* z */
};


/*
 * ========================================================================
 *
 *  Functions that must exist for the GTK+ internals to work.
 *
 * ========================================================================
 */

GtkType libbalsa_address_entry_get_type(void)
{
    static GtkType address_entry_type = 0;

    if (!address_entry_type) {
	static const GTypeInfo address_entry_info = {
	    sizeof(LibBalsaAddressEntryClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
	    (GClassInitFunc) libbalsa_address_entry_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
	    sizeof(LibBalsaAddressEntry),
            0,                  /* n_preallocs */
	    (GInstanceInitFunc) libbalsa_address_entry_init
	};

	address_entry_type =
            g_type_register_static(GTK_TYPE_ENTRY,
	                           "LibBalsaAddressEntry",
                                   &address_entry_info, 0);
    }

    return address_entry_type;
}

static void
libbalsa_address_entry_class_init(LibBalsaAddressEntryClass *klass)
{
    GtkWidgetClass *gtk_widget_class;
    GtkObjectClass *object_class;
    GtkEntryClass  *entry_class;

    object_class = GTK_OBJECT_CLASS(klass);
    gtk_widget_class = GTK_WIDGET_CLASS(klass);
    entry_class = GTK_ENTRY_CLASS(klass);
    parent_class = gtk_type_class(GTK_TYPE_ENTRY);

    object_class->destroy = libbalsa_address_entry_destroy;

    gtk_widget_class->button_press_event = libbalsa_address_entry_button_press;
    gtk_widget_class->key_press_event = libbalsa_address_entry_key_press;
    gtk_widget_class->focus_out_event = libbalsa_address_entry_focus_out;
    gtk_widget_class->selection_received = libbalsa_address_entry_received;

    entry_class->cut_clipboard = libbalsa_cut_clipboard;
    entry_class->paste_clipboard = libbalsa_paste_clipboard;
}

static void
libbalsa_address_entry_init(LibBalsaAddressEntry *address_entry)
{
    address_entry->active = NULL;
    address_entry->domain = NULL;
    address_entry->find_match = NULL;
    address_entry->focus = FOCUS_LOST;

    return;
}


/*************************************************************
 * libbalsa_address_entry_destroy:
 *     Destroys a LibBalsaAddressEntry instance, and frees
 *     all related memory.  It gets called by GTK+ internally.
 *
 *   arguments:
 *     object:     The GTK_OBJECT() of the address_entry.
 *
 *   results:
 *     None.
 *************************************************************/
static void
libbalsa_address_entry_destroy(GtkObject * object)
{
    LibBalsaAddressEntry *address_entry;
    GtkEntry *entry;

    g_return_if_fail(object != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(object));

    entry = GTK_ENTRY(object);
    address_entry = LIBBALSA_ADDRESS_ENTRY(object);

    /*
     * Free the address list.
     */
    libbalsa_inputData_free(address_entry);

    g_free(address_entry->domain);
    address_entry->domain = NULL;

    /*
     * Make sure the data makes sense.
     */
    address_entry->focus = FOCUS_LOST;

    /*
     * Up the food chain...
     */
    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (GTK_OBJECT(object));
}


/*
 * ========================================================================
 *
 *  API that is internal to the widget and shouldn't be used
 *  outside.
 *
 *  These functions are "private" data.
 *
 * ========================================================================
 */


/*************************************************************
 * libbalsa_force_no_match:
 *     Sets an emailData structure'addy' so that it forgets
 *     match any matches that may have been found.
 *
 *   arguments:
 *     addy: The emailData to act on.
 *
 *   results:
 *     Modifies the structure.
 *************************************************************/
static void
libbalsa_force_no_match(emailData *addy) {
    g_return_if_fail(addy != NULL);

    g_free(addy->match);
    addy->match = NULL;
    addy->tabs = 0;
}


/*************************************************************
 * libbalsa_emailData_new:
 *     Returns a newly allocated emailData structure.
 *
 *   arguments:
 *     string to initialize addy->user.
 *
 *   results:
 *     A new emailData structure.
 *************************************************************/
static emailData *
libbalsa_emailData_new(const gchar * str, gint n)
{
    emailData *tmp;

    tmp = g_malloc(sizeof(emailData));
    tmp->user = g_strndup(str, n);
    tmp->address = NULL;
    tmp->match = NULL;
    tmp->cursor = -1;
    tmp->tabs = 0;
    return tmp;
}


/*************************************************************
 * libbalsa_emailData_set_user:
 *     Replaces the user string.
 *
 *   arguments:
 *     an emailData structure.
 *     string to initialize addy->user.
 *
 *   results:
 *     Modifies the structure.
 *************************************************************/
static void
libbalsa_emailData_set_user(emailData * addy, gchar * str)
{
    g_return_if_fail(addy != NULL);

    g_free(addy->user);
    addy->user = str;
    /* we DO NOT reset addy->address here. */
}


/*************************************************************
 * libbalsa_emailData_free:
 *     Frees an emailData structure and all memory it holds.
 *
 *   arguments:
 *     an emailData structure.
 *
 *   results:
 *     None.
 *************************************************************/
static void
libbalsa_emailData_free(emailData *addy)
{
    g_return_if_fail(addy != NULL);

    g_free(addy->user);
    if(addy->address) {
        g_object_unref(addy->address); addy->address = NULL;
    }
    g_free(addy->match);
    g_free(addy);
}

/*************************************************************
 * libbalsa_inputData_free:
 *     Frees the address list.
 *
 *   arguments:
 *     a LibBalsaAddressEntry structure.
 *
 *   results:
 *     None.
 *************************************************************/
static void
libbalsa_inputData_free(LibBalsaAddressEntry * address_entry)
{
    GList *list;

    g_return_if_fail(address_entry);
    list = g_list_first(address_entry->active);
    g_list_foreach(list, (GFunc) libbalsa_emailData_free, NULL);
    g_list_free(list);
    address_entry->active = NULL;
}

/*************************************************************
 * libbalsa_is_an_email:
 *     Tests if a given string is a complete e-mail address.
 *     It does this by examining the string for special
 *     characters used in e-mail To: headers.
 *
 *     This is used to check if the email should be expanded,
 *     and is not a complete RFC822 validator.  Don't use
 *     it like one.
 *
 *   arguments:
 *     a string.
 *
 *   results:
 *     Returns true if it passes a simple test to see if it
 *     is a complete e-mail address.
 *************************************************************/
static gboolean
libbalsa_is_an_email(gchar * str)
{
    while (*str) 
        if (*str == '@' || *str == '%' || *str++ == '!')
            return TRUE;
    return FALSE;
}


/*************************************************************
 * libbalsa_alias_accept_match:
 *     Accepts the currently guessed match for the e-mail
 *     as the one the user wants.
 *
 *   arguments:
 *     an emailData structure.
 *
 *   results:
 *     Modifies the structure.
 *************************************************************/
static void
libbalsa_alias_accept_match(emailData *addy)
{
    g_assert(addy->match != NULL);
    libbalsa_emailData_set_user(addy, addy->match);
    addy->match = NULL;
    addy->tabs = 0;
}


/*************************************************************
 * libbalsa_strsplit:
 *     Splits a string by a delimiter.  Returns a newly
 *     allocated GList* of the split strings.  It does
 *     not free the input string.
 *
 *   arguments:
 *     str:       The string to split.
 *     delimiter: A single character delimiter.
 *
 *   results:
 *     Returns a newly allocated GList* with newly allocated
 *     data inside it.
 *************************************************************/
static GList *
libbalsa_strsplit(const gchar *str, gchar delimiter)
{
    GList *glist;
    const gchar *old, *current;
    gboolean quoted;

    if (!str) return NULL;

    quoted = FALSE;
    glist = NULL;
    old = str;
    for (current=str;*current;current++) {
	if (*current == '"') quoted = !quoted;
	else if ( (!quoted) && (*current == delimiter) ) {
            glist =
                g_list_prepend(glist,
                              libbalsa_emailData_new(old, current - old));
	    old=current+1;
	}
    }
    glist =
        g_list_prepend(glist, libbalsa_emailData_new(old, current - old));
    return g_list_reverse(glist);
}


/*************************************************************
 * libbalsa_move_forward_word:
 *     Moves the cursor forward one e-mail address in the
 *     given address_entry.
 *
 *   arguments:
 *     address_entry: The LibBalsaAddressEntry to act on.
 *
 *   results:
 *     Moves the cursor in the entry.
 *************************************************************/
static void
libbalsa_move_forward_word(LibBalsaAddressEntry *address_entry)
{
    emailData *addy;

    g_return_if_fail(address_entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry));

    addy = address_entry->active->data;
    if (addy->user[addy->cursor] != '\0') {
	addy->cursor = strlen(addy->user);
    } else {
        GList *list;

	libbalsa_force_no_match(addy);
	if ((list = g_list_next(address_entry->active))) {
	    address_entry->active = list;
	    addy = list->data;
	    addy->cursor = strlen(addy->user);
	}
    }
    libbalsa_address_entry_show(address_entry);
}


/*************************************************************
 * libbalsa_move_backward_word:
 *     Moves the cursor backwards one e-mail address in the
 *     given address_entry.
 *
 *   arguments:
 *     address_entry: The LibBalsaAddressEntry to act on.
 *
 *   results:
 *     Moves the cursor in the entry.
 *************************************************************/
static void
libbalsa_move_backward_word(LibBalsaAddressEntry *address_entry)
{
    emailData *addy;

    g_return_if_fail(address_entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry));

    addy = address_entry->active->data;
    if (addy->cursor > 0) {
	addy->cursor = 0;
    } else {
        GList *list;

	libbalsa_force_no_match(addy);
	if ((list = g_list_previous(address_entry->active))) {
	    address_entry->active = list;
	    addy = list->data;
	    addy->cursor = 0;
	}
    }
    libbalsa_address_entry_show(address_entry);
}

static void
lbae_filter(gchar * p)
{
    gchar *q = p;
    g_assert(g_utf8_validate(p, -1, NULL));
    while (*p) {
	gunichar c = g_utf8_get_char(p);
	gchar *r = g_utf8_next_char(p);
	if (g_unichar_iscntrl(c)) {
	    *q++ = ' ';
	    p = r;
	} else if (q < p) {
	    while (p < r)
		*q++ = *p++;
	} else
	    p = q = r;
    }
}

/*************************************************************
 * libbalsa_fill_input:
 *     Set all the data in the LibBalsaAddressEntry structure to values
 *     reflecting the current user input.
 *
 *   arguments:
 *     address_entry: The LibBalsaAddressEntry to act on.
 *
 *   results:
 *     Sets list and active fields of the LibBalsaAddressEntry
 *************************************************************/
#define libbalsa_fill_input libbalsa_address_entry_fill_input
void
libbalsa_fill_input(LibBalsaAddressEntry *address_entry)
{
    gint cursor, size = 0, prev;
    gchar *typed;
    GList *list;
    emailData *addy;
    size_t tmp;

    g_return_if_fail(address_entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry));

    /*
     * First free any existing entry.
     */
    libbalsa_inputData_free(address_entry);

    /*
     * Grab data from the widget.
     */
    cursor = (gint) gtk_editable_get_position(GTK_EDITABLE(address_entry));
    typed = gtk_editable_get_chars(GTK_EDITABLE(address_entry), 0, -1);
    if (!typed)
	typed = g_strdup("");

    lbae_filter(typed);

    /*
     * Split the input string by comma.
     * str contains a list of e-mail addresses seperated by ','.
     *
     * Search for the currently active list member.
     * We have to match the cursor in GtkEntry to the list.
     */
    for (list = libbalsa_strsplit(typed, ','); list;
         list = g_list_next(list)) {
	prev = size;
	addy = list->data;
	size += strlen(addy->user) + 1;
	addy->user = g_strchug(addy->user);

        if (!address_entry->active && (cursor < size || !g_list_next(list))) {
            /* found the cursor, or hit the last addy */
            address_entry->active = list;
            addy->cursor = cursor - prev;
            if (g_list_previous(address_entry->active))
                /* Compensate for the ',' */
                addy->cursor--;
            if (addy->cursor < 0)
                addy->cursor = 0;
            if (addy->cursor > (tmp = strlen(addy->user)))
                addy->cursor = tmp;
        }
    }
    g_free(typed);
}

/*************************************************************
 * libbalsa_delete_line:
 *     Deletes the text string of a LibBalsaAddressEntry.
 *     Clears the whole line.
 *
 *   arguments:
 *     address_entry: The LibBalsaAddressEntry to act on.
 *
 *   results:
 *     Modifies the LibBalsaAddressEntry
 *************************************************************/
static void
libbalsa_delete_line(LibBalsaAddressEntry *address_entry)
{
    libbalsa_keystroke_home(address_entry);
    libbalsa_delete_to_line_end(address_entry);
}


/*************************************************************
 * libbalsa_delete_forward_word:
 *     Deletes the next e-mail entry in a LibBalsaAddressEntry
 *     If the cursor is in the beginning of an e-mail entry,
 *     it deletes the current one instead.
 *
 *   arguments:
 *     address_entry: The LibBalsaAddressEntry to act on.
 *
 *   results:
 *     Sets the text in the widget.
 *************************************************************/
static void
libbalsa_delete_forward_word(LibBalsaAddressEntry *address_entry)
{
    emailData *addy;
    GList *list;

    g_return_if_fail(address_entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry));

    if (!address_entry->active)
	return;

    addy = address_entry->active->data;

    /*
     * Lets see if the user is at the end of an e-mail entry.
     */
    if (addy->user[addy->cursor] == '\0') {
	list = g_list_next(address_entry->active);
	if (list) {
	    libbalsa_emailData_free(list->data);
	    libbalsa_delete_link(list, list);
	}

    /*
     * Delete the current entry.
     */
    } else {
        /* except as noted below, cursor will be in the entry to the
         * left of the current one, so we'll put the cursor at the
         * end */
        gboolean cursor_at_start = FALSE;
        GList *prev;

	list = address_entry->active;
        if ((prev = g_list_previous(list)))
            address_entry->active = prev;
        else
            /* cursor will be in the entry to the right of the
             * current one, so we'll put the cursor at the start */
            cursor_at_start = TRUE;
	libbalsa_emailData_free(list->data);
        address_entry->active =
            libbalsa_delete_link(address_entry->active, list);
	if (address_entry->active) {
	    addy = address_entry->active->data;
	    g_assert(addy != NULL);
	    addy->cursor = cursor_at_start ? 0 : strlen(addy->user);
	} else {
	    gtk_entry_set_text(GTK_ENTRY(address_entry), "");
	    libbalsa_fill_input(address_entry);
	}
    }
    libbalsa_address_entry_show(address_entry);
}


/*************************************************************
 * libbalsa_delete_backward_word:
 *     Deletes the previous e-mail entry in a
 *     LibBalsaAddressEntry.  If the cursor is at the end of
 *     an e-mail entry, it deletes the current one instead.
 *
 *   arguments:
 *     address_entry: The LibBalsaAddressEntry to act on.
 *
 *   results:
 *     Sets the text in the widget.
 *************************************************************/
static void
libbalsa_delete_backward_word(LibBalsaAddressEntry *address_entry)
{
    emailData *addy;
    GList *list;

    g_return_if_fail(address_entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry));

    if (!address_entry->active)
	return;

    addy = address_entry->active->data;

    /*
     * Lets see if the user is at the beginning of an e-mail entry.
     */
    if (!addy->cursor) {
	list = g_list_previous(address_entry->active);
	if (list) {
	    libbalsa_emailData_free(list->data);
	    libbalsa_delete_link(list, list);
	}

    /*
     * Delete the current entry.
     */
    } else {
        /* except as noted below, cursor will be in the entry to the
         * right of the current one, so we'll put the cursor at the
         * start */
        gboolean cursor_at_end = FALSE;

	list = address_entry->active;
	if (!g_list_next(list)) {
            /* cursor will be in the entry to the left of the
             * current one, so we'll put the cursor at the end */
	    address_entry->active = g_list_previous(list);
            cursor_at_end = TRUE;
        }
	libbalsa_emailData_free(list->data);
        address_entry->active =
            libbalsa_delete_link(address_entry->active, list);
	if (address_entry->active) {
	    addy = address_entry->active->data;
	    g_assert(addy != NULL);
	    addy->cursor = cursor_at_end ? strlen(addy->user) : 0;
	} else {
	    gtk_entry_set_text(GTK_ENTRY(address_entry), "");
	    libbalsa_fill_input(address_entry);
	}
    }
    libbalsa_address_entry_show(address_entry);
}


/*************************************************************
 * libbalsa_delete_to_line_end:
 *     Deletes from cursor position to end-of-line in the
 *     entry in a LibBalsaAddressEntry widget.
 *
 *   arguments:
 *     address_entry: The LibBalsaAddressEntry to act on.
 *
 *   results:
 *     Sets the text in the widget.
 *************************************************************/
static void
libbalsa_delete_to_line_end(LibBalsaAddressEntry *address_entry)
{
    emailData *addy;
    GList *list;

    g_return_if_fail(address_entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry));

    addy = address_entry->active->data;

    /*
     * Lets see if the user is at the end of an e-mail entry.
     */
    libbalsa_emailData_set_user(addy, g_strndup(addy->user, addy->cursor));
    libbalsa_force_no_match(addy);

    /*
     * Free all the following data.
     */
    for (list = g_list_next(address_entry->active);
	 list;
	 list = g_list_next(address_entry->active)) {
	 /*
	  * Concatenate the two e-mails.
	  */
	 libbalsa_emailData_free(list->data);
	 libbalsa_delete_link(list, list);
    }

    libbalsa_address_entry_show(address_entry);
}

/*************************************************************
 * libbalsa_address_entry_button_press:
 *     This gets called when the user clicks a mouse button.
 *
 *     A side effect is that this moves the cursor in the
 *     LibBalsaAddressEntry() box, so we must treat it like
 *     we lost focus!  Especially since the mouse /could/ put
 *     the cursor inside text that the user did not type!
 *
 *   arguments:
 *     widget: the widget.
 *     event:  the event.
 *
 *   results:
 *     Changes the appearance of the widget.
 *************************************************************/
static gint
libbalsa_address_entry_button_press(GtkWidget * widget, GdkEventButton * event)
{
    LibBalsaAddressEntry *address_entry;
    gint retval;

    g_return_val_if_fail(widget != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(widget), FALSE);

    address_entry = LIBBALSA_ADDRESS_ENTRY(widget);

    /*
     * We need to mark the widget as tainted.
     */
    address_entry->focus = FOCUS_TAINTED;

    /*
     * Now that we have done the ONE LINE that we needed to do,
     * let's hand over to the parent method.
     */
    retval =
        GTK_WIDGET_CLASS(parent_class)->button_press_event(widget, event);

    /*
     * Process the text in case it changed (e.g. paste).
     */
    libbalsa_fill_input(address_entry);

    return retval;
}

/*************************************************************
 * libbalsa_address_entry_received:
 *     This gets called when a selection is received.
 *
 *   arguments:
 *     widget: the widget.
 *     data:   just passed to the parent method.
 *     time:   ditto.
 *
 *   results:
 *     Accepts and processes the pasted text.
 *************************************************************/
static void
libbalsa_address_entry_received(GtkWidget * widget,
                                GtkSelectionData * data, guint time)
{
    LibBalsaAddressEntry *address_entry;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(widget));

    address_entry = LIBBALSA_ADDRESS_ENTRY(widget);

    GTK_WIDGET_CLASS(parent_class)->selection_received(widget, data, time);

    libbalsa_fill_input(address_entry);
    address_entry->focus = FOCUS_CACHED;
    libbalsa_address_entry_show(address_entry);
}


/*************************************************************
 * libbalsa_delete_backward_character:
 *     If the last key hit was TAB, cancel the last alias
 *     expansion that TAB did.  Otherwise delete one
 *     character.  If its at the beginning of the input, move
 *     one e-mail address left.
 *
 *   arguments:
 *     address_entry: the widget.
 *
 *   results:
 *     Changes the appearance of the widget.
 *************************************************************/
static void
libbalsa_delete_backward_character(LibBalsaAddressEntry *address_entry)
{
    GtkEditable *editable;
    emailData *addy, *extra;
    GList *list;

    addy = address_entry->active->data;
    editable = GTK_EDITABLE(address_entry);

    /*
     * First: Cut the clipboard.
     */
    if (libbalsa_get_selection_bounds(editable, NULL, NULL) && !addy->match) {
	libbalsa_cut_clipboard(GTK_ENTRY(address_entry));
	return;
    }

    /*
     * Check if the user wanted to delete a match.
     * This is only valid if the user has hit tab.
     */
    if ((addy->user[addy->cursor] == '\0') && addy->match && addy->tabs) {
       libbalsa_force_no_match(addy);
       return;
    }

    /*
     * Lets see if the user is at the beginning of an e-mail entry.
     */
    if (!addy->cursor) {
       list = g_list_previous(address_entry->active);
       if (list) {
	   /*
	    * Concatenate the two e-mails.
	    */
	   extra = list->data;
	   extra->cursor = strlen(extra->user);
	   libbalsa_emailData_set_user(addy,
				       g_strconcat(extra->user,
						   addy->user,
						   NULL));
	   
	   /*
	    * Free a whole bunch of RAM.
	    */
	   libbalsa_emailData_free(addy);
	   libbalsa_delete_link(list, address_entry->active);
	   address_entry->active = list;
       }

    /*
     * Normal character needs deleting.
     */
    } else {
        gchar *src= addy->user + addy->cursor, *dest = g_utf8_prev_char(src);
	int chlen = dest-src;
        while( (*dest++ = *src++) )
	    ;
	addy->cursor += chlen;
	if (*addy->user == '\0')
	    libbalsa_force_no_match(addy);
	else if (address_entry->find_match)
	    (*address_entry->find_match) (addy, TRUE);
    }
}


/*************************************************************
 * libbalsa_delete_forward_character:
 *     If there is text selected (by mouse, or whatever):
 *         delete the selected text
 *     else:
 *         If there is a "temporary match" just to the right
 *         of the cursor:
 *             Assume the user wishes to delete the
 *             "temporary match".
 *         else:
 *             Delete the character to the right of the cursor.
 *
 *   arguments:
 *     address_entry: the widget.
 *
 *   results:
 *     Changes the appearance of the widget.
 *************************************************************/
static void
libbalsa_delete_forward_character(LibBalsaAddressEntry *address_entry)
{
    GtkEditable *editable;
    emailData *addy, *extra;
    GList *list;
    
    addy = address_entry->active->data;
    editable = GTK_EDITABLE(address_entry);
    if (libbalsa_get_selection_bounds(editable, NULL, NULL) && !addy->match) {
	libbalsa_cut_clipboard(GTK_ENTRY(address_entry));
	return;
    }

    /*
     * Lets see if the user is at the end of an e-mail entry.
     */
    if (addy->user[addy->cursor] == '\0') {
        /*
         * Check if the user wanted to delete a match.
         */
        if (addy->match) {
           libbalsa_force_no_match(addy);
           return;
        }
    
	list = g_list_next(address_entry->active);
	if (list) {
	    /*
	     * Concatenate the two e-mails.
	     */
	    extra = list->data;
            libbalsa_emailData_set_user(addy,
                                        g_strconcat(addy->user,
                                                    extra->user, NULL));
	   
	    /*
	     * Free a whole bunch of RAM.
	     */
	    libbalsa_emailData_free(extra);
	    libbalsa_delete_link(list, list);
	}
    /*
     * Normal character needs deleting.
     */
    } else {
        gchar *dest= addy->user + addy->cursor, *src = g_utf8_next_char(dest);
        while( (*dest++ = *src++) )
	    ;
    }
}


/*************************************************************
 * libbalsa_accept_match:
 *     This is a wrapper around libbalsa_alias_accept_match().
 *
 *   arguments:
 *     address_entry: the widget.
 *
 *   results:
 *     Changes the appearance of the widget.
 *************************************************************/
static gboolean
libbalsa_accept_match(LibBalsaAddressEntry *address_entry)
{
    emailData *addy;

    g_return_val_if_fail(address_entry != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry), FALSE);
	    
    if (!address_entry->active)
	return TRUE;

    g_assert(address_entry->active->data != NULL);

    addy = address_entry->active->data;
    if (addy->match) {
	libbalsa_alias_accept_match(addy);
	return TRUE;
    }
    return FALSE;
}


/*************************************************************
 * libbalsa_keystroke_enter:
 *     Accept the current input in the LibBalsaAddressEntry.
 *     Makes a policy decision on whether or not to add
 *     @default.domain.com.
 *
 *   arguments:
 *     address_entry: the widget.
 *
 *   results:
 *     Changes the appearance of the widget.
 *************************************************************/
static void 
libbalsa_keystroke_enter(LibBalsaAddressEntry *address_entry)
{
    emailData *addy;

    g_return_if_fail(address_entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry));

    /*
     * User pressed ENTER.  If there was a match, accept the match.
     */
    if (libbalsa_accept_match(address_entry))
	return;

    /*
     * Else no match was found.  Check if there was a default
     * domain to add to e-mail addresses.
     */
    if (!address_entry->domain || *address_entry->domain == '\0')
	return;

    /*
     * There is a default domain to add.  Do we need to add it?
     */
    addy = address_entry->active->data;
    if (libbalsa_is_an_email(addy->user) || *addy->user == '\0')
	return;

    /*
     * Okay, add it.
     */
    libbalsa_emailData_set_user(addy,
                                g_strconcat(addy->user, "@",
                                            address_entry->domain,
                                            NULL));
    return;
}


/*************************************************************
 * libbalsa_keystroke_down:
 *     Simply deletes any match the currently active e-mail
 *     address might have had.
 *
 *   arguments:
 *     address_entry: the widget.
 *
 *   results:
 *     Changes the appearance of the widget.
 *************************************************************/
static void
libbalsa_keystroke_down(LibBalsaAddressEntry *address_entry)
{
    emailData *addy;

    addy = address_entry->active->data;
    libbalsa_force_no_match(addy);
}


/*************************************************************
 * libbalsa_keystroke_down:
 *     Moves the cursor to the beginning of the
 *     LibBalsaAddressEntry widget.
 *
 *   arguments:
 *     address_entry: the widget.
 *
 *   results:
 *     Changes the appearance of the widget.
 *************************************************************/
static void
libbalsa_keystroke_home(LibBalsaAddressEntry *address_entry)
{
    emailData *addy;
    
    g_return_if_fail(address_entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry));

    addy = address_entry->active->data;
    if (addy->match)
	libbalsa_alias_accept_match(addy);
    address_entry->active = g_list_first(address_entry->active);
    addy = address_entry->active->data;
    addy->cursor = 0;
}


/*************************************************************
 * libbalsa_move_backward_character:
 *     Moves the cursor one character further left in the
 *     LibBalsaAddressEntry widget.
 *
 *   arguments:
 *     address_entry: the widget.
 *
 *   results:
 *     Changes the appearance of the widget.
 *************************************************************/
static void
libbalsa_move_backward_character(LibBalsaAddressEntry *address_entry)
{
    emailData *addy;

    addy = address_entry->active->data;
    if (addy->cursor > 0) {
	addy->cursor = g_utf8_prev_char(addy->user + addy->cursor)-addy->user;
	libbalsa_force_no_match(addy);
    } else if (g_list_previous(address_entry->active)) {
	address_entry->active = g_list_previous(address_entry->active);
	addy = address_entry->active->data;
	addy->cursor = strlen(addy->user);
    }
}


/*************************************************************
 * libbalsa_move_forward_character:
 *     Moves the cursor one character further right in the
 *     LibBalsaAddressEntry widget.
 *
 *   arguments:
 *     address_entry: the widget.
 *
 *   results:
 *     Changes the appearance of the widget.
 *************************************************************/
static void
libbalsa_move_forward_character(LibBalsaAddressEntry *address_entry)
{
    emailData *addy;
    GList *list;

    addy = address_entry->active->data;
    if (addy->user[addy->cursor]) {
	addy->cursor = g_utf8_next_char(addy->user + addy->cursor)-addy->user;
	libbalsa_force_no_match(addy);
    } else if ((list = g_list_next(address_entry->active))) {
	address_entry->active = list;
	addy = list->data;
	addy->cursor = 0;
    }
}


/*************************************************************
 * libbalsa_keystroke_end:
 *     Moves the cursor to the end of the
 *     LibBalsaAddressEntry widget.
 *
 *   arguments:
 *     address_entry: the widget.
 *
 *   results:
 *     Changes the appearance of the widget.
 *************************************************************/
static void
libbalsa_keystroke_end(LibBalsaAddressEntry *address_entry) {
    emailData *addy;

    g_return_if_fail(address_entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry));
	    
    addy = address_entry->active->data;
    if (addy->match)
	libbalsa_alias_accept_match(addy);
    address_entry->active = g_list_last(address_entry->active);
    addy = address_entry->active->data;
    addy->cursor = strlen(addy->user);
}


/*************************************************************
 * libbalsa_keystroke_comma:
 *     A comma indicates acceptance of the current alias
 *     expansion.  We need to make sure we expand to what the
 *     user currently sees, and add a comma to the list.
 *
 *     Possible scenarios:
 *       User sees: "abc|" (| represents cursor)
 *       After must be: "abc@default.com, |"
 *
 *       User sees: "abc@def.com|"
 *       After must be: "abc@def.com, |"
 *
 *       User sees: "abc| (John Doe <john@default.com>)"
 *       After must be: "John Doe <john@default.com, |"
 *
 *       User sees: "abc|def"
 *       After must be: "abc@default.com, |def"
 *
 *   arguments:
 *     address_entry: the widget.
 *
 *   results:
 *     Changes the appearance of the widget.
 *************************************************************/
static void
libbalsa_keystroke_comma(LibBalsaAddressEntry *address_entry)
{
    emailData *addy, *extra;
    gchar *left, *right;
    
    g_return_if_fail(address_entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry));

    /*
     * First, check if it expanded, and accept it.
     */
    addy = address_entry->active->data;
    left = addy->user;
    right = NULL;
    if (addy->match) {
	libbalsa_alias_accept_match(addy);
    } else {
	if ((addy->cursor > 0) && (addy->user[addy->cursor] != '\0')) {
	    right = & addy->user[addy->cursor];
	}
    }

    /*
     * At this point, right points to the split where the comma must
     * happen, or it points to NULL
     */

    /*
     * Now we add a new entry.
     */
    if (right) {
        extra = libbalsa_emailData_new(right, strlen(right));
        libbalsa_emailData_set_user(addy, g_strndup(left, addy->cursor));
    } else
        extra = libbalsa_emailData_new("", 0);
    g_list_insert(address_entry->active, extra, 1);

    /*
     * We move the cursor to the right spot in the NEW entry.
     */
    address_entry->active = g_list_next(address_entry->active);
    extra->cursor = 0;
    
    /*
     * And we add the default domain to the original entry.
     */
    if (!address_entry->domain ||
	*address_entry->domain == '\0' ||
	libbalsa_is_an_email(addy->user)) return;

    libbalsa_emailData_set_user(addy,
                                g_strconcat(addy->user, "@",
                                            address_entry->domain,
                                            NULL));
}


/*************************************************************
 * libbalsa_keystroke_add_key:
 *     Add a normal alphanumeric key to the input.
 *
 *   arguments:
 *     address_entry: the widget.
 *
 *   results:
 *     Changes the appearance of the widget.
 *************************************************************/
static void
libbalsa_keystroke_add_key(LibBalsaAddressEntry *address_entry, gchar *add)
{
    emailData *addy;
    gchar *left, *right;
    GtkEditable *editable;

    g_return_if_fail(address_entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry));
    g_return_if_fail(add != NULL);

    editable = GTK_EDITABLE(address_entry);
    addy = address_entry->active->data;
    if (libbalsa_get_selection_bounds(editable, NULL, NULL) && !addy->match) {
	libbalsa_cut_clipboard(GTK_ENTRY(address_entry));
    }

    /*
     * User typed a key, so cancel any matches we may have had.
     */
    addy = address_entry->active->data;
    libbalsa_force_no_match(addy);
    
    /*
     * If this is at the beginning, and the user pressed ' ',
     * ignore it.
     */
    if (!addy->cursor && *add == ' ') return;
    
     /*
     * Split the string at the correct cursor position.
     */
    left = g_strndup(addy->user, addy->cursor);
    right = addy->user + addy->cursor;
    
    /*
     * Add the keystroke to the end of user input.
     */
    libbalsa_emailData_set_user(addy, g_strconcat(left, add, right, NULL));
    g_free(left);
    addy->cursor += strlen(add);

    /*
     * Now search for (any) match.
     */
    addy->tabs = 0;
    /*
    expand_alias_find_match(addy);
    */
    if (address_entry->find_match)
	(*address_entry->find_match) (addy, TRUE);
}


/*************************************************************
 * libbalsa_keystroke_cycle:
 *     Cycle to the next match for the current user input.
 *
 *   arguments:
 *     address_entry: the widget.
 *
 *   results:
 *     Changes the appearance of the widget.
 *************************************************************/
static void
libbalsa_keystroke_cycle(LibBalsaAddressEntry *address_entry)
{
    emailData *addy;

    g_return_if_fail(address_entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry));

    /*
     * Are we at the first cursor position in the GtkEntry box,
     */
    if (gtk_editable_get_position(GTK_EDITABLE(address_entry)) == 0) {
	gtk_widget_activate(GTK_WIDGET(address_entry));
	return;
    }

    /*
     * Are we at the last position?
     */
    addy = address_entry->active->data;
    if (!g_list_next(address_entry->active)) {
	if (addy->user[addy->cursor] == '\0' && !addy->match) {
	    libbalsa_accept_match(address_entry);
	    gtk_widget_activate(GTK_WIDGET(address_entry));
	    return;
	}
    }

    /*
     * Nope - so cycle through the possible matches.
     */
    addy->tabs += 1;
    /*
    expand_alias_find_match(addy);
    */
    if (address_entry->find_match)
	(*address_entry->find_match) (addy, TRUE);
}


/*************************************************************
 * libbalsa_force_expand:
 *     force alias expansion.
 *
 *   arguments:
 *     address_entry: the widget.
 *
 *   results:
 *     modifies address_entry
 *************************************************************/
static void
libbalsa_force_expand(LibBalsaAddressEntry *address_entry)
{
    emailData *addy;

    addy = address_entry->active->data;
    if (address_entry->find_match)
	(*address_entry->find_match) (addy, FALSE);
}

/*************************************************************
 * libbalsa_paste_clipboard:
 *     Wraps around gtk_editable_paste_clipboard to ensure
 *     that user data gets reloaded into the correct
 *     data structures.
 *
 *   arguments:
 *     address_entry: the widget.
 *
 *   results:
 *     modifies address_entry
 *************************************************************/
static void
libbalsa_paste_clipboard(GtkEntry *entry)
{
    parent_class->paste_clipboard(entry);
    libbalsa_fill_input(LIBBALSA_ADDRESS_ENTRY(entry));
}


/*************************************************************
 * libbalsa_cut_clipboard:
 *     Wraps around gtk_editable_paste_clipboard to ensure
 *     that user data gets reloaded into the correct
 *     data structures.
 *
 *   arguments:
 *     address_entry: the widget.
 *
 *   results:
 *     modifies address_entry
 *************************************************************/
static void
libbalsa_cut_clipboard(GtkEntry *entry)
{
    parent_class->cut_clipboard(entry);
    libbalsa_fill_input(LIBBALSA_ADDRESS_ENTRY(entry));
}


/*************************************************************
 * libbalsa_address_entry_key_press:
 *     Default keyboard handler.
 *
 *   credits:
 *     Some of this comes from gtk+-1.2.8/gtk/gtkentry.c
 *
 *   arguments:
 *     address_entry: the widget.
 *
 *   results:
 *     modifies address_entry
 *************************************************************/
static gint
libbalsa_address_entry_key_press(GtkWidget *widget, GdkEventKey *event)
{
    GtkEditable *editable;
    GtkEntry *entry;
    LibBalsaAddressEntry *address_entry;

    gboolean return_val;
    gint key;

    g_return_val_if_fail(widget != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(widget), FALSE);
    g_return_val_if_fail(event != NULL, FALSE);

    editable = GTK_EDITABLE(widget);
    entry = GTK_ENTRY(widget);
    address_entry = LIBBALSA_ADDRESS_ENTRY(widget);

    /*
     * Skip it if its not editable...
     */
    if (!libbalsa_get_editable(editable)) return FALSE;

    /*
     * Setup variables.
     */
    return_val = FALSE;

    /*
     * Check if we have lost focus.
     */
    if (address_entry->focus != FOCUS_CACHED) {
	libbalsa_fill_input(address_entry);
    }
    address_entry->focus = FOCUS_CACHED;

    /*
     * Process the keystroke.
     */
    switch (event->keyval) {
    case GDK_BackSpace:	/* done */
	return_val = TRUE;
	if (event->state & GDK_CONTROL_MASK)
	    libbalsa_delete_backward_word(address_entry);
	else
	    libbalsa_delete_backward_character(address_entry);
	break;
    /*
     * UNTESTED: GDK_Clear doesn't exist on my keyboard.
     */
    case GDK_Clear:	/* Untested */
	return_val = TRUE;
	libbalsa_delete_line(address_entry);
	break;
    /*
     * FIXME: Shift-Insert calls gtk_editable_paste_clipboard(),
     *        but nothing happens.
     */
    case GDK_Insert:	/* done */
	return_val = TRUE;
	if (event->state & GDK_SHIFT_MASK) {
	    libbalsa_paste_clipboard(entry);
	} else if (event->state & GDK_CONTROL_MASK)
	    gtk_editable_copy_clipboard(editable);
	break;
    case GDK_Delete:	/* done */
	return_val = TRUE;
	if (event->state & GDK_CONTROL_MASK) /* done */
	    libbalsa_delete_forward_word(address_entry);
	else if (event->state & GDK_SHIFT_MASK) {
	    libbalsa_cut_clipboard(entry);
	} else
	    libbalsa_delete_forward_character(address_entry);
	break;
    case GDK_Home:	/* done */
	libbalsa_keystroke_home(address_entry);
	return_val = TRUE;
	break;
    case GDK_End:	/* done */
	libbalsa_keystroke_end(address_entry);
	break;
    /*
     * UNTESTED:
     * Ctrl-Left and Ctrl-Right are untested, because they don't work
     * on my keyboard.  They don't trigger in GtkEntry either.
     */
    case GDK_Left:	/* done */
	return_val = TRUE;
	if (event->state & GDK_CONTROL_MASK)
	    libbalsa_move_backward_word(address_entry);
	else
	    libbalsa_move_backward_character(address_entry);
	break;
    case GDK_Right:	/* done */
	return_val = TRUE;
	if (event->state & GDK_CONTROL_MASK)
	    libbalsa_move_forward_word(address_entry);
	else
	    libbalsa_move_forward_character(address_entry);
	break;
    case GDK_Linefeed:	/* Not tested */
    case GDK_Tab:	/* Done */
    case GDK_Return:	/* FIXME: doesn't move to next box.  Why not??? */
	if (event->state & GDK_SHIFT_MASK)
	    break;
	libbalsa_keystroke_enter(address_entry);
	gtk_widget_activate(widget);
	break;
    case GDK_Up:	/* done */
    case GDK_Down:
	return_val = TRUE;
	libbalsa_keystroke_cycle(address_entry);
	break;
    case GDK_Page_Up:	/* done */
    case GDK_Page_Down:
	return_val = TRUE;
	libbalsa_keystroke_down(address_entry);
	gtk_widget_activate(widget);
	break;
    case GDK_comma:	/* done */
	return_val = TRUE;
	libbalsa_keystroke_comma(address_entry);
	break;
    /* The next key should not be inserted literally. Any others ??? */
    case GDK_Escape:
	break;
    default:		/* done */
	/*
	 * Check for a-zA-Z0-9, etc.
	 */
	if ((event->keyval >= 0x20) && (event->keyval <= 0xFF)) {
	    key = event->keyval;
	    /*
	     * Check for Ctrl-anything
	     */
	    if (event->state & GDK_CONTROL_MASK) {
		if ((key >= 'A') && (key <= 'Z'))
		    key -= 'A' - 'a';
		if ((key >= 'a') && (key <= 'z') && control_keys[key - 'a']) {
		    (* control_keys[key - 'a']) (editable, event->time);
		    return_val = TRUE;
		}
	        break;
	    /*
	     * Check for Alt-anything
	     */
	    } else if (event->state & GDK_MOD1_MASK) {
		if ((key >= 'A') && (key <= 'Z'))
		    key -= 'A' - 'a';
	        if ((key >= 'a') && (key <= 'z') && alt_keys[key - 'a']) {
		    (* alt_keys[key - 'a']) (editable, event->time);
		    return_val = TRUE;
		}
	        break;
	    }
	}
	/*
	 * Its not Alt, and its not Ctrl, so add the key.
	 */
	if (event->length > 0) {
	    return_val = TRUE;
	    libbalsa_keystroke_add_key(address_entry, event->string);
	}
	break;
    }

    /*
     * Show the data.
     */
    libbalsa_address_entry_show(address_entry);
    return return_val;
}

/*************************************************************
 * libbalsa_address_entry_show:
 *     Shows the widget.  This will work out the aliases,
 *     and set the widget text to that.
 *
 *   arguments:
 *     address_entry: the widget.
 *
 *   results:
 *     modifies address_entry
 *
 *   FIXME:
 *     - Adding the lists together should be easier.
 *     - It should make more use of helper functions
 *       that got written after this function got written,
 *       like libbalsa_make_address_string()
 *************************************************************/
static void
libbalsa_address_entry_show(LibBalsaAddressEntry *address_entry)
{
    GtkEditable *editable;
    GString *show;
    GList *list;
    emailData *addy;
    gint cursor, end;

    g_return_if_fail(address_entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry));

    if (!GTK_WIDGET_DRAWABLE(address_entry)) return;

    editable = GTK_EDITABLE(address_entry);

    show = g_string_new("");
    cursor = end = 0;
    for (list = g_list_first(address_entry->active);
	 list;
	 list = g_list_next(list)) {
	addy = (emailData *)list->data;
	g_assert(addy != NULL);
        if (list == address_entry->active) {
	    int curbyte = show->len + addy->cursor;
	    g_string_append(show, addy->user);
            cursor = end = 
		g_utf8_pointer_to_offset(show->str,
					 show->str + curbyte);
	} else g_string_append(show, addy->user);

	if (addy->match) {
	    g_string_append(show, " (");
	    g_string_append(show, addy->match);
	    g_string_append(show, ") ");
            end = show->len;
	}
	if (g_list_next(list))
	    g_string_append(show, ", ");
    }

    /* has the text changed? */
    if (strcmp(gtk_entry_get_text(GTK_ENTRY(address_entry)), show->str))
        gtk_entry_set_text(GTK_ENTRY(address_entry), show->str);
    gtk_editable_select_region(editable, end, cursor);
#if BALSA_MAJOR == 1
    /* this shouldn't be needed with gtk+-2.0 */
    gtk_editable_set_position(editable, cursor);
#endif                          /* BALSA_MAJOR == 1 */
    g_string_free(show, TRUE);
}


/*************************************************************
 * libbalsa_address_clear_match:
 *     Clears the input cache of any data.
 *
 *   arguments:
 *     address_entry: the widget.
 *
 *   results:
 *     modifies address_entry
 *************************************************************/
static void
libbalsa_address_entry_clear_match(LibBalsaAddressEntry *address_entry)
{
    emailData *addy;

    g_return_if_fail(address_entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry));

    /*
     * First grab the input, and get rid of any matches we may
     * or may not have found.  This is done for two reasons:
     * - It makes it easier to parse the matches back.
     * - It makes it impossible to put the cursor out of bounds.
     */
    if (address_entry->active) {
	addy = address_entry->active->data;
	libbalsa_force_no_match(addy);
	/*
	 * Now show the changes.
	 */
       libbalsa_address_entry_show(address_entry);
    }
    /*
     * And mark the GtkEntry as 'tainted'.
     */
    address_entry->focus = FOCUS_TAINTED;

    return;
}


/*
 * ==========================================================================
 * 
 * API for using this widget.
 *
 * These are the "public" functions.
 *
 * ==========================================================================
 */


/*************************************************************
 * libbalsa_address_entry_new:
 *     Allocate a new LibBalsaAddressEntry for use.
 *
 *   arguments:
 *     none.
 *
 *   results:
 *     Returns a newly allocated LibBalsaAddressEntry.
 *************************************************************/
GtkWidget *
libbalsa_address_entry_new(void)
{
    LibBalsaAddressEntry *entry;

    entry =
        LIBBALSA_ADDRESS_ENTRY(g_object_new
                               (LIBBALSA_TYPE_ADDRESS_ENTRY, NULL));
    return GTK_WIDGET(entry);
}


/*************************************************************
 * libbalsa_address_entry_focus_out:
 *     Called when focus is lost.  Its main purpose is to keep
 *     track, so we can cache user-input safely.
 *
 *   arguments:
 *     widget: the widget.
 *     event:  the event.
 *
 *   results:
 *     Sets a flag in the widget.
 *************************************************************/
static gint
libbalsa_address_entry_focus_out(GtkWidget *widget, GdkEventFocus *event)
{
    LibBalsaAddressEntry *address_entry;

    g_return_val_if_fail(widget != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(widget), FALSE);
    g_return_val_if_fail(event != NULL, FALSE);

#if BALSA_MAJOR == 1
    GTK_WIDGET_UNSET_FLAGS(widget, GTK_HAS_FOCUS);
    /* not in gtk+-2.0: */
    gtk_widget_draw_focus(widget);
#endif                          /* BALSA_MAJOR == 1 */

    address_entry = LIBBALSA_ADDRESS_ENTRY(widget);
    if (address_entry->focus == FOCUS_CACHED) {
	libbalsa_address_entry_clear_match(address_entry);
	libbalsa_address_entry_show(address_entry);
    }
    address_entry->focus = FOCUS_TAINTED;
#ifdef USE_XIM
    gdk_im_end ();
#endif
    return
        GTK_WIDGET_CLASS(parent_class)->focus_out_event(widget, event);
}


/*************************************************************
 * libbalsa_address_entry_set_find_match:
 *     Sets the function to call to match emailData structures
 *     to something valid.  The user can set this because that
 *     function probably depends on completely different data
 *     than this widget.
 *
 *   arguments:
 *     address_entry: the widget.
 *     matcher:       the function to call.
 *
 *   results:
 *     Sets a function pointer in the widget.
 *************************************************************/
void
libbalsa_address_entry_set_find_match(LibBalsaAddressEntry *address_entry,
	void *matcher)
{
    g_return_if_fail(address_entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry));

    address_entry->find_match = matcher;
}


/*************************************************************
 * libbalsa_address_entry_set_domain:
 *     Set the default domain to append.  It does not copy
 *     the domain, so don't free it until after the widget
 *     is destroyed.
 *
 *   arguments:
 *     address_entry: the widget.
 *     domain:        the domain.
 *
 *   results:
 *     Sets the domain in the widget.
 *************************************************************/
void
libbalsa_address_entry_set_domain(LibBalsaAddressEntry *address_entry,
	void *domain)
{
    g_return_if_fail(address_entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry));

    address_entry->domain = g_strdup(domain);
}


/*************************************************************
 * libbalsa_address_entry_clear_to_send:
 *     Clears the current input buffer of incomplete alias
 *     matches, so that its clear to send this message.
 *
 *   arguments:
 *     widget: the widget.
 *
 *   results:
 *     Modifies the text in the widget.
 *************************************************************/
void
libbalsa_address_entry_clear_to_send(LibBalsaAddressEntry * address_entry)
{
    emailData *addy;

    /*
     * Grab the input.
     */
    if (!address_entry->active)
        return;

    /*
     * Set the active data to no match.
     */
    addy = address_entry->active->data;
    libbalsa_force_no_match(addy);

    /*
     * Show the input, so that we fill the GtkEntry box.
     */
    libbalsa_address_entry_show(address_entry);
}

/*************************************************************
 * libbalsa_address_entry_matching:
 *     Is the widget showing a match? 
 *************************************************************/
gboolean 
libbalsa_address_entry_matching(LibBalsaAddressEntry * address_entry)
{
    if (address_entry->active) {
        emailData *addy = address_entry->active->data;

        if (addy->match)
            return TRUE;
    }
    return FALSE;
}

/*
 * Compatibility:
 */
static gboolean
libbalsa_get_selection_bounds(GtkEditable * editable, gint * start,
                              gint * end)
{
#if BALSA_MAJOR == 1
    if (start)
        *start = editable->selection_start_pos;
    if (end)
        *end = editable->selection_end_pos;
    return editable->has_selection;
#else
    return gtk_editable_get_selection_bounds(editable, start, end);
#endif                          /* BALSA_MAJOR == 1 */
}

static gboolean
libbalsa_get_editable(GtkEditable * editable)
{
#if BALSA_MAJOR == 1
    return editable->editable;
#else
    return gtk_editable_get_editable(editable);
#endif                          /* BALSA_MAJOR == 1 */
}

static GList *
libbalsa_delete_link(GList * list, GList * link)
{
#if BALSA_MAJOR == 1
    list = g_list_remove_link(list, link);
    g_list_free_1(link);
    return list;
#else
    return g_list_delete_link(list, link);
#endif                          /* BALSA_MAJOR == 1 */
}

/* libbalsa_address_entry_get_list:
   create list of LibBalsaAddress objects corresponding to the
   entry content. If possible, references objects from the address books.
   if not, creates new ones.
   The objects must be dereferenced later and the list disposed, eg.
   g_list_foreach(l, g_object_unref, NULL); g_list_free(l);
*/
GList*
libbalsa_address_entry_get_list(LibBalsaAddressEntry *address_entry)
{
    GList *l, *res = NULL;
    
    /* FIXME: this should not be necessary but is to parse address list
     * set by eg. gtk_entry_set_text(). We should do it earlier, though. */
    if(!address_entry->active) 
        libbalsa_fill_input(address_entry);

    for(l = g_list_first(address_entry->active); l;  l = l->next) {
        emailData *ed = l->data;
        if(!ed->user || !*ed->user)
            continue;
        if(ed->address) {
            g_object_ref(ed->address);
            res = g_list_append(res, ed->address);
        } else {
	    LibBalsaAddress *addr =
		libbalsa_address_new_from_string(ed->user);
	    if (addr)
		res = g_list_append(res, addr);
        }
    }
    return res;
}
#else /* NEW_ADDRESS_ENTRY_WIDGET */
/*************************************************************
 *     Address entry widget based on GtkEntryCompletion.
 *************************************************************/

#include "config.h"
#include <stdio.h>
#include <gtk/gtk.h>

#include "address-book.h"

/*************************************************************
 *     Static data.
 *************************************************************/

static GList *lbae_address_book_list;
enum {
    NAME_COL,
    ADDRESS_COL
};

/*************************************************************
 *     Data structure attached to the GtkCompletion.
 *************************************************************/
typedef struct {
    GSList *list;
    GSList *active;
    gchar *domain;
} LibBalsaAddressEntryInfo;

#define LIBBALSA_ADDRESS_ENTRY_INFO "libbalsa-address-entry-info"

/*************************************************************
 *     Allocate and initialize LibBalsaAddressEntryInfo.
 *************************************************************/
static LibBalsaAddressEntryInfo *
lbae_info_new(void)
{
    LibBalsaAddressEntryInfo *info;

    info = g_new(LibBalsaAddressEntryInfo, 1);
    info->list = NULL;
    info->active = NULL;
    info->domain = NULL;

    return info;
}

/*************************************************************
 *     Deallocate LibBalsaAddressEntryInfo.
 *************************************************************/
static void
lbae_info_free(LibBalsaAddressEntryInfo * info)
{
    g_slist_foreach(info->list, (GFunc) g_free, NULL);
    g_slist_free(info->list);
    g_free(info->domain);
    g_free(info);
}

/* Helpers. */

/*************************************************************
 *     Does the UTF-8 string begin with the prefix?
 *************************************************************/
static gboolean
lbae_utf8_name_has_prefix(const gchar * name, const gchar * prefix)
{
    gchar *name_n, *name_f, *prefix_n, *prefix_f;
    gboolean retval;

    if (!name || !prefix)
        return FALSE;

    name_n = g_utf8_normalize(name, -1, G_NORMALIZE_DEFAULT);
    name_f = g_utf8_casefold(name_n, -1);
    g_free(name_n);
    prefix_n = g_utf8_normalize(prefix, -1, G_NORMALIZE_DEFAULT);
    prefix_f = g_utf8_casefold(prefix_n, -1);
    g_free(prefix_n);
    retval = g_str_has_prefix(name_f, prefix_f);
    g_free(name_f);
    g_free(prefix_f);

    return retval;
}

/*************************************************************
 *     Is the name in a row of the model?  If so, return the
 *     corresponding address, if requested.
 *************************************************************/
static gboolean
lbae_name_in_model(const gchar * name, GtkTreeModel * model,
                   LibBalsaAddress ** address)
{
    GtkTreeIter iter;
    gboolean valid;

    if (!name)
        return FALSE;

    for (valid = gtk_tree_model_get_iter_first(model, &iter); valid;
         valid = gtk_tree_model_iter_next(model, &iter)) {
        gchar *this_name;
        LibBalsaAddress *this_address;

        gtk_tree_model_get(model, &iter,
                           NAME_COL, &this_name,
                           ADDRESS_COL, &this_address, -1);
        if (this_name && strcmp(name, this_name) == 0) {
            if (address)
                *address = this_address;
            else if (this_address)
                g_object_unref(this_address);
            g_free(this_name);
            return TRUE;
        }
        g_free(this_name);
        if (this_address)
            g_object_unref(this_address);
    }

    return FALSE;
}

/*************************************************************
 *     Create an address string with either the addressee's
 *     full name or nick name, depending on which we matched;
 *     returns a newly allocated string, which must be
 *     deallocated with g_free.
 *************************************************************/
static gchar *
lbae_get_name_from_address(LibBalsaAddress * address, const gchar * prefix)
{
    gchar *address_string;

    if (lbae_utf8_name_has_prefix(address->full_name, prefix))
        address_string = libbalsa_address_to_gchar(address, 0);
    else {
        LibBalsaAddress *tmp_address;

        tmp_address = libbalsa_address_new();
        libbalsa_address_set_copy(address, tmp_address);
        g_free(tmp_address->full_name);
        tmp_address->full_name = g_strdup(tmp_address->nick_name);

        address_string = libbalsa_address_to_gchar(tmp_address, 0);
        g_object_unref(tmp_address);
    }

    if (address_string) {
        /* dequote */
        gchar *p, *q;

        for (p = q = address_string; *p;) {
            if (*p == '\\') {
                *q++ = *p++;
                if (*p)
                    *q++ = *p++;
            } else if (*p == '"')
                p++;
            else
                *q++ = *p++;
        }
        *q = '\0';
    }

    return address_string;
}

/*************************************************************
 *     Parse the entry's text and populate the
 *     LibBalsaAddressEntryInfo.
 *************************************************************/
static void
lbae_parse_entry(GtkEntry * entry, LibBalsaAddressEntryInfo * info)
{
    const gchar *string, *p;
    gint position;
    gboolean quoted;

    g_slist_foreach(info->list, (GFunc) g_free, NULL);
    g_slist_free(info->list);
    info->list = NULL;

    p = string = gtk_entry_get_text(entry);
    position = gtk_editable_get_position(GTK_EDITABLE(entry));

    info->active = NULL;
    quoted = FALSE;
    while (*p) {
        gunichar c;
        const gchar *q;

        c = g_utf8_get_char(p);
        q = g_utf8_next_char(p);
        --position;
        /* position is the number of characters between c and the cursor. */
        if (c == '"')
            quoted = !quoted;
        else if (!quoted && c == ',') {
            info->list =
                g_slist_prepend(info->list,
                                g_strstrip(g_strndup(string, p - string)));
            if (position < 0 && !info->active)
                /* The cursor was in the string we just saved. */
                info->active = info->list;
            string = q;
        }
        p = q;
    }
    info->list =
        g_slist_prepend(info->list,
                        g_strstrip(g_strndup(string, p - string)));
    if (!info->active)
        info->active = info->list;
    info->list = g_slist_reverse(info->list);
}

/*************************************************************
 *     Create a GList of addresses matching the prefix.
 *************************************************************/
static GList *
lbae_get_matching_addresses(const gchar * prefix)
{
    GList *match = NULL, *list;
    gchar *prefix_up;

    prefix_up = g_utf8_strup(prefix, -1);
    for (list = lbae_address_book_list; list; list = list->next) {
        LibBalsaAddressBook *ab;
        gchar *new_prefix;

        ab = LIBBALSA_ADDRESS_BOOK(list->data);
        if (!ab->expand_aliases || ab->is_expensive)
            continue;

        match =
            g_list_concat(match,
                          libbalsa_address_book_alias_complete(ab,
                                                               prefix_up,
                                                               &new_prefix));
        g_free(new_prefix);
    }
    g_free(prefix_up);

    return match;
}

/*************************************************************
 *     Update the GtkEntryCompletion's GtkTreeModel with
 *     the list of addresses.
 *************************************************************/
static void
lbae_append_addresses(GtkEntryCompletion * completion, GList * match,
                      const gchar * prefix)
{
    LibBalsaAddressEntryInfo *info;
    GtkTreeModel *model;
    GtkListStore *store;
    GtkTreeIter iter;
    gboolean valid;

    info = g_object_get_data(G_OBJECT(completion),
                             LIBBALSA_ADDRESS_ENTRY_INFO);
    model = gtk_entry_completion_get_model(completion);
    store = GTK_LIST_STORE(model);

    /* Remove any previous autocompletion row; it has a NULL address. */
    for (valid = gtk_tree_model_get_iter_first(model, &iter); valid;) {
        LibBalsaAddress *address;

        gtk_tree_model_get(model, &iter, ADDRESS_COL, &address, -1);
        valid = address ? gtk_tree_model_iter_next(model, &iter) :
            gtk_list_store_remove(store, &iter);
    }
    /* Synchronize the filtered model. */
    gtk_entry_completion_complete(completion);

    for (; match; match = match->next) {
        gchar *name;

        name = match->data ?
            lbae_get_name_from_address(match->data, prefix) :
            g_strconcat(prefix, "@", info->domain, NULL);
        if (!lbae_name_in_model(name, model, NULL)) {
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter, NAME_COL, name,
                               ADDRESS_COL, match->data, -1);
        }
        g_free(name);
    }
}

/* Callbacks. */

/*************************************************************
 *     The completion's GtkEntryCompletionMatchFunc.
 *************************************************************/
static gboolean
lbae_completion_match(GtkEntryCompletion * completion, const gchar * key,
                      GtkTreeIter * iter, gpointer user_data)
{
    LibBalsaAddressEntryInfo *info;
    const gchar *prefix;
    gchar *name;
    gboolean retval;

    info = g_object_get_data(G_OBJECT(completion),
                             LIBBALSA_ADDRESS_ENTRY_INFO);
    prefix = info->active->data;
    if (!*prefix)
        return FALSE;

    gtk_tree_model_get(gtk_entry_completion_get_model(completion), iter,
                       NAME_COL, &name, -1);
    retval = lbae_utf8_name_has_prefix(name, prefix);
    g_free(name);

    return retval;
}

/*************************************************************
 *     Callback for the entry's "changed" signal
 *************************************************************/
static void
lbae_entry_changed(GtkEntry * entry, gpointer data)
{
    GtkEntryCompletion *completion;
    LibBalsaAddressEntryInfo *info;
    const gchar *prefix;
    GList *match;

    completion = gtk_entry_get_completion(entry);
    info = g_object_get_data(G_OBJECT(completion),
                             LIBBALSA_ADDRESS_ENTRY_INFO);
    lbae_parse_entry(entry, info);
    prefix = info->active->data;
    if (!*prefix)
        return;

    match = lbae_get_matching_addresses(prefix);
    if (info->domain && *info->domain && !strpbrk(prefix, "@%!"))
        /* No domain in the user's entry, and the current identity has a
         * default domain, so we'll add user@domain as a possible
         * autocompletion; lbae_append_addresses treats a NULL address
         * as a request for that option. */
        match = g_list_append(match, NULL);
    lbae_append_addresses(completion, match, prefix);
    match = g_list_remove(match, NULL);
    g_list_foreach(match, (GFunc) g_object_unref, NULL);
    g_list_free(match);
}

/*************************************************************
 *     Callback for the completion's "match-selected" signal
 *************************************************************/
static gboolean
lbae_completion_match_selected(GtkEntryCompletion * completion,
                               GtkTreeModel * model, GtkTreeIter * iter,
                               gpointer user_data)
{
    LibBalsaAddressEntryInfo *info;
    GSList *list;
    gchar *name;
    GtkEditable *editable;
    gint position, cursor;

    info = g_object_get_data(G_OBJECT(completion),
                             LIBBALSA_ADDRESS_ENTRY_INFO);

    /* Replace the partial address with the selected one. */
    gtk_tree_model_get(model, iter, NAME_COL, &name, -1);
    g_free(info->active->data);
    info->active->data = name;

    /* Rewrite the entry. */
    editable = GTK_EDITABLE(gtk_entry_completion_get_entry(completion));
    g_signal_handlers_block_by_func(editable, lbae_entry_changed, NULL);
    gtk_editable_delete_text(editable, 0, -1);
    cursor = position = 0;
    for (list = info->list; list; list = list->next) {
        gtk_editable_insert_text(editable, list->data, -1, &position);
        if (list == info->active)
            cursor = position;
        if (list->next)
            gtk_editable_insert_text(editable, ", ", -1, &position);
    }
    gtk_editable_set_position(editable, cursor);
    g_signal_handlers_unblock_by_func(editable, lbae_entry_changed, NULL);

    return TRUE;
}

/* Public API. */

/*************************************************************
 *     Allocate a new LibBalsaAddressEntry for use.
 *************************************************************/
GtkWidget *
libbalsa_address_entry_new()
{
    GtkWidget *entry;
    GtkEntryCompletion *completion;
    GtkListStore *store;
    LibBalsaAddressEntryInfo *info;

    store = gtk_list_store_new(2, G_TYPE_STRING, LIBBALSA_TYPE_ADDRESS);

    completion = gtk_entry_completion_new();
    gtk_entry_completion_set_model(completion, GTK_TREE_MODEL(store));
    g_object_unref(store);
    gtk_entry_completion_set_match_func(completion,
                                        (GtkEntryCompletionMatchFunc)
                                        lbae_completion_match, NULL, NULL);
    gtk_entry_completion_set_text_column(completion, NAME_COL);
    g_signal_connect(completion, "match-selected",
                     G_CALLBACK(lbae_completion_match_selected), NULL);

    info = lbae_info_new();
    g_object_set_data_full(G_OBJECT(completion),
                           LIBBALSA_ADDRESS_ENTRY_INFO, info,
                           (GDestroyNotify) lbae_info_free);

    entry = gtk_entry_new();
    g_signal_connect(entry, "changed", G_CALLBACK(lbae_entry_changed),
                     NULL);
    gtk_entry_set_completion(GTK_ENTRY(entry), completion);
    g_object_unref(completion);

    return entry;
}

/*************************************************************
 *     Must be called before using the widget.
 *************************************************************/
void
libbalsa_address_entry_set_address_book_list(GList * list)
{
    lbae_address_book_list = list;
}

/*************************************************************
 *     Create list of LibBalsaAddress objects corresponding to the entry
 *     content. If possible, references objects from the address books.
 *     if not, creates new ones.  The objects must be dereferenced later
 *     and the list disposed, eg.  g_list_foreach(l, g_object_unref,
 *     NULL); g_list_free(l);
 *************************************************************/
GList *
libbalsa_address_entry_get_list(GtkEntry * address_entry)
{
    GtkEntryCompletion *completion;
    LibBalsaAddressEntryInfo *info;
    GtkTreeModel *model;
    GSList *list;
    GList *res = NULL;

    completion = gtk_entry_get_completion(address_entry);
    info = g_object_get_data(G_OBJECT(completion),
                             LIBBALSA_ADDRESS_ENTRY_INFO);
    model = gtk_entry_completion_get_model(completion);

    for (list = info->list; list; list = list->next) {
        LibBalsaAddress *address;

        if (!lbae_name_in_model(list->data, model, &address) || !address)
            address = libbalsa_address_new_from_string(list->data);
        if (address)
            res = g_list_prepend(res, address);
    }

    return g_list_reverse(res);
}

void
libbalsa_address_entry_set_domain(GtkEntry * address_entry, void *domain)
{
    GtkEntryCompletion *completion;
    LibBalsaAddressEntryInfo *info;

    g_return_if_fail(address_entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry));

    completion = gtk_entry_get_completion(address_entry);
    info = g_object_get_data(G_OBJECT(completion),
                             LIBBALSA_ADDRESS_ENTRY_INFO);
    g_free(info->domain);
    info->domain = g_strdup(domain);
}
#endif /* NEW_ADDRESS_ENTRY_WIDGET */
