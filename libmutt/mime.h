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

/* Content-Type */
enum
{
  TYPEOTHER,
  TYPEAUDIO,
  TYPEAPPLICATION,
  TYPEIMAGE,
  TYPEMESSAGE,
  TYPEMODEL,
  TYPEMULTIPART,
  TYPETEXT,
  TYPEVIDEO
};

/* Content-Transfer-Encoding */
enum
{
  ENCOTHER,
  ENC7BIT,
  ENC8BIT,
  ENCQUOTEDPRINTABLE,
  ENCBASE64,
  ENCBINARY,
  ENCUUENCODED
};

/* Content-Disposition values */
enum
{
  DISPINLINE,
  DISPATTACH,
  DISPFORMDATA
};

/* MIME encoding/decoding global vars */
extern int Index_hex[];
extern int Index_64[];
extern char Base64_chars[];

#define hexval(c) Index_hex[(unsigned int)(c)]
#define base64val(c) Index_64[(unsigned int)(c)]

#define is_multipart(x) \
    ((x)->type == TYPEMULTIPART \
     || ((x)->type == TYPEMESSAGE && (!strcasecmp((x)->subtype, "rfc822") \
				      || !strcasecmp((x)->subtype, "news"))))

extern const char *BodyTypes[];
extern const char *BodyEncodings[];

#define TYPE(X) ((X->type == TYPEOTHER) && (X->xtype != NULL) ? X->xtype : BodyTypes[(X->type)])
#define ENCODING(X) BodyEncodings[(X)]
