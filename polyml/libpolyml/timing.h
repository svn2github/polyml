/*
    Title:  Header for time functions

    Copyright (c) 2000
        Cambridge University Technical Services Limited

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.
    
    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.
    
    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

*/

#ifndef _TIMING_H_DEFINED
#define _TIMING_H_DEFINED 1

class SaveVecEntry;
typedef SaveVecEntry *Handle;

/* time functions etc */

extern Handle timing_dispatch_c(Handle args, Handle code);

/* Called by the garbage collector at the beginning and
   end of garbage collection. */
extern void record_gc_time(int isEnd);


#endif
