/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
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

#include "libbalsa.h"

#include <stdio.h>
#include <string.h>
#include <gnome.h>
#include <ctype.h>

#include <sys/stat.h>		/* for check_if_regular_file() */
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>

#include "balsa-app.h"
#include "balsa-message.h"
#include "balsa-index.h"
#include "misc.h"
#include "mime.h"
#include "sendmsg-window.h"
#include "address-book.h"
#include "main.h"
#include "expand-alias.h"

#define CASE_INSENSITIVE_NAME
#define PRESERVE_CASE TRUE
#define OVERWRITE_CASE FALSE

/*
 * Structure to keep track of what the user is typing.
 *
 * list is a list of different e-mail addresses the user has typed.
 *      These are usually comma seperated.
 *
 * skip is a pointer to the list we are working on.  It is not a copy.
 *
 * typo is a pointer to the string in list.  It is not a copy.
 *
 * show is what we show the user, for the current e-mail he is typing,
 *      not the entire list.
 *
 * cursor is where the current cursor is in the string the user is typing.
 *
 * tabs is how many times the user has pressed <tab> so we can cycle.
 *
 * have_match tells us if tab-completion was performed.
 *
 * old_cursor is the old user input (minus tab completion).
 *
 * expand_cursor is at the end.
 */
typedef struct {
    GList *list;
    GList *skip;
    gchar *typo;
    gchar *show;
    gint cursor;
    gint *tabs;
    gboolean have_match;
    gint old_cursor;
    gint expand_cursor;
} inputData;

/*
 * Struct to keep track of the items in a completion
 *
 * string is the string to complete on
 * address is the associated address
 */
typedef struct _CompletionData CompletionData;
struct _CompletionData {
    gchar *string;
    LibBalsaAddress *address;
};

/*
 * Function prototypes or something...
 */
static void clip_expand_cursor(inputData * input);
static void expand_input_wrapper(inputData * input, gboolean preserve);

static gchar *expand_input(gchar ** input, gint * tabs);
static inputData *create_input(void);
static inputData *fill_input(inputData * input, GtkWidget * widget);
static void show_input(inputData * input, GtkWidget * widget);
static void input_data_free(inputData * data);

static inputData *process_keystroke_backspace(inputData * input);
static inputData *process_keystroke_delete(inputData * input);
static inputData *process_keystroke_enter(inputData * input);
static inputData *process_keystroke_down(inputData * input);
static inputData *process_keystroke_left(inputData * input);
static inputData *process_keystroke_right(inputData * input);
static inputData *process_keystroke_home(inputData * input);
static inputData *process_keystroke_comma(inputData * input);
static inputData *process_keystroke_add_key(inputData * input,
					    gchar * add);
static inputData *process_keystroke(inputData * input, GtkWidget * widget,
				    GdkEventKey * event);

/*
 * Functions for managing CompletionData structures
 */
static CompletionData *completion_data_new(LibBalsaAddress * address,
					   gboolean alias);
static void completion_data_free(CompletionData * data);
static gchar *completion_data_extract(CompletionData * data);

/*
 * FIXME: Unfortunately, I can't do this without a global.
 * Need to rewrite the addressbook routines with some kind
 * of cache.
 * 
 * [ijc] it should now be possible to add the completions as
 * a per address book field (implemented in LibBalsaAddressBook)
 */
static GList *address_name_data = NULL;
static GList *address_alias_data = NULL;
static GCompletion *complete_name = NULL;
static GCompletion *complete_alias = NULL;

/*
 * next_entrybox()
 *
 * Transfers focus to the next visible entrybox in line.
 */
void
next_entrybox(GtkWidget * widget, GtkWidget * entry)
{
    GtkWidget *next;

    next = entry;
    while ((next = gtk_object_get_data(GTK_OBJECT(next), "next_in_line"))
	   != NULL) {
	if (GTK_WIDGET_DRAWABLE(GTK_WIDGET(next))) {
	    gtk_widget_grab_focus(GTK_WIDGET(next));
	    break;
	}
    }
}


/*
 * clip_expand_cursor()
 *
 * Clips the expand cursor to within proper boundaries:
 * that is, between the actual cursor, and the end of the string.
 */
static void
clip_expand_cursor(inputData * input)
{
    if (input->expand_cursor < input->cursor)
	input->expand_cursor = input->cursor;
    if (input->expand_cursor > strlen(input->typo))
	input->expand_cursor = strlen(input->typo);
    return;
}


/*
 * Expand some given input with g_completion_complete() by name
 * and returns a newly allocated gchar*
 */
static gchar *
expand_input(gchar ** input, gint * tabs)
{
    gchar *prefix = NULL;	/* the longest common string. */
    GList *match = NULL;	/* A list of matches.         */
    GList *search = NULL;	/* Used to search the list.   */
    gchar *output = NULL;	/* We return this.            */
    LibBalsaAddress *addr = NULL;	/* Process the list data.     */
    gint i;			/* A counter for the tabs.    */

    if (strlen(*input) > 0) {
	if (complete_name) {
#ifdef CASE_INSENSITIVE_NAME
	    gchar *str;
	    str = g_strdup(*input);
	    g_strup(str);
	    match = g_completion_complete(complete_name, str, &prefix);
	    g_free(str);
#else
	    match = g_completion_complete(complete_name, *input, &prefix);
#endif				/* CASE_INSENSITIVE_NAME */
	}
	if (!match && complete_alias)
	    match = g_completion_complete(complete_alias, *input, &prefix);

	if (match) {
	    i = *tabs;
	    if ((i == 1) && (strlen(prefix) > strlen(*input))) {
		addr =
		    LIBBALSA_ADDRESS(((CompletionData *) match->data)->
				     address);
		output =
		    g_strdup_printf("%s <%s>", addr->full_name,
				    (gchar *) addr->address_list->data);
		g_free(*input);
		if (g_list_next(match))
		    *input = g_strndup(output, strlen(prefix));
		else
		    *input = g_strdup(output);
	    } else {
		for (search = match; i > 0; i--) {
		    search = g_list_next(search);
		    if (!search) {
			*tabs = i = 0;
			search = match;
		    }
		}
		addr =
		    LIBBALSA_ADDRESS(((CompletionData *) search->data)->
				     address);
		output =
		    g_strdup_printf("%s <%s>", addr->full_name,
				    (gchar *) addr->address_list->data);
	    }
	} else {
	    output = NULL;
	}

	if (prefix)
	    g_free(prefix);
	prefix = NULL;

    } else {
	output = g_strdup("");
    }

    return output;
}


/*
 * expand_input_wrapper()
 *
 * This wrapper takes care of preserving the user's typed case,
 * and cursor position.  It also sets the input->have_match flag.
 */
static void
expand_input_wrapper(inputData * input, gboolean preserve)
{
    gchar *tmp, *str;
    gint i;

    tmp = g_strndup(input->typo, input->expand_cursor);
    if ((str = expand_input(&tmp, input->tabs))) {
	input->have_match = TRUE;
	input->typo = tmp;
	g_free(input->skip->data);
	if (input->show)
	    g_free(input->show);
	if (preserve) {
	    input->show = g_strndup(tmp, input->expand_cursor);
	    tmp = str;
	    for (i = 0; i < input->expand_cursor; i++)
		tmp++;
	    if (tmp)
		tmp = g_strconcat(input->show, tmp, NULL);
	    else
		tmp = g_strdup(input->show);
	    g_free(str);
	    g_free(input->show);
	    input->show = tmp;
	} else
	    input->show = str;
	input->old_cursor = input->cursor;
	if (*(input->tabs) > 0)
	    input->cursor = strlen(input->typo);
    } else {
	input->have_match = FALSE;
	input->typo = tmp;
	g_free(input->skip->data);
    }
    input->skip->data = input->typo;

    return;
}


/*
 * create_input()
 *
 * Returns an inputData structure with the default values set.
 */
static inputData *
create_input(void)
{
    inputData *tmp;

    tmp = g_malloc(sizeof(inputData));

    tmp->typo = NULL;

    tmp->tabs = g_malloc(sizeof(gint));

    *(tmp->tabs) = 0;
    tmp->list = NULL;
    tmp->skip = NULL;
    tmp->have_match = FALSE;

    tmp->expand_cursor = tmp->cursor = tmp->old_cursor = 0;

    return tmp;
}


/*
 * fill_input()
 *
 * Set all the data in the inputData structure to values reflecting
 * the current user input.
 */
static inputData *
fill_input(inputData * input, GtkWidget * widget)
{
    gint cursor = 0, size = 0, prev = 0;
    gchar *str = NULL;
    gchar **str_array = NULL, **el = NULL;
    GList *list = NULL;

    if ((!input) || (!widget))
	return NULL;

    /*
     * Grab data from the widget.
     */
    cursor = (gint) gtk_editable_get_position(GTK_EDITABLE(widget));
    if (!(str = gtk_editable_get_chars(GTK_EDITABLE(widget), 0, -1)))
	str = g_strdup("");
    else
	str = g_strdup(str);

    /*
     * Split the input string by comma, and store the result in
     * input->list.
     */
    if (input->list)
	g_list_free(input->list);

    input->list = input->skip = NULL;
    input->typo = NULL;
    el = str_array = g_strsplit(str, ",", 2048);
    g_free(str);
    str = NULL;

    for (str = *el; str != NULL; str = *(++el))
	input->list = g_list_append(input->list, g_strdup(str));
    g_strfreev(str_array);

    /*
     * Search for the currently active list member.
     */
    size = 0;
    for (list = g_list_first(input->list); list != NULL;
	 list = g_list_next(list)) {
	if (cursor >= size) {
	    prev = size;
	    input->skip = list;
	}
	size = size + strlen(list->data) + 1;
    }

    /*
     * Take care of empty user input, and other invalid data.
     */
    if (!(input->skip)) {
	str = g_strdup("");
	input->skip = g_list_append(input->skip, str);
	input->list = input->skip;
	str = NULL;
    }

    input->typo = g_strdup((input->skip)->data);
    g_strchug(input->typo);

    input->cursor = cursor - prev - strlen((gchar *) (input->skip)->data)
	+ (gint) strlen((gchar *) input->typo);

    if (input->cursor < 0)
	input->cursor = 0;
    g_free((input->skip)->data);

    (input->skip)->data = input->typo;
    input->show = NULL;

    if (input->expand_cursor < 0)
	input->expand_cursor = input->cursor;

    clip_expand_cursor(input);

    return input;
}


/*
 * process_keystroke_backspace()
 *
 * If the last key hit was TAB, cancel the last alias expansion that TAB
 * did.  Otherwise delete one character.  If its at the beginning of the
 * input, move one e-mail address left.
 */
static inputData *
process_keystroke_backspace(inputData * input)
{
    gchar *str1 = NULL, *str2 = NULL;
    gint i = 0;
    GList *list = NULL;

    if ((input->have_match) && (input->cursor > (input->old_cursor + 2))) {
	str1 = g_strndup(input->typo, input->old_cursor);
	g_free(input->typo);
	input->typo = input->skip->data = str1;
	input->cursor = strlen(str1);
    } else if (input->cursor > 0) {
	input->cursor--;
	str1 = g_strndup(input->typo, input->cursor);
	for (i = 0; i <= input->cursor; i++)
	    input->typo++;
	str2 = g_strconcat(str1, input->typo, NULL);
	g_free(str1);		/* also does g_free(input->typo) */
	input->typo = input->skip->data = str2;
	input->expand_cursor--;
	clip_expand_cursor(input);
    } else {
	if (g_list_previous(input->skip)) {
	    list = g_list_previous(input->skip);
	    input->cursor = strlen(list->data);
	    input->typo =
		g_strconcat(list->data, (input->skip)->data, NULL);
	    g_free(list->data);
	    list->data = input->typo;
	    input->list = g_list_remove_link(input->list, input->skip);
	    g_list_free(input->skip);
	    input->skip = list;
	    input->expand_cursor = input->expand_cursor + input->cursor;
	    clip_expand_cursor(input);
	} else {
	    input->cursor = 0;
	    clip_expand_cursor(input);
	}
    }
    *(input->tabs) = 0;
    expand_input_wrapper(input, PRESERVE_CASE);

    return input;
}


static inputData *
process_keystroke_delete(inputData * input)
{
    gchar *str1 = NULL, *str2 = NULL;
    gint i = 0;
    GList *list = NULL;

    if (input->cursor < strlen(input->typo)) {
	str1 = g_strndup(input->typo, input->cursor);
	for (i = 0; i <= input->cursor; i++)
	    input->typo++;
	str2 = g_strconcat(str1, input->typo, NULL);
	g_free(str1);		/* also does g_free(input->typo) */
	input->typo = input->skip->data = str2;
	input->expand_cursor--;
	clip_expand_cursor(input);
    } else {
	if (g_list_next(input->skip)) {
	    list = g_list_next(input->skip);
	    str1 = input->typo;
	    input->skip->data =
		input->typo = g_strconcat(input->typo, list->data, NULL);
	    g_free(str1);
	    g_free(list->data);
	    input->list = g_list_remove_link(input->list, list);
	    g_list_free(list);
	    input->expand_cursor = strlen(input->typo);
	    clip_expand_cursor(input);
	} else {
	    input->cursor = strlen(input->typo);
	    clip_expand_cursor(input);
	}
    }
    *(input->tabs) = 0;
    expand_input_wrapper(input, PRESERVE_CASE);

    return input;
}


static inputData *
process_keystroke_enter(inputData * input)
{
    gchar *str;
    gint i;
    gboolean found;

    expand_input_wrapper(input, OVERWRITE_CASE);
    if ((input->have_match == FALSE) && (balsa_app.domain != NULL)
	&& (strlen(balsa_app.domain) > 0)) {
	found = FALSE;
	for (i = 0; i < strlen(input->typo); i++)
	    if (input->typo[i] == (gchar) '@')
		found = TRUE;
	if (found == FALSE) {
	    str = g_strconcat(input->typo, "@", balsa_app.domain, NULL);
	    g_free(input->typo);
	    input->skip->data = input->typo = str;
	}
    }
    *(input->tabs) = 0;

    return input;
}


static inputData *
process_keystroke_down(inputData * input)
{
    gchar *str;

    *(input->tabs) = 0;
    str = g_strndup(input->typo, input->expand_cursor);
    g_free(input->typo);
    input->skip->data = input->typo = str;
    if (input->show)
	g_free(input->show);
    input->cursor = input->expand_cursor;

    return input;
}


static inputData *
process_keystroke_home(inputData * input)
{
    input->skip = g_list_first(input->list);
    input->typo = input->skip->data;
    input->cursor = 0;
    clip_expand_cursor(input);
    *(input->tabs) = 0;
    expand_input_wrapper(input, PRESERVE_CASE);

    return input;
}


static inputData *
process_keystroke_left(inputData * input)
{

    if (input->cursor > 0)
	input->cursor--;
    else if (g_list_previous(input->skip)) {
	input->skip = g_list_previous(input->skip);
	input->typo = input->skip->data;
	input->cursor = strlen(input->typo);
    }
    clip_expand_cursor(input);
    *(input->tabs) = 0;
    expand_input_wrapper(input, PRESERVE_CASE);
    return input;
}


static inputData *
process_keystroke_right(inputData * input)
{
    if (input->cursor < strlen(input->typo))
	input->cursor++;
    else if (g_list_next(input->skip)) {
	input->skip = g_list_next(input->skip);
	input->typo = input->skip->data;
	input->cursor = 0;
	input->expand_cursor = strlen(input->typo);
    }
    clip_expand_cursor(input);
    *(input->tabs) = 0;
    expand_input_wrapper(input, PRESERVE_CASE);
    return input;
}


/*
 * process_keystroke_comma()
 *
 * A comma indicates acceptance of the current alias expansion.
 * We need to make sure we expand to what the user currently sees, and
 * add a comma to the list.
 *
 * TODO: Create a new g_list() entry, and do NOT append the comma to
 *       the current string.
 */
static inputData *
process_keystroke_comma(inputData * input)
{
    expand_input_wrapper(input, OVERWRITE_CASE);

    if (!(input->show))
	input->show = g_strdup(input->typo);
    g_free(input->typo);

    input->skip->data = input->typo = g_strconcat(input->show, ", ", NULL);

    g_free(input->show);
    input->show = NULL;
    input->cursor = strlen(input->typo);
    clip_expand_cursor(input);
    *(input->tabs) = 0;

    return input;
}


/*
 * process_keystroke_add_key()
 *
 * Add a normal alphanumeric key to the input.
 */
static inputData *
process_keystroke_add_key(inputData * input, gchar * add)
{
    gchar *str1, *str2;
    gint i;

    str1 = g_strndup(input->typo, input->cursor);

    for (i = 0; i < input->cursor; i++)
	input->typo++;

    str2 = g_strconcat(str1, add, input->typo, NULL);
    g_free(str1);		/* also does g_free (input¯>typo); */

    input->typo = input->skip->data = str2;
    *(input->tabs) = 0;
    input->cursor++;
    input->expand_cursor++;
    clip_expand_cursor(input);
    expand_input_wrapper(input, PRESERVE_CASE);

    return input;
}


/*
 * process_keystroke()
 *
 * Chooses which function to call, depending on the user's keystroke.
 */
static inputData *
process_keystroke(inputData * input, GtkWidget * widget,
		  GdkEventKey * event)
{
    if ((!input) || (!event))
	return NULL;

    switch (event->keyval) {
    case GDK_BackSpace:
	input = process_keystroke_backspace(input);
	break;

    case GDK_Delete:
	input = process_keystroke_delete(input);
	break;

    case GDK_Tab:
	if (input->cursor == 0)
	    next_entrybox(widget, widget);
	else {
	    *(input->tabs) = *(input->tabs) + 1;
	    input->expand_cursor = input->cursor;
	    expand_input_wrapper(input, PRESERVE_CASE);
	}
	break;

    case GDK_Linefeed:
    case GDK_Return:
	input = process_keystroke_enter(input);
	next_entrybox(widget, widget);
	break;

    case GDK_Home:
	input = process_keystroke_home(input);
	break;

    case GDK_Left:
	input = process_keystroke_left(input);
	break;

    case GDK_Down:
	input = process_keystroke_down(input);
	next_entrybox(widget, widget);
	break;

    case GDK_Right:
	input = process_keystroke_right(input);
	break;

    case GDK_End:
    case GDK_comma:
	input = process_keystroke_comma(input);
	break;

    case GDK_Shift_L:
    case GDK_Shift_R:
	break;

    default:
	input = process_keystroke_add_key(input, event->string);
    }
    return input;
}


/*
 * show_input()
 *
 * Shows the current input to the user.  It shows what the user typed,
 * what it will expand to, and highlight that section.
 *
 * TODO: Adding the lists together should be easier.
 */
static void
show_input(inputData * input, GtkWidget * widget)
{
    gchar *app = NULL, *str = NULL;
    GList *list = NULL;
    gboolean found = FALSE;
    gint size = 0;
    gchar *show;

    if ((!input) || (!widget))
	return;

    /*
     * If it expanded to nothing, create a copy of what the user typed.
     */
    if (!input->show)
	input->show = g_strdup(input->typo);

    str = g_strdup("");
    size = 0;
    found = FALSE;
    for (list = g_list_first(input->list);
	 list != NULL; list = g_list_next(list)) {

	if (list == input->skip) {
	    found = TRUE;
	    show = input->show;
	    g_strchug(show);
	} else {
	    show = list->data;
	    g_strchug(show);
	}

	if (g_list_next(list))
	    app = g_strconcat(str, show, ", ", NULL);
	else
	    app = g_strconcat(str, show, NULL);

	g_free(str);
	str = app;

	if (found == FALSE)
	    size = strlen(str);
    }

    if (input->cursor > strlen(input->typo))
	input->cursor = strlen(input->typo);

    /*
     * Set the gtk_entry() text, highlight it, etc.
     */
    gtk_entry_set_text(GTK_ENTRY(widget), str);
    gtk_editable_set_position(GTK_EDITABLE(widget), size + input->cursor);
    if (input->have_match)
	gtk_editable_select_region(GTK_EDITABLE(widget),
				   size + input->cursor,
				   size + strlen(input->show));
    g_free(str);

    return;
}


/*
 * key_pressed_cb()
 *
 * This gets called every time a user hits a key in the gtkEntry() box.
 */
gboolean
key_pressed_cb(GtkWidget * widget, GdkEventKey * event, gpointer user_data)
{
    inputData *input;
    gint *i;

    /*
     * Skip ALL special characters, and let gtkentry() process them.
     * This allows, for example, Ctrl-S to send the message.
     */
    switch (event->keyval) {
    case GDK_Control_L:
    case GDK_Control_R:
    case GDK_Meta_L:
    case GDK_Meta_R:
    case GDK_Alt_L:
    case GDK_Alt_R:
    case GDK_Super_L:
    case GDK_Super_R:
    case GDK_Hyper_L:
    case GDK_Hyper_R:
	return FALSE;
    }

    /*
     * We cannot use GDK_MODIFIER_MASK because that would trap SHIFT.
     */
    if (event->state & GDK_CONTROL_MASK)
	return FALSE;

    /*
     * Check if GCompletion is valid - we reload the addressbook
     * if someone closed it.
     *
     * This fixes multiple compose windows.
     */
    if (address_name_data == NULL && address_alias_data == NULL)
	alias_load_addressbook();

    /*
     * Grab the old information from the widget - this way the user
     * can switch back and forth between To: and Cc:
     */
    input =
	(inputData *) gtk_object_get_data(GTK_OBJECT(widget), "old_input");
    if (!input)
	input = create_input();

    /*
     * Check if we have lost focus.
     */
    i = (gint *) gtk_object_get_data(GTK_OBJECT(widget), "focus_c");

    if (*i == 1)
	input->expand_cursor = -1;
    *i = 2;

    gtk_object_set_data(GTK_OBJECT(widget), "focus_c", (gpointer) i);

    /*
     * Load the input with data from the GtkEntry() box.
     */
    input = fill_input(input, widget);

    /*
     * Modify the data according to the key-pressed event.
     */
    input = process_keystroke(input, widget, event);

    /*
     * Show the data.
     */
    show_input(input, widget);

    /*
     * Save the data, stop the signal, and quit.
     */
    gtk_object_set_data(GTK_OBJECT(widget), "old_input", (gpointer) input);
    gtk_signal_emit_stop(GTK_OBJECT(widget),
			 gtk_signal_lookup("key-press-event",
					   GTK_TYPE_EDITABLE));
    return TRUE;
}


/*
 * button_pressed_cb()
 *
 * This gets called when the user clicks a mouse button.
 * A side effect is that this moves the cursor in the GtkEntry() box,
 * so we must treat it like we lost focus!
 */
gboolean
button_pressed_cb(GtkWidget * widget,
		  GdkEventButton * event, gpointer user_data)
{
    gint *i;

    i = (gint *) gtk_object_get_data(GTK_OBJECT(widget), "focus_c");
    *i = 1;
    gtk_object_set_data(GTK_OBJECT(widget), "focus_c", (gpointer) i);

    return FALSE;
}


/*
 * lost_focus_cb()
 * 
 * Called when focus is lost.  Its main purpose is to keep track,
 * so we can cache user-input safely.
 */
gboolean
lost_focus_cb(GtkWidget * widget,
	      GdkEventFocus * event, gpointer user_data)
{
    gint *i;

    if ((i = (gint *) gtk_object_get_data(GTK_OBJECT(widget), "focus_c"))) {
	*i = 1;
	gtk_object_set_data(GTK_OBJECT(widget), "focus_c", (gpointer) i);
    }

    return TRUE;
}


/*
 * input_data_free ()
 *
 * Input: None.
 * Output: None.
 *
 * Frees an inputData structure.
 */
static void
input_data_free(inputData * data)
{
    g_list_foreach(data->list, (GFunc) g_free, NULL);
    g_list_free(data->list);
    g_free(data->show);
    g_free(data->tabs);
    /* FIXME: pawsa disabled since the memory handling is screwed up.
       This g_free contreadicts the comment above.
       use MALLOC_CHECK_ 1 for debugging.
       I know leaving problem for others is not what the real men do...
       g_free (data->typo); 
     */
    g_free(data);
}


/*
 * destroy_cb()
 *
 * Input: The widget with the data.
 * Output: None.
 *
 * Call this when the entry box gets destroyed to free user data.
 */
void
destroy_cb(GtkWidget * widget, gpointer user_data)
{
    gint *i;
    inputData *data;

    i = (gint *) gtk_object_get_data(GTK_OBJECT(widget), "focus_c");

    if (i)
	g_free(i);
    i = NULL;

    gtk_object_set_data(GTK_OBJECT(widget), "focus_c", (gpointer) i);
    data =
	(inputData *) gtk_object_get_data(GTK_OBJECT(widget), "old_input");
    if (data)
	input_data_free(data);
    data = NULL;

    gtk_object_set_data(GTK_OBJECT(widget), "old_input", (gpointer) data);
}

void
alias_free_addressbook(void)
{
    if (address_name_data) {
	g_list_foreach(address_name_data, (GFunc) completion_data_free,
		       NULL);
	g_list_free(address_name_data);
	address_name_data = NULL;
    }

    if (address_alias_data) {
	g_list_foreach(address_alias_data, (GFunc) completion_data_free,
		       NULL);
	g_list_free(address_alias_data);
	address_alias_data = NULL;
    }
}

/*
 * alias_load_addressbook ()
 *
 * Input: None.
 * Output: None.
 * 
 * Load the addresses. We take a copy of the list - This means that if
 * the addressbook is modified while we the user composes a messages, 
 * the old addressbook is used for alias expansion.
 *
 * The alternative is to load it with every keystroke (really slow)
 * or program the addresses with a caching structure (lots of work)
 */
void
alias_load_addressbook(void)
{
    GList *address_book_list, *address_list;
    LibBalsaAddressBook *address_book;

    alias_free_addressbook();

    address_book_list = balsa_app.address_book_list;
    while (address_book_list) {
	address_book = LIBBALSA_ADDRESS_BOOK(address_book_list->data);

	if (address_book->expand_aliases) {
	    CompletionData *data;

	    libbalsa_address_book_load(address_book);

	    address_list = address_book->address_list;
	    while (address_list) {
		data =
		    completion_data_new(LIBBALSA_ADDRESS
					(address_list->data), TRUE);
		if (data != NULL)
		    address_alias_data =
			g_list_append(address_alias_data, data);

		data =
		    completion_data_new(LIBBALSA_ADDRESS
					(address_list->data), FALSE);
		if (data != NULL)
		    address_name_data =
			g_list_append(address_name_data, data);

		address_list = g_list_next(address_list);
	    }
	}
	address_book_list = g_list_next(address_book_list);
    }

    if (address_alias_data || address_name_data) {

	if (complete_name)
	    g_completion_free(complete_name);

	if (complete_alias)
	    g_completion_free(complete_alias);

	complete_name = g_completion_new(
					 (GCompletionFunc)
					 completion_data_extract);
	g_completion_add_items(complete_name, address_name_data);

	complete_alias = g_completion_new(
					  (GCompletionFunc)
					  completion_data_extract);
	g_completion_add_items(complete_alias, address_alias_data);
    }
}

/*
 * Create a new CompletionData
 */
static CompletionData *
completion_data_new(LibBalsaAddress * address, gboolean alias)
{
    CompletionData *ret;

    ret = g_new0(CompletionData, 1);

    gtk_object_ref(GTK_OBJECT(address));
    ret->address = address;

    if (alias)
	ret->string = g_strdup(address->id);
    else
	ret->string = g_strdup(address->full_name);

#ifdef CASE_INSENSITIVE_NAME
    g_strup(ret->string);
#endif

    return ret;
}

/*
 * Free a CompletionData
 */
static void
completion_data_free(CompletionData * data)
{
    gtk_object_unref(GTK_OBJECT(data->address));

    g_free(data->string);
    g_free(data);
}

/*
 * The GCompletionFunc
 */
static gchar *
completion_data_extract(CompletionData * data)
{
    return data->string;
}
