/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2003 Stuart Parmenter and others,
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
 * This file includes two message threading functions.
 * The first is the implementation of jwz's algorithm describled at
 * http://www.jwz.org/doc/threading.html . The another is very simple and 
 * trivial one. If you confirm that your mailbox includes every threaded 
 * messages, the later will be enough. Those functions are selectable on
 * each mailbox by setting the 'type' member in BalsaIndex. If you don't need
 * message threading functionality, just specify 'LB_MAILBOX_THREADING_FLAT'. 
 *
 * ymnk@jcraft.com
 *
 * This version is based on a BalsaIndex that subclasses GtkTreeView,
 * and differs in some ways from the original GtkCTree code.
 * GtkTreeRowReferences are used to keep track of rows (messages) as
 * threads are constructed. However, moving a row or subtree isn't
 * supported, so we have to do it the hard way: copy the rows one at a
 * time to the new location, then delete the old copy. Of course, the
 * GtkTreeRowReferences to the rows are lost, so we have to update them
 * ourselves.
 *
 * To cope with all this, the message is used as the key, and when we
 * need to access the row, the row reference is looked up and converted
 * to a path. The hash table is passed to balsa_index_move_subtree,
 * which updates it.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <gnome.h>
#include <glib.h>
#include <ctype.h>
#include "balsa-app.h"
#include "balsa-index.h"
#include "main-window.h"

struct ThreadingInfo {
    BalsaIndex *index;
    GNode *root;
    GHashTable *id_table;
    GSList *message_list;
};

static void threading_jwz(BalsaIndex* index);
static gboolean gen_container(GtkTreeModel * model, GtkTreePath * path,
                              GtkTreeIter * iter, gpointer data);
static GNode *get_container(LibBalsaMessage * message,
                            GHashTable * id_table);
static GNode *check_references(LibBalsaMessage * message,
                               struct ThreadingInfo * ti);
static gboolean prune(GNode * node, GNode * root);
static gboolean construct(GNode * node, struct ThreadingInfo * ti);
static void subject_gather(GNode * node, GHashTable * subject_table);
static void subject_merge(GNode * node, GHashTable * subject_table);
static const gchar* chop_re(const gchar* str);

static void threading_simple(BalsaIndex * index,
                             LibBalsaMailboxThreadingType th_type);
static gboolean add_message(GtkTreeModel * model, GtkTreePath * path,
                            GtkTreeIter * iter, gpointer data);

void
balsa_index_threading(BalsaIndex* index, LibBalsaMailboxThreadingType th_type)
{
    if (th_type == LB_MAILBOX_THREADING_JWZ)
        threading_jwz(index);
    else
        threading_simple(index, th_type);
}

static void
threading_jwz(BalsaIndex * index)
{
    struct ThreadingInfo ti;
    GHashTable *subject_table;

    ti.index = index;
    ti.id_table = g_hash_table_new(g_str_hash, g_str_equal);
    ti.root = g_node_new(NULL);
    gtk_tree_model_foreach(gtk_tree_view_get_model(GTK_TREE_VIEW(index)),
                           gen_container, &ti);

    g_node_traverse(ti.root, G_POST_ORDER, G_TRAVERSE_ALL, -1,
                    (GNodeTraverseFunc) prune, ti.root);

    subject_table = g_hash_table_new(g_str_hash, g_str_equal);
    g_node_children_foreach(ti.root, G_TRAVERSE_ALL,
                            (GNodeForeachFunc) subject_gather,
                            subject_table);
    g_node_children_foreach(ti.root, G_TRAVERSE_ALL,
                            (GNodeForeachFunc) subject_merge,
                            subject_table);

    balsa_window_setup_progress(BALSA_WINDOW(index->window),
                                g_node_n_children(ti.root));
    g_node_traverse(ti.root, G_PRE_ORDER, G_TRAVERSE_ALL, -1,
                    (GNodeTraverseFunc) construct, &ti);
    balsa_window_clear_progress(BALSA_WINDOW(index->window));

    g_hash_table_destroy(subject_table);
    g_hash_table_destroy(ti.id_table);
    g_node_destroy(ti.root);
}

static gboolean
gen_container(GtkTreeModel * model, GtkTreePath * path,
              GtkTreeIter * iter, gpointer data)
{
    struct ThreadingInfo *ti = data;
    GHashTable *ref_table;
    LibBalsaMessage *message;
    GNode *container;

    /* Make sure the index's ref table has an entry for this message. */
    ref_table = ti->index->ref_table;
    gtk_tree_model_get(model, iter, BNDX_MESSAGE_COLUMN, &message, -1);
    if (!g_hash_table_lookup(ref_table, message))
        g_hash_table_insert(ref_table, message,
                            gtk_tree_row_reference_new(model, path));

    container = get_container(message, ti->id_table);
    if (container) {
    /*
     * Set the parent of this message to be the last element in References.
     * Note that this message may have a parent already: this can happen
     * because we saw this ID in a References field, and presumed a
     * parent based on the other entries in that field. Now that we have
     * the actual message, we can be more definitive, so throw away the
     * old parent and use this new one. Find this Container in the
     * parent's children list, and unlink it.
     *
     * Note that this could cause this message to now have no parent, if
     * it has no references field, but some message referred to it as the
     * non-first element of its references. (Which would have been some
     * kind of lie...)
     *
     * Note that at all times, the various ``parent'' and ``child''
     * fields must be kept inter-consistent.
     */
        /* In this implementation, check_references always returns a
         * parent; if the message has no parent, it's the root of the
         * whole mailbox tree. */
        GNode* parent = check_references(message, ti);
        g_node_unlink(container);
        g_node_prepend(parent, container);
    }

    return FALSE;
}

static GNode *
check_references(LibBalsaMessage * message, struct ThreadingInfo * ti)
{
    /*
     * For each element in the message's References field:
     *   + Find a Container object for the given Message-ID:
     *     + If there's one in id_table use that;
     *     + Otherwise, make (and index) one with a null Message.
     *   + Link the References field's Containers together in the order
     *     implied by the References header.
     *     + If they are already linked, don't change the existing links.
     *     + Do not add a link if adding that link would introduce a loop:
     *       that is, before asserting A->B, search down the children of B
     *       to see if A is reachable.
     */

    /* The root of the mailbox tree is the default parent. */
    GNode *parent = ti->root;
    GList *reference;

    for (reference = message->references_for_threading; reference;
         reference = g_list_next(reference)) {
        gchar *id = reference->data;
        GNode *foo = g_hash_table_lookup(ti->id_table, id);

        if (foo == NULL) {
            foo = g_node_new(NULL);
            g_hash_table_insert(ti->id_table, id, foo);
        }

        /* Avoid nasty surprises. */
        if (foo != parent && !g_node_is_ancestor(foo, parent)) {
            if (foo->parent == ti->root)
                /* foo has the default parent; we'll unlink it, so that
                 * it can be linked to its rightful parent. */
                g_node_unlink(foo);
            if (G_NODE_IS_ROOT(foo))
                g_node_prepend(parent, foo);
        }

        parent = foo;
    }
    return parent;
}

static GNode *
get_container(LibBalsaMessage * message, GHashTable * id_table)
{
    /*
     * If id_table contains an *empty* Container for this ID:
     *   + Store this message in the Container's message slot.
     * else
     *   + Create a new Container object holding this message;
     *   + Index the Container by Message-ID in id_table.
     */

    GNode *container;

    if (!message->message_id)
        return NULL;

    container = g_hash_table_lookup(id_table, message->message_id);
    if (container && !container->data)
        container->data = message;
    else {
        container = g_node_new(message);
        g_hash_table_insert(id_table, message->message_id, container);
    }

    return container;
}

/* helper (why doesn't g_node_unlink return the node?!) */
static GNode *
bndt_unlink(GNode * node)
{
    g_node_unlink(node);
    return node;
}

static gboolean
prune(GNode * node, GNode * root)
{
    /*
     * Recursively walk all containers under the root set. For each container: 
     *
     * + If it is an empty container with no children, nuke it. 
     * + If the Container has no Message, but does have children, 
     *   remove this container but promote its children to this level 
     *  (that is, splice them in to the current child list.) 
     *
     * Do not promote the children if doing so would promote them to 
     * the root set -- unless there is only one child, in which case, do. 
     */

    if (node->data != NULL || node == root)
        return FALSE;

    if (node->children != NULL
        && (node->parent != root || node->children->next == NULL))
        while (node->children)
            g_node_prepend(node->parent, bndt_unlink(node->children));

    if (node->children == NULL)
        g_node_destroy(node);

    return FALSE;
}

/*
 * check_parent: find the paths to the messages, and if the parent isn't
 * the immediate parent of the child, move the child
 *
 * parent == NULL means the child should be at the top level
 */
static void
check_parent(struct ThreadingInfo *ti, LibBalsaMessage * parent,
             LibBalsaMessage * child)
{
    GHashTable *ref_table;
    GtkTreeRowReference *child_ref;
    GtkTreePath *parent_path;
    GtkTreePath *child_path;
    GtkTreePath *test;

    ref_table = ti->index->ref_table;
    child_ref = g_hash_table_lookup(ref_table, child);
    child_path = gtk_tree_row_reference_get_path(child_ref);

    if (parent) {
        GtkTreeRowReference *parent_ref =
            g_hash_table_lookup(ref_table, parent);
        parent_path = gtk_tree_row_reference_get_path(parent_ref);
    } else
        parent_path = NULL;

    test = gtk_tree_path_copy(child_path);

    if ((!parent_path && gtk_tree_path_get_depth(test) > 1)
        || (parent_path && (!gtk_tree_path_up(test)
                            || gtk_tree_path_get_depth(test) <= 0
                            || gtk_tree_path_compare(test, parent_path))))
        balsa_index_move_subtree(ti->index, child_path, parent_path);

    gtk_tree_path_free(test);
    gtk_tree_path_free(child_path);
    if (parent_path)
        gtk_tree_path_free(parent_path);
}

static gboolean
construct(GNode * node, struct ThreadingInfo *ti)
{
    if (node->data)
        check_parent(ti, node->parent ? node->parent->data : NULL,
                     node->data);

    balsa_window_increment_progress(BALSA_WINDOW(ti->index->window));
    return FALSE;
}

static void
subject_gather(GNode * node, GHashTable * subject_table)
{
    LibBalsaMessage *message, *old_message;
    const gchar *subject=NULL, *old_subject;
    const gchar *chopped_subject=NULL;
    GNode* old;

    /*
     * If any two members of the root set have the same subject, merge them. 
     * This is so that messages which don't have References headers at all 
     * still get threaded (to the extent possible, at least.) 
     *
     * + Construct a new hash table, subject_table, which associates subject 
     *   strings with Container objects. 
     *
     * + For each Container in the root set: 
     *
     *   Find the subject of that sub-tree: 
     *   + If there is a message in the Container, the subject is the subject of
     *     that message. 
     *   + If there is no message in the Container, then the Container will have
     *     at least one child Container, and that Container will have a message.
     *     Use the subject of that message instead. 
     *   + Strip ``Re:'', ``RE:'', ``RE[5]:'', ``Re: Re[4]: Re:'' and so on. 
     *   + If the subject is now "", give up on this 
     *   + Add this Container to the subject_table if: Container. 
     *     + There is no container in the table with this subject, or 
     *     + This one is an empty container and the old one is not: the empty 
     *       one is more interesting as a root, so put it in the table instead. 
     *     + The container in the table has a ``Re:'' version of this subject,
     *       and this container has a non-``Re:'' version of this subject.
     *       The non-re version is the more interesting of the two. 
     *
     * + Now the subject_table is populated with one entry for each subject 
     *   which occurs in the root set. Now iterate over the root set, 
     *   and gather together the difference. 
     * 
     *   For each Container in the root set: 
     * 
     *   Find the subject of this Container (as above.) 
     *   Look up the Container of that subject in the table. 
     *   If it is null, or if it is this container, continue. 
     *   Otherwise, we want to group together this Container and the one 
     *   in the table. There are a few possibilities: 
     *     + If both are dummies, append one's children to the other, and 
     *       remove the now-empty container. 
     * 
     *     + If one container is a empty and the other is not, make the 
     *       non-empty one be a child of the empty, and a sibling of the 
     *       other ``real'' messages with the
     *       same subject (the empty's children.) 
     *     + If that container is a non-empty, and that message's subject 
     *       does not begin with ``Re:'', but this message's subject does,
     *       then make this be a child of the other. 
     *     + If that container is a non-empty, and that message's subject 
     *       begins with ``Re:'', but this message's subject does not, 
     *       then make that be a child of this one -- they were misordered. 
     *       (This happens somewhat implicitly, since if there are two 
     *       messages, one with Re: and one without, the one without
     *       will be in the hash table, regardless of the order in which 
     *       they were seen.) 
     * 
     *     + Otherwise, make a new empty container and make both msgs be
     *       a child of it. This catches the both-are-replies and 
     *       neither-are-replies cases, and makes them be siblings instead of
     *       asserting a hierarchical relationship which might not be true. 
     * 
     *     (People who reply to messages without using ``Re:'' and without
     *     using a References line will break this slightly. Those people suck.) 
     * 
     *     (It has occurred to me that taking the date or message number into
     *     account would be one way of resolving some of the ambiguous cases,
     *     but that's not altogether straightforward either.) 
     */

    message = node->data ? node->data : node->children->data;

    g_return_if_fail(message!=NULL);

    subject=LIBBALSA_MESSAGE_GET_SUBJECT(message);
    if(subject==NULL)return;
    chopped_subject=chop_re(subject);
    if(chopped_subject==NULL) {
	return;
    }

    /*
      printf("subject_gather: subject %s, %s\n", subject, chopped_subject);
    */

    old=g_hash_table_lookup(subject_table, chopped_subject);
    if(old==NULL ||
       (node->data==NULL&&old->data!=NULL)) {
	g_hash_table_insert(subject_table, (char*)chopped_subject, node);
	return;
    }

    old_message = old->data ? old->data : old->children->data;
    g_return_if_fail(old_message!=NULL);
    old_subject = LIBBALSA_MESSAGE_GET_SUBJECT(old_message);

    if( old_subject != chop_re(old_subject) && subject==chopped_subject)
        g_hash_table_insert(subject_table, (gchar*)chopped_subject, node);
}

static void
subject_merge(GNode * node, GHashTable * subject_table)
{
    LibBalsaMessage *message, *message2;
    const gchar *subject, *subject2;
    const gchar *chopped_subject, *chopped_subject2;
    GNode *node2;

    message = node->data ? node->data : node->children->data;

    g_return_if_fail(message != NULL);

    subject = LIBBALSA_MESSAGE_GET_SUBJECT(message);
    if (subject == NULL)
        return;
    chopped_subject = chop_re(subject);
    if (chopped_subject == NULL)
        return;

    node2 = g_hash_table_lookup(subject_table, chopped_subject);
    if (node2 == NULL || node2 == node)
        return;

    if (node2->data == NULL) {
        while (node->children)
            g_node_prepend(node2, bndt_unlink(node->children));
        return;
    }

    if (node->data == NULL) {
        while (node2->children)
            g_node_prepend(node, bndt_unlink(node2->children));
        return;
    }

    message2 = node2->data;
    subject2 = LIBBALSA_MESSAGE_GET_SUBJECT(message2);
    chopped_subject2 = chop_re(subject2);

    if ((subject2 == chopped_subject2) && subject != chopped_subject)
        g_node_prepend(node2, bndt_unlink(node));
    else if ((subject2 != chopped_subject2)
               && subject == chopped_subject) {
        g_node_prepend(node, bndt_unlink(node2));
        g_hash_table_insert(subject_table, (char *) chopped_subject, node);
    } else {
        GNode *new_node = g_node_new(NULL);

        g_node_prepend(node->parent, new_node);
        g_node_prepend(new_node, bndt_unlink(node));
        g_node_prepend(new_node, bndt_unlink(node2));
    }
}

/* The more heuristics should be added. */
static const gchar *
chop_re(const gchar * str)
{
    const gchar *p = str;
    while (*p) {
        while (*p && isspace((int) *p))
            p++;
        if (!*p)
            break;

        if (g_ascii_strncasecmp(p, "re:", 3) == 0
            || g_ascii_strncasecmp(p, "aw:", 3) == 0) {
            p += 3;
            continue;
        } else if (g_ascii_strncasecmp(p, _("Re:"), strlen(_("Re:"))) == 0) {
            /* should "re" be localized ? */
            p += strlen(_("Re:"));
            continue;
        } else
            if (g_ascii_strncasecmp
                (p, balsa_app.current_ident->reply_string,
                 strlen(balsa_app.current_ident->reply_string)) == 0) {
            p += strlen(balsa_app.current_ident->reply_string);
            continue;
        }
        break;
    }
    return p;
}

/* yet another message threading function */

static void
threading_simple(BalsaIndex * index, LibBalsaMailboxThreadingType type)
{
    GtkTreeModel *model;
    GSList *p;
    struct ThreadingInfo ti;
    gint length = 0;

    ti.index = index;
    ti.id_table = g_hash_table_new(g_str_hash, g_str_equal);
    ti.message_list = NULL;

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(index));
    gtk_tree_model_foreach(model, add_message, &ti);

    balsa_window_setup_progress(BALSA_WINDOW(index->window), length);
    for (p = ti.message_list; p; p = g_slist_next(p)) {
        LibBalsaMessage *message = p->data;
        LibBalsaMessage *parent = NULL;

        if (type == LB_MAILBOX_THREADING_SIMPLE
            && message->references_for_threading != NULL)
            parent =
                g_hash_table_lookup(ti.id_table,
                                    g_list_last(message->
                                                references_for_threading)->
                                    data);
        check_parent(&ti, parent, message);
        balsa_window_increment_progress(BALSA_WINDOW(index->window));
    }
    balsa_window_clear_progress(BALSA_WINDOW(index->window));
    g_slist_free(ti.message_list);
    g_hash_table_destroy(ti.id_table);
}

static gboolean
add_message(GtkTreeModel * model, GtkTreePath * path, GtkTreeIter * iter,
            gpointer data)
{
    struct ThreadingInfo *ti = data;
    GHashTable *ref_table;
    LibBalsaMessage *message = NULL;

    ref_table = ti->index->ref_table;
    gtk_tree_model_get(model, iter, BNDX_MESSAGE_COLUMN, &message, -1);
    if (!g_hash_table_lookup(ref_table, message))
        g_hash_table_insert(ref_table, message,
                            gtk_tree_row_reference_new(model, path));
    if (message->message_id
        && !g_hash_table_lookup(ti->id_table, message->message_id))
        g_hash_table_insert(ti->id_table, message->message_id, message);
    ti->message_list = g_slist_prepend(ti->message_list, message);

    return FALSE;
}
