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
#   include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "spell-check.h"

#include <enchant.h>
#include <glib/gi18n.h>

#include "balsa-app.h"
#include "quote-color.h"
#include "balsa-icons.h"

#if HAVE_GTKSOURCEVIEW
#   include <gtksourceview/gtksource.h>
#endif                          /* HAVE_GTKSOURCEVIEW */

#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "spell-check"


/* the basic structures */
struct _BalsaSpellCheck {
    GtkWindow window;

    GtkTextView *text_view;
    GtkTreeView *tree_view;
    GtkEntry    *entry;

    /* actual spell checking variables */
    EnchantBroker *broker;
    EnchantDict   *dict;
    gchar        **suggestions;

    /* restoration information */
#if HAVE_GTKSOURCEVIEW
    GtkSourceBuffer *original_text;
#else                           /* HAVE_GTKSOURCEVIEW */
    GtkTextBuffer *original_text;
#endif                          /* HAVE_GTKSOURCEVIEW */
    GtkTextMark *original_mark;
    gint         original_offset;

    /* word selection */
    GtkTextIter  start_iter;
    GtkTextIter  end_iter;
    GtkTextMark *start_mark;
    GtkTextMark *end_mark;

    /* config stuff */
    gchar *language_tag;

    /* idle handler id */
    guint highlight_idle_id;
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
    PROP_LANGUAGE
};


/* initialization stuff */
static void spch_set_property(GObject * object, guint prop_id,
                              const GValue * value, GParamSpec * pspec);
static void spch_get_property(GObject * object, guint prop_id,
                              GValue * value, GParamSpec * pspec);
static void balsa_spell_check_destroy(GObject * object);


/* signal callbacks */
static void done_cb(GtkButton *,
                    gpointer);
static void change_cb(GtkButton *,
                      gpointer);
static void change_all_cb(GtkButton *,
                          gpointer);
static void ignore_cb(GtkButton *,
                      gpointer);
static void ignore_all_cb(GtkButton *,
                          gpointer);
static void learn_cb(GtkButton *button,
                     gpointer);
static void cancel_cb(GtkButton *button,
                      gpointer);
static void select_word_cb(GtkTreeSelection *selection,
                           gpointer          data);


/* function prototypes */
static gboolean next_word(BalsaSpellCheck *spell_check);
static gboolean check_word(BalsaSpellCheck *spell_check);
static void     switch_word(BalsaSpellCheck *spell_check,
                            const gchar     *new_word);
static void     finish_check(BalsaSpellCheck *spell_check);
static gboolean check_error(BalsaSpellCheck *spell_check);

static void     setup_suggestions(BalsaSpellCheck *spell_check,
                                  gsize            n_suggs);
static gboolean balsa_spell_check_next(BalsaSpellCheck *spell_check);
static gboolean highlight_idle(BalsaSpellCheck *spell_check);
static void     balsa_spell_check_fix(BalsaSpellCheck *spell_check,
                                      gboolean         fix_al);
static void     balsa_spell_check_learn(BalsaSpellCheck *spell_check,
                                        LearnType        learn);
static void     spch_save_word_iters(BalsaSpellCheck *spell_check);
static void     spch_restore_word_iters(BalsaSpellCheck *spell_check);
static void     spch_finish(BalsaSpellCheck *spell_check,
                            gboolean         keep_changes);

/* define the class */

G_DEFINE_TYPE(BalsaSpellCheck, balsa_spell_check, GTK_TYPE_WINDOW);

static void
balsa_spell_check_class_init(BalsaSpellCheckClass *klass)
{
    GObjectClass *object_class;

    object_class = (GObjectClass *) klass;

    /* GObject signals */
    object_class->set_property = spch_set_property;
    object_class->get_property = spch_get_property;

    g_object_class_install_property(object_class, PROP_LANGUAGE,
                                    g_param_spec_string("language-tag",
                                                        NULL, NULL,
                                                        NULL,
                                                        G_PARAM_READWRITE));

    object_class->dispose = balsa_spell_check_destroy;
}


static void
spch_set_property(GObject      *object,
                  guint         prop_id,
                  const GValue *value,
                  GParamSpec   *pspec)
{
    BalsaSpellCheck *spell_check = BALSA_SPELL_CHECK(object);

    switch (prop_id) {
    case PROP_LANGUAGE:
        balsa_spell_check_set_language(spell_check,
                                       g_value_get_string(value));
        break;

    default:
        break;
    }
}


static void
spch_get_property(GObject    *object,
                  guint       prop_id,
                  GValue     *value,
                  GParamSpec *pspec)
{
    BalsaSpellCheck *spell_check = BALSA_SPELL_CHECK(object);

    switch (prop_id) {
    case PROP_LANGUAGE:
        g_value_set_string(value, spell_check->language_tag);
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
balsa_spell_check_new(GtkWindow *parent)
{
    BalsaSpellCheck *spell_check;

    g_return_val_if_fail(GTK_IS_WINDOW(parent), NULL);

    spell_check = g_object_new(BALSA_TYPE_SPELL_CHECK,
                               "type", GTK_WINDOW_TOPLEVEL,
                               "transient-for", parent,
                               "destroy-with-parent", TRUE,
                               "title", _("Spell check"),
                               "border-width", HIG_PADDING,
                               NULL);

    return (GtkWidget *) spell_check;
}


/* balsa_spell_check_new_with_text
 *
 * Create a new spell check widget, assigning the GtkText to check.
 * */
GtkWidget *
balsa_spell_check_new_with_text(GtkWindow   *parent,
                                GtkTextView *check_text)
{
    BalsaSpellCheck *spell_check;

    g_return_val_if_fail(GTK_IS_WINDOW(parent), NULL);
    g_return_val_if_fail(GTK_IS_TEXT_VIEW(check_text), NULL);

    spell_check            = (BalsaSpellCheck *) balsa_spell_check_new(parent);
    spell_check->text_view = check_text;

    return (GtkWidget *) spell_check;
}


/* balsa_spell_check_set_text ()
 *
 * Set the text widget the spell check should check.
 * */
void
balsa_spell_check_set_text(BalsaSpellCheck *spell_check,
                           GtkTextView     *check_text)
{
    g_return_if_fail(BALSA_IS_SPELL_CHECK(spell_check));
    g_return_if_fail(GTK_IS_TEXT_VIEW(check_text));

    spell_check->text_view = check_text;
}


/* balsa_spell_check_set_language ()
 *
 * Set the language to do spell checking in.
 * */
void
balsa_spell_check_set_language(BalsaSpellCheck *spell_check,
                               const gchar     *language)
{
    g_return_if_fail(BALSA_IS_SPELL_CHECK(spell_check));

    g_free(spell_check->language_tag);
    spell_check->language_tag = g_strdup(language);
}


/* balsa_spell_check_init ()
 *
 * Initialization of the class to reasonable default values, set up
 * buttons signal callbacks.
 * */
static void
balsa_spell_check_init(BalsaSpellCheck *spell_check)
{
    GtkWidget *widget;
    GtkGrid *grid;
    GtkWidget *sw;
    GtkTreeView *tree_view;
    GtkListStore *store;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeSelection *selection;
    GtkWidget *box;

    /* Set spell checker */

    spell_check->original_text = NULL;
    spell_check->suggestions   = NULL;

    spell_check->language_tag = g_strdup("en");

    /* setup suggestion display */
    widget             = gtk_entry_new();
    spell_check->entry = GTK_ENTRY(widget);
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, HIG_PADDING);
    gtk_container_add(GTK_CONTAINER(spell_check), box);

    gtk_container_add(GTK_CONTAINER(box), widget);

    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(sw, TRUE);
    gtk_widget_set_valign(sw, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(box), sw);

    /* setup suggestion list */
    store  = gtk_list_store_new(1, G_TYPE_STRING);
    widget = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);

    gtk_container_add(GTK_CONTAINER(sw), widget);
    spell_check->tree_view = tree_view = GTK_TREE_VIEW(widget);

    renderer = gtk_cell_renderer_text_new();
    column   = gtk_tree_view_column_new_with_attributes(NULL, renderer,
                                                        "text", 0, NULL);
    gtk_tree_view_append_column(tree_view, column);

    selection = gtk_tree_view_get_selection(tree_view);
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_BROWSE);
    g_signal_connect(selection, "changed",
                     G_CALLBACK(select_word_cb), spell_check);

    gtk_tree_view_set_headers_visible(tree_view, FALSE);

    /* setup buttons to perform actions */
    widget = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(box), widget);

    grid = GTK_GRID(widget);
    gtk_grid_set_row_spacing(grid, HIG_PADDING);
    gtk_grid_set_column_spacing(grid, HIG_PADDING);

    widget = gtk_button_new_with_mnemonic(_("C_hange"));
    gtk_widget_set_tooltip_text(widget,
                                _("Replace the current word "
                                  "with the selected suggestion"));
    g_signal_connect(widget, "clicked",
                     G_CALLBACK(change_cb), spell_check);
    gtk_grid_attach(grid, widget, 0, 0, 1, 1);

    widget = gtk_button_new_with_mnemonic(_("Change _All"));
    gtk_widget_set_tooltip_text(widget,
                                _("Replace all occurrences of the current word "
                                  "with the selected suggestion"));
    g_signal_connect(widget, "clicked",
                     G_CALLBACK(change_all_cb), spell_check);
    gtk_grid_attach(grid, widget, 0, 1, 1, 1);

    widget = gtk_button_new_with_mnemonic(_("_Ignore"));
    gtk_widget_set_tooltip_text(widget,
                                _("Skip the current word"));
    g_signal_connect(widget, "clicked",
                     G_CALLBACK(ignore_cb), spell_check);
    gtk_grid_attach(grid, widget, 1, 0, 1, 1);

    widget = gtk_button_new_with_mnemonic(_("I_gnore All"));
    gtk_widget_set_tooltip_text(widget,
                                _("Skip all occurrences of the current word"));
    g_signal_connect(widget, "clicked",
                     G_CALLBACK(ignore_all_cb), spell_check);
    gtk_grid_attach(grid, widget, 1, 1, 1, 1);

    widget = gtk_button_new_with_mnemonic(_("_Learn"));
    gtk_widget_set_tooltip_text(widget,
                                _("Add the current word to your personal dictionary"));
    g_signal_connect(widget, "clicked",
                     G_CALLBACK(learn_cb), spell_check);
    gtk_grid_attach(grid, widget, 2, 0, 1, 1);

    widget = gtk_button_new_with_mnemonic(_("_Done"));
    gtk_widget_set_tooltip_text(widget, _("Finish spell checking"));
    g_signal_connect(widget, "clicked",
                     G_CALLBACK(done_cb), spell_check);
    gtk_grid_attach(grid, widget, 3, 0, 1, 1);

    widget = gtk_button_new_with_mnemonic(_("_Cancel"));
    gtk_widget_set_tooltip_text(widget,
                                _("Revert all changes and finish spell checking"));
    g_signal_connect(widget, "clicked",
                     G_CALLBACK(cancel_cb), spell_check);
    gtk_grid_attach(grid, widget, 3, 1, 1, 1);
}


/* select_word_cb ()
 *
 * When the user selects a word from the list of available
 * suggestions, replace the text in the entry text box with the text
 * from the clist selection.
 * */
static void
select_word_cb(GtkTreeSelection *selection,
               gpointer          data)
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
ignore_cb(GtkButton *button,
          gpointer   data)
{
    BalsaSpellCheck *spell_check;

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
ignore_all_cb(GtkButton *button,
              gpointer   data)
{
    /* add the current word to the session library */
    BalsaSpellCheck *spell_check;

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
change_cb(GtkButton *button,
          gpointer   data)
{
    BalsaSpellCheck *spell_check;

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
change_all_cb(GtkButton *button,
              gpointer   data)
{
    BalsaSpellCheck *spell_check;

    /* change all similarly misspelled words without asking */

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
learn_cb(GtkButton *button,
         gpointer   data)
{
    BalsaSpellCheck *spell_check;

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
cancel_cb(GtkButton *button,
          gpointer   data)
{
    BalsaSpellCheck *spell_check;

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
done_cb(GtkButton *button,
        gpointer   data)
{
    BalsaSpellCheck *spell_check;

    spell_check = BALSA_SPELL_CHECK(data);
    spch_restore_word_iters(spell_check);

    spch_finish(spell_check, TRUE);
}


/* balsa_spell_check_start ()
 *
 * Start the spell check, allocating the Enchant broker and
 * dictionary to do the checking.
 * */

static GRegex *quoted_rex = NULL;

void
balsa_spell_check_start(BalsaSpellCheck *spell_check)
{
    const gchar *enchant_error;
    GtkTextBuffer *buffer;
    GtkTextIter start, end, iter;
    GtkTextMark *insert;

    g_return_if_fail(BALSA_IS_SPELL_CHECK(spell_check));

    spell_check->broker = enchant_broker_init();

    enchant_error = enchant_broker_get_error(spell_check->broker);
    if (enchant_error) {
        /* quit without breaking things */
        GtkWindow *parent;

        parent = gtk_window_get_transient_for(GTK_WINDOW(spell_check));
        balsa_information_parented(parent,
                                   LIBBALSA_INFORMATION_ERROR,
                                   "%s",
                                   enchant_error);

        gtk_widget_destroy((GtkWidget *) spell_check);

        return;
    }

    spell_check->dict =
        enchant_broker_request_dict(spell_check->broker,
                                    spell_check->language_tag);

    buffer = gtk_text_view_get_buffer(spell_check->text_view);
    insert = gtk_text_buffer_get_insert(buffer);
    gtk_text_buffer_get_iter_at_mark(buffer, &start, insert);
    spell_check->original_offset = gtk_text_iter_get_offset(&start);
    spell_check->original_mark   =
        gtk_text_buffer_create_mark(buffer, NULL, &start, FALSE);

    /* Marks for saving iter locations. */
    spell_check->start_mark =
        gtk_text_buffer_create_mark(buffer, NULL, &start, TRUE);
    spell_check->end_mark =
        gtk_text_buffer_create_mark(buffer, NULL, &start, FALSE);

    /* Get the original text so we can always revert */
#if HAVE_GTKSOURCEVIEW
    spell_check->original_text =
        gtk_source_buffer_new(gtk_text_buffer_get_tag_table(buffer));
    gtk_text_buffer_get_start_iter((GtkTextBuffer *)
                                   spell_check->original_text, &iter);
    gtk_text_buffer_get_bounds((GtkTextBuffer *) buffer, &start, &end);
    gtk_text_buffer_insert_range((GtkTextBuffer *)
                                 spell_check->original_text, &iter,
                                 &start, &end);
#else                           /* HAVE_GTKSOURCEVIEW */
    spell_check->original_text =
        gtk_text_buffer_new(gtk_text_buffer_get_tag_table(buffer));
    gtk_text_buffer_get_start_iter(spell_check->original_text, &iter);
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gtk_text_buffer_insert_range(spell_check->original_text, &iter,
                                 &start, &end);
#endif                          /* HAVE_GTKSOURCEVIEW */

    g_debug("start");

    /*
     * compile the quoted-text regular expression (note:
     * balsa_app.quote_regex may change, so compile it new every
     * time!)
     */
    if (quoted_rex != NULL)
        g_regex_unref(quoted_rex);
    quoted_rex = balsa_quote_regex_new();

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
balsa_spell_check_next(BalsaSpellCheck *spell_check)
{
    GtkTextBuffer *buffer;
    GtkTreeView *tree_view;
    GtkTreeModel *model;
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

    tree_view = spell_check->tree_view;
    model     = gtk_tree_view_get_model(tree_view);

    /* found an incorrect spelling */
    if (gtk_tree_model_get_iter_first(model, &iter)) {
        GtkTreeSelection *selection;
        GtkTreePath *path;

        selection = gtk_tree_view_get_selection(tree_view);
        path      = gtk_tree_model_get_path(model, &iter);
        gtk_tree_selection_select_path(selection, path);
        gtk_tree_view_scroll_to_cell(tree_view, path, NULL, TRUE, 0.5, 0);
        gtk_tree_path_free(path);
    }

    /* Highlight current word by selecting it; first we'll move the
     * cursor to start of this word; we'll highlight it by moving
     * the selection-bound to its end, but we must do that in an idle
     * callback, otherwise the first word is never highlighted. */
    buffer = gtk_text_view_get_buffer(spell_check->text_view);
    gtk_text_buffer_place_cursor(buffer, &spell_check->start_iter);
    spell_check->highlight_idle_id =
        g_idle_add((GSourceFunc) highlight_idle, spell_check);

    /* scroll text window to show current word */
    gtk_text_view_scroll_to_mark(spell_check->text_view,
                                 gtk_text_buffer_get_insert(buffer),
                                 0, FALSE, 0, 0);

    spch_save_word_iters(spell_check);
    return FALSE;
}


/* Move the selection bound to the end of the current word, to highlight
 * it. */
static gboolean
highlight_idle(BalsaSpellCheck *spell_check)
{
    GtkTextBuffer *buffer;

    if (spell_check->highlight_idle_id) {
        spch_restore_word_iters(spell_check);
        buffer = gtk_text_view_get_buffer(spell_check->text_view);
        gtk_text_buffer_move_mark_by_name(buffer, "selection_bound",
                                          &spell_check->end_iter);
        spell_check->highlight_idle_id = 0;
    }
    return FALSE;
}


/* balsa_spell_check_learn ()
 *
 * Learn the current word, either to the personal or session
 * dictionaries.
 * */
static void
balsa_spell_check_learn(BalsaSpellCheck *spell_check,
                        LearnType        learn_type)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(spell_check->text_view);
    gchar *word;

    word =
        gtk_text_buffer_get_text(buffer, &spell_check->start_iter,
                                 &spell_check->end_iter, FALSE);

    g_debug("learn %s", word);

    if (learn_type == SESSION_DICT)
        enchant_dict_add_to_session(spell_check->dict, word, -1);
    else
        enchant_dict_add(spell_check->dict, word, -1);

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
balsa_spell_check_fix(BalsaSpellCheck *spell_check,
                      gboolean         fix_all)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(spell_check->text_view);
    gchar *new_word;
    gchar *old_word;

    old_word =
        gtk_text_buffer_get_text(buffer, &spell_check->start_iter,
                                 &spell_check->end_iter, FALSE);
    new_word = gtk_editable_get_chars(GTK_EDITABLE(spell_check->entry),
                                      0, -1);

    if (!*new_word) {
        /* no word to replace, ignore */
        g_free(new_word);
        g_free(old_word);
        return;
    }

    /* Some spelling modules can learn from user
     * replacement choices. */
    enchant_dict_store_replacement(spell_check->dict,
                                   old_word, -1, new_word, -1);

    if (check_error(spell_check)) {
        spch_finish(spell_check, TRUE);
        g_free(new_word);
        g_free(old_word);
        return;
    }

    g_debug("replace %s with %s", old_word, new_word);

    switch_word(spell_check, new_word);

    if (fix_all) {
        spch_save_word_iters(spell_check);
        while (next_word(spell_check)) {
            gchar *this_word;

            this_word =
                gtk_text_buffer_get_text(buffer, &spell_check->start_iter,
                                         &spell_check->end_iter, FALSE);
            if (g_ascii_strcasecmp(old_word, this_word) == 0)
                switch_word(spell_check, new_word);
            g_free(this_word);
        }
        spch_restore_word_iters(spell_check);
    }

    finish_check(spell_check);

    g_free(new_word);
    g_free(old_word);
}


/* balsa_spell_check_destroy ()
 *
 * Clean up variables if the widget is destroyed.
 * */
static void
balsa_spell_check_destroy(GObject *object)
{
    BalsaSpellCheck *spell_check;

    spell_check = BALSA_SPELL_CHECK(object);

    if (spell_check->suggestions)
        finish_check(spell_check);

    if (spell_check->broker)
        spch_finish(spell_check, FALSE);

    if (spell_check->highlight_idle_id) {
        g_source_remove(spell_check->highlight_idle_id);
        spell_check->highlight_idle_id = 0;
    }

    g_clear_pointer(&spell_check->language_tag, g_free);
    g_clear_pointer(&quoted_rex, g_regex_unref);

    if (G_OBJECT_CLASS(balsa_spell_check_parent_class)->dispose)
        (*G_OBJECT_CLASS(balsa_spell_check_parent_class)->
         dispose) (object);
}


/* spch_finish ()
 *
 * Clean up the variables from the spell check
 * */
static void
spch_finish(BalsaSpellCheck *spell_check,
            gboolean         keep_changes)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(spell_check->text_view);
    GtkTextIter original;


    finish_check(spell_check);

    if (keep_changes) {
        gtk_text_buffer_get_iter_at_mark(buffer, &original,
                                         spell_check->original_mark);
        gtk_text_buffer_delete_mark(buffer, spell_check->original_mark);
        gtk_text_buffer_delete_mark(buffer, spell_check->start_mark);
        gtk_text_buffer_delete_mark(buffer, spell_check->end_mark);
        g_object_unref(spell_check->original_text);
    } else {
        /* replace corrected text with original text */
#if HAVE_GTKSOURCEVIEW
        buffer = (GtkTextBuffer *) spell_check->original_text;
#else                           /* HAVE_GTKSOURCEVIEW */
        buffer = spell_check->original_text;
#endif                          /* HAVE_GTKSOURCEVIEW */
        gtk_text_view_set_buffer(spell_check->text_view, buffer);
        gtk_text_buffer_get_iter_at_offset(buffer, &original,
                                           spell_check->original_offset);
    }
    gtk_text_buffer_place_cursor(buffer, &original);

    spell_check->original_text = NULL;

    check_error(spell_check);

    if (spell_check->broker) {
        if (spell_check->dict) {
            enchant_broker_free_dict(spell_check->broker,
                                     spell_check->dict);
            spell_check->dict = NULL;
        }
        enchant_broker_free(spell_check->broker);
        spell_check->broker = NULL;
    }

    g_debug("finished");

    gtk_widget_destroy((GtkWidget *) spell_check);
}


/* setup_suggestions ()
 *
 * Retrieves the suggestions for the word that is currently being
 * checked, and place them in the word list.
 * */
static void
setup_suggestions(BalsaSpellCheck *spell_check,
                  gsize            n_suggs)
{
    GtkTreeModel *model;
    GtkListStore *store;
    guint i;

    model = gtk_tree_view_get_model(spell_check->tree_view);
    store = GTK_LIST_STORE(model);

    for (i = 0; i < n_suggs; i++) {
        GtkTreeIter iter;

        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, spell_check->suggestions[i],
                           -1);
    }
}


/* check_word ()
 *
 * Check the current word of the BalsaSpellCheck object.  If
 * incorrect, fill the tree_view and entrybox with the suggestions
 * obtained from enchant, and return a false value.  Otherwise return
 * true.
 * */
static gboolean
check_word(BalsaSpellCheck *spell_check)
{
    gboolean correct;
    GtkTextBuffer *buffer;
    gchar *word;


    buffer = gtk_text_view_get_buffer(spell_check->text_view);
    word   = gtk_text_buffer_get_text(buffer,
                                      &spell_check->start_iter,
                                      &spell_check->end_iter, FALSE);

    if (word) {
        gint enchant_check;

        g_debug("check %s…", word);

        enchant_check = enchant_dict_check(spell_check->dict, word, -1);

        if (enchant_check < 0) {
            check_error(spell_check);
            return TRUE;
        }
        correct = !enchant_check;
    } else {
        return TRUE;
    }

    if (!correct) {
        gsize n_suggs;

        g_debug(" …incorrect.");

        spell_check->suggestions =
            enchant_dict_suggest(spell_check->dict, word, -1, &n_suggs);
        setup_suggestions(spell_check, n_suggs);
    } else {
    	g_debug(" …correct.");
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
finish_check(BalsaSpellCheck *spell_check)
{
    GtkTreeModel *model = gtk_tree_view_get_model(spell_check->tree_view);

    /* get rid of the suggestions */
    gtk_list_store_clear(GTK_LIST_STORE(model));

    gtk_entry_set_text(spell_check->entry, "");

    enchant_dict_free_string_list(spell_check->dict,
                                  spell_check->suggestions);
    spell_check->suggestions = NULL;
}


/* check_error ()
 *
 * To be called after trying things with the enchant broker, if there
 * were any errors with the operation it will generate an error
 * message and return true.
 * */
static gboolean
check_error(BalsaSpellCheck *spell_check)
{
    const gchar *enchant_error;

    enchant_error = enchant_broker_get_error(spell_check->broker);
    if (enchant_error) {
        balsa_information(LIBBALSA_INFORMATION_WARNING,
                          _("BalsaSpellCheck: Enchant Error: %s\n"),
                          enchant_error);
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
next_word(BalsaSpellCheck *spell_check)
{
    GtkTextBuffer *buffer;
    GtkTextIter line_start, line_end;
    gchar *line = NULL;
    gboolean skip_sig, skip_quoted = FALSE;

    buffer = gtk_text_view_get_buffer(spell_check->text_view);

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
            skip_quoted = (!balsa_app.check_quoted && quoted_rex != NULL
                           && is_a_quote(line, quoted_rex) > 0);
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
 * Replace the current word with the new_word.
 * */
static void
switch_word(BalsaSpellCheck *spell_check,
            const gchar     *new_word)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(spell_check->text_view);

    /* remove and replace the current word. */
    spch_save_word_iters(spell_check);
    gtk_text_buffer_delete(buffer, &spell_check->start_iter,
                           &spell_check->end_iter);
    gtk_text_buffer_insert(buffer, &spell_check->end_iter, new_word, -1);
    spch_restore_word_iters(spell_check);
}


static void
spch_save_word_iters(BalsaSpellCheck *spell_check)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(spell_check->text_view);

    gtk_text_buffer_move_mark(buffer, spell_check->start_mark,
                              &spell_check->start_iter);
    gtk_text_buffer_move_mark(buffer, spell_check->end_mark,
                              &spell_check->end_iter);
}


static void
spch_restore_word_iters(BalsaSpellCheck *spell_check)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(spell_check->text_view);

    gtk_text_buffer_get_iter_at_mark(buffer, &spell_check->start_iter,
                                     spell_check->start_mark);
    gtk_text_buffer_get_iter_at_mark(buffer, &spell_check->end_iter,
                                     spell_check->end_mark);
}
