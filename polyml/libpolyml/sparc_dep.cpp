/*
    Title:  Sparc dependent code.
    Author:     Dave Matthews, Cambridge University Computer Laboratory
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

#include "config.h"

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_FILE_H
#include <sys/file.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif
#ifdef HAVE_ASSERT_H
#include <assert.h>
#define ASSERT(x) assert(x)
#else
#define  ASSERT(x)
#endif
#ifdef HAVE_SYS_ERRNO_H
#include <sys/errno.h>
#endif
#ifdef HAVE_UCONTEXT_H
#include <ucontext.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "globals.h"
#include "gc.h"
#include "run_time.h"
#include "mpoly.h"
#include "arb.h"
#include "diagnostics.h"
#include "processes.h"
#include "sys.h"
#include "profiling.h"
#include "sighandler.h"
#include "machine_dep.h"
#include "save_vec.h"
#include "scanaddrs.h"
#include "check_objects.h"
#include "memmgr.h"

// Values for the returnReason
enum RETURN_REASON {
    RETURN_IO_CALL = 0,
    RETURN_IO_EXCEPTION,
    RETURN_HEAP_OVERFLOW,
    RETURN_STACK_OVERFLOW,
    RETURN_STACK_OVERFLOWEX,
    RETURN_RAISE_DIV,
    RETURN_ARB_EMULATION
};

/* Note: we've reserved a slot for %o6, but we're not allowed to change it
  since it's the C stack pointer, so we really only have 17 usable
  tagged registers. SPF 30/1/97.
*/

/* Location of these registers in poly_stack->p_reg[] */ 
#define OFFSET_REGRESULT    2 /* regResult == argReg0 on SPARC */ 
#define OFFSET_ARGREG0      2
#define OFFSET_ARGREG1      3
#define OFFSET_ARGREG2      4
#define OFFSET_ARGREG3      5
#define OFFSET_REGCLOSURE   7
#define OFFSET_REGRETURN    9
/* slots 0 and 1 are used for %g1 and %g2 */


#define SAVERETURNOFFSET 2

/* The first instruction executed after native code returns
   is the saved return address + 6 */ 
#define RETURNOFFSET    6
#define HANDLEROFFSET   2

/* the amount of ML stack space to reserve for registers,
   C exception handling etc. The compiler requires us to
   reserve 2 stack-frames worth (2 * 20 words) plus whatever
   we require for the register save area. We actually reserve
   slightly more than this. SPF 3/3/97
*/
#define CHECKED_REGS    18
#define UNCHECKED_REGS  3
#define EXTRA_STACK     0

#define OVERFLOW_STACK_SIZE \
  (50 + \
   sizeof(StackObject)/sizeof(PolyWord) + \
   CHECKED_REGS + \
   UNCHECKED_REGS + \
   EXTRA_STACK)

// %i0 points to this structure while in the ML code.  The offsets
// of some items, in particular raiseException, are bound into the
// code and must not be changed.  Other offsets are bound into the
// assembly code.
static struct MemRegisters {
    int inRTS;
        /* This is set when poly_stack->p_pc and poly_stack->p_sp are set */
    int requestCode;
    int returnReason;

    PolyWord    *heapPointer;
    POLYUNSIGNED heapSpaceT; // The heap space = -maxint+space
    StackObject *polyStack;
    PolyWord    *stackLimit;
    PolyWord    *stackTop; // Used in "raisex"
    byte        *raiseException; // Called to raise an exception
    byte        *ioEntry; // Called to save the ML state and return to C.
} memRegisters;

class SparcDependent: public MachineDependent {
public:
    SparcDependent(): allocSpace(0), allocWords(0) {}

    virtual void InitStackFrame(StackObject *stack, Handle proc, Handle arg);
    virtual unsigned InitialStackSize(void) { return 128+OVERFLOW_STACK_SIZE; } // Initial size of a stack 
    virtual int SwitchToPoly(void);
    virtual void SetForRetry(int ioCall);
    virtual void InitInterfaceVector(void);
    virtual void SetException(StackObject *stack,poly_exn *exc);
    virtual void ResetSignals(void);
    virtual void ScanConstantsWithinCode(PolyObject *addr, PolyObject *oldAddr, POLYUNSIGNED length, ScanAddress *process);
    virtual void InterruptCodeUsingContext(SIGNALCONTEXT *context);
    virtual void InterruptCode(void) { assert(false); } // We should always use InterruptCodeUsingContext.
    virtual int  GetIOFunctionRegisterMask(int ioCall);
    virtual bool GetPCandSPFromContext(SIGNALCONTEXT *context, PolyWord *&sp, POLYCODEPTR &pc);
    virtual bool InRunTimeSystem(void);
    virtual void CallIO0(Handle(*ioFun)(void));
    virtual void CallIO1(Handle(*ioFun)(Handle));
    virtual void CallIO2(Handle(*ioFun)(Handle, Handle));
    virtual void CallIO3(Handle(*ioFun)(Handle, Handle, Handle));
    virtual void CallIO4(Handle(*ioFun)(Handle, Handle, Handle, Handle));
    virtual void CallIO5(Handle(*ioFun)(Handle, Handle, Handle, Handle, Handle));
    virtual Handle CallBackResult(void);
    virtual void SetExceptionTrace(void);
    virtual void CallCodeTupled(void);
    virtual Architectures MachineArchitecture(void) { return MA_Sparc; }
    virtual void SetCodeConstant(Handle data, Handle constant, Handle offseth, Handle base);
    virtual void FlushInstructionCache(void *p, POLYUNSIGNED bytes);

private:
    bool TrapHandle(void);
    void StartIOCall(void);
    void SetMemRegisters(void);
    Handle BuildCodeSegment(const unsigned *code, unsigned codeWords, char functionName);
    Handle BuildKillSelfCode(void);

    LocalMemSpace *allocSpace;
    POLYUNSIGNED allocWords;
};

bool SparcDependent::InRunTimeSystem(void) { return memRegisters.inRTS != 0; }

#define VERSION_NUMBER POLY_version_number

extern "C" {
extern int finisha();
extern int install_roota();
extern int strconcata();
extern int change_dira();
extern int str_comparea();
extern int teststreq();
extern int teststrneq();
extern int teststrgtr();
extern int teststrlss();
extern int teststrgeq();
extern int teststrleq();
extern int locksega();
extern int profilera();
extern int add_long();
extern int sub_long(); 
extern int mult_long();
extern int div_longa();
extern int rem_longa();
extern int neg_long();
extern int or_long();
extern int and_long();
extern int xor_long();
extern int equal_long();
extern int Real_stra();
extern int Real_geqa();
extern int Real_leqa();
extern int Real_gtra();
extern int Real_lssa();
extern int Real_eqa();
extern int Real_neqa();
extern int Real_dispatcha();
extern int Real_adda();
extern int Real_suba();
extern int Real_mula();
extern int Real_diva();
extern int Real_nega();
extern int Real_repra();
extern int Real_conva();
extern int Real_inta();
extern int Real_floata();
extern int Real_sqrta();
extern int Real_sina();
extern int Real_cosa();
extern int Real_arctana();
extern int Real_expa();
extern int Real_lna();
extern int io_operationa();
extern int shift_right_word();
extern int word_neq();
extern int not_bool();
extern int string_length();
extern int int_eq();
extern int int_neq();
extern int int_geq();
extern int int_leq();
extern int int_gtr();
extern int int_lss();
extern int string_suba();
extern int or_word();
extern int and_word();
extern int xor_word();
extern int shift_left_word();
extern int word_eq();
extern int load_byte();
extern int load_word();
extern int assign_byte();
extern int assign_word();
extern int fork_processa();
extern int choice_processa();
extern int int_processa();
extern int kill_selfa();
extern int send_on_channela();
extern int receive_on_channela();
extern int alloc_store();
extern int get_length_a();
extern int get_flags_a();
extern int set_flags_a();
extern int int_to_word();
extern int set_code_constanta();
extern int move_bytes();
extern int move_words();
extern int shift_right_arith_word();
extern int raisex();
extern int exception_tracea();
extern int offset_address();
extern int is_shorta();
extern int is_big_endian();
extern int bytes_per_word();
extern int mul_word();
extern int plus_word();
extern int minus_word();
extern int div_worda();
extern int mod_worda();
extern int word_geq();
extern int word_leq();
extern int word_gtr();
extern int word_lss();

extern int objsize_a();
extern int showsize_a();
extern int timing_dispatch_a();
extern int interrupt_console_processes_a(); /* MJC 01/08/90 */

extern int install_subshells_a();           /* MJC 12/09/90 */

extern int XWindows_a();                    /* MJC 27/09/90 */

extern int full_gc_a();                     /* MJC 18/03/91 */
extern int stack_trace_a();                 /* MJC 18/03/91 */

extern int foreign_dispatch_a();            /* NIC 22/04/94 */
extern int callcode_tupleda();              /* SPF 07/07/94 - for ML version of compiler */
extern int process_env_dispatch_a();        /* DCJM 25/4/00 */
extern int set_string_length_a();           /* DCJM 28/2/01 */
extern int get_first_long_word_a();         /* DCJM 28/2/01 */
extern int shrink_stack_a();                /* SPF  1/12/96 */

extern int IO_dispatch_a();                 /* DCJM 8/5/00 */
extern int Net_dispatch_a();                /* DCJM 22/5/00 */
extern int OS_spec_dispatch_a();            /* DCJM 22/5/00 */
extern int Sig_dispatch_a();                /* DCJM 18/7/00 */
extern int poly_dispatch_a();

extern void SparcAsmSwitchToPoly(struct MemRegisters *);
extern int SparcAsmSaveStateAndReturn(void);
extern void SparcAsmFlushInstructionCache(void *p, POLYUNSIGNED bytes);

};

void SparcDependent::InitStackFrame(StackObject *stack, Handle proc, Handle arg)
/* Initialise stack frame. */
{
   /* This code is pretty tricky. 
        (1) We pretend that the function we want to call is actually an RTS entry
            that has been interrupted, so that it gets called via the RTS call
            retry mechanism.
            
        (2) We have to ensure that if the function returns or is interrupted,
            it gets into the kill_self code.
            
        (3) This is the last exception handler on the stack, so we make it
            point at itself.     
    */
    unsigned i;

    POLYUNSIGNED stack_size = stack->Length();
    stack->p_space = OVERFLOW_STACK_SIZE;
    stack->p_pc    = PC_RETRY_SPECIAL; /* As if we had called MD_set_for_retry. */
    stack->p_nreg  = CHECKED_REGS;
    
    /* Reset all registers since this may be an old stack frame */
    for (i = 0; i < CHECKED_REGS; i++)
        stack->p_reg[i] = TAGGED(0);

    // Set the default handler and return address to point to this code.
    // We have to offset the addresses by two bytes because the code to enter and a handler
    // or return from a function always subtracts two.
    Handle killCode = BuildKillSelfCode();
    PolyWord killJump = PolyWord::FromUnsigned(killCode->Word().AsUnsigned() | HANDLEROFFSET);

    stack->p_reg[OFFSET_REGCLOSURE] = DEREFWORD(proc); /* Set regClosureto the closure address. */
    stack->p_reg[CHECKED_REGS]      = PolyWord::FromUnsigned(UNCHECKED_REGS);
    // If this function takes an argument store it in the argument register.  If it doesn't
    // we've set argReg0 to TAGGED(0) already.
    if (arg != 0) stack->p_reg[OFFSET_ARGREG0] = DEREFWORD(arg);
    
    /* Since they're unchecked, they don't need to be initialised,
       but set them to 0 anyway. */
    for (i = 0; i < UNCHECKED_REGS; i++)
        stack->p_reg[CHECKED_REGS+1+i] = PolyWord::FromUnsigned(0);
    
    /* Set up exception handler group - there's no previous handler so point
       "previous handler" pointer at itself.*/
    stack->Set(stack_size-1, PolyWord::FromStackAddr(stack->Offset(stack_size-1)));
    stack->Set(stack_size-2, killJump);
    stack->Set(stack_size-3, TAGGED(0)); /* Default handler. */
    
    stack->p_sp = (PolyWord*)stack + stack_size - 3;
    stack->p_hr = stack->p_sp;
    
    // Return address into %o7 plus 2 bytes.  A return will always add 6 to the value.
    stack->p_reg[OFFSET_REGRETURN] = killJump;
}

bool SparcDependent::GetPCandSPFromContext(SIGNALCONTEXT *context, PolyWord *&sp, POLYCODEPTR &pc)
{
    if (memRegisters.inRTS)
    {
        sp = poly_stack->p_sp;
        pc = poly_stack->p_pc;
        return true;
    }
    else /* in poly code or assembly code */
    {
        /* NB Poly/ML uses register g4 as the stack pointer, C appears to use o6.  */
        sp = (PolyWord *)context->uc_mcontext.gregs[REG_G4];
        pc = (byte *)context->uc_mcontext.gregs[REG_PC];
        return true;
    }
}

void SparcDependent::InterruptCodeUsingContext(SIGNALCONTEXT *context)
{
    if (poly_stack != 0) 
        memRegisters.stackLimit = poly_stack->Offset(poly_stack->Length()-1); 

    if (! memRegisters.inRTS && poly_stack != 0) /* only if in Poly code */
    {
        PolyWord **sl = (PolyWord **)&(context->uc_mcontext.gregs[REG_G5]);
        PolyWord *limit  = *sl;
        PolyWord *top    = poly_stack->Offset(poly_stack->Length()-1);
        PolyWord *bottom = (PolyWord*)poly_stack + poly_stack->p_space;

        if (limit == top) /* already set for interrupt */
            return;
        else if (limit == bottom)
            *sl = top;
        else
            Crash("Unable to interrupt code: bottom = %p, limit = %p, top = %p",
                bottom, limit, top);
    }

}

// These are just for debugging.  They record the last point before
// the memory was checked.
byte *lastPC;
int lastRequest, lastReason;
PolyWord *lastBase;

int SparcDependent::SwitchToPoly(void)
/* (Re)-enter the Poly code from C. */
{
    while (1)
    {
        CheckMemory(); // Do any memory checking.

        // Remember the position after the last time we checked
        // the memory.
        lastPC = poly_stack->p_pc;
        lastRequest = memRegisters.requestCode;
        lastReason = memRegisters.returnReason;
        if (allocSpace) lastBase = allocSpace->pointer;
        
        SetMemRegisters();

        SparcAsmSwitchToPoly(&memRegisters); // Load registers.  Returns as a result of an RTS call.
        allocSpace->pointer = memRegisters.heapPointer - 1; // Get updated limit pointer.
        allocWords = 0; // Always zero except if this is a memory trap.

        switch (memRegisters.returnReason)
        {
        case RETURN_IO_CALL:
            // The ML code wants an IO call.
            return memRegisters.requestCode;
        case RETURN_IO_EXCEPTION:
            try {
                // We have had a trap of some sort
                if (TrapHandle())
                    return -1; // Safe to process interrupts.
            }
            catch (IOException) {
               // We may get an exception while handling this if we run out of store
            }
        }
    }
}

void SparcDependent::SetMemRegisters(void)
{
    // Find a space to allocate in.  Try and find a space with a reasonable amount of
    // free space first.
    allocSpace = gMem.GetAllocSpace(1000+allocWords);
    if (allocSpace == 0) // If there isn't one get any mutable space.
        allocSpace = gMem.GetAllocSpace(allocWords);

    // If we have had a heap trap we actually do the allocation here.
    // We will have already garbage collected and recovered sufficient space.
    allocSpace->pointer -= allocWords;
    ASSERT(allocSpace->pointer >= allocSpace->bottom);
    allocWords = 0;

    memRegisters.requestCode = 0;
    memRegisters.returnReason = RETURN_IO_CALL;
    
    memRegisters.polyStack = poly_stack;
    memRegisters.stackTop = poly_stack->Offset(poly_stack->Length());
    
    if (poly_stack->p_pc == PC_RETRY_SPECIAL)
    {
        // We need to retry the call.  The entry point should be the
        // first word of the closure which is in %o4.
        poly_stack->p_pc = poly_stack->p_reg[OFFSET_REGCLOSURE].AsObjPtr()->Get(0).AsCodePtr();
    }
    // Set up heap pointers.
    memRegisters.heapPointer = allocSpace->pointer+1; // This points beyond the length word
    // The Sparc version sets %g7 to be -maxint + space.  Each time an object is allocated
    // the size is deducted from this until eventually the space is exhausted.  At that
    // point the subtraction results in an overflow which traps.
    if (store_profiling || (userOptions.debug & DEBUG_REGION_CHECK))
        memRegisters.heapSpaceT = 0x80000000;
    else
        memRegisters.heapSpaceT = ((char*)allocSpace->pointer-(char*)allocSpace->bottom) | 0x80000000;
    
    if (interrupted)
        memRegisters.stackLimit = memRegisters.stackTop;
    else
        memRegisters.stackLimit = poly_stack->Offset(poly_stack->p_space);
}

// Called as part of the call of an IO function.
void SparcDependent::StartIOCall(void)
{
    // Set the return address to be the contents of the return register
    // after skipping the CALL instruction and delay slot.
    // This is a real return address which is safe for the p_pc field but
    // not allowed to remain in a register field.  We have to OR in the
    // return offset there.  Because this may be a retry we may already
    // have been here before so we use OR rather than ADD.
    POLYUNSIGNED returnAddr = poly_stack->p_reg[OFFSET_REGRETURN].AsUnsigned();
    returnAddr |= SAVERETURNOFFSET; // Make it a valid code address.
    poly_stack->p_reg[OFFSET_REGRETURN] = PolyWord::FromUnsigned(returnAddr);
    poly_stack->p_pc = (byte*)(returnAddr + RETURNOFFSET);
}

// IO Functions called indirectly from assembly code.
void SparcDependent::CallIO0(Handle (*ioFun)(void))
{
    StartIOCall();
    try {
        Handle result = (*ioFun)();
        poly_stack->p_reg[OFFSET_REGRESULT] = result->Word();
    }
    catch (IOException exc) {
        switch (exc.m_reason)
        {
        case EXC_EXCEPTION:
            return;
        case EXC_RETRY:
            return;
        }
    }
}

void SparcDependent::CallIO1(Handle (*ioFun)(Handle))
{
    StartIOCall();
    Handle saved1 = gSaveVec->push(poly_stack->p_reg[OFFSET_ARGREG0]);
    try {
        Handle result = (*ioFun)(saved1);
        poly_stack->p_reg[OFFSET_REGRESULT] = result->Word();
    }
    catch (IOException exc) {
        switch (exc.m_reason)
        {
        case EXC_EXCEPTION:
            return;
        case EXC_RETRY:
            return;
        }
    }
}

void SparcDependent::CallIO2(Handle (*ioFun)(Handle, Handle))
{
    StartIOCall();
    Handle saved1 = gSaveVec->push(poly_stack->p_reg[OFFSET_ARGREG0]);
    Handle saved2 = gSaveVec->push(poly_stack->p_reg[OFFSET_ARGREG1]);
    try {
        Handle result = (*ioFun)(saved2, saved1);
        poly_stack->p_reg[OFFSET_REGRESULT] = result->Word();
    }
    catch (IOException exc) {
        switch (exc.m_reason)
        {
        case EXC_EXCEPTION:
            return;
        case EXC_RETRY:
            return;
        }
    }
}

void SparcDependent::CallIO3(Handle (*ioFun)(Handle, Handle, Handle))
{
    StartIOCall();
    Handle saved1 = gSaveVec->push(poly_stack->p_reg[OFFSET_ARGREG0]);
    Handle saved2 = gSaveVec->push(poly_stack->p_reg[OFFSET_ARGREG1]);
    Handle saved3 = gSaveVec->push(poly_stack->p_reg[OFFSET_ARGREG2]);
    try {
        Handle result = (*ioFun)(saved3, saved2, saved1);
        poly_stack->p_reg[OFFSET_REGRESULT] = result->Word();
    }
    catch (IOException exc) {
        switch (exc.m_reason)
        {
        case EXC_EXCEPTION:
            return;
        case EXC_RETRY:
            return;
        }
    }
}

// The only function with four arguments is SetCodeConstant.
void SparcDependent::CallIO4(Handle (*ioFun)(Handle, Handle, Handle, Handle))
{
    StartIOCall();
    Handle saved1 = gSaveVec->push(poly_stack->p_reg[OFFSET_ARGREG0]);
    Handle saved2 = gSaveVec->push(poly_stack->p_reg[OFFSET_ARGREG1]);
    Handle saved3 = gSaveVec->push(poly_stack->p_reg[OFFSET_ARGREG2]);
    Handle saved4 = gSaveVec->push(poly_stack->p_reg[OFFSET_ARGREG3]);
    try {
        Handle result = (*ioFun)(saved4, saved3, saved2, saved1);
        poly_stack->p_reg[OFFSET_REGRESULT] = result->Word();
    }
    catch (IOException exc) {
        switch (exc.m_reason)
        {
        case EXC_EXCEPTION:
            return;
        case EXC_RETRY:
            return;
        }
    }
}

// The only functions with 5 args are move_bytes/word_long
void SparcDependent::CallIO5(Handle (*ioFun)(Handle, Handle, Handle, Handle, Handle))
{
    StartIOCall();
    Handle saved1 = gSaveVec->push(poly_stack->p_reg[OFFSET_ARGREG0]);
    Handle saved2 = gSaveVec->push(poly_stack->p_reg[OFFSET_ARGREG1]);
    Handle saved3 = gSaveVec->push(poly_stack->p_reg[OFFSET_ARGREG2]);
    Handle saved4 = gSaveVec->push(poly_stack->p_reg[OFFSET_ARGREG3]);
    Handle saved5 = gSaveVec->push(poly_stack->p_sp[0]);
    try {
        Handle result = (*ioFun)(saved5, saved4, saved3, saved2, saved1);
        poly_stack->p_reg[OFFSET_REGRESULT] = result->Word();
        poly_stack->p_sp++; // Pop the final argument now we're returning.
    }
    catch (IOException exc) {
        switch (exc.m_reason)
        {
        case EXC_EXCEPTION:
            return;
        case EXC_RETRY:
            return;
        }
    }
}

// Return the callback result.  The current ML process (thread) terminates.
Handle SparcDependent::CallBackResult(void)
{
    return gSaveVec->push(poly_stack->p_reg[OFFSET_ARGREG0]); // Argument to return is in %o0.
}

void SparcDependent::SetForRetry(int ioCall)
{
    poly_stack->p_pc = PC_RETRY_SPECIAL; /* This value is treated specially. */
}

// Call a piece of compiled code.  Note: this doesn't come via CallIO1
// so StartIOCall has not been called.
void SparcDependent::CallCodeTupled(void)
{
    // The eventual return address is in %o7 - leave it there
    // but call StartIOCall to make sure it's tagged before any possible G.C.
    StartIOCall();
    PolyObject *argTuple = poly_stack->p_reg[OFFSET_ARGREG0].AsObjPtr();
    Handle closure = gSaveVec->push(argTuple->Get(0));
    Handle argvec = gSaveVec->push(argTuple->Get(1));

    if (! IS_INT(DEREFWORD(argvec))) // May be nil if there are no args.
    {
        PolyObject *argv = DEREFHANDLE(argvec);
        POLYUNSIGNED argCount = argv->Length();
        if (argCount > 4)
        {
            // Check we have space for the arguments.  This may result in a GC which
            // in turn may throw a C++ exception.
            try {
                check_current_stack_size(poly_stack->p_sp - (argCount - 4));
            }
            catch (IOException exc)
            {
                ASSERT(exc.m_reason == EXC_EXCEPTION);  // This should be the only one
                return; // Will have been set up to raise an exception.
            }
        }
        // First argument is in %o0
        poly_stack->p_reg[OFFSET_ARGREG0] = argv->Get(0);
        // Second arg, if there is one, goes into %o1 etc.
        if (argCount > 1)
            poly_stack->p_reg[OFFSET_ARGREG1] = argv->Get(1);
        if (argCount > 2)
            poly_stack->p_reg[OFFSET_ARGREG2] = argv->Get(2);
        if (argCount > 3)
            poly_stack->p_reg[OFFSET_ARGREG3] = argv->Get(3);
        // Remaining args go on the stack.
        for (POLYUNSIGNED i = 4; i < argCount; i++)
            *(--poly_stack->p_sp) = argv->Get(i+2);
    }
    // The closure goes into the closure reg.
    poly_stack->p_reg[OFFSET_REGCLOSURE] = DEREFWORD(closure);
    // First word of closure is entry point.
    poly_stack->p_pc = DEREFHANDLE(closure)->Get(0).AsCodePtr(); // pc points to the start of the code
}

// This code is executed if the function returns without raising an exception.  Because
// the normal function return sequence jumps to %o7+6 we have to have two nops at the start.
static unsigned setExceptionCode[] =
{
    0x01000000, // nop
    0x01000000, // nop
    0xde01200c, // ld  [%g4+12],%o7  ! Get original return addr
    0xc6012008, // ld  [%g4+8],%g3   ! Restore previous handler
    0x81c3e006, // jmp %o7+6
    0x88012010  // add %g4,16,%g4    ! Pop these from stack DELAY SLOT
};

// Set up a special handler that will trace any uncaught exception within a function.
void SparcDependent::SetExceptionTrace(void)
{
    // Save the return address for when we've called the function.  This will
    // be popped by the special "return" code we'll set up.
    *(--poly_stack->p_sp) =
        PolyWord::FromUnsigned(poly_stack->p_reg[OFFSET_REGRETURN].AsUnsigned() | SAVERETURNOFFSET);
    *(--poly_stack->p_sp) = PolyWord::FromStackAddr(poly_stack->p_hr); // Save previous handler.
    *(--poly_stack->p_sp) = TAGGED(0); // Push special handler address.
    *(--poly_stack->p_sp) = TAGGED(0); // Push "catch all" exception id.
    poly_stack->p_hr = poly_stack->p_sp; // This is the new handler.
    Handle fun = gSaveVec->push(poly_stack->p_reg[OFFSET_ARGREG0]); // Argument - function to call and trace
    poly_stack->p_reg[OFFSET_REGCLOSURE] = DEREFWORD(fun); // Closure register must contain the closure
    poly_stack->p_pc = DEREFHANDLE(fun)->Get(0).AsCodePtr(); // pc points to the start of the code

    Handle retCode = BuildCodeSegment(setExceptionCode, sizeof(setExceptionCode)/sizeof(unsigned), 'R');

    // Set %o7 so that if the traced function returns normally (i.e. without raising an
    // exception) it will enter the "return" code which will remove this handler.
    poly_stack->p_reg[OFFSET_REGRETURN] = PolyWord::FromUnsigned(retCode->Word().AsUnsigned() | SAVERETURNOFFSET);
    poly_stack->p_reg[OFFSET_ARGREG0] = TAGGED(0); // Give the function we're calling a unit argument.
}


// In Solaris the trap instructions result in SIGSEGVs.
static void catchSEGV(SIG_HANDLER_ARGS(s, context))
{
    assert(s == SIGSEGV);
    assert(context != NULL);
    SIGNALCONTEXT *cntxt = (SIGNALCONTEXT *)context;
    
    if (memRegisters.inRTS)
    {
        signal(SIGSEGV,SIG_DFL);
        return;
    }
    
    /* NOW, we are in the run-time system */
    memRegisters.inRTS = 1;
    memRegisters.returnReason = RETURN_IO_EXCEPTION;
    
    /* This piece of code is extremely messy. It has to get the state when the
       interrupt occured by unwinding the stack. It can then save the registers
       and call ``translate''. */
    poly_stack->p_pc = (byte*)cntxt->uc_mcontext.gregs[REG_PC]; /* Save trapping pc. */
    cntxt->uc_mcontext.gregs[REG_PC]  = (int)&SparcAsmSaveStateAndReturn; /* Restart in trap_handler. */
    cntxt->uc_mcontext.gregs[REG_nPC] = cntxt->uc_mcontext.gregs[REG_PC] + 4;
}


/******************************************************************************/
/*                                                                            */
/*      catchILL - utility function                                           */
/*                                                                            */
/******************************************************************************/
static void catchILL(SIG_HANDLER_ARGS(s, context))
{
    assert(s == SIGILL);
    assert(context != NULL);
    SIGNALCONTEXT *cntxt = (SIGNALCONTEXT *)context;
    
    /* Shouldn't get stack overflow in run-time system */
    if (memRegisters.inRTS)
    {
        { /* use standard SYSV calls */
            sigset_t mask;
            assert(sigemptyset(&mask) == 0);
            assert(sigaddset(&mask,SIGILL) == 0);
            assert(sigprocmask(SIG_UNBLOCK,&mask,NULL) == 0);
        }
        
        if (gc_phase != 0)
            printf("\nStack overflow in the garbage collector.\n");
        else
            printf("\nStack overflow in the runtime system.\n");
        
        printf("You may need to increase your stack limit and try again.\n");
        fflush(stdout);
        exit(1);
        /*NOTREACHED*/
    }
    
    /* NOW, we are in the run-time system */
    memRegisters.inRTS = 1;
    memRegisters.returnReason = RETURN_IO_EXCEPTION;
    
    /* This piece of code is extremely messy. It has to get the state when the
       interrupt occured by unwinding the stack. It can then save the registers
       and call ``translate''. */
    poly_stack->p_pc = (byte*)cntxt->uc_mcontext.gregs[REG_PC]; /* Save trapping pc. */
    cntxt->uc_mcontext.gregs[REG_PC]  = (int)&SparcAsmSaveStateAndReturn; /* Restart in trap_handler. */
    cntxt->uc_mcontext.gregs[REG_nPC] = cntxt->uc_mcontext.gregs[REG_PC] + 4;
    
    /* "returns" to MD_trap_handler */
}

/******************************************************************************/
/*                                                                            */
/*      catchEMT - utility function                                           */
/*                                                                            */
/******************************************************************************/
static void catchEMT(SIG_HANDLER_ARGS(s, context))
{
    assert(s == SIGEMT);
    assert(context != NULL);
    SIGNALCONTEXT *cntxt = (SIGNALCONTEXT *)context;
    
    /* shouldn't get SIGEMT from run-time system,         */
    /* so reinstall default handler and return for retry, */
    /* which should lead to core dump.                    */
    if (memRegisters.inRTS)
    {
        signal(SIGEMT,SIG_DFL);
        return;
    }
    
    /* NOW, we are in the run-time system */
    memRegisters.inRTS = 1;
    memRegisters.returnReason = RETURN_IO_EXCEPTION;
    
    /* This piece of code is extremely messy. It has to get the state when the
       interrupt occured by unwinding the stack. It can then save the registers
       and call ``translate''. */
    poly_stack->p_pc = (byte*)cntxt->uc_mcontext.gregs[REG_PC]; /* Save trapping pc. */
    cntxt->uc_mcontext.gregs[REG_PC]  = (int)&SparcAsmSaveStateAndReturn; /* Restart in trap_handler. */
    cntxt->uc_mcontext.gregs[REG_nPC] = cntxt->uc_mcontext.gregs[REG_PC] + 4;
    
    /* "returns" to MD_trap_handler */
}

/* end of Solaris2 signal handling */

static PolyWord zero = PolyWord::FromUnsigned(0);

/******************************************************************************/
/*                                                                            */
/*      get_reg - utility function                                            */
/*                                                                            */
/******************************************************************************/
static PolyWord *get_reg(int rno)
/* Returns a pointer to the register given by the 5 bit value rno. */
{
    if (8 <= rno && rno <= 23) /* %o0 - %l7 */
        return &poly_stack->p_reg[rno-6];
    
    else switch (rno) 
    {
    case 0:  /* %g0 */ return &zero;
    case 1:  /* %g1 */ return &poly_stack->p_reg[0];
    case 2:  /* %g2 */ return &poly_stack->p_reg[1];
         /* These last two are used as unchecked work registers. */
    case 28: /* %i4 */ return &poly_stack->p_reg[CHECKED_REGS+1];
    case 29: /* %i5 */ return &poly_stack->p_reg[CHECKED_REGS+2];
    case 3:  /* %g3 (hr) */ return (PolyWord*)&poly_stack->p_hr;
    case 4:  /* %g4 (sp) */ return (PolyWord*)&poly_stack->p_sp;
    default: Crash("Unknown register %d at %x\n", rno, (int)(poly_stack->p_pc));
         /*NOTREACHED*/
    }
}

#define ALIGNED(p) ((p.AsUnsigned() & (sizeof(PolyWord)-1)) == 0)

// In many case we will have subtracted one from the argument and need to add it back.
// If this was a short integer that will retag it but if it was actually long
// it will turn it back into an address.
inline PolyWord AddOne(PolyWord p)
{
    return PolyWord::FromUnsigned(p.AsUnsigned() + 1);
}

// The reverse operation to put the result back.
inline PolyWord SubOne(PolyWord p)
{
    return PolyWord::FromUnsigned(p.AsUnsigned() - 1);
}

static void emulate_trap(POLYUNSIGNED instr)
/* Emulate a taddcctv or tsubcctv instruction. */
/* One or both of the arguments may be i4 or i5.  These registers are saved
   by the trap handler but are not preserved by the garbage collector.  */
{
    gSaveVec->init();
    
    unsigned rd = (instr >> 25) & 31; /* Destination register. */

    Handle arg1 = gSaveVec->push(AddOne(*(get_reg((instr >> 14) & 31))));
    Handle arg2;
    
    if (instr & 0x2000)
    {
        /* Immediate - the argument is the value in the instruction with
            the bottom tag bit set. */
        if (instr & 0x1000)
            arg2 = gSaveVec->push(PolyWord::FromSigned(-((POLYSIGNED)instr & 0xfff) + 1));
        else
            arg2 = gSaveVec->push(PolyWord::FromUnsigned((instr & 0xfff) + 1));
    }
    else 
        arg2 = gSaveVec->push(AddOne(*(get_reg(instr & 31))));
    
    if (rd == 0) /* g0 */
    {
        /* If we are putting the result into g0 it must be because we are
           interested in the condition code.  We do a comparison rather than
           a subtraction. */
        
        int r = compareLong(arg2, arg1);
        // Put the result of the comparison in the condition code field.  We compare
        // this with zero in MD_switch_to_poly to actually set the codes.
        poly_stack->p_reg[CHECKED_REGS+3] = PolyWord::FromUnsigned(r);
    }
    else if ((instr & 0x2000) == 0)
    {
        /* We have to be careful here.  Multiplication is done by repeated addition
           using values in i4 and i5.  They are assumed to have had 1 subtracted from
           them, but if we have a garbage collection while doing the long-precision
           operation they may be left pointing in the wrong place.  We save the
           values +1 on the save vec so they will be updated if they are addresses,
           and put them back just before putting in the result. */
        Handle i4v = gSaveVec->push(AddOne(poly_stack->p_reg[CHECKED_REGS+1]));
        Handle i5v = gSaveVec->push(AddOne(poly_stack->p_reg[CHECKED_REGS+2]));
        
        Handle res;
        if (instr & 0x80000) 
            res = sub_longc(arg2, arg1);
        else
            res = add_longc(arg2, arg1);
        
        /* Put back the values into i4 and i5, and then put in the result (into 
           either i4 or i5). */
        poly_stack->p_reg[CHECKED_REGS+1] = SubOne(i4v->Word());
        poly_stack->p_reg[CHECKED_REGS+2] = SubOne(i5v->Word());
        *(get_reg(rd)) = SubOne(res->Word());
    }
    else { /* Adding or subtracting a constant - can't be doing a multiplication
              and we must not save i4 or i5 because only one of them is being
              used in this operation and the other may contain anything. */
        Handle res;
        
        if (instr & 0x80000)
            res = sub_longc(arg2, arg1);
        else
            res = add_longc(arg2, arg1);
        /* Subtract 1 from the result (we will add it back in the next instr),
           and put it in the destination register. */
        *(get_reg(rd)) = SubOne(res->Word());
    }
}


bool SparcDependent::TrapHandle(void)
/* Called from MD_trap_handler after registers have been saved in poly_stack. */
{
    memRegisters.inRTS = 1;
    
    POLYUNSIGNED instr = *(POLYUNSIGNED*)poly_stack->p_pc; /* instruction that trapped. */
    
    /* Trap instructions can be as a result of stack or heap overflow,
       or may be caused by arithmetic overflows when using tagged numbers.
       Various different traps are in use.
       tlu 16 occurs as a result of stack overflow.
       tvs 16 and tnz 16 are a result of tagged PolyWord overflow.
       tsubcctv %g7,?,%g7 occurs as a result of storage allocation.
       taddcctv and tsubcctv occur as a result of arithmetic overflow. */
    
    /* Skip over the trap instruction. */
    poly_stack->p_pc += 4;
    
    if (instr == 0x8bd02010)  /* tlu 16 is stack overflow */
    {
        /* Check the size of the stack and expand if necessary. */
        /* We need to examine the previous instruction */
        /* in order to work out the required amount of space. */
        
        instr = *(POLYUNSIGNED*)(poly_stack->p_pc-8);  /* get previous instruction */
        
        if (instr == 0x80a10005) /* cmp %g4, %g5 is normal stack check */
            check_current_stack_size(poly_stack->p_sp); /* may allocate */
        else if (instr == 0x80a74005) /* cmp %i5, %g5 is large stack check */
            check_current_stack_size(poly_stack->p_reg[CHECKED_REGS+2].AsStackAddr());
        else Crash ("Bad stack check sequence"); /* may allocate */
        
        // Now handle any interrupts. This may switch processes.
        return true;
    }
    else if ((instr & 0xfffff000) == 0x8f19e000 /* tsubcctv %g7,len,%g7 */ ||
        (instr & 0xffffe01f) == 0x8f19c01d /* tsubcctv %g7,%i5,%g7 */)
    {
        POLYUNSIGNED len;
        if ((instr & 0xfffff000) == 0x8f19e000) 
            len = instr & 0xfff;
        else
            len = poly_stack->p_reg[CHECKED_REGS+2].AsUnsigned();

        len = len / sizeof(PolyWord);
        
        /* printf ("Trap:0x%08x = tsubcctv %%g7,%d,%%g7\n",instr,len); */
        
        if (store_profiling)
            add_count(poly_stack->p_pc, poly_stack->p_sp, len);
        else
            if (allocSpace->pointer >= allocSpace->bottom && ! (userOptions.debug & DEBUG_REGION_CHECK))
                Crash ("Spurious %%g7 trap");
            
            if (allocSpace->pointer < allocSpace->bottom)
            {
                /* Put back allocSpace->pointer (garbage collector expects allocSpace->pointer >= allocSpace->bottom) */
                
                allocSpace->pointer += len;
                
                if (allocSpace->pointer < allocSpace->bottom)
                    Crash ("Bad length in %%g7 trap");
                // See if we have another space to satisfy the request.
                allocSpace = gMem.GetAllocSpace(len);
                if (allocSpace == 0)
                {
                    if (! QuickGC(len) ) /* Garbage-collect. */
                    {
                        fprintf(stderr,"Run out of store - interrupting console processes\n");
                        processes->interrupt_console_processes();
                        throw IOException(EXC_RETRY);
                    }
                }
                allocWords = len; // Actually allocate it in SetMemRegisters.
            }
            
            /* QuickGC will check that there is space for the object so
               we can allocate it just by decrementing allocSpace->pointer. g6 will be
               loaded with allocSpace->pointer+1 in switch_to_poly and so will
               point to this new object. g7 is reloaded with the amount
               of free space. The result will be to continue as
               though the allocation had happened without a garbage-collect. */
    }
    else if ((instr & 0xc1f00000) == 0x81100000 /* tsubcctv or taddcctv */)
    {
        if (emulate_profiling)
            add_count(poly_stack->p_pc, poly_stack->p_sp, 1);
        
        emulate_trap(instr);
    }
    else if (instr == 0x8fd02010 || instr == 0x93d02010 /* tvs 16 or tnz 16 */)
        raise_exception0(EXC_size);
    
    else Crash("Bad trap pc=%p, instr=%08x",poly_stack->p_pc-4,instr);
    return false;
}

static void add_function_to_io_area(int x, int (*y)())
{
    add_word_to_io_area(x, PolyWord::FromUnsigned((POLYUNSIGNED)y));
}

void SparcDependent::InitInterfaceVector(void)
{
    memRegisters.inRTS = 1; // We start off in the RTS

    add_function_to_io_area(POLY_SYS_exit, &finisha);
    add_function_to_io_area(POLY_SYS_install_root, &install_roota);
    add_function_to_io_area(POLY_SYS_strconcat, &strconcata);
    add_function_to_io_area(POLY_SYS_alloc_store, &alloc_store);
    add_function_to_io_area(POLY_SYS_chdir, &change_dira);
    add_function_to_io_area(POLY_SYS_get_length, &get_length_a);
    add_function_to_io_area(POLY_SYS_get_flags, &get_flags_a);
    add_function_to_io_area(POLY_SYS_str_compare, &str_comparea);
    add_function_to_io_area(POLY_SYS_teststreq, &teststreq);
    add_function_to_io_area(POLY_SYS_teststrneq, &teststrneq);
    add_function_to_io_area(POLY_SYS_teststrgtr, &teststrgtr);
    add_function_to_io_area(POLY_SYS_teststrlss, &teststrlss);
    add_function_to_io_area(POLY_SYS_teststrgeq, &teststrgeq);
    add_function_to_io_area(POLY_SYS_teststrleq, &teststrleq);
    add_function_to_io_area(POLY_SYS_exception_trace, &exception_tracea);
    add_function_to_io_area(POLY_SYS_lockseg, &locksega);
    add_function_to_io_area(POLY_SYS_profiler, &profilera);
    add_function_to_io_area(POLY_SYS_is_short, &is_shorta);
    add_function_to_io_area(POLY_SYS_aplus, &add_long);
    add_function_to_io_area(POLY_SYS_aminus, &sub_long);
    add_function_to_io_area(POLY_SYS_amul, &mult_long);
    add_function_to_io_area(POLY_SYS_adiv, &div_longa);
    add_function_to_io_area(POLY_SYS_amod, &rem_longa);
    add_function_to_io_area(POLY_SYS_aneg, &neg_long);
    add_function_to_io_area(POLY_SYS_equala, &equal_long);
    add_function_to_io_area(POLY_SYS_ora, &or_long);
    add_function_to_io_area(POLY_SYS_anda, &and_long);
    add_function_to_io_area(POLY_SYS_xora, &xor_long);
    add_function_to_io_area(POLY_SYS_Real_str, &Real_stra);
    add_function_to_io_area(POLY_SYS_Real_geq, &Real_geqa);
    add_function_to_io_area(POLY_SYS_Real_leq, &Real_leqa);
    add_function_to_io_area(POLY_SYS_Real_gtr, &Real_gtra);
    add_function_to_io_area(POLY_SYS_Real_lss, &Real_lssa);
    add_function_to_io_area(POLY_SYS_Real_eq, &Real_eqa);
    add_function_to_io_area(POLY_SYS_Real_neq, &Real_neqa);
    add_function_to_io_area(POLY_SYS_Real_Dispatch, &Real_dispatcha);
    add_function_to_io_area(POLY_SYS_Add_real, &Real_adda);
    add_function_to_io_area(POLY_SYS_Sub_real, &Real_suba);
    add_function_to_io_area(POLY_SYS_Mul_real, &Real_mula);
    add_function_to_io_area(POLY_SYS_Div_real, &Real_diva);
    add_function_to_io_area(POLY_SYS_Neg_real, &Real_nega);
    add_function_to_io_area(POLY_SYS_Repr_real, &Real_repra);
    add_function_to_io_area(POLY_SYS_conv_real, &Real_conva);
    add_function_to_io_area(POLY_SYS_real_to_int, &Real_inta);
    add_function_to_io_area(POLY_SYS_int_to_real, &Real_floata);
    add_function_to_io_area(POLY_SYS_sqrt_real, &Real_sqrta);
    add_function_to_io_area(POLY_SYS_sin_real, &Real_sina);
    add_function_to_io_area(POLY_SYS_cos_real, &Real_cosa);
    add_function_to_io_area(POLY_SYS_arctan_real, &Real_arctana);
    add_function_to_io_area(POLY_SYS_exp_real, &Real_expa);
    add_function_to_io_area(POLY_SYS_ln_real, &Real_lna);
    add_function_to_io_area(POLY_SYS_io_operation, &io_operationa);
    add_function_to_io_area(POLY_SYS_fork_process, &fork_processa);
    add_function_to_io_area(POLY_SYS_choice_process, &choice_processa);
    add_function_to_io_area(POLY_SYS_int_process, &int_processa);
    add_function_to_io_area(POLY_SYS_send_on_channel, &send_on_channela);
    add_function_to_io_area(POLY_SYS_receive_on_channel, &receive_on_channela);
    add_function_to_io_area(POLY_SYS_offset_address, &offset_address);
    add_function_to_io_area(POLY_SYS_shift_right_word, &shift_right_word);
    add_function_to_io_area(POLY_SYS_word_neq, &word_neq);
    add_function_to_io_area(POLY_SYS_not_bool, &not_bool);
    add_function_to_io_area(POLY_SYS_string_length, &string_length);
    add_function_to_io_area(POLY_SYS_int_eq, &int_eq);
    add_function_to_io_area(POLY_SYS_int_neq, &int_neq);
    add_function_to_io_area(POLY_SYS_int_geq, &int_geq);
    add_function_to_io_area(POLY_SYS_int_leq, &int_leq);
    add_function_to_io_area(POLY_SYS_int_gtr, &int_gtr);
    add_function_to_io_area(POLY_SYS_int_lss, &int_lss);
    add_function_to_io_area(POLY_SYS_string_sub, &string_suba); // This is really redundant.
    add_function_to_io_area(POLY_SYS_or_word, &or_word);
    add_function_to_io_area(POLY_SYS_and_word, &and_word);
    add_function_to_io_area(POLY_SYS_xor_word, &xor_word);
    add_function_to_io_area(POLY_SYS_shift_left_word, &shift_left_word);
    add_function_to_io_area(POLY_SYS_word_eq, &word_eq);
    add_function_to_io_area(POLY_SYS_load_byte, &load_byte);
    add_function_to_io_area(POLY_SYS_load_word, &load_word);
    add_function_to_io_area(POLY_SYS_is_big_endian, &is_big_endian);
    add_function_to_io_area(POLY_SYS_bytes_per_word, &bytes_per_word);
    add_function_to_io_area(POLY_SYS_assign_byte, &assign_byte);
    add_function_to_io_area(POLY_SYS_assign_word, &assign_word);
    add_function_to_io_area(POLY_SYS_objsize, & objsize_a ); /* MJC 27/04/88 */
    add_function_to_io_area(POLY_SYS_showsize,& showsize_a); /* MJC 09/03/89 */
    add_function_to_io_area(POLY_SYS_timing_dispatch, & timing_dispatch_a); /* DCJM 10/4/00 */
    add_function_to_io_area (POLY_SYS_interrupt_console_processes,
                        & interrupt_console_processes_a); /* MJC 01/08/90 */
    add_function_to_io_area(POLY_SYS_install_subshells, & install_subshells_a); /* MJC 12/09/90 */
    add_function_to_io_area(POLY_SYS_XWindows,& XWindows_a); /* MJC 27/09/90 */
    add_function_to_io_area(POLY_SYS_full_gc,     & full_gc_a);     /* MJC 18/03/91 */
    add_function_to_io_area(POLY_SYS_stack_trace, & stack_trace_a); /* MJC 18/03/91 */
    add_function_to_io_area(POLY_SYS_foreign_dispatch, &foreign_dispatch_a); /* NIC 22/04/94 */
    add_function_to_io_area(POLY_SYS_callcode_tupled,  &callcode_tupleda);    /* SPF 07/07/94 */
    add_function_to_io_area(POLY_SYS_process_env,      &process_env_dispatch_a); /* DCJM 25/4/00 */
    add_function_to_io_area(POLY_SYS_set_string_length, &set_string_length_a); /* DCJM 28/2/01 */
    add_function_to_io_area(POLY_SYS_get_first_long_word, &get_first_long_word_a); /* DCJM 28/2/01 */
    add_function_to_io_area(POLY_SYS_shrink_stack,     &shrink_stack_a);     /* SPF  9/12/96 */
    add_function_to_io_area(POLY_SYS_code_flags,       &set_flags_a);        /* SPF 12/02/97 */
    add_function_to_io_area(POLY_SYS_shift_right_arith_word, &shift_right_arith_word); /* DCJM 10/10/99 */
    add_function_to_io_area(POLY_SYS_int_to_word,      &int_to_word);        /* DCJM 10/10/99 */
    add_function_to_io_area(POLY_SYS_set_code_constant,&set_code_constanta); /* DCJM 2/1/01 */
    add_function_to_io_area(POLY_SYS_move_bytes,       &move_bytes);        /* DCJM 10/10/99 */
    add_function_to_io_area(POLY_SYS_move_words,       &move_words);        /* DCJM 10/10/99 */
    add_function_to_io_area(POLY_SYS_mul_word,         &mul_word);        /* DCJM 10/10/99 */
    add_function_to_io_area(POLY_SYS_plus_word,        &plus_word);        /* DCJM 10/10/99 */
    add_function_to_io_area(POLY_SYS_minus_word,       &minus_word);        /* DCJM 10/10/99 */
    add_function_to_io_area(POLY_SYS_div_word,         &div_worda);        /* DCJM 10/10/99 */
    add_function_to_io_area(POLY_SYS_mod_word,         &mod_worda);        /* DCJM 10/10/99 */
    add_function_to_io_area(POLY_SYS_word_geq,         &word_geq);
    add_function_to_io_area(POLY_SYS_word_leq,         &word_leq);
    add_function_to_io_area(POLY_SYS_word_gtr,         &word_gtr);
    add_function_to_io_area(POLY_SYS_word_lss,         &word_lss);

    add_function_to_io_area(POLY_SYS_io_dispatch, &IO_dispatch_a); /* DCJM 8/5/00 */
    add_function_to_io_area(POLY_SYS_network, &Net_dispatch_a); /* DCJM 8/5/00 */
    add_function_to_io_area(POLY_SYS_os_specific, &OS_spec_dispatch_a); /* DCJM 8/5/00 */
    add_function_to_io_area(POLY_SYS_signal_handler, &Sig_dispatch_a); /* DCJM 18/7/00 */
    add_function_to_io_area(POLY_SYS_poly_specific, &poly_dispatch_a); /* DCJM 17/6/06 */

    // This is now a "closure" like all the other entries.
    add_function_to_io_area(POLY_SYS_raisex,           raisex);

    // Set the raiseException entry to point to the assembly code.
    memRegisters.raiseException = (byte*)raisex;

    // Entry point to save the state for an IO call.  This is the common entry
    // point for all the return and IO-call cases.
    memRegisters.ioEntry = (byte*)SparcAsmSaveStateAndReturn;

    // Set up the signal handlers.
    {

      /* Solaris 2 */
      struct sigaction catchvec;
      
      /* disable all interrupts while we are in the interupt handler  */
      sigfillset(&catchvec.sa_mask);
      catchvec.sa_flags = SA_ONSTACK | SA_SIGINFO | SA_RESTART;
      catchvec.sa_sigaction = catchEMT;
      assert(sigaction(SIGEMT, &catchvec, 0) == 0);
      markSignalInuse(SIGEMT);

      sigfillset(&catchvec.sa_mask);
      catchvec.sa_flags = SA_ONSTACK | SA_SIGINFO | SA_RESTART;
      catchvec.sa_sigaction = catchILL;
      assert(sigaction(SIGILL, &catchvec, 0) == 0);
      markSignalInuse(SIGILL);

      /* SIGSEGV seems to be generated by the trap instructions in Solaris. */
      sigemptyset(&catchvec.sa_mask);
      catchvec.sa_flags = SA_ONSTACK | SA_SIGINFO;
      catchvec.sa_sigaction = catchSEGV;
      assert(sigaction(SIGSEGV, &catchvec, 0) == 0);
      markSignalInuse(SIGSEGV);
    }
}

// Build an ML code segment on the heap to hold a copy of a piece of code
Handle SparcDependent::BuildCodeSegment(const unsigned *code, unsigned codeWords, char functionName)
{
    POLYUNSIGNED words = codeWords + 6;
    Handle codeHandle = alloc_and_save(words, F_CODE_BIT);
    byte *cp = codeHandle->Word().AsCodePtr();
    memcpy(cp, code, codeWords*sizeof(PolyWord));
    codeHandle->WordP()->Set(codeWords++, PolyWord::FromUnsigned(0)); // Marker word
    codeHandle->WordP()->Set(codeWords, PolyWord::FromUnsigned(codeWords*sizeof(PolyWord))); // Bytes to the start
    codeWords++;
    codeHandle->WordP()->Set(codeWords++, PolyWord::FromUnsigned(0)); // Profile count
    codeHandle->WordP()->Set(codeWords++, TAGGED(functionName)); // Name of function - single character
    codeHandle->WordP()->Set(codeWords++, TAGGED(0)); // Register set
    codeHandle->WordP()->Set(codeWords++, PolyWord::FromUnsigned(2)); // Number of constants
    FlushInstructionCache(cp, codeWords*sizeof(PolyWord));
    return codeHandle;
}

// Generate a code sequence to enter the RTS with a request to kill
// the current process (thread).  This is slightly different to the normal
// RTS call sequence because it is entered as though some previous code had
// RETURNED to it.  That's why we need two nops at the start and don't adjust
// %o7.
static unsigned killSelfCode[] =
{
    0x01000000, // nop
    0x01000000, // nop
    0xf8062024, // ld [36+%i0].%i4
    0xb6102054, // mov POLY_SYS_kill_self,%i3
    0x81c70000, // jmp %i4
    0xf6262004  // st %i3,[%i0+4] ! DELAY SLOT
};

// We need the kill-self code in a little function.
Handle SparcDependent::BuildKillSelfCode(void)
{
    return BuildCodeSegment(killSelfCode, sizeof(killSelfCode)/sizeof(unsigned), 'K');
}

void SparcDependent::SetException(StackObject *stack, poly_exn *exc)
/* Set up the stack of a process to raise an exception. */
{
    stack->p_reg[OFFSET_REGCLOSURE] = (PolyObject*)IoEntry(POLY_SYS_raisex);
    stack->p_pc   = PC_RETRY_SPECIAL;
    stack->p_reg[OFFSET_REGRESULT] = exc;
}

/******************************************************************************/
/*                                                                            */
/*      MD_reset_signals - called by run_time.c                               */
/*                                                                            */
/******************************************************************************/
void SparcDependent::ResetSignals(void)
{
    /* restore default signal handling in child process after a "fork". */
    signal(SIGFPE,  SIG_DFL);
    signal(SIGEMT,  SIG_DFL);
    signal(SIGILL,  SIG_DFL);
    
    /* "just in case" */
    signal(SIGBUS,  SIG_DFL);
    signal(SIGSEGV, SIG_DFL);
}

void SparcDependent::ScanConstantsWithinCode(PolyObject *addr, PolyObject *old,
                                               POLYUNSIGNED length, ScanAddress *process)
{
    PolyWord *pt = (PolyWord*)addr;
    PolyWord *end;
    POLYUNSIGNED unused;
    /* Find the end of the code (before the constants). */
    addr->GetConstSegmentForCode(length, end, unused);
    end -= 3;
    assert(end->AsUnsigned() == 0); /* This should be the marker PolyWord. */
    
    while (pt != end)
    {
        POLYUNSIGNED instr = (*pt).AsUnsigned();
        if ((instr & 0xc1c00000) == 0x01000000) /* sethi instr. */
        {
            unsigned reg = (instr >> 25) & 31;
            // If the register is %i3, %i4 or %i5 the value is an integer even
            // if it is untagged.  If it's %g0 this is simply a nop.
            if (reg < 27 && reg != 0)
            {
                /* Next must be an ADD instruction. */
                assert((pt[1].AsUnsigned() & 0xc1f83000) == 0x80002000);
                /* Process this address. */
                process->ScanConstant((byte*)pt, PROCESS_RELOC_SPARCDUAL);
                pt++; // Skip the second word.
            }
        }
        else if ((instr & 0xc0000000) == 0x40000000) /* Call instr. */
        {
            POLYSIGNED disp = instr & 0x3fffffff; // Word displacement.
            // We're assuming here that multiplying by 4 will turn an unsigned value into a signed.
            PolyWord *absAddr = pt + disp;
            // Ignore it if it's local to this code seg.
            if (! (absAddr >= (PolyWord*)addr && absAddr < end))
            {
                /* The old value of the displacement was relative to the old address.  */
                absAddr = absAddr - (PolyWord*)addr + (PolyWord*)old;
                // We have to correct the displacement for the new location and store
                // that away before we call ScanConstant.
                POLYSIGNED newDisp = absAddr - pt;
                *pt = PolyWord::FromUnsigned((newDisp & 0x3fffffff) | 0x40000000);
                process->ScanConstant((byte*)pt, PROCESS_RELOC_SPARCRELATIVE);
            }
        }
        pt++;
    }
}

/* Store a constant at a specific address in the code.  This is used by
   the code generator.  It needs to be built into the RTS because we
   have to split the value in order to store it into two instructions.
   Separately the two values might well be invalid addresses. */
void SparcDependent::SetCodeConstant(Handle data, Handle constant, Handle offseth, Handle base)
{
    /* The offset is a byte count. */
    unsigned offset = get_C_ulong(DEREFWORD(offseth));
    POLYUNSIGNED *pointer = (POLYUNSIGNED*)(DEREFWORD(base).AsCodePtr() + offset);
    assert((offset & 3) == 0);
    if (pointer[0] == 0x40000000) /* Call instruction. */
    {
        POLYUNSIGNED *c = (POLYUNSIGNED*)(DEREFWORD(constant).AsCodePtr());
        int disp = c - pointer; /* Signed displacement in words. */
        pointer[0] = (disp & 0x3fffffff) | 0x40000000;
    }
    else
    {
        POLYUNSIGNED c = DEREFWORD(constant).AsUnsigned(); // N.B.  This may well really be an address.
        unsigned hi = c >> 10, lo = c & 0x3ff;
        /* The first PolyWord must be SETHI. */
        assert((pointer[0] & 0xc1ffffff) == 0x01000000);
        /* and the next must be ADD.  The immediate value must currently be tagged(0). */
        assert((pointer[1] & 0xc1f83fff) == 0x80002001);
        pointer[0] |= hi;
        pointer[1] = (pointer[1] & 0xfffff000) | lo;
    }
}

void SparcDependent::FlushInstructionCache(void *p, POLYUNSIGNED bytes)
{
    SparcAsmFlushInstructionCache(p, bytes);
}

extern "C" int registerMaskVector[];

int SparcDependent::GetIOFunctionRegisterMask(int ioCall)
{
    return registerMaskVector[ioCall];
}

static SparcDependent sparcDependent;

MachineDependent *machineDependent = &sparcDependent;
