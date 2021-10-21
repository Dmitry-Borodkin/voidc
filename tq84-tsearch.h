/*
   Copy of glibc search.h, version 2.29, for use with tsearch(), tfind(), tdelete(), tdestroy()

   Renamed tsearch() etc to tq84_t*

   ------------------------------------------------------------------------------------

   Declarations for System V style searching functions.
   Copyright (C) 1995-2019 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

#ifndef TQ84_SEARCH_H
#define	TQ84_SEARCH_H 1

#include "voidc_dllexport.h"

extern "C"
{

VOIDC_DLLEXPORT_BEGIN_FUNCTION

typedef enum
{
  preorder,
  postorder,
  endorder,
  leaf
}
VISIT;

typedef int (*tq84__compar_fn_t) (const void *, const void *);

/* Search for an entry matching the given KEY in the tree pointed to
   by *ROOTP and insert a new element if not found.  */
void *tq84_tsearch (const void *__key, void **__rootp,
		      tq84__compar_fn_t __compar);

/* Search for an entry matching the given KEY in the tree pointed to
   by *ROOTP.  If no matching entry is available return NULL.  */
void *tq84_tfind (const void *__key, void *const *__rootp,
		    tq84__compar_fn_t __compar);

/* Remove the element matching KEY from the tree pointed to by *ROOTP.  */
void *tq84_tdelete (const void *__restrict __key,
		      void **__restrict __rootp,
		      tq84__compar_fn_t __compar);


typedef void (*tq84__action_fn_t) (const void *__nodep, VISIT __value,
			       int __level);

/* Walk through the whole tree and call the ACTION callback for every node
   or leaf.  */
void tq84_twalk (const void *__root, tq84__action_fn_t __action);

typedef void (*tq84__free_fn_t) (void *__nodep);

void tq84_tdestroy (void *__root, tq84__free_fn_t __freefct);

VOIDC_DLLEXPORT_END
}

#endif
