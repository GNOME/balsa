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

#ifndef __SEND_H__
#define __SEND_H__

#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */

  gboolean balsa_send_message (Message * message);
  gboolean balsa_postpone_message (Message * message);


#ifdef __cplusplus
}
#endif				/* __cplusplus */

#endif				/* __SEND_H__ */
