/*
    Title:  poly_specific.cpp - Poly/ML specific RTS calls.

    Copyright (c) 2006 David C. J. Matthews

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

/* This module is used for various run-time calls that are either in the
   PolyML structure or otherwise specific to Poly/ML. */

#ifdef _WIN32_WCE
#include "winceconfig.h"
#include "wincelib.h"
#else
#ifdef WIN32
#include "winconfig.h"
#else
#include "config.h"
#endif
#endif

#ifdef HAVE_ASSERT_H
#include <assert.h>
#endif

#include "globals.h"
#include "poly_specific.h"
#include "arb.h"
#include "mpoly.h"
#include "sys.h"
#include "machine_dep.h"
#include "polystring.h"
#include "run_time.h"
#include "version.h"
#include "save_vec.h"
#ifndef _WIN32_WCE
#include "exporter.h"
#endif
#include "loadsave.h"
#include "version.h"
#include "sharedata.h"
#include "objsize.h"
#include "memmgr.h"

#define SAVE(x) gSaveVec->push(x)
#define ALLOC(n) alloc_and_save(n)


Handle poly_dispatch_c(Handle args, Handle code)
{
    int c = get_C_long(DEREFWORDHANDLE(code));
    switch (c)
    {
    case 1:
#ifdef _WIN32_WCE
		raise_syscall("Exporting is not implemented in Windows CE", 0);
		return 0;
#else
        return exportNative(args); // Export
#endif
    case 2:
		raise_syscall("C Export has been withdrawn", 0);
		return 0;
    case 3:
#ifdef _WIN32_WCE
		raise_syscall("Exporting is not implemented in Windows CE", 0);
		return 0;
#else
        return exportPortable(args); // Export as portable format
#endif

    case 10: // Return the RTS version string.
        return SAVE(C_string_to_Poly(poly_runtime_system_version));

    case 11: // Return the RTS copyright string
        return SAVE(C_string_to_Poly(poly_runtime_system_copyright));

    case 12: // Return the architecture
        {
            const char *arch;
            switch (machineDependent->MachineArchitecture())
            {
            case MA_Interpreted:    arch = "Interpreted"; break;
            case MA_I386:           arch = "I386"; break;
            case MA_PPC:            arch = "PPC"; break;
            case MA_Sparc:          arch = "SPARC"; break;
            case MA_X86_64:         arch = "X86_64"; break;
            default:                arch = "Unknown"; break;
            }
            return SAVE(C_string_to_Poly(arch));
        }

    case 13: // Share common immutable data.
        {
            ShareData(args);
            return SAVE(TAGGED(0));
        }

        // ObjSize and ShowSize have their own IO vector entries but really they don't
        // need them.  Include them here and add ObjProfile.
    case 14:
        return ObjSize(args);

    case 15:
        return ShowSize(args);

    case 16:
        return ObjProfile(args);

    case 17:
        return loadHeap(args); // Load in a saved file
    case 18:
        return saveHeap(args); // Save state.

    case 19: // Return the RTS argument help string.
        return SAVE(C_string_to_Poly(RTSArgHelp()));

        // These next ones were originally in process_env and have now been moved here,
    case 100: /* Return the maximum word segment size. */
            return Make_arbitrary_precision(MAX_OBJECT_SIZE);
    case 101: /* Return the maximum string size (in bytes).
                 It is the maximum number of bytes in a segment
                 less one word for the length field. */
            return Make_arbitrary_precision(
                (MAX_OBJECT_SIZE)*sizeof(PolyWord) - sizeof(PolyWord));
    case 102: /* Test whether the supplied address is in the io area.
                 This was previously done by having get_flags return
                 256 but this was changed so that get_flags simply
                 returns the top byte of the length word. */
        {
            PolyWord *pt = (PolyWord*)DEREFWORDHANDLE(args);
            if (gMem.IsIOPointer(pt))
                return Make_arbitrary_precision(1);
            else return Make_arbitrary_precision(0);
        }
    case 103: /* Return the register mask for the given function.
                 This is used by the code-generator to find out
                 which registers are modified by the function and
                 so need to be saved if they are used by the caller. */
        {
            PolyObject *pt = DEREFWORDHANDLE(args);
            if (gMem.IsIOPointer(pt))
            {
                /* IO area.  We need to get this from the vector. */
                int i;
                for (i=0; i < POLY_SYS_vecsize; i++)
                {
                    if (pt == (PolyObject*)IoEntry(i))
                    {
                        return Make_arbitrary_precision(machineDependent->GetIOFunctionRegisterMask(i));
                    }
                }
                raise_syscall("Io pointer not found", 0);
            }
            else
            {
                /* We may have a pointer to the code or a pointer to
                   a closure.  If it's a closure we have to find the
                   code. */
                if (! pt->IsCodeObject() && ! pt->IsByteObject())
                    pt = pt->Get(0).AsObjPtr();

                /* Should now be a code object. */
                if (pt->IsCodeObject())
                {
                    /* Compiled code.  This is the second constant in the
                       constant area. */
                    PolyWord *codePt = pt->ConstPtrForCode();
                    PolyWord mask = codePt[1];
                    /* A real mask will be an integer.  For backwards
                       compatibility if we find something that isn't we
                       treat it as all registers. */
                    if (IS_INT(mask))
                    {
                        return SAVE(mask);
                    }
                    else return Make_arbitrary_precision(-1);
                }
                else raise_syscall("Not a code pointer", 0);
            }
        }

    case 104: return Make_arbitrary_precision(POLY_version_number);

    case 105: /* Get the name of the function. */
        {
            PolyObject *pt = DEREFWORDHANDLE(args);
            if (gMem.IsIOPointer(pt))
            {
                /* IO area. */
                int i;
                for (i=0; i < POLY_SYS_vecsize; i++)
                {
                    if (pt == (PolyObject*)IoEntry(i))
                    {
                        char buff[8];
                        sprintf(buff, "RTS%d", i);
                        return SAVE(C_string_to_Poly(buff));
                    }
                }
                raise_syscall("Io pointer not found", 0);
            }
            else if (pt->IsCodeObject()) /* Should now be a code object. */ 
            {
                /* Compiled code.  This is the first constant in the constant area. */
                PolyWord *codePt = pt->ConstPtrForCode();
                PolyWord name = codePt[0];
                /* May be zero indicating an anonymous segment - return null string. */
                if (name == PolyWord::FromUnsigned(0))
                    return SAVE(C_string_to_Poly(""));
                else return SAVE(name);
            }
            else raise_syscall("Not a code pointer", 0);
        }

    default:
        {
            char msg[100];
            sprintf(msg, "Unknown poly-specific function: %d", c);
            raise_exception_string(EXC_Fail, msg);
			return 0;
        }
    }
}