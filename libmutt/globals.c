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

/* LIBMUTT - balsa - mutt-1.3 deprecated this, it's actually a 
   nice way to get the globals without main.c - chbm */
#define MAIN_C	1
#define GLOBALS_C	1
#include "mutt.h"
#include "mutt_curses.h"
#include "keymap.h"
#include "mailbox.h"
#include "mutt_regex.h"
#ifdef _PGPPATH
#include "pgp.h"
#endif
