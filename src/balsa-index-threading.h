/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-1999 Jay Painter and Stuart Parmenter
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

#ifndef __BALSA_INDEX_THREADING_H__
#define __BALSA_INDEX_THREADING_H__

#include <gnome.h>
#include "libbalsa.h"

#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */

    typedef enum
    {
	BALSA_INDEX_THREADING_FLAT,
	BALSA_INDEX_THREADING_SIMPLE,
	BALSA_INDEX_THREADING_JWZ
    } BalsaIndexThreadingType;

    void balsa_index_threading(BalsaIndex * bindex); 

#ifdef __cplusplus
}
#endif				/* __cplusplus */
#endif				/* __BALSA_INDEX_H__ */
