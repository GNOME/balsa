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

/*
 * This file includes two message threading functions.
 * The first is the implementation of jwz's algorithm describled at
 * http://www.jwz.org/doc/threading.html . The another is very simple and 
 * trivial one. If you confirm that your mailbox includes every threaded 
 * messages, the later will be enough. Those functions are selectable on
 * each mailbox by setting the 'type' member in BalsaIndex. If you don't need
 * message threading functionality, just specify 'BALSA_INDEX_THREADING_FLAT'. 
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
#include "balsa-index-threading.h"
#include "main-window.h"

struct ThreadingInfo {
    GHashTable *subject_table;
    GHashTable *id_table;
    GHashTable *ref_table;
    GtkTreeModel *model;
};

static void threading_jwz(BalsaIndex* index);
static gboolean is_cyclic_tree(GNode* root, GNode* child);
static gboolean gen_container(GtkTreeModel * model, GtkTreePath * path,
                              GtkTreeIter * iter, gpointer data);
static GNode *get_container(LibBalsaMessage * message,
                            GHashTable * id_table);
static GNode* check_references(LibBalsaMessage* message,
			       GHashTable *id_table);
static void adopt_child(GNode* parent, GNode* container);
static void find_root_set(gpointer key, GNode* value, GSList ** root_set);
static gboolean prune(GNode *node, GSList *root_set);
static gboolean node_equal(GNode *node1, GNode **node2);
static gboolean construct(GNode * node, struct ThreadingInfo * ti);
static void subject_gather(GNode * node, struct ThreadingInfo * ti);
static void subject_merge(GNode * node, GSList * root_set,
                          GSList ** save_node, GHashTable * subject_table);
static void reparent(GNode* node, GNode* children);
static void free_node(GNode* value);
static const gchar* chop_re(const gchar* str);

static void threading_simple(BalsaIndex* index);
static gboolean add_message(GtkTreeModel * model, GtkTreePath * path,
                            GtkTreeIter * iter, gpointer data);

void
balsa_index_threading(BalsaIndex* index)
{
    switch (index->threading_type){
    case BALSA_INDEX_THREADING_SIMPLE:
	threading_simple(index);
	break;
    case BALSA_INDEX_THREADING_JWZ:
	threading_jwz(index);
	break;
    case BALSA_INDEX_THREADING_FLAT:
	break;
    default:
	break;
    }
}

static void
threading_jwz(BalsaIndex* index)
{
    GSList *root_set=NULL;
    GSList *save_node=NULL;
    GSList *foo=NULL;
    struct ThreadingInfo ti;

    ti.id_table =
        g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
                              (GDestroyNotify) free_node);
    ti.ref_table =
        g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL,
                              (GDestroyNotify) gtk_tree_row_reference_free);
    ti.model = gtk_tree_view_get_model(GTK_TREE_VIEW(index));
    gtk_tree_model_foreach(ti.model, gen_container, &ti);

    g_hash_table_foreach(ti.id_table, 
			 (GHFunc)find_root_set,
			 &root_set);

    balsa_window_setup_progress(BALSA_WINDOW(index->window), 
                                g_slist_length(root_set));
    
    for(foo=root_set; foo; foo=g_slist_next(foo)) {
	g_node_traverse(foo->data,
			/*G_PRE_ORDER,*/
			G_POST_ORDER,
			G_TRAVERSE_ALL,
			-1,
			(GNodeTraverseFunc)prune,
			root_set);
    }

    ti.subject_table = g_hash_table_new(g_str_hash, g_str_equal);
    g_slist_foreach(root_set, (GFunc)subject_gather, &ti);

    for (foo = root_set; foo; foo = g_slist_next(foo)) {
        if (foo->data != NULL)
            subject_merge(foo->data, root_set, &save_node,
                          ti.subject_table);
    }

    for(foo = root_set; foo; foo=g_slist_next(foo)) {
	if(foo->data!=NULL)
	    g_node_traverse(foo->data,
			    G_PRE_ORDER,   
			    G_TRAVERSE_ALL,
			    -1,
			    (GNodeTraverseFunc)construct,
			    &ti);
        balsa_window_increment_progress(BALSA_WINDOW(index->window));
    }

    for(foo=save_node; foo; foo=g_slist_next(foo)) {
	if(foo->data!=NULL) {
	    ((GNode*)(foo->data))->children=NULL;
	    g_node_destroy((GNode*)(foo->data));
	}
    }
    if(save_node!=NULL)
	g_slist_free(save_node);

    balsa_window_clear_progress(BALSA_WINDOW(index->window));
    g_hash_table_destroy(ti.subject_table);
    g_hash_table_destroy(ti.ref_table);
    g_hash_table_destroy(ti.id_table);

    if(root_set!=NULL)
	g_slist_free(root_set);
}

static gboolean
gen_container(GtkTreeModel * model, GtkTreePath * path,
              GtkTreeIter * iter, gpointer data)
{
    GtkTreeRowReference *reference =
        gtk_tree_row_reference_new(model, path);
    struct ThreadingInfo *ti = data;
    LibBalsaMessage *message = NULL;
    GNode *container;

    gtk_tree_model_get(model, iter, BNDX_MESSAGE_COLUMN, &message, -1);
    g_hash_table_insert(ti->ref_table, message, reference);

    container = get_container(message, ti->id_table);
    if(container) {
        GNode* parent = check_references(message, ti->id_table);
        if(parent)
            adopt_child(parent, container);
    }

    return FALSE;
}

static GNode*
check_references(LibBalsaMessage* message, GHashTable *id_table)
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

    GNode* parent=NULL;
    GList *reference=message->references_for_threading;

    while(reference) {
	char *id=(char *)(reference->data);
	GNode* foo=g_hash_table_lookup(id_table, id);
	if(foo==NULL) {
	    foo=g_node_new(NULL);
	    g_hash_table_insert(id_table, id, foo);
	}

	if(foo==parent) {
	    /* printf("duplicate!!\n"); */
	    reference=g_list_next(reference);
	    continue;
	}

	if(parent==NULL) {
	    parent=foo;
	    reference=g_list_next(reference);
	    continue;
	}

	if(is_cyclic_tree(foo, parent)) {
	    /* hmm.... */
	    /*printf("cyclic_tree 1#\n");*/
	    return NULL;
	}

	/*
	  printf("parent->children=%x, foo->parent=%x\n",
	  (int)(parent->children), (int)(foo->parent));
	*/

	if(parent->children==NULL && foo->parent==NULL) {
	    parent->children=foo;
	    foo->parent=parent;
	}
	else {
	    if(foo->parent!=NULL) {
		if(parent->children!=NULL) {
		    if(foo->parent==parent) {
			parent=foo;
			reference=g_list_next(reference);
			continue;
		    }

#if 1
		    /* This part is not defined in jwz's algorithm. */
		    parent=foo;
		    reference=g_list_next(reference);
		    continue;
#else
		    return NULL;
#endif
		}

		if(foo->children!=NULL) {  
		    if(g_list_next(reference)!=NULL &&
		       g_list_next(reference)->data!=NULL) {
			GNode *bar=g_hash_table_lookup(id_table,
						       g_list_next(reference)->data);
			if(bar==foo->children) {
			    parent=foo;
			    reference=g_list_next(reference);
			    continue;
			}
		    }
		}
		return NULL;
	    }

	    if(parent->children!=NULL) {
#if 1                                     
		/* This part is not defined in jwz's algorithm. */
		GNode* p; 
		for(p=parent->children; p->next; p=p->next) {
		    if(p==foo) break;
		}
		if(p!=foo) {            /* foo is not a children of parent. */
		    p->next=foo;
		    foo->prev=p;
		}
		           /* foo has siblings. */
		for(p=foo; p; p=p->next) {
		    p->parent=parent;
		}
		parent=foo;
		reference=g_list_next(reference);
		continue;
	    }
#else
	    /* printf("return 2#\n"); */
	    return;
#endif
	}
	parent=foo;
	reference=g_list_next(reference);
    }
    return parent;
}

static void
adopt_child(GNode* parent, GNode* container)
{

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

    if(container->parent==parent) return;

    if(is_cyclic_tree(container, parent)) {
	/*printf("cyclic 2#\n");*/
	return;
    }

    if(container->parent!=NULL) {
	GNode* foo=container->parent->children;
	if(foo==container) {
	    container->parent->children=container->next;
	    if(container->next!=NULL) {
		container->next->prev=NULL;
	    }
	}
	else{
	    container->prev->next=container->next;
	    if(container->next!=NULL) container->next->prev=container->prev;
	}
    }

    /* If parent had children already, told the first one that now it is
       the second one by linking it to container that is now the first one */
    if(parent->children!=NULL)
	parent->children->prev=container;

    /* Now container is the first child so container->prev must be NULL
       and we link it to the existing children of parent by setting
       container->next to parent->children */
    container->prev=NULL;
    container->next=parent->children;

    /* Actually set container as the first child, and parent as its parent */
    parent->children=container;
    container->parent=parent;
}

static gboolean
is_cyclic_tree(GNode* root, GNode* child)
{
    gboolean find=FALSE;
    GNode* foo=NULL;
#if 0
    /* This case will be enough for jwz's rule */
    
    for(foo=root; foo; foo=foo->children) {
	if(foo->children==child) {find=TRUE; break;}
    }
#else
    foo=child;
    g_node_traverse(root,
		    G_PRE_ORDER,   
		    G_TRAVERSE_ALL,
		    -1,
		    (GNodeTraverseFunc)node_equal,
		    &foo);
    if(foo==NULL) find=TRUE;
#endif
    return find;
}

static GNode*
get_container(LibBalsaMessage * message, GHashTable* id_table)
{
    GNode* container=NULL;

    /*
     * If id_table contains a Container for this ID:
     *   + Store this message in the Container's message slot.
     * else
     *   + Create a new Container object holding this message;
     *   + Index the Container by Message-ID in id_table.
     */
    
    if(!message->message_id) {
	return NULL;
    }

    container=g_hash_table_lookup(id_table, message->message_id);
    if(container!=NULL) {
	container->data = message;
    }
    else{
	container=g_node_new(message);
	g_hash_table_insert(id_table, message->message_id, container);
    }
    return container;
}

static void
find_root_set(gpointer key, GNode* value, GSList ** root_set)
{
    /*
     * Walk over the elements of id_table, and gather a list of the 
     * Container objects that have no parents. 
     */
    if(value->parent==NULL)
        *root_set=g_slist_append(*root_set, value);
}

static gboolean 
prune(GNode *node, GSList *root_set)
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

    if(node->data==NULL && node->children==NULL) {
	/*
	  printf("node! %x, %x\n", (int)node, (int)(node->parent));
	*/
	if(node->parent!=NULL) {
	    if(node->prev==NULL) {
		node->parent->children=node->next;
		if(node->next!=NULL)
		    node->next->prev=NULL;
	    }
	    else {
		node->prev->next=node->next;
		if(node->next!=NULL)
		    node->next->prev=node->prev;
	    }
	}
	else {
	    while(root_set) {
		if(root_set->data==node) {
		    root_set->data=NULL;
		    break;
		}
		root_set=g_slist_next(root_set);
	    }
	}
	return FALSE;
    }

    if(node->children!=NULL && node->data==NULL &&
       (node->parent!=NULL || node->children->next==NULL)) {
	if(node->parent==NULL) {
	    while(root_set) {
		if(root_set->data==node) {
                    root_set->data=node->children; 
                    break;
                }
		root_set=g_slist_next(root_set);
	    }
	    node->children->parent=NULL;
	}
	else {
	    GNode* foo; 
	    if(node->prev==NULL) {
		node->parent->children=node->children;
	    }
	    else {
		node->prev->next=node->children;
		node->children->prev=node->prev;
	    }

	    for(foo=node->children; foo->next!=NULL; foo=foo->next) {
		foo->parent=node->parent;
	    }

	    /* Reparent the last one also */
	    foo->parent=node->parent;

	    foo->next=node->next;
	    if(node->next!=NULL) 
		node->next->prev=foo;
	    {
		GNode* bar;
		for(bar=node->parent->children; bar; bar=bar->next) {
		    if(bar==node) {printf("find!!!");break;}
		}
	    }
	}
    }
    return FALSE;
}

static gboolean 
node_equal(GNode *node, GNode** parent)
{
    if(node==*parent) {
	*parent=NULL;
	return TRUE;
    }
    return FALSE;
}

/*
 * check_parent: find the paths to the messages, and if the parent isn't
 * the immediate parent of the child, move the child
 */
static void
check_parent(struct ThreadingInfo *ti, LibBalsaMessage * parent,
             LibBalsaMessage * child)
{
    GtkTreeRowReference *parent_ref;
    GtkTreeRowReference *child_ref;
    GtkTreePath *parent_path;
    GtkTreePath *child_path;
    GtkTreePath *test;

    if (parent == NULL)
        return;
    g_return_if_fail(child != NULL);

    parent_ref = g_hash_table_lookup(ti->ref_table, parent);
    g_return_if_fail(parent_ref != NULL);

    child_ref = g_hash_table_lookup(ti->ref_table, child);
    g_return_if_fail(child_ref != NULL);

    parent_path = gtk_tree_row_reference_get_path(parent_ref);
    g_return_if_fail(parent_path != NULL);

    child_path = gtk_tree_row_reference_get_path(child_ref);
    g_return_if_fail(child_path != NULL);

    test = gtk_tree_path_copy(child_path);

    if (!gtk_tree_path_up(test) || gtk_tree_path_get_depth(test) <= 0
        || gtk_tree_path_compare(test, parent_path)) {
        balsa_index_move_subtree(ti->model, child_path, parent_path,
                                 ti->ref_table);
    }
    gtk_tree_path_free(test);
    gtk_tree_path_free(child_path);
    gtk_tree_path_free(parent_path);
}

static gboolean
construct(GNode * node, struct ThreadingInfo * ti)
{
    if (node->parent)
        check_parent(ti, node->parent->data, node->data);

    return FALSE;
}

static void
subject_gather(GNode * node, struct ThreadingInfo * ti)
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

    if(node==NULL) return;

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

    old=g_hash_table_lookup(ti->subject_table, chopped_subject);
    if(old==NULL ||
       (node->data==NULL&&old->data!=NULL)) {
	g_hash_table_insert(ti->subject_table, (char*)chopped_subject, node);
	return;
    }

    old_message = old->data ? old->data : old->children->data;
    g_return_if_fail(old_message!=NULL);
    old_subject = LIBBALSA_MESSAGE_GET_SUBJECT(old_message);

    if( old_subject != chop_re(old_subject) && subject==chopped_subject)
        g_hash_table_insert(ti->subject_table, (gchar*)chopped_subject, node);
}

static void
subject_merge(GNode *node, GSList * root_set, GSList ** save_node,
              GHashTable * subject_table)
{
    LibBalsaMessage *message, *message2;
    const gchar *subject, *subject2;
    const gchar *chopped_subject, *chopped_subject2;
    GNode* node2;

    if(node==NULL) return;

    
    message = node->data ? node->data : node->children->data;

    g_return_if_fail(message!=NULL);

    subject=LIBBALSA_MESSAGE_GET_SUBJECT(message);
    if(subject==NULL)return;
    chopped_subject=chop_re(subject);
    if(chopped_subject==NULL) {
	return;
    }

    node2=g_hash_table_lookup(subject_table, chopped_subject);
    if(node2==NULL || node2==node) {
	return;
    }

    if(node2->data==NULL) {
	reparent(node2, node->children);
	node->children=NULL;
	return;
    }

    if(node->data==NULL) {
	reparent(node, node2->children);
	node2->children=NULL;
	return;
    }

    message2 = node2->data;
    subject2 = LIBBALSA_MESSAGE_GET_SUBJECT(message2);
    chopped_subject2= chop_re(subject2);

    if((subject2==chopped_subject2) && subject!=chopped_subject) {
        GSList* foo;
        reparent(node2, node);
        
        for(foo=root_set; foo; foo=g_slist_next(foo)) {
            if(foo->data==node)
                foo->data=NULL;
        }
        return;
    }
    if((subject2!=chopped_subject2) && subject==chopped_subject) {
        GSList* foo;
        reparent(node, node2);
        g_hash_table_insert(subject_table, (char*)chopped_subject, node);
        for(foo=root_set; foo; foo=g_slist_next(foo)) {
            if(foo->data==node2)
                foo->data=NULL;
        }
        return;
    }

    {
	GNode *new_node=g_node_new(NULL);
	GSList* foo;
	for(foo=root_set; foo; foo=g_slist_next(foo)) {
	    if(foo->data==node)
		foo->data=new_node;
	    else if(foo->data==node2)
		foo->data=NULL;
	}
	reparent(new_node, node);
	reparent(new_node, node2);
      
	*save_node=g_slist_append(*save_node, new_node);
    }
    return;
}

static void
reparent(GNode* node, GNode* children)
{
    GNode* p;
    for(p=children; p; p=p->next)
	p->parent=node;

    p=node->children;
    if(p!=NULL) {
	while(p->next)p=p->next;
	p->next=children;
	if(children!=NULL)children->prev=p;
    }
    else
	node->children=children;
}

/* The more heuristics should be added. */
static const gchar *
chop_re(const gchar* str)
{
    const gchar *p=str;
    while(*p) {
	while(*p && isspace((int)*p)) p++;
	if(!*p) break;
	
	if(g_strncasecmp(p, "re:", 3)==0 || g_strncasecmp(p, "aw:", 3)==0) {
	    p+=3;
	    continue;
	} else if(g_strncasecmp(p, _("Re:"), strlen(_("Re:")))==0) {
	    /* should "re" be localized ? */
	    p+=strlen(_("Re:"));
	    continue;
	} else if(g_strncasecmp(p, balsa_app.current_ident->reply_string,
				strlen(balsa_app.current_ident->reply_string))==0) {
	    p+=strlen(balsa_app.current_ident->reply_string);
	    continue;
	};
	break;
    }
    return p;
}

static void
free_node(GNode* node)
{
    node->data=NULL;
    node->children=NULL;
    g_node_destroy(node);
}

/* yet another message threading function */

static void
threading_simple(BalsaIndex * index)
{
    GtkTreeIter iter;
    GSList *root_children = NULL;
    GSList *p;
    struct ThreadingInfo ti;
    gint length = 0;

    ti.model = gtk_tree_view_get_model(GTK_TREE_VIEW(index));
    if (!gtk_tree_model_get_iter_first(ti.model, &iter))
        return;

    ti.id_table = g_hash_table_new(g_str_hash, g_str_equal);
    ti.ref_table =
        g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL,
                              (GDestroyNotify) gtk_tree_row_reference_free);
    gtk_tree_model_foreach(ti.model, add_message, &ti);

    do {
        LibBalsaMessage *message = NULL;

        gtk_tree_model_get(ti.model, &iter,
                           BNDX_MESSAGE_COLUMN, &message,
                           -1);
        root_children = g_slist_prepend(root_children, message);
        length++;
    } while (gtk_tree_model_iter_next(ti.model, &iter));

    balsa_window_setup_progress(BALSA_WINDOW(index->window), length);
    for (p = root_children; p; p = g_slist_next(p)) {
        LibBalsaMessage *message = p->data;

        if (message->references_for_threading != NULL) {
            LibBalsaMessage *parent =
                g_hash_table_lookup(ti.id_table,
                                    g_list_last(message->
                                                references_for_threading)->
                                    data);

            check_parent(&ti, parent, message);
        }
        balsa_window_increment_progress(BALSA_WINDOW(index->window));
    }
    balsa_window_clear_progress(BALSA_WINDOW(index->window));
    g_slist_free(root_children);
    g_hash_table_destroy(ti.id_table);
    g_hash_table_destroy(ti.ref_table);
}

static gboolean
add_message(GtkTreeModel * model, GtkTreePath * path, GtkTreeIter * iter,
            gpointer data)
{
    struct ThreadingInfo *ti = data;
    LibBalsaMessage *message = NULL;

    gtk_tree_model_get(model, iter, BNDX_MESSAGE_COLUMN, &message, -1);
    g_hash_table_insert(ti->ref_table, message,
                        gtk_tree_row_reference_new(model, path));
    if (message->message_id
        && !g_hash_table_lookup(ti->id_table, message->message_id))
        g_hash_table_insert(ti->id_table, message->message_id, message);

    return FALSE;
}
