/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
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

#define USE_ORIGINAL_MANAGER_FUNCS

#include <ctype.h>
#include <gnome.h>
/* use old 0.11-style API in pspell */
#define USE_ORIGINAL_MANAGER_FUNCS
#include <pspell/pspell.h>
#include <stdio.h>
#include <string.h>
#include "i18n.h"

#ifdef HAVE_PCRE
#  include <pcre.h>
#  include <pcreposix.h>
#else
#  include <sys/types.h>
#  include <regex.h>
#endif

#include "balsa-app.h"
#include "quote-color.h"
#include "spell-check.h"
#include "balsa-icons.h"

#define SPELLMGR_CODESET "UTF-8"

/* the basic structures */
struct _BalsaSpellCheck {
    GtkDialog dialog;

    GtkTextView *view;
    GtkTreeView *list;
    GtkEntry *entry;

    /* actual spell checking variables */
    PspellConfig *spell_config;
    PspellManager *spell_manager;
    const PspellWordList *word_list;
    PspellStringEmulation *suggestions;

    /* restoration information */
    GtkTextBuffer *original_text;
    GtkTextMark *original_mark;
    gint original_offset;

    /* word selection */
    GtkTextIter start_iter;
    GtkTextIter end_iter;
    GtkTextMark *start_mark;
    GtkTextMark *end_mark;

    /* config stuff */
    gchar *module;
    gchar *suggest_mode;
    guint ignore_length;
    gchar *language_tag;
    gchar *character_set;

    /* idle handler id */
    guint highlight_idle_id;
};

struct _BalsaSpellCheckClass {
    GtkDialogClass parent_class;

    void (*done_spell_check) (BalsaSpellCheck * spell_check);
};

/* enumerations */
typedef enum _LearnType LearnType;
enum _LearnType {
    SESSION_DICT,
    PERMANENT_DICT
};

enum {
    DONE_SPELLCHECK,
    LAST_SIGNAL
};

enum {
    PROP_0,
    PROP_MODULE,
    PROP_SUGGEST,
    PROP_IGNORE,
    PROP_LANGUAGE,
    PROP_CHARSET
};


/* initialization stuff */
static void balsa_spell_check_class_init(BalsaSpellCheckClass *);
static void balsa_spell_check_init(BalsaSpellCheck *);
static void spch_set_property(GObject * object, guint prop_id,
                              const GValue * value, GParamSpec * pspec);
static void spch_get_property(GObject * object, guint prop_id,
                              GValue * value, GParamSpec * pspec);
static void balsa_spell_check_destroy(GtkObject * object);


/* signal callbacks */
static void done_cb(GtkButton *, gpointer);
static void change_cb(GtkButton *, gpointer);
static void change_all_cb(GtkButton *, gpointer);
static void ignore_cb(GtkButton *, gpointer);
static void ignore_all_cb(GtkButton *, gpointer);
static void learn_cb(GtkButton * button, gpointer);
static void cancel_cb(GtkButton * button, gpointer);
static void select_word_cb(GtkTreeSelection * selection, gpointer data);


/* function prototypes */
static gboolean next_word(BalsaSpellCheck * spell_check);
static gboolean check_word(BalsaSpellCheck * spell_check);
static void switch_word(BalsaSpellCheck * spell_check,
			const gchar * old_word, const gchar * new_word);
static void finish_check(BalsaSpellCheck * spell_check);
static gboolean check_pspell_errors(PspellManager * manager);

static void setup_suggestions(BalsaSpellCheck * spell_check);
static gboolean balsa_spell_check_next(BalsaSpellCheck * spell_check);
static gboolean highlight_idle(BalsaSpellCheck * spell_check);
static void balsa_spell_check_fix(BalsaSpellCheck * spell_check,
				  gboolean fix_al);
static void balsa_spell_check_learn(BalsaSpellCheck * spell_check,
				    LearnType learn);
static void spch_save_word_iters(BalsaSpellCheck * spell_check);
static void spch_restore_word_iters(BalsaSpellCheck * spell_check);
static void spch_finish(BalsaSpellCheck * spell_check,
                        gboolean keep_changes);

/* static variables */
static GtkDialogClass *parent_class = NULL;



/* begin actual code */
GtkType
balsa_spell_check_get_type()
{
    static GtkType balsa_spell_check_type = 0;

    if (!balsa_spell_check_type) {
	static const GTypeInfo balsa_spell_check_info = {
	    sizeof(BalsaSpellCheckClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
	    (GClassInitFunc) balsa_spell_check_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
	    sizeof(BalsaSpellCheck),
            0,                  /* n_preallocs */
	    (GInstanceInitFunc) balsa_spell_check_init
	};

	balsa_spell_check_type =
	    g_type_register_static(GTK_TYPE_DIALOG, "BalsaSpellCheck",
                                   &balsa_spell_check_info, 0);
    }

    return balsa_spell_check_type;
}


static void
balsa_spell_check_class_init(BalsaSpellCheckClass * klass)
{
    GtkObjectClass *object_class;
    GObjectClass *o_class;
    GtkWidgetClass *widget_class;
    GtkContainerClass *container_class;

    object_class = (GtkObjectClass *) klass;
    o_class = (GObjectClass *) klass;
    widget_class = (GtkWidgetClass *) klass;
    container_class = (GtkContainerClass *) klass;

    parent_class = gtk_type_class(GTK_TYPE_FRAME);

    /* GObject signals */
    o_class->set_property = spch_set_property;
    o_class->get_property = spch_get_property;

    g_object_class_install_property(o_class, PROP_MODULE,
                                    g_param_spec_string("spell-module",
                                                        NULL, NULL,
                                                        NULL,
                                                        G_PARAM_READWRITE));
    g_object_class_install_property(o_class, PROP_SUGGEST,
                                    g_param_spec_string("suggest-mode",
                                                        NULL, NULL,
                                                        NULL,
                                                        G_PARAM_READWRITE));
    g_object_class_install_property(o_class, PROP_IGNORE,
                                    g_param_spec_uint("ignore-length",
                                                      NULL, NULL,
                                                      0, -1, 0,
                                                      G_PARAM_READWRITE));
    g_object_class_install_property(o_class, PROP_LANGUAGE,
                                    g_param_spec_string("language-tag",
                                                        NULL, NULL,
                                                        NULL,
                                                        G_PARAM_READWRITE));
    g_object_class_install_property(o_class, PROP_CHARSET,
                                    g_param_spec_string("character-set",
                                                        NULL, NULL,
                                                        NULL,
                                                        G_PARAM_READWRITE));

    object_class->destroy = balsa_spell_check_destroy;

    klass->done_spell_check = NULL;
}


static void
spch_set_property(GObject * object, guint prop_id, const GValue * value,
                  GParamSpec * pspec)
{
    BalsaSpellCheck *spell_check = BALSA_SPELL_CHECK(object);

    switch (prop_id) {
    case PROP_MODULE:
	balsa_spell_check_set_module(spell_check,
                                     g_value_get_string(value));
	break;

    case PROP_SUGGEST:
	balsa_spell_check_set_suggest_mode(spell_check,
					   g_value_get_string(value));
	break;

    case PROP_IGNORE:
	balsa_spell_check_set_ignore_length(spell_check,
					    g_value_get_uint(value));
	break;

    case PROP_LANGUAGE:
	balsa_spell_check_set_language(spell_check,
				       g_value_get_string(value));
	break;

    case PROP_CHARSET:
	balsa_spell_check_set_character_set(spell_check,
					    g_value_get_string(value));
	break;

    default:
	break;
    }
}


static void
spch_get_property(GObject * object, guint prop_id, GValue * value,
                  GParamSpec * pspec)
{
    BalsaSpellCheck *spell_check = BALSA_SPELL_CHECK(object);

    switch (prop_id) {
    case PROP_MODULE:
        g_value_set_string(value, spell_check->module);
	break;

    case PROP_SUGGEST:
        g_value_set_string(value, spell_check->suggest_mode);
	break;

    case PROP_IGNORE:
        g_value_set_uint(value, spell_check->ignore_length);
	break;

    case PROP_LANGUAGE:
        g_value_set_string(value, spell_check->language_tag);
	break;

    case PROP_CHARSET:
        g_value_set_string(value, spell_check->character_set);
	break;

    default:
	break;
    }
}


/* balsa_spell_check_new ()
 * 
 * Create a new spell check widget.
 * */
GtkWidget *
balsa_spell_check_new(GtkWindow * parent)
{
    BalsaSpellCheck *spell_check;

    spell_check = g_object_new(balsa_spell_check_get_type(), NULL);
    gtk_window_set_transient_for(GTK_WINDOW(spell_check), parent);

    return GTK_WIDGET(spell_check);
}


/* balsa_spell_check_new_with_text
 * 
 * Create a new spell check widget, assigning the GtkText to check.
 * */
GtkWidget *
balsa_spell_check_new_with_text(GtkWindow * parent, 
                                GtkTextView * check_text)
{
    BalsaSpellCheck *spell_check;

    spell_check = BALSA_SPELL_CHECK(balsa_spell_check_new(parent));
    spell_check->view = check_text;

    return GTK_WIDGET(spell_check);
}


/* balsa_spell_check_set_text ()
 * 
 * Set the text widget the spell check should check.
 * */
void
balsa_spell_check_set_text(BalsaSpellCheck * spell_check,
			   GtkTextView * check_text)
{
    g_return_if_fail(check_text != NULL);
    g_return_if_fail(GTK_IS_TEXT_VIEW(check_text));

    spell_check->view = check_text;
}


/* balsa_spell_check_set_module ()
 * 
 * Set the pspell module to use for spell checking (ispell, aspell,
 * etc.).
 * */
void
balsa_spell_check_set_module(BalsaSpellCheck * spell_check,
			     const gchar * module_name)
{
    g_return_if_fail(spell_check != NULL);
    g_return_if_fail(BALSA_IS_SPELL_CHECK(spell_check));

    g_free(spell_check->module);
    spell_check->module = g_strdup(module_name);
}


/* balsa_spell_check_set_suggest_mode ()
 * 
 * Select the suggestion mode to use.  This determines how hard the
 * spell checking engine works to find suggestions to use, and which
 * suggestions to put near the beginning.  There are three settings:
 * fast, normal, and bad-spellers.
 * */
void
balsa_spell_check_set_suggest_mode(BalsaSpellCheck * spell_check,
				   const gchar * suggest)
{
    g_return_if_fail(spell_check != NULL);
    g_return_if_fail(BALSA_IS_SPELL_CHECK(spell_check));

    g_free(spell_check->suggest_mode);
    spell_check->suggest_mode = g_strdup(suggest);
}


/* balsa_spell_check_set_ignore_length ()
 * 
 * Set the minimum length words must be to be considered for checking.
 * */
void
balsa_spell_check_set_ignore_length(BalsaSpellCheck * spell_check,
				    guint length)
{
    g_return_if_fail(spell_check != NULL);
    g_return_if_fail(BALSA_IS_SPELL_CHECK(spell_check));

    spell_check->ignore_length = length;
}


/* balsa_spell_check_set_language ()
 * 
 * Set the language to do spell checking in.
 * */
void
balsa_spell_check_set_language(BalsaSpellCheck * spell_check,
			       const gchar * language)
{
    g_return_if_fail(spell_check != NULL);
    g_return_if_fail(BALSA_IS_SPELL_CHECK(spell_check));

    g_free(spell_check->language_tag);
    spell_check->language_tag = g_strdup(language);
}


/* balsa_spell_check_set_character_set ()
 * 
 * Set the character set to spell check with.
 * */
void
balsa_spell_check_set_character_set(BalsaSpellCheck * spell_check,
				    const gchar * char_set)
{
    g_return_if_fail(spell_check != NULL);
    g_return_if_fail(BALSA_IS_SPELL_CHECK(spell_check));

    g_free(spell_check->character_set);
    spell_check->character_set = g_strdup(char_set);
}


/* balsa_spell_check_init ()
 * 
 * Initialization of the class to reasonable default values, set up
 * buttons signal callbacks.  
 * */
static void
balsa_spell_check_init(BalsaSpellCheck * spell_check)
{
    GtkWidget *vbox;
    GtkWidget *new_word_text;
    GtkWidget *sw;
    GtkWidget *suggestion_list;
    GtkListStore *store;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeSelection *selection;
    GtkWidget *change;
    GtkWidget *change_all;
    GtkWidget *ignore;
    GtkWidget *ignore_all;
    GtkWidget *learn;
    GtkWidget *done;
    GtkWidget *cancel;

    const guint padding = 4;

    /* Set spell checker */

    spell_check->original_text = NULL;
    spell_check->word_list = NULL;
    spell_check->suggestions = NULL;

    spell_check->module = g_strdup("aspell");
    spell_check->suggest_mode = g_strdup("normal");
    spell_check->ignore_length = 0;
    spell_check->language_tag = g_strdup("en");
    spell_check->character_set = g_strdup("iso8859-*");

    /* setup suggestion display */
    new_word_text = gtk_entry_new();
    spell_check->entry = GTK_ENTRY(new_word_text);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(spell_check)->vbox),
                       new_word_text, FALSE, FALSE, 0);

    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(spell_check)->vbox),
                       sw, TRUE, TRUE, 0);

    /* setup suggestion list */
    store = gtk_list_store_new(1, G_TYPE_STRING);
    suggestion_list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes(NULL, renderer,
                                                      "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(suggestion_list), column);
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(suggestion_list));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_BROWSE);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(suggestion_list), FALSE);
    gtk_container_add(GTK_CONTAINER(sw), suggestion_list);
    spell_check->list = GTK_TREE_VIEW(suggestion_list);

    /* setup buttons to perform actions */
    vbox = gtk_vbox_new(FALSE, padding);
    change = balsa_stock_button_with_label(GTK_STOCK_REDO,
					   "_Change");
#if GTK_CHECK_VERSION(2, 11, 0)
    gtk_widget_set_tooltip_text(change,
                                _("Replace the current word "
                                  "with the selected suggestion"));
#else                           /* GTK_CHECK_VERSION(2, 11, 0) */
    gtk_tooltips_set_tip(balsa_app.tooltips, change,
			 _("Replace the current word "
                           "with the selected suggestion"),
			 NULL);
#endif                          /* GTK_CHECK_VERSION(2, 11, 0) */
    gtk_box_pack_start(GTK_BOX(vbox), change, FALSE, FALSE, 0);

    change_all = balsa_stock_button_with_label(GTK_STOCK_REFRESH,
					       "Change _All");
#if GTK_CHECK_VERSION(2, 11, 0)
    gtk_widget_set_tooltip_text(change_all,
                                _("Replace all occurrences of the current word "
                                  "with the selected suggestion"));
#else                           /* GTK_CHECK_VERSION(2, 11, 0) */
    gtk_tooltips_set_tip(balsa_app.tooltips, change_all,
			 _("Replace all occurrences of the current word "
                           "with the selected suggestion"),
			 NULL);
#endif                          /* GTK_CHECK_VERSION(2, 11, 0) */
    gtk_box_pack_start(GTK_BOX(vbox), change_all, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(spell_check)->action_area),
                       vbox, FALSE, FALSE, 0);

    vbox = gtk_vbox_new(FALSE, padding);
    ignore = balsa_stock_button_with_label(GTK_STOCK_GO_FORWARD,
                                           "_Ignore");

#if GTK_CHECK_VERSION(2, 11, 0)
    gtk_widget_set_tooltip_text(ignore,
                                _("Skip the current word"));
#else                           /* GTK_CHECK_VERSION(2, 11, 0) */
    gtk_tooltips_set_tip(balsa_app.tooltips, ignore,
			 _("Skip the current word"), NULL);
#endif                          /* GTK_CHECK_VERSION(2, 11, 0) */
    gtk_box_pack_start(GTK_BOX(vbox), ignore, FALSE, FALSE, 0);

    ignore_all = balsa_stock_button_with_label(GTK_STOCK_GOTO_LAST,
                                               "Ignore A_ll");
#if GTK_CHECK_VERSION(2, 11, 0)
    gtk_widget_set_tooltip_text(ignore_all,
                                _("Skip all occurrences of the current word"));
#else                           /* GTK_CHECK_VERSION(2, 11, 0) */
    gtk_tooltips_set_tip(balsa_app.tooltips, ignore_all,
			 _("Skip all occurrences of the current word"),
			 NULL);
#endif                          /* GTK_CHECK_VERSION(2, 11, 0) */
    gtk_box_pack_start(GTK_BOX(vbox), ignore_all, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(spell_check)->action_area),
                       vbox, FALSE, FALSE, 0);

    vbox = gtk_vbox_new(FALSE, padding);
    learn = balsa_stock_button_with_label(BALSA_PIXMAP_BOOK_OPEN,
                                          "_Learn");
#if GTK_CHECK_VERSION(2, 11, 0)
    gtk_widget_set_tooltip_text(learn,
                                _("Add the current word to your personal dictionary"));
#else                           /* GTK_CHECK_VERSION(2, 11, 0) */
    gtk_tooltips_set_tip(balsa_app.tooltips, learn,
			 _("Add the current word to your personal dictionary"),
			 NULL);
#endif                          /* GTK_CHECK_VERSION(2, 11, 0) */
    gtk_box_pack_start(GTK_BOX(vbox), learn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(spell_check)->action_area),
                       vbox, FALSE, FALSE, 0);

    vbox = gtk_vbox_new(FALSE, padding);
    done = balsa_stock_button_with_label(GTK_STOCK_OK, "_Done");
#if GTK_CHECK_VERSION(2, 11, 0)
    gtk_widget_set_tooltip_text(done, _("Finish spell checking"));
#else                           /* GTK_CHECK_VERSION(2, 11, 0) */
    gtk_tooltips_set_tip(balsa_app.tooltips, done,
			 _("Finish spell checking"), NULL);
#endif                          /* GTK_CHECK_VERSION(2, 11, 0) */
    gtk_box_pack_start(GTK_BOX(vbox), done, FALSE, FALSE, 0);

    cancel = gtk_button_new_from_stock(GTK_STOCK_CANCEL);
#if GTK_CHECK_VERSION(2, 11, 0)
    gtk_widget_set_tooltip_text(learn,
                                _("Revert all changes and finish spell checking"));
#else                           /* GTK_CHECK_VERSION(2, 11, 0) */
    gtk_tooltips_set_tip(balsa_app.tooltips, cancel,
			 _("Revert all changes and finish spell checking"),
			 NULL);
#endif                          /* GTK_CHECK_VERSION(2, 11, 0) */
    gtk_box_pack_start(GTK_BOX(vbox), cancel, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(GTK_DIALOG(spell_check)->action_area),
                     vbox, FALSE, FALSE, 0);

    /* connect signal handlers */
    g_signal_connect(G_OBJECT(selection), "changed",
		     G_CALLBACK(select_word_cb), spell_check);

    g_signal_connect(G_OBJECT(change), "clicked",
		     G_CALLBACK(change_cb), spell_check);

    g_signal_connect(G_OBJECT(change_all), "clicked",
		     G_CALLBACK(change_all_cb), spell_check);

    g_signal_connect(G_OBJECT(ignore), "clicked",
		     G_CALLBACK(ignore_cb), spell_check);

    g_signal_connect(G_OBJECT(ignore_all), "clicked",
		     G_CALLBACK(ignore_all_cb), spell_check);

    g_signal_connect(G_OBJECT(learn), "clicked",
		     G_CALLBACK(learn_cb), spell_check);

    g_signal_connect(G_OBJECT(cancel), "clicked",
		     G_CALLBACK(cancel_cb), spell_check);

    g_signal_connect(G_OBJECT(done), "clicked",
		     G_CALLBACK(done_cb), spell_check);

    gtk_window_set_title(GTK_WINDOW(spell_check), _("Spell check"));
    gtk_window_set_wmclass(GTK_WINDOW(spell_check), "spell", "Balsa");
}


/* select_word_cb ()
 * 
 * When the user selects a word from the list of available
 * suggestions, replace the text in the entry text box with the text
 * from the clist selection.
 * */
static void
select_word_cb(GtkTreeSelection * selection, gpointer data)
{
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar *str;
        BalsaSpellCheck *spell_check = BALSA_SPELL_CHECK(data);

        gtk_tree_model_get(model, &iter, 0, &str, -1);
        gtk_entry_set_text(spell_check->entry, str);
        g_free(str);
    }
}


/* ignore_cb ()
 * 
 * Selecting the ignore button causes the current word to be skipped
 * for checking.
 * */
static void
ignore_cb(GtkButton * button, gpointer data)
{
    BalsaSpellCheck *spell_check;

    g_return_if_fail(data != NULL);
    g_return_if_fail(BALSA_IS_SPELL_CHECK(data));

    spell_check = BALSA_SPELL_CHECK(data);
    spch_restore_word_iters(spell_check);

    /* Ignore the current word, go to next */
    finish_check(spell_check);
    balsa_spell_check_next(spell_check);
}


/* ignore_all_cb ()
 * 
 * Add the current word to the session library, causing it to be
 * skipped for the rest of the checking session.
 * */
static void
ignore_all_cb(GtkButton * button, gpointer data)
{
    /* add the current word to the session library */
    BalsaSpellCheck *spell_check;

    g_return_if_fail(data != NULL);
    g_return_if_fail(BALSA_IS_SPELL_CHECK(data));

    spell_check = BALSA_SPELL_CHECK(data);
    spch_restore_word_iters(spell_check);
    balsa_spell_check_learn(spell_check, SESSION_DICT);
    balsa_spell_check_next(spell_check);
}


/* change_cb ()
 * 
 * Change the current word being checked to the selected suggestion or
 * what the user enters in the entry box.
 * */
static void
change_cb(GtkButton * button, gpointer data)
{
    BalsaSpellCheck *spell_check;

    g_return_if_fail(data != NULL);
    g_return_if_fail(BALSA_IS_SPELL_CHECK(data));

    spell_check = BALSA_SPELL_CHECK(data);
    spch_restore_word_iters(spell_check);

    balsa_spell_check_fix(spell_check, FALSE);
    balsa_spell_check_next(spell_check);
}


/* change_all_cb ()
 * 
 * Replace all occurances of the currently misspelled word in the text
 * to the current suggestion.
 * */
static void
change_all_cb(GtkButton * button, gpointer data)
{
    BalsaSpellCheck *spell_check;

    /* change all similarly misspelled words without asking */
    g_return_if_fail(data != NULL);
    g_return_if_fail(BALSA_IS_SPELL_CHECK(data));

    spell_check = BALSA_SPELL_CHECK(data);
    spch_restore_word_iters(spell_check);

    balsa_spell_check_fix(spell_check, TRUE);
    balsa_spell_check_next(spell_check);
}


/* learn_cb ()
 * 
 * Add the current word to the permanent personal dictionary.
 * */
static void
learn_cb(GtkButton * button, gpointer data)
{
    BalsaSpellCheck *spell_check;

    g_return_if_fail(data != NULL);
    g_return_if_fail(BALSA_IS_SPELL_CHECK(data));

    spell_check = BALSA_SPELL_CHECK(data);
    spch_restore_word_iters(spell_check);

    balsa_spell_check_learn(spell_check, PERMANENT_DICT);
    balsa_spell_check_next(spell_check);
}


/* cancel_cb ()
 * 
 * Cancel the check, restoring the original text.
 * */
static void
cancel_cb(GtkButton * button, gpointer data)
{
    BalsaSpellCheck *spell_check;

    g_return_if_fail(data != NULL);
    g_return_if_fail(BALSA_IS_SPELL_CHECK(data));

    spell_check = BALSA_SPELL_CHECK(data);
    spch_restore_word_iters(spell_check);

    spch_finish(spell_check, FALSE);
}

/* done_cb ()
 * 
 * Signal callback for the done button, end the spell check, keeping
 * all changes up to this point.
 * */
static void
done_cb(GtkButton * button, gpointer data)
{
    BalsaSpellCheck *spell_check;

    g_return_if_fail(data != NULL);
    g_return_if_fail(BALSA_IS_SPELL_CHECK(data));

    spell_check = BALSA_SPELL_CHECK(data);
    spch_restore_word_iters(spell_check);

    spch_finish(spell_check, TRUE);
}


/* balsa_spell_check_start ()
 * 
 * Start the spell check, allocating the PSpellConfig and
 * PSpellManager to do the checking.
 * */

static regex_t quoted_rex;
static gboolean quoted_rex_compiled = FALSE;

void
balsa_spell_check_start(BalsaSpellCheck * spell_check, GtkWindow *parent_wnd)
{
    PspellCanHaveError *spell_error;
    gchar *string;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(spell_check->view);
    GtkTextIter start, end, iter;
    GtkTextMark *insert;


    /* Config the spell check */
    spell_check->spell_config = new_pspell_config();
    pspell_config_replace(spell_check->spell_config,
			  "language-tag", spell_check->language_tag);
    pspell_config_replace(spell_check->spell_config,
			  "module", spell_check->module);
    pspell_config_replace(spell_check->spell_config,
			  "sug-mode", spell_check->suggest_mode);

    string = g_ascii_strdown(spell_check->character_set, -1);
    if (!strncmp(string, "iso-", 4)) {
	    /* pspell .map files are iso8859-* */
	    memmove(&string[3], &string[4], strlen(string) - 3);
    }
    pspell_config_replace(spell_check->spell_config,
			  "encoding", string);
    g_free(string);

    string = g_strdup_printf("%d", spell_check->ignore_length);
    pspell_config_replace(spell_check->spell_config, "ignore", string);
    g_free(string);

    spell_error = new_pspell_manager(spell_check->spell_config);
    delete_pspell_config(spell_check->spell_config);

    if (pspell_error_number(spell_error) != 0) {
	/* quit without breaking things */
	balsa_information_parented(parent_wnd,
                                   LIBBALSA_INFORMATION_ERROR,
                                   pspell_error_message(spell_error));

	/* Generate a response signal. */
	gtk_dialog_response(GTK_DIALOG(spell_check), 0);

	return;
    }

    spell_check->spell_manager = to_pspell_manager(spell_error);
    spell_check->spell_config =
	pspell_manager_config(spell_check->spell_manager);

    insert = gtk_text_buffer_get_insert(buffer);
    gtk_text_buffer_get_iter_at_mark(buffer, &start, insert);
    spell_check->original_offset = gtk_text_iter_get_offset(&start);
    spell_check->original_mark =
        gtk_text_buffer_create_mark(buffer, NULL, &start, FALSE);

    /* Marks for saving iter locations. */
    spell_check->start_mark =
        gtk_text_buffer_create_mark(buffer, NULL, &start, TRUE);
    spell_check->end_mark =
        gtk_text_buffer_create_mark(buffer, NULL, &start, FALSE);

    /* Get the original text so we can always revert */
    spell_check->original_text =
	gtk_text_buffer_new(gtk_text_buffer_get_tag_table(buffer));
    gtk_text_buffer_get_start_iter(spell_check->original_text, &iter);
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gtk_text_buffer_insert_range(spell_check->original_text, &iter,
                                 &start, &end);

    if (balsa_app.debug)
	balsa_information(LIBBALSA_INFORMATION_DEBUG,
			  "BalsaSpellCheck: Start\n");

    /* 
     * compile the quoted-text regular expression (note:
     * balsa_app.quote_regex may change, so compile it new every
     * time!)
     */
    if (quoted_rex_compiled)
        regfree(&quoted_rex);
    if (regcomp(&quoted_rex, balsa_app.quote_regex, REG_EXTENDED)) {
        balsa_information(LIBBALSA_INFORMATION_ERROR,
                          "BalsaSpellCheck: Quoted text "
                          "regular expression compilation failed\n");
        quoted_rex_compiled = FALSE;
    } else
        quoted_rex_compiled = TRUE;

    spell_check->end_iter = start;

    /* start the check */
    if (!balsa_spell_check_next(spell_check))
        gtk_widget_show_all(GTK_WIDGET(spell_check));
}

/* balsa_spell_check_next ()
 * 
 * Continue the spell check, clear the old words and suggestions, and
 * moving onto the next incorrect word.  Replace the incorrect word
 * with a highlighted version, move the text to display it, and setup
 * the suggestions.
 *
 * Return TRUE if there are no more errors, FALSE if we found one
 * */
static gboolean
balsa_spell_check_next(BalsaSpellCheck * spell_check)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(spell_check->view);
    GtkTreeView *tree_view = spell_check->list;
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    GtkTreeIter iter;

    if (!next_word(spell_check)) {
	spch_finish(spell_check, TRUE);
	return TRUE;
    }

    while (check_word(spell_check)) {
	if (!next_word(spell_check)) {
	    spch_finish(spell_check, TRUE);
	    return TRUE;
	}
    }

    /* found an incorrect spelling */
    if (gtk_tree_model_get_iter_first(model, &iter)) {
        GtkTreePath *path = gtk_tree_model_get_path(model, &iter);
        gtk_tree_selection_select_path(selection, path);
        gtk_tree_view_scroll_to_cell(tree_view, path, NULL, TRUE, 0.5, 0);
        gtk_tree_path_free(path);
    }

    /* Highlight current word by selecting it; first we'll move the
     * cursor to start of this word; we'll highlight it by moving
     * the selection-bound to its end, but we must do that in an idle
     * callback, otherwise the first word is never highlighted. */
    gtk_text_buffer_place_cursor(buffer, &spell_check->start_iter);
    spell_check->highlight_idle_id =
        g_idle_add((GSourceFunc) highlight_idle, spell_check);

    /* scroll text window to show current word */
    gtk_text_view_scroll_to_mark(spell_check->view,
                                 gtk_text_buffer_get_insert(buffer),
                                 0, FALSE, 0, 0);

    spch_save_word_iters(spell_check);
    return FALSE;
}

/* Move the selection bound to the end of the current word, to highlight
 * it. */
static gboolean
highlight_idle(BalsaSpellCheck * spell_check)
{
    GtkTextBuffer *buffer;

    gdk_threads_enter();
    if (spell_check->highlight_idle_id) {
        spch_restore_word_iters(spell_check);
        buffer = gtk_text_view_get_buffer(spell_check->view);
        gtk_text_buffer_move_mark_by_name(buffer, "selection_bound",
                                          &spell_check->end_iter);
        spell_check->highlight_idle_id = 0;
    }
    gdk_threads_leave();
    return FALSE;
}


/* balsa_spell_check_learn ()
 * 
 * Learn the current word, either to the personal or session
 * dictionaries.  
 * */
static void
balsa_spell_check_learn(BalsaSpellCheck * spell_check,
			LearnType learn_type)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(spell_check->view);
    gchar *word;
    gint result;

    word =
        gtk_text_buffer_get_text(buffer, &spell_check->start_iter,
                                 &spell_check->end_iter, FALSE);

    if (balsa_app.debug)
	balsa_information(LIBBALSA_INFORMATION_DEBUG,
			  "BalsaSpellCheck: Learn %s\n", word);

    if (learn_type == SESSION_DICT) {
	result =
	    pspell_manager_add_to_session(spell_check->spell_manager,
					  word);
    } else {
	result =
	    pspell_manager_add_to_personal(spell_check->spell_manager,
					   word);
    }

    /* If result is 0, the learn operation failed */
    if (!result) {
	if (pspell_manager_error_number(spell_check->spell_manager) != 0) {
	    balsa_information(LIBBALSA_INFORMATION_ERROR,
			      "BalsaSpellCheck: Learn operation failed;\n%s\n",
			      pspell_manager_error_message
			      (spell_check->spell_manager));
	} else {
	    balsa_information(LIBBALSA_INFORMATION_ERROR,
			      "BalsaSpellCheck: Learn operation failed.\n");
	}
    }

    g_free(word);
    finish_check(spell_check);
}


/* balsa_spell_check_fix ()
 * 
 * Replace the current word with the currently selected word, and if
 * fix_all is true, replace all other occurances of the current word
 * in the text with the correction.
 * */
static void
balsa_spell_check_fix(BalsaSpellCheck * spell_check, gboolean fix_all)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(spell_check->view);
    gchar *new_word;
    gchar *old_word;
    gchar *wrong_word = NULL;

    old_word =
        gtk_text_buffer_get_text(buffer, &spell_check->start_iter,
                                 &spell_check->end_iter, FALSE);
    new_word = gtk_editable_get_chars(GTK_EDITABLE(spell_check->entry),
				      0, -1);

    if (!new_word) {
	/* no word to replace, ignore */
	g_free(old_word);
	return;
    }

    /* Some spelling modules can learn from user
     * replacement choices. */
    pspell_manager_store_replacement(spell_check->spell_manager,
				     old_word, new_word);

    if (check_pspell_errors(spell_check->spell_manager)) {
	g_free(new_word);
	g_free(old_word);
	spch_finish(spell_check, TRUE);
	return;
    }

    if (balsa_app.debug)
	balsa_information(LIBBALSA_INFORMATION_DEBUG,
			  "BalsaSpellCheck: Replace %s with %s\n",
			  old_word, new_word);

    switch_word(spell_check, old_word, new_word);


    if (fix_all) {
        spch_save_word_iters(spell_check);
	while (next_word(spell_check)) {
            wrong_word =
                gtk_text_buffer_get_text(buffer, &spell_check->start_iter,
                                         &spell_check->end_iter, FALSE);
	    if (g_ascii_strcasecmp(old_word, wrong_word) == 0) {
		switch_word(spell_check, wrong_word, new_word);
	    }
	    g_free(wrong_word);
	}
        spch_restore_word_iters(spell_check);
    }

    g_free(new_word);
    g_free(old_word);
    finish_check(spell_check);
}


/* balsa_spell_check_destroy ()
 * 
 * Clean up variables if the widget is destroyed.
 * */
static void
balsa_spell_check_destroy(GtkObject * object)
{
    BalsaSpellCheck *spell_check;

    g_return_if_fail(object != NULL);
    g_return_if_fail(BALSA_IS_SPELL_CHECK(object));

    spell_check = BALSA_SPELL_CHECK(object);

    if (spell_check->suggestions)
	finish_check(spell_check);

    if (spell_check->spell_manager)
	spch_finish(spell_check, TRUE);

    g_free(spell_check->module);
    spell_check->module = NULL;
    g_free(spell_check->suggest_mode);
    spell_check->suggest_mode = NULL;
    g_free(spell_check->language_tag);
    spell_check->language_tag = NULL;
    g_free(spell_check->character_set);
    spell_check->character_set = NULL;

    if (spell_check->highlight_idle_id) {
        g_source_remove(spell_check->highlight_idle_id);
        spell_check->highlight_idle_id = 0;
    }

    if (quoted_rex_compiled) {
        regfree(&quoted_rex);
        quoted_rex_compiled = FALSE;
    }

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (object);
}


/* spch_finish ()
 * 
 * Clean up the variables from the spell check, and emit the done
 * signal so that the main program knows to resume normal operation.
 * */
static void
spch_finish(BalsaSpellCheck * spell_check, gboolean keep_changes)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(spell_check->view);
    GtkTextIter original;


    finish_check(spell_check);

    if (keep_changes) {
	pspell_manager_save_all_word_lists(spell_check->spell_manager);
        gtk_text_buffer_get_iter_at_mark(buffer, &original,
                                         spell_check->original_mark);
        gtk_text_buffer_delete_mark(buffer, spell_check->original_mark);
        gtk_text_buffer_delete_mark(buffer, spell_check->start_mark);
        gtk_text_buffer_delete_mark(buffer, spell_check->end_mark);
        g_object_unref(spell_check->original_text);
    } else {
	/* replace corrected text with original text */
        buffer = spell_check->original_text;
        gtk_text_view_set_buffer(spell_check->view, buffer);
        gtk_text_buffer_get_iter_at_offset(buffer, &original,
                                           spell_check->original_offset);
    }
    gtk_text_buffer_place_cursor(buffer, &original);

    spell_check->original_text = NULL;

    check_pspell_errors(spell_check->spell_manager);

    delete_pspell_manager(spell_check->spell_manager);
    spell_check->spell_manager = NULL;


    if (balsa_app.debug)
	balsa_information(LIBBALSA_INFORMATION_DEBUG,
			  "BalsaSpellCheck: Finished\n");

    /* Generate a response signal. */
    gtk_dialog_response(GTK_DIALOG(spell_check), 0);
}


/* setup_suggestions ()
 * 
 * Retrieves the suggestions for the word that is currently being
 * checked, and place them in the word list.
 * */
static void
setup_suggestions(BalsaSpellCheck * spell_check)
{
    GtkTreeModel *model = gtk_tree_view_get_model(spell_check->list);
    const PspellWordList *wl;
    const gchar *new_word;

    wl = spell_check->word_list;
    spell_check->suggestions = pspell_word_list_elements(wl);

    while ((new_word =
            pspell_string_emulation_next(spell_check->suggestions)) !=
           NULL) {
        GtkTreeIter iter;
	gsize m, n=strlen(new_word);
		gchar *cword=
		    g_convert(new_word, n, SPELLMGR_CODESET,
			      pspell_config_retrieve(spell_check->spell_config, "encoding"),
			      &n, &m, NULL);
	

        if (balsa_app.debug)
            balsa_information(LIBBALSA_INFORMATION_DEBUG,
                              "BalsaSpellCheck: Suggest %s (%s)\n", new_word,
			      cword);
	if(new_word)
	    new_word=cword;

        gtk_list_store_append(GTK_LIST_STORE(model), &iter);
        gtk_list_store_set(GTK_LIST_STORE(model), &iter, 0, new_word, -1);
	g_free(cword);
    }
}


/* check_word ()
 * 
 * Check the current word of the BalsaSpellCheck object.  If
 * incorrect, fill the clist and entrybox with the suggestions
 * obtained from pspell, and retrurn a false value.  Otherwise return
 * true.
 * */
static gboolean
check_word(BalsaSpellCheck * spell_check)
{
    gboolean correct;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(spell_check->view);
    gchar *word = NULL;


    word = gtk_text_buffer_get_text(buffer,
				    &spell_check->start_iter,
				    &spell_check->end_iter, FALSE);

    if (word) {
	gsize m, n=strlen(word);
	gchar *cword=
	    g_convert(word, n, 
		      pspell_config_retrieve(spell_check->spell_config, "encoding"),
		      SPELLMGR_CODESET, &n, &m, NULL);

	if(cword) {
	    g_free(word);
	    word=cword;
	}

	if (balsa_app.debug)
	    balsa_information(LIBBALSA_INFORMATION_DEBUG,
			      "BalsaSpellCheck: Check %s", word);

	correct = pspell_manager_check(spell_check->spell_manager, word);
    } else {
	return TRUE;
    }

    if (!correct) {
	if (balsa_app.debug)
	    balsa_information(LIBBALSA_INFORMATION_DEBUG,
			      " ...incorrect.\n");

	spell_check->word_list =
	    pspell_manager_suggest(spell_check->spell_manager, word);
	setup_suggestions(spell_check);
    } else {
	if (balsa_app.debug)
	    balsa_information(LIBBALSA_INFORMATION_DEBUG,
			      " ...correct.\n");
    }

    g_free(word);
    return correct;
}


/* finish_check ()
 * 
 * Clean up all of the variables from the spell checking, 
 * freeing the suggestions.  
 * */
static void
finish_check(BalsaSpellCheck * spell_check)
{
    GtkTreeModel *model = gtk_tree_view_get_model(spell_check->list);

    /* get rid of the suggestions */
    gtk_list_store_clear(GTK_LIST_STORE(model));

    gtk_entry_set_text(spell_check->entry, "");

    free(spell_check->suggestions);
    spell_check->suggestions = NULL;
}


/* check_pspell_errors ()
 * 
 * To be called after trying things with the pspell manager, if there
 * were any errors with the operation it will generate an error
 * message and return true.
 * */
static gboolean
check_pspell_errors(PspellManager * manager)
{
    if (pspell_manager_error_number(manager) != 0) {
	balsa_information(LIBBALSA_INFORMATION_WARNING,
			  "BalsaSpellCheck: Pspell Error: %s\n",
			  pspell_manager_error_message(manager));
	return TRUE;
    }
    return FALSE;
}


/* next_word() 
 * 
 * Move the pointer positions to the next word in preparation for
 * checking.  Returns true if successful, false if it has reached the
 * end of the text.
 * */
static gboolean
next_word(BalsaSpellCheck * spell_check)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(spell_check->view);
    GtkTextIter line_start, line_end;
    gchar *line = NULL;
    gboolean skip_sig, skip_quoted = FALSE;

    /* find end of current line */
    line_end = spell_check->end_iter;
    gtk_text_iter_forward_line(&line_end);

    do {
        gunichar last_char;

        /* move forward one word */
        do {

            if (!gtk_text_iter_forward_word_end(&spell_check->end_iter))
                /* end of buffer */
                return FALSE;

            /* we want only alpha words */
            spell_check->start_iter = spell_check->end_iter;
            gtk_text_iter_backward_char(&spell_check->start_iter);
            last_char = gtk_text_iter_get_char(&spell_check->start_iter);
        } while (!g_unichar_isalpha(last_char));

        /* is the new word on a new line? */
        while (gtk_text_iter_compare(&spell_check->end_iter, &line_end) > 0) {
            line_start = line_end;
            gtk_text_iter_forward_line(&line_end);
            line = gtk_text_buffer_get_text(buffer, &line_start,
                                        &line_end, FALSE);
            skip_sig = (!balsa_app.check_sig
                        && strcmp(line, "-- \n") == 0);
            skip_quoted = (!balsa_app.check_quoted && quoted_rex_compiled
                           && is_a_quote(line, &quoted_rex));
            g_free(line);

            if (skip_sig)
                /* new word is in the sig */
                return FALSE;
        }
        /* we've found the line that the new word is on--keep looking if
         * it's quoted */
    } while (skip_quoted);

    spell_check->start_iter = spell_check->end_iter;
    gtk_text_iter_backward_word_start(&spell_check->start_iter);

    return TRUE;
}


/* switch_word ()
 * 
 * Perform the actual replacement of the incorrect old_word with the
 * new_word, calculating the overall change in length and change in
 * position for the beginning and end of the word.
 * */
static void
switch_word(BalsaSpellCheck * spell_check, const gchar * old_word,
	    const gchar * new_word)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(spell_check->view);

    /* remove and replace the current word. */
    spch_save_word_iters(spell_check);
    gtk_text_buffer_delete(buffer, &spell_check->start_iter,
                           &spell_check->end_iter);
    gtk_text_buffer_insert(buffer, &spell_check->end_iter, new_word, -1);
    spch_restore_word_iters(spell_check);
}

static void
spch_save_word_iters(BalsaSpellCheck * spell_check)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(spell_check->view);

    gtk_text_buffer_move_mark(buffer, spell_check->start_mark,
                              &spell_check->start_iter);
    gtk_text_buffer_move_mark(buffer, spell_check->end_mark,
                              &spell_check->end_iter);
}

static void
spch_restore_word_iters(BalsaSpellCheck * spell_check)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(spell_check->view);

    gtk_text_buffer_get_iter_at_mark(buffer, &spell_check->start_iter,
                                     spell_check->start_mark);
    gtk_text_buffer_get_iter_at_mark(buffer, &spell_check->end_iter,
                                     spell_check->end_mark);
}
