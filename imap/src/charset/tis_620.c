/*
 * Program:	TIS 620-2529 conversion table
 *
 * Author:	Mark Crispin
 *		Networks and Distributed Computing
 *		Computing & Communications
 *		University of Washington
 *		Administration Building, AG-44
 *		Seattle, WA  98195
 *		Internet: MRC@CAC.Washington.EDU
 *
 * Date:	24 October 1997
 * Last Edited:	2 June 1998
 *
 * Copyright 1998 by the University of Washington
 *
 *  Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notices appear in all copies and that both the
 * above copyright notices and this permission notice appear in supporting
 * documentation, and that the name of the University of Washington not be
 * used in advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  This software is made
 * available "as is", and
 * THE UNIVERSITY OF WASHINGTON DISCLAIMS ALL WARRANTIES, EXPRESS OR IMPLIED,
 * WITH REGARD TO THIS SOFTWARE, INCLUDING WITHOUT LIMITATION ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, AND IN
 * NO EVENT SHALL THE UNIVERSITY OF WASHINGTON BE LIABLE FOR ANY SPECIAL,
 * INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING
 * FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, TORT
 * (INCLUDING NEGLIGENCE) OR STRICT LIABILITY, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

/* TIS 620-2529 is the "Thai Industrial Standard for Thai Character Code
 * for Computer", published by the Thai Industrial Standards Institute,
 * Ministry of Industry of Thailand.
 */

static unsigned short tis620tab[128] = {
  0x0080,0x0081,0x0082,0x0083,0x0084,0x0085,0x0086,0x0087,
  0x0088,0x0089,0x008a,0x008b,0x008c,0x008d,0x008e,0x008f,
  0x0090,0x0091,0x0092,0x0093,0x0094,0x0095,0x0096,0x0097,
  0x0098,0x0099,0x009a,0x009b,0x009c,0x009d,0x009e,0x009f,
  0x0000,0x0e01,0x0e02,0x0e03,0x0e04,0x0e05,0x0e06,0x0e07,
  0x0e08,0x0e09,0x0e0a,0x0e0b,0x0e0c,0x0e0d,0x0e0e,0x0e0f,
  0x0e10,0x0e11,0x0e12,0x0e13,0x0e14,0x0e15,0x0e16,0x0e17,
  0x0e18,0x0e19,0x0e1a,0x0e1b,0x0e1c,0x0e1d,0x0e1e,0x0e1f,
  0x0e20,0x0e21,0x0e22,0x0e23,0x0e24,0x0e25,0x0e26,0x0e27,
  0x0e28,0x0e29,0x0e2a,0x0e2b,0x0e2c,0x0e2d,0x0e2e,0x0e2f,
  0x0e30,0x0e31,0x0e32,0x0e33,0x0e34,0x0e35,0x0e36,0x0e37,
  0x0e38,0x0e39,0x0e3a,0x0000,0x0000,0x0000,0x0000,0x0e3f,
  0x0e40,0x0e41,0x0e42,0x0e43,0x0e44,0x0e45,0x0e46,0x0e47,
  0x0e48,0x0e49,0x0e4a,0x0e4b,0x0e4c,0x0e4d,0x0e4e,0x0e4f,
  0x0e50,0x0e51,0x0e52,0x0e53,0x0e54,0x0e55,0x0e56,0x0e57,
  0x0e58,0x0e59,0x0e5a,0x0e5b,0x0000,0x0000,0x0000,0x0000
};
