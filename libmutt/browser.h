/* $Id$ */
/*
 * Copyright (C) 1996-2000 Michael R. Elkins <me@cs.hmc.edu>
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
 *     Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 */ 

#ifndef _BROWSER_H
#define _BROWSER_H 1

struct folder_file
{
  mode_t mode;
  off_t size;
  time_t mtime;
  struct stat *st;

  char *name;
  char *desc;
#ifdef USE_IMAP
  char delim;
  
  unsigned imap : 1;
  unsigned selectable : 1;
  unsigned inferiors : 1;
#endif
  unsigned tagged : 1;
  unsigned is_new : 1;
};

struct browser_state
{
  struct folder_file *entry;
  short entrylen; /* number of real entries */
  short entrymax;  /* max entry */
#ifdef USE_IMAP
  short imap_browse;
  char *folder;
  int noselect : 1;
  int marked : 1;
  int unmarked : 1;
  /*BALSA data begin */
  int scanned : 1;
  void * rnode;        /* points to a GNode */
  void * server;       /* points to a LibBalsaServer */
  void (*mailbox_handler)();
  void (*folder_handler)();
  void* subfolders;    /* points to a GList */
  void* cb_data;       /* data passed to {mailbox,folder}_handlers */
  /* BALSA data end */
#endif
};

/* BALSA: add declaration of these fuctions */
void init_state (struct browser_state *state);
void state_free (struct browser_state *state);

#endif /* _BROWSER_H */
