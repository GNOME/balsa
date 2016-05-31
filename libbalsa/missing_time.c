/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2013 Stuart Parmenter and others,
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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */

#if !defined(HAVE_CTIME_R)

#include "missing.h"

#include <string.h>

#if BALSA_USE_THREADS
#include <pthread.h>
static pthread_mutex_t time_lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK(mutex)   pthread_mutex_lock(&mutex)
#define UNLOCK(mutex) pthread_mutex_unlock(&mutex)
#else
#define LOCK(mutex)
#define UNLOCK(mutex)
#endif /* BALSA_USE_THREADS */

#ifndef HAVE_CTIME_R
char *
ctime_r(const time_t *clock, char *buf)
{
    LOCK(time_lock);
    strcpy(buf, ctime(clock));
    UNLOCK(time_lock);
    return buf;
}
#endif

#endif
