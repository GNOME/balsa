/*
 * Copyright (C) 1996-8 Michael R. Elkins <me@cs.hmc.edu>
 *
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */ 

#include "mutt.h"
#include "sort.h"

#include <string.h>
#include <ctype.h>

/* This function makes use of the fact that Mutt stores message references in
   reverse order (i.e., last to first).  This is optiminal since we would like
   to find the most recent message to which "cur" refers itself.  */
static HEADER *find_reference (HEADER *cur, CONTEXT *ctx)
{
  LIST *refs = cur->env->references;
  void *ptr;

  for (; refs; refs = refs->next)
    if ((ptr = hash_find (ctx->id_hash, refs->data)))
      return ptr;

  return NULL;
}

/* Since the graphics characters have a value >255, I have to resort to
 * using escape sequences to pass the information to print_enriched_string().
 *
 *   '\001': lower corner  (ACS_LLCORNER)
 *   '\002': upper corner  (ACS_ULCORNER)
 *   '\003': left tee      (ACS_LTEE)
 *   '\004': horiz. line   (ACS_HLINE)
 *   '\005': vertical line (ACS_VLINE)
 *   '\006': space         ( )
 *   '\007': greater than  (>)
 *   '\010': asterisk      (*)
 *
 * ncurses should automatically use the default ASCII characters instead of
 * graphics chars on terminals which don't support them (see the man page
 * for curs_addch).
 */
static void linearize_tree (HEADER *tree, HEADER **array)
{
  char *pfx = NULL, *mypfx = NULL;
  int depth = 0, max_depth = 0;
  char corner = Sort & SORT_REVERSE ? '\002' : '\001';

  /* A NULL tree should never be passed here, but may occur if there is
   * a cycle.
   */
  if (!tree)
    return;

  FOREVER
  {
    if (tree->subject_changed || (tree->prev && tree->prev->subject_changed))
      tree->display_subject = 1;
    else 
      tree->display_subject = 0;

    if (depth >= max_depth)
      safe_realloc ((void **) &pfx, (max_depth += 32) * 2 * sizeof (char));

    if (depth)
    {
      mypfx = pfx + (depth - 1) * 2 * sizeof(char);

      sprintf (mypfx, "%c%c\007",
	tree->next ? '\003' : corner,
	tree->fake_thread ? '\010' : '\004');
    }
    else
      pfx[0] = 0;

    safe_free ((void **) &tree->tree);
    tree->tree = safe_strdup (pfx);

    *array = tree;
    array += Sort & SORT_REVERSE ? -1 : 1;

    if (tree->child)
    {
      if (depth)
      {
	mypfx[0] = tree->next ? '\005' : '\006';
	mypfx[1] = '\006';
      }
      tree = tree->child;
      depth++;
    }
    else
    {
      while (!tree->next && tree->parent)
      {
	tree = tree->parent;
	depth--;
      }
      if (!(tree = tree->next))
	break;
    }
  }

  safe_free ((void **) &pfx);
}

/* inserts `msg' into the list `tree' using an insertion sort.  this function
 * assumes that `tree' is the first element in the list, and not some
 * element in the middle of the list.
 */
static void insert_message (HEADER **tree, HEADER *msg, sort_t *sortFunc)
{
  HEADER *tmp;

  /* NOTE: we do NOT clear the `msg->child' link here because when we do
   * the pseudo-threading, we want to preserve any sub-threads.  So we clear
   * the `msg->child' in the main routine where we know it is safe to do.
   */

  /* if there are no elements in the list, just add it and return */
  if (!*tree)
  {
    msg->prev = msg->next = NULL;
    *tree = msg;
    return;
  }

  /* check to see if this message belongs at the beginning of the list */
  if (!sortFunc || sortFunc ((void *) &msg, (void *) tree) < 0)
  {
    (*tree)->prev = msg;
    msg->next = *tree;
    msg->prev = NULL;
    *tree = msg;
    return;
  }
  
  /* search for the correct spot in the list to insert */
  for (tmp = *tree; tmp->next; tmp = tmp->next)
    if (sortFunc ((void *) &msg, (void *) &tmp->next) < 0)
    {
      msg->prev = tmp;
      msg->next = tmp->next;
      tmp->next->prev = msg;
      tmp->next = msg;
      return;
    }

  /* did not insert yet, so add this message to the end of the list */
  tmp->next = msg;
  msg->prev = tmp;
  msg->next = NULL;
}

/* returns 1 if `a' is a descendant (child) of thread `b' */
static int is_descendant (HEADER *a, HEADER *b)
{
  /* find the top parent of the thread */
  while (a->parent)
    a = a->parent;
  return (a == b);
}

/* find the best possible match for a parent mesage based upon subject.
   if there are multiple matches, the one which was sent the latest, but
   before the current message, is used. */
static HEADER *find_subject (CONTEXT *ctx, HEADER *cur)
{
  struct hash_elem *ptr;
  HEADER *tmp, *last = NULL;
  ENVELOPE *env = cur->env;
  int hash;

  if (env->real_subj &&
      ((env->real_subj != env->subject) || (!option (OPTSORTRE))))
  {
    hash = hash_string ((unsigned char *) env->real_subj, ctx->subj_hash->nelem);
    for (ptr = ctx->subj_hash->table[hash]; ptr; ptr = ptr->next)
    {
      tmp = ptr->data;
      if (tmp != cur &&			/* don't match the same message */
	  !tmp->fake_thread &&		/* don't match pseudo threads */
	  tmp->subject_changed &&	/* only match interesting replies */
	  !is_descendant (tmp, cur) &&	/* don't match in the same thread */
	  cur->date_sent >= tmp->date_sent &&
	  (!last || (last->date_sent <= tmp->date_sent)) &&
	  tmp->env->real_subj &&
	  strcmp (env->real_subj, tmp->env->real_subj) == 0)
      {
	last = tmp; /* best match so far */
      }
    }
  }
  return last;
}

static void unlink_message (HEADER **top, HEADER *cur)
{
  if (cur->prev)
  {
    cur->prev->next = cur->next;

    if (cur->next)
      cur->next->prev = cur->prev;
  }
  else
  {
    if (cur->next)
      cur->next->prev = NULL;
    *top = cur->next;
  }
}

static void pseudo_threads (CONTEXT *ctx, sort_t *sortFunc)
{
  HEADER *tree = ctx->tree;
  HEADER *top = tree, *cur, *tmp, *curchild, *nextchild;

  while (tree)
  {
    cur = tree;
    tree = tree->next;
    if ((tmp = find_subject (ctx, cur)) != NULL)
    {
      /* detach this message from it's current location */
      unlink_message (&top, cur);

      cur->subject_changed = 0;
      cur->fake_thread = 1;
      cur->parent = tmp;
      insert_message (&tmp->child, cur, sortFunc);
      
      /* if the message we're attaching has pseudo-children, they
	 need to be attached to its parent, so move them up a level.  */
      for (curchild = cur->child; curchild; )
      {
	nextchild = curchild->next;
	if (curchild->fake_thread)
	{
	  /* detach this message from its current location */
	  unlink_message (&cur->child, curchild);
	  curchild->parent = tmp;
	  insert_message (&tmp->child, curchild, sortFunc);
	}
	curchild = nextchild;
      }
    }
  }
  ctx->tree = top;
}

static HEADER *sort_last (HEADER *top)
{
  HEADER *tree;
  HEADER *tmp;
  HEADER *first;
  HEADER *last;
  HEADER *nextsearch;
  sort_t *usefunc;
  
  usefunc = mutt_get_sort_func (Sort);
  
  tree = top;
  FOREVER
  {
    if (tree->child)
      tree = tree->child;
    else
    {
      while (!tree->next)
      {
	first = last = tree;
	nextsearch = tree->prev;
	first->prev = NULL;
	last->next = NULL;
	while ((tree = nextsearch) != NULL)
	{
          tmp = last;
          nextsearch = nextsearch->prev;
	  while (tmp && (*usefunc) ((void *) &tree->last_sort, 
				    (void *) &tmp->last_sort) < 0)
	    tmp = tmp->prev;
	  if (tmp)
	  {
	    if ((tree->next = tmp->next) != NULL)
	      tmp->next->prev = tree;
	    else
	      last = tree;
	    tmp->next = tree;
	    tree->prev = tmp;
	  }
	  else
	  {
	    tree->next = first;
	    first->prev = tree;
	    first = tree;
	    tree->prev = NULL;
	  }
	}
	if (first->parent)
	{
	  first->parent->child = first;
	  tree = first->parent;
	  if (Sort & SORT_REVERSE)
	  {
	    if ((*usefunc) ((void *) &tree->last_sort,
			    (void *) &first->last_sort) > 0)
	      tree->last_sort = first->last_sort;
	  }
	  else
	  {
	    if ((*usefunc) ((void *) &tree->last_sort,
			    (void *) &last->last_sort) < 0)
	      tree->last_sort = last->last_sort;
	  }
	}
	else
	{
	  top = first;
	  tree = last;
	  break;
	}
      }
      if ((tree = tree->next) == NULL)
	break;
    }
  }
  return top;
}

static void move_descendants (HEADER **tree, HEADER *cur, sort_t *usefunc)
{
  HEADER *ptr, *tmp = *tree;

  while (tmp)
  {
    /* only need to look at the last reference */
    if (tmp->env->references &&
	strcmp (tmp->env->references->data, cur->env->message_id) == 0)
    {
      /* remove message from current location */
      unlink_message (tree, tmp);

      tmp->parent = cur;
      if (cur->env->real_subj && tmp->env->real_subj)
	tmp->subject_changed = strcmp (tmp->env->real_subj, cur->env->real_subj) ? 1 : 0;
      else
	tmp->subject_changed = (cur->env->real_subj || tmp->env->real_subj) ? 1 : 0;
      tmp->fake_thread = 0; /* real reference */

      ptr = tmp;
      tmp = tmp->next;
      insert_message (&cur->child, ptr, usefunc);
    }
    else
      tmp = tmp->next;
  }
}

void mutt_clear_threads (CONTEXT *ctx)
{
  int i;

  for (i = 0; i < ctx->msgcount; i++)
  {
    ctx->hdrs[i]->parent = NULL;
    ctx->hdrs[i]->next = NULL;
    ctx->hdrs[i]->prev = NULL;
    ctx->hdrs[i]->child = NULL;
    ctx->hdrs[i]->threaded = 0;
    ctx->hdrs[i]->fake_thread = 0;
  }
  ctx->tree = NULL;
}

HEADER *mutt_sort_subthreads (HEADER *hdr, sort_t *func)
{
  HEADER *top = NULL;
  HEADER *t;
  
  while (hdr)
  {
    t = hdr;
    hdr = hdr->next;
    insert_message (&top, t, func);
    if (t->child)
      t->child = mutt_sort_subthreads (t->child, func);
  }
  return top;
}

void mutt_sort_threads (CONTEXT *ctx, int init)
{
  sort_t *usefunc = NULL;
  HEADER *tmp, *CUR;
  int i, oldsort;
  
  /* set Sort to the secondary method to support the set sort_aux=reverse-*
   * settings.  The sorting functions just look at the value of
   * SORT_REVERSE
   */
  oldsort = Sort;
  Sort = SortAux;
  
  /* get secondary sorting method.  we can't have threads, so use the date
   * if the user specified it
   */
  if ((Sort & SORT_MASK) == SORT_THREADS)
    Sort = (Sort & ~SORT_MASK) | SORT_DATE;
  
  /* if the SORT_LAST bit is set, we save sorting for later */
  if (!(Sort & SORT_LAST))
    usefunc = mutt_get_sort_func (Sort);

  for (i = 0; i < ctx->msgcount; i++)
  {
    CUR = ctx->hdrs[i];

    if (CUR->fake_thread)
    {
      /* Move pseudo threads back to the top level thread so that they can
       * can be moved later if they are descendants of messages that were
       * just delivered.
       */
      CUR->fake_thread = 0;
      CUR->subject_changed = 1;
      unlink_message (&CUR->parent->child, CUR);
      CUR->parent = NULL;
      insert_message (&ctx->tree, CUR, usefunc);
    }
    else if (!CUR->threaded)
    {
      if ((tmp = find_reference (CUR, ctx)) != NULL)
      {
	CUR->parent = tmp;
	if (CUR->env->real_subj && tmp->env->real_subj)
	  CUR->subject_changed = strcmp (tmp->env->real_subj, CUR->env->real_subj) ? 1 : 0;
	else
	  CUR->subject_changed = (CUR->env->real_subj || tmp->env->real_subj) ? 1 : 0;
      }
      else
	CUR->subject_changed = 1;

      if (!init)
      {
	/* Search the children of `tmp' for decendants of `cur'.  This is only
	 * done when the mailbox has already been threaded since we don't have
	 * to worry about the tree being threaded wrong (because of a missing
	 * parent) during the initial threading.
	 */
	if (CUR->env->message_id)
	  move_descendants (tmp ? &tmp->child : &ctx->tree, CUR, usefunc);
      }
      insert_message (tmp ? &tmp->child : &ctx->tree, CUR, usefunc);
      CUR->threaded = 1;
    }
  }

  if (!option (OPTSTRICTTHREADS))
    pseudo_threads (ctx, usefunc);

  /* now that the whole tree is put together, we can sort by last-* */
  if (Sort & SORT_LAST)
  {
    for (i = 0; i < ctx->msgcount; i++)
      ctx->hdrs[i]->last_sort = ctx->hdrs[i];
    ctx->tree = sort_last (ctx->tree);
  }
  
  /* restore the oldsort order. */
  Sort = oldsort;

  /* Put the list into an array.  If we are reverse sorting, give the
   * offset of the last message, and work backwards (tested for and
   * done inside the function), so that the threads go backwards. 
   * This, of course, means the auxillary sort has to go forwards
   * because we map it backwards here.
   */
  linearize_tree (ctx->tree, ctx->hdrs + (Sort & SORT_REVERSE ? ctx->msgcount - 1 : 0));
}

int _mutt_aside_thread (HEADER *hdr, short dir, short subthreads)
{
  if ((Sort & SORT_MASK) != SORT_THREADS)
  {
    mutt_error ("Threading is not enabled.");
    return (hdr->virtual);
  }

  if (!subthreads)
  {
    while (hdr->parent)
      hdr = hdr->parent;
  }
  else
  {
    if (dir)
    {
      while (!hdr->next && hdr->parent)
	hdr = hdr->parent;
    }
    else
    {
      while (!hdr->prev && hdr->parent)
	hdr = hdr->parent;
    }
  }
  
  hdr = (dir != 0) ^ ((Sort & SORT_REVERSE) != 0) ? hdr->next : hdr->prev;
  if (hdr)
  {
    if (Sort & SORT_REVERSE)
      return (hdr->next ? hdr->next->virtual + 1 : 0);
    else
      return (hdr->virtual);
  }
  else
    return (-1);
}
