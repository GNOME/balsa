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

/*
 * A subclass of gtkentry to support alias completion.
 */

#include "config.h"

#include <gnome.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdki18n.h>

#include <stdio.h>
#include <sys/stat.h>
#include <ctype.h>

/*
 * LibBalsa includes.
 */
#include "libbalsa.h"
#include "address-entry.h"

/*
 * Global variable.  We need this for access to parent methods.
 */
static GtkWidgetClass *parent_class = NULL;


/*
 * Function prototypes all GtkObjects need.
 */
static void libbalsa_address_entry_class_init(LibBalsaAddressEntryClass *klass);
static void libbalsa_address_entry_init(LibBalsaAddressEntry *ab);
static void libbalsa_address_entry_destroy(GtkObject * object);
static void libbalsa_address_entry_draw(GtkWidget * widget,
                                        GdkRectangle * area);

/*
 * Other function prototypes.
 */
static void libbalsa_inputData_free(inputData * data);
static inputData *libbalsa_inputData_new(void);
static emailData *libbalsa_emailData_new(void);
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
static void libbalsa_paste_clipboard		(LibBalsaAddressEntry *);
static void libbalsa_cut_clipboard		(LibBalsaAddressEntry *);

/*
 * Map the Control keys to relevant functions.
 */
static const GtkTextFunction control_keys[26] =
{
    (GtkTextFunction)libbalsa_keystroke_home,		/* a */
    (GtkTextFunction)libbalsa_move_backward_character,	/* b */
    (GtkTextFunction)gtk_editable_copy_clipboard,	/* c */
    (GtkTextFunction)libbalsa_delete_forward_character,	/* d */
    (GtkTextFunction)libbalsa_keystroke_end,		/* e */
    (GtkTextFunction)libbalsa_move_forward_character,	/* f */
    NULL,						/* g */
    (GtkTextFunction)libbalsa_delete_backward_character,/* h */
    NULL,						/* i */
    NULL,						/* j */
    (GtkTextFunction)libbalsa_delete_to_line_end,	/* k */
    NULL,						/* l */
    NULL,						/* m */
    NULL,						/* n */
    NULL,						/* o */
    NULL,						/* p */
    NULL,						/* q */
    (GtkTextFunction)libbalsa_force_expand,		/* r */
    NULL,						/* s */
    NULL,						/* t */
    (GtkTextFunction)libbalsa_delete_line,		/* u */
    (GtkTextFunction)libbalsa_paste_clipboard,		/* v */
    (GtkTextFunction)libbalsa_delete_backward_word,	/* w */
    (GtkTextFunction)libbalsa_cut_clipboard,		/* x */
    NULL,						/* y */
    NULL,						/* z */
};


/*
 * Map the Alt keys to relevant functions.
 */
static const GtkTextFunction alt_keys[26] =
{
    NULL,						/* a */
    (GtkTextFunction)libbalsa_move_backward_word,	/* b */
    NULL,						/* c */
    (GtkTextFunction)libbalsa_delete_forward_word,	/* d */
    NULL,						/* e */
    (GtkTextFunction)libbalsa_move_forward_word,	/* f */
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
	static const GtkTypeInfo address_entry_info = {
	    "LibBalsaAddressEntry",
	    sizeof(LibBalsaAddressEntry),
	    sizeof(LibBalsaAddressEntryClass),
	    (GtkClassInitFunc) libbalsa_address_entry_class_init,
	    (GtkObjectInitFunc) libbalsa_address_entry_init,
	    /* reserved_1 */ NULL,
	    /* reserved_2 */ NULL,
	    (GtkClassInitFunc) NULL,
	};

	address_entry_type =
	    gtk_type_unique(GTK_TYPE_ENTRY, &address_entry_info);
    }

    return address_entry_type;
}

static void
libbalsa_address_entry_class_init(LibBalsaAddressEntryClass *klass)
{
    GtkWidgetClass *gtk_widget_class;
    GtkObjectClass *object_class;

    object_class = GTK_OBJECT_CLASS(klass);
    gtk_widget_class = GTK_WIDGET_CLASS(klass);
    parent_class = gtk_type_class(GTK_TYPE_ENTRY);

    object_class->destroy = libbalsa_address_entry_destroy;

    gtk_widget_class->draw = libbalsa_address_entry_draw;
    gtk_widget_class->button_press_event = libbalsa_address_entry_button_press;
    gtk_widget_class->key_press_event = libbalsa_address_entry_key_press;
    gtk_widget_class->focus_out_event = libbalsa_address_entry_focus_out;
    gtk_widget_class->selection_received = libbalsa_address_entry_received;
}

static void
libbalsa_address_entry_init(LibBalsaAddressEntry *address_entry)
{
    address_entry->input = NULL;
    address_entry->domain = NULL;
    address_entry->find_match = NULL;
    address_entry->alias_start_pos = 0;
    address_entry->alias_end_pos = 0;
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
     * Remove known data references set by gtk_object_set_data()
     */
    gtk_object_remove_data(GTK_OBJECT(object), "next_in_line");

    /*
     * Remove timers.
     */
    if (entry->timer) gtk_timeout_remove (entry->timer);
    entry->timer = 0;

    /*
     * Remove the inputData structure.
     */
    if (address_entry->input)
	libbalsa_inputData_free(address_entry->input);
    address_entry->input = NULL;

    g_free(address_entry->domain);
    address_entry->domain = NULL;

    /*
     * Make sure the data makes sense.
     */
    address_entry->alias_start_pos = address_entry->alias_end_pos = 0;
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
 *     None.
 *
 *   results:
 *     A new emailData structure.
 *************************************************************/
static emailData *
libbalsa_emailData_new(void)
{
    emailData *tmp;

    tmp = g_malloc(sizeof(emailData));
    tmp->user = NULL;
    tmp->match = NULL;
    tmp->cursor = -1;
    tmp->tabs = 0;
    return tmp;
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
    addy->user = NULL;
    g_free(addy->match);
    addy->match = NULL;
    g_free(addy);
}


/*************************************************************
 * libbalsa_inputData_new:
 *     Creates and returns an inputData structure which will
 *     be initialized correctly.
 *
 *   arguments:
 *     None.
 *
 *   results:
 *     A newly allocated structure.
 *************************************************************/
static inputData *
libbalsa_inputData_new(void)
{
    inputData *tmp;

    tmp = g_malloc(sizeof(inputData));
    tmp->list = NULL;
    tmp->active = NULL;
    return tmp;
}


/*************************************************************
 * libbalsa_inputData_free:
 *     Frees an inputData structure and all memory it holds.
 *
 *   arguments:
 *     an inputData structure.
 *
 *   results:
 *     None.
 *************************************************************/
static void
libbalsa_inputData_free(inputData * data)
{
    g_return_if_fail(data != NULL);

    g_list_foreach(data->list, (GFunc) libbalsa_emailData_free, NULL);
    g_free(data);
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
libbalsa_alias_accept_match(emailData *addy) {
    g_assert(addy->match != NULL);
    g_free(addy->user);
    addy->user = addy->match;
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
    gchar *data;
    const gchar *old, *current;
    gint i, previous;
    gboolean quoted;

    g_return_val_if_fail(str != NULL, NULL);

    quoted = FALSE;
    glist = NULL;
    previous = 0;
    old = current = str;
    for (i = 0; str[i]; i++) {
	if (str[i] == '"') quoted = !quoted;
	if ( (!quoted) && (str[i] == delimiter) ) {
	    data = g_strndup(old, i - previous);
	    glist = g_list_append(glist, data);
	    previous = i + 1;
	    old = current;
	    old++;
	}
	current++;
    }
    if (str) {
	data = g_strndup(old, i - previous);
	glist = g_list_append(glist, data);
    }
    return glist;
}


/*************************************************************
 * libbalsa_length_of_address:
 *     Calculated how long an address would be when it is
 *     printed to the screen.
 *
 *   arguments:
 *     addy: an emailData structure.
 *
 *   results:
 *     The length of the structure when printed out
 *     as a gint.
 *************************************************************/
static gint
libbalsa_length_of_address(emailData *addy)
{
    gint i;

    i = 0;
    if (addy->user)
	i = i + strlen(addy->user);
    if (addy->match)
	i = i + 3 + strlen(addy->match);
    return i;
}


/*************************************************************
 * libbalsa_make_address_string:
 *     Prints out the e-mail address in a newly allocated
 *     gchar* string.
 *
 *   arguments:
 *     addy: an emailData structure.
 *
 *   results:
 *     The e-mail address printed out as a gchar*
 *************************************************************/
static gchar *
libbalsa_make_address_string(emailData *addy)
{
    gchar *str;

    if (addy->match)
        str = g_strconcat(addy->user, " (", addy->match, ")", NULL);
    else
	str = g_strdup(addy->user);
    return str;
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
    inputData *input;
    emailData *addy;
    size_t tmp;

    g_return_if_fail(address_entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry));

    input = address_entry->input;
    addy = input->active->data;
    if (addy->cursor < (tmp = strlen(addy->user))) {
	addy->cursor = tmp;
    } else {
	libbalsa_force_no_match(addy);
	if (g_list_next(input->active)) {
	    input->active = g_list_next(input->active);
	    addy = input->active->data;
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
    inputData *input;
    emailData *addy;

    g_return_if_fail(address_entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry));

    input = address_entry->input;
    addy = input->active->data;
    if (addy->cursor > 0) {
	addy->cursor = 0;
    } else {
	libbalsa_force_no_match(addy);
	if (g_list_previous(input->active)) {
	    input->active = g_list_previous(input->active);
	    addy = input->active->data;
	    addy->cursor = 0;
	}
    }
    libbalsa_address_entry_show(address_entry);
}


/*************************************************************
 * libbalsa_fill_input:
 *     Set all the data in the inputData structure to values
 *     reflecting the current user input.
 *
 *   arguments:
 *     address_entry: The LibBalsaAddressEntry to act on.
 *
 *   results:
 *     Returns an inputData structure that reflects the input
 *     and is suitable for adding to the LibBalsaAddressEntry
 *************************************************************/
static inputData *
libbalsa_fill_input(LibBalsaAddressEntry *address_entry)
{
    gint cursor = 0, size = 0, prev = 0;
    gchar *typed = NULL;
    GList *el, *current;
    GList *list = NULL;
    emailData *addy;
    inputData *input;
    size_t tmp;

    g_return_val_if_fail(address_entry != NULL, NULL);
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry), NULL);

    /*
     * Grab data from the widget.
     */
    input = libbalsa_inputData_new();
    cursor = (gint) gtk_editable_get_position(GTK_EDITABLE(address_entry));
    typed = gtk_editable_get_chars(GTK_EDITABLE(address_entry), 0, -1);
    if (typed == NULL)
	typed = g_strdup("");

    /*
     * Split the input string by comma, and store the result in
     * input->list.
     * str contains a list of e-mail addresses seperated by ','.
     *
     * FIXME: Breaks for '"Doe, John" <john@doe.com>'
     */
    el = libbalsa_strsplit(typed, ',');
    g_free(typed);
    /*
     * Store it all in a glist.
     */
    if (el != NULL) {
	for (current = el;
	     current != NULL;
	     current = g_list_next(current)) {
	    addy = libbalsa_emailData_new();
	    addy->user = g_strdup((gchar *)current->data);
	    input->list = g_list_append(input->list, addy);
	}
	g_list_foreach(el, (GFunc)g_free, NULL);
    } else {
       addy = libbalsa_emailData_new();
       addy->user = g_strdup("");
       input->list = g_list_append(input->list, addy);
    }

    /*
     * Search for the currently active list member.
     * We have to match the cursor in GtkEntry to the list.
     */
    g_assert(input->list != NULL);
    size = prev = 0;
    for (list = g_list_first(input->list);
	 list != NULL;
	 list = g_list_next(list)) {
	if (cursor >= size) {
	    prev = size;
	    input->active = list;
	}
	addy = (emailData *)list->data;
	size = size + strlen(addy->user) + 1;
	addy->user = g_strchug(addy->user);
    }

    addy = (emailData *)input->active->data;
    addy->cursor = cursor - prev;
    if (input->active != input->list)
	addy->cursor = addy->cursor - 1; /* Compensate for the ',' */
    if (addy->cursor < 0) addy->cursor = 0;
    if (addy->cursor > (tmp = strlen(addy->user)))
        addy->cursor = tmp;

    return input;
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
    inputData *input;

    g_return_if_fail(address_entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry));

    input = address_entry->input;
    if (input == NULL)
	return;
    if (input->list == NULL)
	return;

    addy = input->active->data;

    /*
     * Lets see if the user is at the end of an e-mail entry.
     */
    if (addy->user[addy->cursor] == '\0') {
	list = g_list_next(input->active);
	if (list != NULL) {
	    input->list = g_list_remove_link(input->list, list);
	    libbalsa_emailData_free(list->data);
	    g_list_free_1(list);
	}

    /*
     * Delete the current entry.
     */
    } else {
	list = input->active;
	if (g_list_previous(input->active) != NULL)
	    input->active = g_list_previous(input->active);
	else if (g_list_next(input->active) != NULL)
	    input->active = g_list_next(input->active);
	else
	    input->active = NULL;
	input->list = g_list_remove_link(input->list, list);
	libbalsa_emailData_free(list->data);
	g_list_free_1(list);
	if (input->active != NULL) {
	    addy = input->active->data;
	    g_assert(addy != NULL);
	    addy->cursor = 0;
	} else {
	    gtk_entry_set_text(GTK_ENTRY(address_entry), "");
	    libbalsa_inputData_free(address_entry->input);
	    address_entry->input = libbalsa_fill_input(address_entry);
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
    inputData *input;

    g_return_if_fail(address_entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry));

    input = address_entry->input;
    if (input == NULL)
	return;
    if (input->list == NULL)
	return;

    addy = input->active->data;

    /*
     * Lets see if the user is at the beginning of an e-mail entry.
     */
    if (addy->cursor == 0) {
	list = g_list_previous(input->active);
	if (list != NULL) {
	    input->list = g_list_remove_link(input->list, list);
	    libbalsa_emailData_free(list->data);
	    g_list_free_1(list);
	}

    /*
     * Delete the current entry.
     */
    } else {
	list = input->active;
	if (g_list_next(input->active) != NULL) {
	    input->active = g_list_next(input->active);
	} else if (g_list_previous(input->active) != NULL) {
	    input->active = g_list_previous(input->active);
	} else {
	    input->active = NULL;
	}
	input->list = g_list_remove_link(input->list, list);
	libbalsa_emailData_free(list->data);
	g_list_free_1(list);
	if (input->active != NULL) {
	    addy = input->active->data;
	    g_assert(addy != NULL);
	    addy->cursor = strlen(addy->user);
	} else {
	    gtk_entry_set_text(GTK_ENTRY(address_entry), "");
	    libbalsa_inputData_free(address_entry->input);
	    address_entry->input = libbalsa_fill_input(address_entry);
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
    gchar *str;
    GList *list;
    inputData *input;

    g_return_if_fail(address_entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry));

    input = address_entry->input;
    addy = input->active->data;

    /*
     * Lets see if the user is at the end of an e-mail entry.
     */
    str = g_strndup(addy->user, addy->cursor);
    g_free(addy->user);
    addy->user = str;
    libbalsa_force_no_match(addy);

    /*
     * Free all the following data.
     */
    for (list = g_list_next(input->active);
	 list != NULL;
	 list = g_list_next(input->active)) {
	 /*
	  * Concatenate the two e-mails.
	  */
	 libbalsa_emailData_free(list->data);
	 input->list = g_list_remove_link(input->list, list);
	 g_list_free_1(list);
    }
    /* libbalsa_inputData_free(address_entry->input); 
     * look above: the line below is not necessary */
    /* address_entry->input = input; */
    libbalsa_address_entry_show(address_entry);
}

/*************************************************************
 * libbalsa_address_entry_draw:
 *     Draws the entire widget.
 *
 *   credits:
 *     hands over all the work to GTK_WIDGET_CLASS(parent_class)->draw
 *
 *   arguments:
 *     widget: the widget.
 *     area:   ignored.
 *
 *   results:
 *     None?  Changes the appearance of the widget.
 *************************************************************/
#define INNER_BORDER     2
static void
libbalsa_address_entry_draw(GtkWidget * widget, GdkRectangle * area)
{
    GtkEditable *editable = GTK_EDITABLE(widget);

    if (editable->has_selection || !GTK_WIDGET_HAS_FOCUS(widget))
        (*GTK_WIDGET_CLASS(parent_class)->draw) (widget, area);
    else {
        /* highlight the expansion text by faking a selection */
        gint start = editable->selection_start_pos;
        gint end = editable->selection_end_pos;
        LibBalsaAddressEntry *address_entry =
            LIBBALSA_ADDRESS_ENTRY(widget);

        editable->selection_start_pos = address_entry->alias_start_pos;
        editable->selection_end_pos = address_entry->alias_end_pos;

        (*GTK_WIDGET_CLASS(parent_class)->draw) (widget, area);

        editable->selection_start_pos = start;
        editable->selection_end_pos = end;

        /* now draw the cursor (there's no method specifically for that,
         * but draw_focus includes the cursor) */
        gtk_widget_draw_focus(widget);
    }
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
    return
        GTK_WIDGET_CLASS(parent_class)->button_press_event(widget, event);
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

    if (address_entry->input != NULL)
        libbalsa_inputData_free(address_entry->input);
    address_entry->input = libbalsa_fill_input(address_entry);
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
    gchar *left, *right, *str;
    GList *list;
    unsigned i;
    inputData *input;

    input = address_entry->input;
    addy = input->active->data;
    editable = GTK_EDITABLE(address_entry);

    /*
     * First: Cut the clipboard.
     */
    if (editable->selection_start_pos != editable->selection_end_pos) {
	libbalsa_cut_clipboard(address_entry);
	return;
    }

    /*
     * Check if the user wanted to delete a match.
     * This is only valid if the user has hit tab.
     */
    if ((addy->cursor >= strlen(addy->user)) &&
       (addy->match != NULL) && (addy->tabs > 0)) {
       libbalsa_force_no_match(addy);
       return;
    }

    /*
     * Lets see if the user is at the beginning of an e-mail entry.
     */
    if (addy->cursor == 0) {
       list = g_list_previous(input->active);
       if (list != NULL) {
	   /*
	    * Concatenate the two e-mails.
	    */
	   extra = list->data;
	   extra->cursor = strlen(extra->user);
	   str = g_strconcat(extra->user, addy->user, NULL);
	   g_free(extra->user);
	   extra->user = str;
	   
	   /*
	    * Free a whole bunch of RAM.
	    */
	   input->list = g_list_remove_link(input->list, input->active);
	   libbalsa_emailData_free(addy);
	   g_list_free_1(input->active);
	   input->active = list;
       }

    /*
     * Normal character needs deleting.
     */
    } else {
       left = g_strndup(addy->user, (addy->cursor - 1));
       right = addy->user;
       for (i = 0; i < addy->cursor; i++) right++;
       str = g_strconcat(left, right, NULL);
       g_free(addy->user);
       g_free(left);
       addy->user = str;
       addy->cursor--;
       if (*str == '\0')
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
    gchar *left, *right, *str;
    GList *list;
    inputData *input;
    
    input = address_entry->input;
    addy = input->active->data;
    editable = GTK_EDITABLE(address_entry);
    if (editable->selection_start_pos != editable->selection_end_pos) {
	libbalsa_cut_clipboard(address_entry);
	return;
    }

    /*
     * Check if the user wanted to delete a match.
     */
    if ((addy->cursor >= strlen(addy->user)) &&
       (addy->match != NULL)) {
       libbalsa_force_no_match(addy);
       return;
    }

    /*
     * Lets see if the user is at the end of an e-mail entry.
     */
    if (addy->cursor >= strlen(addy->user)) {
	list = g_list_next(input->active);
	if (list != NULL) {
	    /*
	     * Concatenate the two e-mails.
	     */
	    extra = list->data;
	    str = g_strconcat(addy->user, extra->user, NULL);
	    g_free(addy->user);
	    addy->user = str;
	   
	    /*
	     * Free a whole bunch of RAM.
	     */
	    input->list = g_list_remove_link(input->list, list);
	    libbalsa_emailData_free(extra);
	    g_list_free_1(list);
	}
    /*
     * Normal character needs deleting.
     */
    } else {
	unsigned i;
	left = g_strndup(addy->user, addy->cursor);
	right = addy->user;
	for (i = 0; i <= addy->cursor; i++) right++;
	str = g_strconcat(left, right, NULL);
	g_free(addy->user);
	g_free(left);
	addy->user = str;
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
	    
    g_assert(address_entry->input != NULL);

    if (address_entry->input->list == NULL)
	return TRUE;

    g_assert(address_entry->input->active != NULL);
    g_assert(address_entry->input->active->data != NULL);

    addy = address_entry->input->active->data;
    if (addy->match != NULL) {
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
    gchar *str;

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
    if (address_entry->domain == NULL || *address_entry->domain == '\0')
	return;

    /*
     * There is a default domain to add.  Do we need to add it?
     */
    addy = address_entry->input->active->data;
    if (libbalsa_is_an_email(addy->user) || *addy->user == '\0')
	return;

    /*
     * Okay, add it.
     */
    str = g_strconcat(addy->user, "@", address_entry->domain, NULL);
    g_free(addy->user);
    addy->user = str;
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

    addy = address_entry->input->active->data;
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

    addy = address_entry->input->active->data;
    if (addy->match != NULL)
	libbalsa_alias_accept_match(addy);
    address_entry->input->active = address_entry->input->list;
    addy = address_entry->input->active->data;
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
    inputData *input;
    emailData *addy;

    input = address_entry->input;
    addy = input->active->data;
    if (addy->cursor > 0) {
	addy->cursor--;
	libbalsa_force_no_match(addy);
    } else if (g_list_previous(input->active)) {
	input->active = g_list_previous(input->active);
	addy = input->active->data;
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
    inputData *input;
    emailData *addy;

    input = address_entry->input;
    addy = input->active->data;
    if (addy->cursor < strlen(addy->user)) {
	addy->cursor++;
	libbalsa_force_no_match(addy);
    } else if (g_list_next(input->active)) {
	input->active = g_list_next(input->active);
	addy = input->active->data;
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
    inputData *input;
    emailData *addy;

    g_return_if_fail(address_entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry));
	    
    input = address_entry->input;
    addy = input->active->data;
    if (addy->match != NULL)
	libbalsa_alias_accept_match(addy);
    input->active = g_list_last(input->list);
    addy = input->active->data;
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
    inputData *input;
    emailData *addy, *extra;
    gchar *left, *right, *str;
    
    g_return_if_fail(address_entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry));
    g_return_if_fail(address_entry->input != NULL);

    /*
     * First, check if it expanded, and accept it.
     */
    input = address_entry->input;
    addy = input->active->data;
    left = addy->user;
    if (addy->match != NULL) {
	right = NULL;
	libbalsa_alias_accept_match(addy);
    } else {
	if ((addy->cursor > 0) && (addy->cursor < strlen(addy->user))) {
	    right = & addy->user[addy->cursor];
	} else {
	    right = NULL;
	}
    }

    /*
     * At this point, right points to the split where the comma must
     * happen, or it points to NULL
     */

    /*
     * Now we add a new entry.
     */
    extra = libbalsa_emailData_new();
    g_list_insert(input->active, extra, 1);
    if (right != NULL) {
	extra->user = g_strdup(right);
	str = g_strndup(left, addy->cursor);
	g_free(addy->user);
	addy->user = str;
    } else
	extra->user = g_strdup("");

    /*
     * We move the cursor to the right spot in the NEW entry.
     */
    input->active = g_list_next(input->active);
    extra->cursor = 0;
    
    /*
     * And we add the default domain to the original entry.
     */
    if (address_entry->domain == NULL ||
	*address_entry->domain == '\0' ||
	libbalsa_is_an_email(addy->user)) return;

    str = g_strconcat(addy->user, "@", address_entry->domain, NULL);
    g_free(addy->user);
    addy->user = str;
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
    gchar *str, *left, *right;
    gint i;
    GtkEditable *editable;
    inputData *input;

    g_return_if_fail(address_entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry));
    g_return_if_fail(add != NULL);

    editable = GTK_EDITABLE(address_entry);
    if (editable->selection_start_pos != editable->selection_end_pos) {
	libbalsa_cut_clipboard(address_entry);
    }

    /*
     * User typed a key, so cancel any matches we may have had.
     */
    input = address_entry->input;
    addy = input->active->data;
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
    right = addy->user+addy->cursor;
    
    /*
     * Add the keystroke to the end of user input.
     */
    str = g_strconcat(left, add, right, NULL);
    g_free(addy->user);
    g_free(left);
    addy->user = str;
    addy->cursor++;

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
    inputData *input;
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
    input = address_entry->input;
    addy = input->active->data;
    if (input->active == g_list_last(input->list)) {
	if (addy->cursor >= strlen(addy->user) && (addy->match == NULL)) {
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
 * libbalsa_find_list_entry:
 *     Finds out in which e-mail entry a certain position
 *     is.  This is used to figure out where a selection
 *     starts and stops.
 *
 *   arguments:
 *     address_entry: the widget.
 *     *cursor:       the position to look for.
 *
 *   results:
 *     Returns a GList* to the e-mail entry that has the
 *     position.
 *     Returns the position from the start of that e-mail
 *     in *cursor.
 *************************************************************/
static GList *
libbalsa_find_list_entry(LibBalsaAddressEntry *address_entry, gint *cursor)
{
    GList *list, *previous;
    gint address_start;
    gboolean found;
    emailData *addy;
    gint pos;

    g_return_val_if_fail(address_entry != NULL, NULL);
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry), NULL);

    address_start = 0;
    found = FALSE;
    pos = *cursor;
    *cursor = 0;
    for (list = previous = address_entry->input->list;
	 (list != NULL) && (found == FALSE);
	 list = g_list_next(list)) {
	addy = (emailData *)list->data;
	address_start += libbalsa_length_of_address(addy);
	if (pos <= address_start) {
	    found = TRUE;
	    *cursor = libbalsa_length_of_address(addy) - (address_start - pos);
	}
	address_start += 2; /* strlen(", ") */
	previous = list;
    }
    g_assert(found == TRUE);
    if(*cursor<0) { /* error, correct it and print a warning.
		       This needs to be fixed in long term. */
	*cursor = 0;
	g_warning("libbalsa_find_list_entry failed to compute the cursor.\n"
		  "find a way to reproduce it and report it.");
    }
    return previous;
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
    inputData *input;

    input = address_entry->input;
    addy = input->active->data;
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
libbalsa_paste_clipboard(LibBalsaAddressEntry *address_entry)
{
    GtkEditable *editable;

    editable = GTK_EDITABLE(address_entry);
    gtk_editable_paste_clipboard(editable);
    if (address_entry->input)
	libbalsa_inputData_free(address_entry->input);
    address_entry->input = libbalsa_fill_input(address_entry);
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
libbalsa_cut_clipboard(LibBalsaAddressEntry *address_entry)
{
    GtkEditable *editable;

    editable = GTK_EDITABLE(address_entry);
    gtk_editable_cut_clipboard(editable);
    if (address_entry->input)
	libbalsa_inputData_free(address_entry->input);
    address_entry->input = libbalsa_fill_input(address_entry);
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
    guint initial_pos;
    gint extend_selection;
    gint extend_start;
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
    if (editable->editable == FALSE) return FALSE;

    /*
     * Setup variables, and see if we need to select text.
     */
    return_val = FALSE;
    initial_pos = editable->current_pos;
    extend_selection = event->state & GDK_SHIFT_MASK;
    extend_start = FALSE;
    if (extend_selection) {
	if (editable->selection_start_pos == editable->selection_end_pos) {
	    editable->selection_start_pos = editable->current_pos;
	    editable->selection_end_pos = editable->current_pos;
	}
	extend_start = (editable->current_pos == editable->selection_start_pos);
    }

    /*
     * Grab the old information from the widget - this way the user
     * can switch back and forth between To: and Cc:
     */
    if (!address_entry->input)
	address_entry->input = libbalsa_inputData_new();

    /*
     * Check if we have lost focus.
     */
    if (address_entry->focus != FOCUS_CACHED) {
	libbalsa_inputData_free(address_entry->input);
	address_entry->input = libbalsa_fill_input(address_entry);
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
	    extend_selection = FALSE;
	    libbalsa_paste_clipboard(address_entry);
	} else if (event->state & GDK_CONTROL_MASK)
	    gtk_editable_copy_clipboard(editable);
	break;
    case GDK_Delete:	/* done */
	return_val = TRUE;
	if (event->state & GDK_CONTROL_MASK) /* done */
	    libbalsa_delete_forward_word(address_entry);
	else if (event->state & GDK_SHIFT_MASK) {
	    extend_selection = FALSE;
	    libbalsa_cut_clipboard(address_entry);
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
	    extend_selection = FALSE;
	    editable->has_selection = FALSE;
	    return_val = TRUE;
	    libbalsa_keystroke_add_key(address_entry, event->string);
	}
	break;
    }

    /*
     * Since we emit signals from within the above code,
     * the widget might already be destroyed or at least
     * unrealized.
     */
    if (GTK_WIDGET_REALIZED(editable) && return_val &&
	    (editable->current_pos != initial_pos)) {
	if (extend_selection) {
	    if (editable->current_pos < editable->selection_start_pos)
	        editable->selection_start_pos = editable->current_pos;
	    else if (editable->current_pos > editable->selection_end_pos)
	        editable->selection_end_pos = editable->current_pos;
	    else {
	        if (extend_start)
		    editable->selection_start_pos = editable->current_pos;
	        else
		    editable->selection_end_pos = editable->current_pos;
	    }
	} else {
	    editable->selection_start_pos = 0;
	    editable->selection_end_pos = 0;
	}
	gtk_editable_claim_selection(editable,
		editable->selection_start_pos != editable->selection_end_pos,
		event->time);
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
 *     and set the widget text to that, and then call
 *     libbalsa_address_entry_draw_text() to draw the
 *     widget.
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
    gchar *out;
    gint cursor, start, end;
    gboolean found;
    inputData *input;

    g_return_if_fail(address_entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(address_entry));
    g_return_if_fail(address_entry->input != NULL);

    if (!GTK_WIDGET_DRAWABLE(address_entry)) return;

    editable = GTK_EDITABLE(address_entry);

    input = address_entry->input;
    show = g_string_new("");
    cursor = start = end = 0;
    found = FALSE;
    for (list = input->list;
	 list;
	 list = g_list_next(list)) {
	/*
	 * Is it a normal string, or is it a match that requires ()
	 */
	addy = (emailData *)list->data;
	g_assert(addy != NULL);
	if (addy->match != NULL) {
	    out = g_strconcat("", addy->user, " (", addy->match, ")", NULL);
	} else {
	    out = g_strdup(addy->user);
	}
	/*
	 * Copy the string, adding a delimiter if need be.
	 */
	show = g_string_append(show, out);
	if (g_list_next(list) != NULL)
	    show = g_string_append(show, ", ");

	/*
	 * Check for the cursor position.
	 */
	if (!found) {
	    if (list != input->active) {
		cursor += strlen(out);
	    	if (g_list_next(list) != NULL) cursor += 2;
	    } else {
		found = TRUE;
		cursor += addy->cursor;
		if (addy->match) {
		    start = cursor - addy->cursor;
		    start += strlen(addy->user) + 1;
		    end = start + strlen(addy->match) + 2;
		}
	    }
	}
	g_free(out);
    }

    /*
     * Show it...
     */
    address_entry->alias_start_pos = start;
    address_entry->alias_end_pos = end;
    start = editable->selection_start_pos;
    end = editable->selection_end_pos;
    gtk_entry_set_text(GTK_ENTRY(address_entry), show->str);
    gtk_editable_set_position(editable, cursor);
    g_string_free(show, TRUE);
    editable->selection_start_pos = start;
    editable->selection_end_pos = end;
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
    if (address_entry->input != NULL) {
	addy = address_entry->input->active->data;
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

    entry = gtk_type_new(LIBBALSA_TYPE_ADDRESS_ENTRY);
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

    GTK_WIDGET_UNSET_FLAGS(widget, GTK_HAS_FOCUS);
    gtk_widget_draw_focus(widget);

    address_entry = LIBBALSA_ADDRESS_ENTRY(widget);
    if (address_entry->focus == FOCUS_CACHED) {
	libbalsa_address_entry_clear_match(address_entry);
	libbalsa_address_entry_show(address_entry);
    }
    address_entry->focus = FOCUS_TAINTED;
#ifdef USE_XIM
    gdk_im_end ();
#endif
    return FALSE;
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
libbalsa_address_entry_clear_to_send(GtkWidget *widget)
{
    inputData *input;
    emailData *addy;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(widget));

    /*
     * Grab the input.
     */
    input = LIBBALSA_ADDRESS_ENTRY(widget)->input;
    if (!input) return;
    if ((input->active) == NULL) return;

    /*
     * Set the active data to no match.
     */
    addy = input->active->data;
    libbalsa_force_no_match(addy);

    /*
     * Show the input, so that we fill the GtkEntry box.
     */
    libbalsa_address_entry_show(LIBBALSA_ADDRESS_ENTRY(widget));
}

