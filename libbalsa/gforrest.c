/* Balsa E-Mail Library
 * Copyright (C) 1998 Stuart Parmenter
 *  
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <glib.h>
#include "gforrest.h"

GForrest *g_forrest_new(void)
{
	GForrest *gf;

	gf = g_malloc(sizeof(GForrest));
	gf->key = NULL;
	gf->data = NULL;
	gf->list = NULL;
	gf->sibling = NULL;
	gf->children = NULL;

	return gf;
}


