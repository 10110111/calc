/*
 * string - string list routines
 *
 * Copyright (C) 1999  David I. Bell
 *
 * Calc is open software; you can redistribute it and/or modify it under
 * the terms of the version 2.1 of the GNU Lesser General Public License
 * as published by the Free Software Foundation.
 *
 * Calc is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU Lesser General
 * Public License for more details.
 *
 * A copy of version 2.1 of the GNU Lesser General Public License is
 * distributed with calc under the filename COPYING-LGPL.  You should have
 * received a copy with calc; if not, write to Free Software Foundation, Inc.
 * 59 Temple Place, Suite 330, Boston, MA  02111-1307, USA.
 *
 * @(#) $Revision: 29.2 $
 * @(#) $Id: string.h,v 29.2 2000/06/07 14:02:13 chongo Exp $
 * @(#) $Source: /usr/local/src/cmd/calc/RCS/string.h,v $
 *
 * Under source code control:	1990/02/15 01:48:36
 * File existed as early as:	before 1990
 *
 * Share and enjoy!  :-)	http://www.isthe.com/chongo/tech/comp/calc/
 */


#if !defined(__CALCSTRING_H__)
#define __CALCSTRING_H__


#include "zmath.h"


struct string {
	char *s_str;
	long s_len;
	long s_links;
	struct string *s_next;
};

typedef struct string STRING;


typedef struct {
	char *h_list;	/* list of strings separated by nulls */
	long h_used;	/* characters used so far */
	long h_avail;	/* characters available for use */
	long h_count;	/* number of strings */
} STRINGHEAD;


extern void initstr(STRINGHEAD *hp);
extern char *addstr(STRINGHEAD *hp, char *str);
extern char *namestr(STRINGHEAD *hp, long n);
extern int findstr(STRINGHEAD *hp, char *str);
extern char *charstr(int ch);
extern char *addliteral(char *str);
extern long stringindex(char *str1, char *str2);
extern STRING *stralloc(void);
extern long addstring(char *str, long len);
extern STRING *charstring(int ch);
extern STRING *makestring(char *str);
extern STRING *makenewstring(char *str);
extern STRING *findstring(long index);
extern STRING *slink(STRING *);
extern void sfree(STRING *);
extern void fitstring(char *, long, long);
extern void showstrings(void);
extern void showliterals(void);
extern STRING _nullstring_;


#endif /* !__CALCSTRING_H__ */
