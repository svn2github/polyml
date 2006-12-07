/*
    Title:  Machine dependent code for Power architecture.

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

#ifdef WIN32
#include "winconfig.h"
#else
#include "config.h"
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif

#ifdef HAVE_ASSERT_H
#include <assert.h>
#define ASSERT(x) assert(x)
#else
#define ASSERT(x)
#endif

#ifdef HAVE_MCONTEXT_T_REGS
// Linux
#define NIP(scp)    (scp)->uc_mcontext.regs->nip
#define GPR27(scp)  (scp)->uc_mcontext.regs->gpr[PT_R27]
#define GPR28(scp)  (scp)->uc_mcontext.regs->gpr[PT_R28]

#elif defined(HAVE_STRUCT_PPC_THREAD_STATE)
/* Mac OS X 10.2 and, hopefully, later. */
#define NIP(scp)    scp->uc_mcontext->ss.srr0
#define GPR27(scp)  scp->uc_mcontext->ss.r27
#define GPR28(scp)  scp->uc_mcontext->ss.r28

#elif defined(HAVE_MCONTEXT_T_GREGS)
// Linux (new)
#define NIP(scp)    (scp)->uc_mcontext.uc_regs->gregs[PT_NIP]
#define GPR27(scp)  (scp)->uc_mcontext.uc_regs->gregs[PT_R27]
#define GPR28(scp)  (scp)->uc_mcontext.uc_regs->gregs[PT_R28]

#else

/* Before OS X 10.2 signal handlers were given sigcontext structures. */
// This may need testing
#define NIP(scp)    scp->sc_ir
#define GPR27(scp)  (((int*)(scp)->sc_regs)[27+2])
#define GPR28(scp)  (((int*)(scp)->sc_regs)[28+2])
#endif

// R27 is used as the ML stack pointer and R28 as the ML stack limit register.
#define GPSP(scp)   GPR27(scp)
#define GPSL(scp)   GPR28(scp)

#include "globals.h"
#include "gc.h"
#include "run_time.h"
#include "mpoly.h"
#include "arb.h"
#include "int_opcodes.h"
#include "reals.h"
#include "diagnostics.h"
#include "processes.h"
#include "sighandler.h"
#include "machine_dep.h"
#include "profiling.h"
#include "save_vec.h"
#include "scanaddrs.h"
#include "sys.h"
#include "check_objects.h"
#include "memmgr.h"

// Values for the returnReason
enum RETURN_REASON {
    RETURN_IO_CALL = 0,
    RETURN_HEAP_OVERFLOW,
    RETURN_STACK_OVERFLOW,
    RETURN_STACK_OVERFLOWEX,
    RETURN_RAISE_DIV,
    RETURN_ARB_EMULATION
};

// Registers saved in poly_stack.
#define NUMCHECKED      21  // Number of checked registers.
#define NUMUNCHECKED    3   // Number of unchecked registers

// These registers must contain valid PolyWord values.  Any addresses
// may be updated as the result of a garbage collection.
#define PS_R3   p_reg[0] // First argument and function result - r3
#define PS_R4   p_reg[1] // Second argument - r4
#define PS_R5   p_reg[2] // Third argument - r5
#define PS_R6   p_reg[3] // Fourth argument - r6
#define PS_R7   p_reg[4] // r7
#define PS_R8   p_reg[5] // r8
#define PS_R9   p_reg[6] // r9
#define PS_R10  p_reg[7] // r10
#define PS_R14  p_reg[8] // r14
#define PS_R15  p_reg[9] // r15
#define PS_R16  p_reg[10] // r16
#define PS_R17  p_reg[11] // r17
#define PS_R18  p_reg[12] // r18
#define PS_R19  p_reg[13] // r19
#define PS_R20  p_reg[14] // r20
#define PS_R21  p_reg[15] // r21
#define PS_R22  p_reg[16] // r22
#define PS_R23  p_reg[17] // r23
#define PS_R24  p_reg[18] // r24 - Closure register
#define PS_R25  p_reg[19] // r25 - Return register
#define PS_LR   p_reg[20] // lr - Link register

// These registers can contain values that are not valid PolyWord values.
// They are copied unchanged if the stack is moved so must not contain addresses.
#define PS_R11  p_reg[NUMCHECKED+1] // r11 - untagged register
#define PS_R12  p_reg[NUMCHECKED+2] // r12 - untagged register
#define PS_CR0  p_reg[NUMCHECKED+3] // Condition code register - untagged


#define EXTRA_STACK 0

/* the amount of ML stack space to reserve for registers,
   C exception handling etc. The compiler requires us to
   reserve 2 stack-frames worth (2 * 20 words) plus whatever
   we require for the register save area. We actually reserve
   slightly more than this. SPF 3/3/97
*/
#define OVERFLOW_STACK_SIZE \
  (50 + \
   sizeof(StackObject)/sizeof(PolyWord) + \
   NUMCHECKED + \
   NUMUNCHECKED + \
   EXTRA_STACK)


// This vector is pointed to by r13 
static struct MemRegisters {
    int inRTS;
        /* This is set when poly_stack->p_pc and poly_stack->p_sp are set */
    int requestCode;
    int returnReason;

    PolyWord    *heapPointer;
    PolyWord    *heapBase;
    StackObject *polyStack;
    PolyWord    *stackLimit;
    byte        *heapOverflow;
    byte        *stackOverflow;
    byte        *stackOverflowEx;
    byte        *raiseException;
    byte        *ioEntry; /* Offset 44 */
    byte        *raiseDiv;
    byte        *arbEmulation;
    PolyWord    *stackTop; // Used in "raisex"
} memRegisters;

class PowerPCDependent: public MachineDependent {
public:
    PowerPCDependent(): allocSpace(0), allocWords(0) {}
    virtual void InitStackFrame(Handle stack, Handle proc, Handle arg);
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
    virtual bool InRunTimeSystem(void) { return memRegisters.inRTS != 0; }
    virtual void CallIO0(Handle(*ioFun)(void));
    virtual void CallIO1(Handle(*ioFun)(Handle));
    virtual void CallIO2(Handle(*ioFun)(Handle, Handle));
    virtual void CallIO3(Handle(*ioFun)(Handle, Handle, Handle));
    virtual void CallIO4(Handle(*ioFun)(Handle, Handle, Handle, Handle));
    virtual void CallIO5(Handle(*ioFun)(Handle, Handle, Handle, Handle, Handle));
    virtual Handle CallBackResult(void);
    virtual void SetExceptionTrace(void);
    virtual void CallCodeTupled(void);
    virtual void SetCodeConstant(Handle data, Handle constant, Handle offseth, Handle base);
    virtual void FlushInstructionCache(void *p, POLYUNSIGNED bytes);
    virtual Architectures MachineArchitecture(void) { return MA_PPC; } 

private:

    void StartIOCall(void);
    void SetMemRegisters(void);
    void HeapOverflowTrap(void);
    void ArbitraryPrecisionTrap(void);

    Handle BuildCodeSegment(const byte *code, unsigned codeWords, char functionName);
    Handle BuildKillSelfCode(void);

    LocalMemSpace *allocSpace;
    POLYUNSIGNED allocWords;
};


// Functions in the assembly code segment.
extern "C" {
extern int str_compare();
extern int teststreq();
extern int teststrneq();
extern int teststrgtr();
extern int teststrlss();
extern int teststrgeq();
extern int teststrleq();
extern int locksega();
extern int add_long();
extern int sub_long(); 
extern int mult_long();
extern int div_long();
extern int rem_long();
extern int neg_long();
extern int or_long();
extern int and_long();
extern int xor_long();
extern int equal_long();
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
extern int or_word();
extern int and_word();
extern int xor_word();
extern int shift_left_word();
extern int word_eq();
extern int load_byte();
extern int load_word();
extern int assign_byte();
extern int assign_word();
extern int kill_selfa();
extern int alloc_store();
extern int get_length_a();
extern int int_to_word();
extern int move_bytes();
extern int move_words();
extern int shift_right_arith_word();
extern int raisex();
extern int extrace_normal_return();
extern int offset_address();
extern int is_shorta();
extern int is_big_endian();
extern int bytes_per_word();
extern int mul_word();
extern int plus_word();
extern int minus_word();
extern int div_word();
extern int mod_word();
extern int word_geq();
extern int word_leq();
extern int word_gtr();
extern int word_lss();
extern int set_string_length_a();
extern int get_first_long_word_a();
extern void PPCAsmSwitchToPoly(struct MemRegisters *);
extern void MD_flush_instruction_cache(void *p, POLYUNSIGNED bytes);
extern void PPCSaveStateAndReturn(void);
};

/******************************************************************************/
/*                                                                            */
/*      Stack Frame                                                           */
/*                                                                            */
/******************************************************************************/

/*

What do we store in the ML stack object?

  p_space
  p_pc
  p_sp (r30)
  p_hr (r31)
  p_nregs = 27
  20 tagged registers (r3-r10, r14-r25)
  the literal 3
  3 untagged registers (r11, r12, CR)
  the real ML stack
  
Register Usage
--------------

r0 scratch register (unsaved?)
r1 - don't touch - dedicated C register (stack - like SPARC %o6) 
r2 - don't touch - dedicated C register (TOC) (Is the relevant now?)
r3      used for the first argument to a function, and for the result.
r4-r6  used for the next 3 args, any others being passed on the stack.
(We may later decide to use r7-r10 for parameters too).
 
r24        is the closure pointer or static link pointer
r25 (rr)   is used as the compiler-visible link register
r26        is an RTS scratch register (unsaved).
r27 (rsp)  is the ML stack pointer,
r28 (rsl)  is the stack limit,
r29 (rhp)  is the heap pointer,
r30 (rhl)  is the heap limit,
r31 (rhr)  points to the top exception handler.

r7-r10 and r14-r23 (15 registers) are available as general work registers,
as are r3-r6 and r24-r25, when they are not fulfilling their specialised
duties. That's a total of 20 general-purpose registers (as opposed to
17 on the SPARC).

r11, r12 are used as code-generator visible untagged registers.
r26 is used as a compiler-invisible RTS scratch register for
handling traps.

LR is a semi-volatile register used to cache the return address.
Executing a trap can copy rr into LR, even if it wasn't
there before. Furthermore, it can change the tagged status of
LR - it may be tagged after the trap, even if it was untagged
before. What we do guarantee is that if LR cached rr before
the trap, then it caches it afterwards (modulo possible tag bit
discrepancies). Executing any function call (including an RTS
call) invalidates both rr and LR.

CTR is a volatile register - we use it to to return from traps
and normal RTS calls, and it is also used to implement tail-calls.

*/
#define ML_HR 31
#define ML_HL 30
#define ML_HP 29
#define ML_SL 28
#define ML_SP 27

/* The first instruction executed after native code returns
   is the saved return address -2 */ 
#define RETURNOFFSET 2

/* The first instruction executed in a handler
   is the stored address -2 */
#define HANDLEROFFSET 2

#ifndef NOISY
#define NOISY 0
#endif
#define NOTIFYGC 0

// InitStackFrame.  Set up a stack frame for a new thread (process).
void PowerPCDependent::InitStackFrame(Handle stackh, Handle proc, Handle arg)
/* Initialise stack frame. */
{
    StackObject *stack = (StackObject *)DEREFWORDHANDLE(stackh);
    POLYUNSIGNED stack_size = stack->Length();
    stack->p_space = OVERFLOW_STACK_SIZE;
    stack->p_pc = PC_RETRY_SPECIAL; /* As if we had called MD_set_for_retry. */
    stack->p_nreg = NUMCHECKED; /* r3-r8, r13-r25, link */
    stack->p_hr = stack->Offset(stack_size-3);
    stack->p_sp  = stack->Offset(stack_size-3); /* sp */
    /* Reset all registers since this may be an old stack frame */
    for (unsigned i = 0; i < NUMCHECKED; i++)
        stack->p_reg[i] = TAGGED(0);
        
    // Set the default handler and return address to point to this code.
    // We have to offset the addresses by two bytes because the code to enter and a handler
    // or return from a function always subtracts two.
    Handle killCode = BuildKillSelfCode();
    PolyWord killJump = PolyWord::FromUnsigned(killCode->Word().AsUnsigned() | HANDLEROFFSET);
    stack = (StackObject *)DEREFWORDHANDLE(stackh); // In case it's moved

    stack->PS_R24 = DEREFWORD(proc); /* Set r24 to the closure address. */
    stack->PS_LR = killJump; /* Set link reg to code to kill process. */
    stack->p_reg[NUMCHECKED] = PolyWord::FromUnsigned(NUMUNCHECKED); /* 3 unchecked registers. */
    /* Since they're unchecked, they don't need to be initialised,
       but set them to 0 anyway. */
    stack->PS_R11 = PolyWord::FromUnsigned(0);
    stack->PS_R12 = PolyWord::FromUnsigned(0);
    stack->PS_CR0 = PolyWord::FromUnsigned(0);
    /* If this function takes an argument store it in the argument register. */
    if (arg != 0) stack->PS_R3 = DEREFWORD(arg);
    /* No previous handler so point it at itself. */
    stack->Set(stack_size-1, PolyWord::FromStackAddr(stack->Offset(stack_size-1)));

    /* Default handler. */
    stack->Set(stack_size-2, killJump);
    /* Set up exception handler */
    stack->Set(stack_size-3, TAGGED(0)); /* Default handler. */
}

// These are just for debugging.  They record the last point before
// the memory was checked.
byte *lastPC;
int lastRequest, lastReason;
PolyWord *lastBase;

// SwitchToPoly - start running Poly code.  
int PowerPCDependent::SwitchToPoly(void)
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

        PPCAsmSwitchToPoly(&memRegisters); // Load registers.  Returns as a result of an RTS call.
        allocSpace->pointer = memRegisters.heapPointer; // Get updated limit pointer.
        allocWords = 0;

        switch (memRegisters.returnReason)
        {
        case RETURN_IO_CALL:
            // The ML code wants an IO call.
            return memRegisters.requestCode;

        case RETURN_HEAP_OVERFLOW:
            try {
                // The heap has overflowed.  Put the return address into the program counter.
                // It may well not be a valid code address anyway.
                poly_stack->p_pc = (byte*)poly_stack->PS_LR.AsUnsigned();
                poly_stack->PS_LR = TAGGED(0); // Clobber this
                HeapOverflowTrap();
            }
            catch (IOException) {
                // We may get an exception while handling this if we run out of store
            }
            break;

        case RETURN_STACK_OVERFLOW:
            try {
                // The stack check has failed.  This may either be because we really have
                // overflowed the stack or because the stack limit value has been adjusted
                // to result in a call here.
                poly_stack->p_pc = (byte*)poly_stack->PS_LR.AsUnsigned();
                // Restore the original value from the link register (with the
                // +2 byte tagging).  This enables the link register caching to work.
                poly_stack->PS_LR = poly_stack->PS_R25;
                check_current_stack_size(poly_stack->p_sp);
            }
            catch (IOException) {
               // We may get an exception while handling this if we run out of store
            }
            return -1; // We're in a safe state to handle any interrupts.

        case RETURN_STACK_OVERFLOWEX:
            try {
                // Stack limit overflow.  If the required stack space is larger than
                // the fixed overflow size the code will calculate the limit in rtemp1
                PolyWord *stackP = poly_stack->PS_R11.AsStackAddr();
                poly_stack->p_pc = (byte*)poly_stack->PS_LR.AsUnsigned();
                poly_stack->PS_LR = poly_stack->PS_R25; // Restore LR
                check_current_stack_size(stackP);
            }
            catch (IOException) {
               // We may get an exception while handling this if we run out of store
            }
            return -1; // We're in a safe state to handle any interrupts.

        case RETURN_RAISE_DIV:
            try {
                // Generally arithmetic operations don't raise exceptions.  Overflow
                // is either ignored, for Word operations, or results in a call to
                // the abitrary precision emulation code.  This is the exception
                // (no pun intended).
                poly_stack->p_pc = (byte*)poly_stack->PS_LR.AsUnsigned();
                poly_stack->PS_LR = TAGGED(0);
                // Set all the registers to a safe value here.  We will almost certainly
                // have shifted a value in one of the registers before testing it for zero.
                // This may not actually be needed on the PPC
                for (POLYUNSIGNED i = 0; i < poly_stack->p_nreg; i++)
                    poly_stack->p_reg[i] = TAGGED(0);
                raise_exception0(EXC_divide);
            }
            catch (IOException) {
                // Handle the C++ exception.
            }
            break;

        case RETURN_ARB_EMULATION:
            try {
                poly_stack->p_pc = (byte*)poly_stack->PS_LR.AsUnsigned();
                poly_stack->PS_LR = TAGGED(0);
                ArbitraryPrecisionTrap();
            }
            catch (IOException) {
                // We may get an exception in the trap handler e.g. if we run out of store.
            }
            break;
        }
    }
}

void PowerPCDependent::SetMemRegisters(void)
{
    memRegisters.requestCode = 0;
    memRegisters.returnReason = RETURN_IO_CALL;
    
    memRegisters.polyStack = poly_stack;
    memRegisters.stackTop = poly_stack->Offset(poly_stack->Length());
    
    if (poly_stack->p_pc == PC_RETRY_SPECIAL)
    {
        // We need to retry the call.  The entry point should be the
        // first word of the closure which is in r24.
        poly_stack->p_pc = poly_stack->PS_R24.AsObjPtr()->Get(0).AsCodePtr();
    }
    // Set up heap pointers.
    allocSpace = gMem.GetLargestSpace();

    // If we have had a heap trap we actually do the allocation here.
    // We will have already garbage collected and recovered sufficient space.
    allocSpace->pointer -= allocWords;
    ASSERT(allocSpace->pointer >= allocSpace->bottom);
    allocWords = 0;

    memRegisters.heapPointer = allocSpace->pointer;
    // If we want heap profiling set the size to zero otherwise to the free space.
    // Setting this to zero may result in the heap size register becoming negative.
    // The code to allocate memory generated by the code generator tests for space,
    // traps if it is negative and afterwards subtracts the allocated space from
    // the space available.  If we set it to zero here it will then become negative.
    if (store_profiling || (userOptions.debug & DEBUG_REGION_CHECK))
        memRegisters.heapBase = allocSpace->pointer;
    else
        memRegisters.heapBase = allocSpace->bottom;
    
    if (interrupted)
        memRegisters.stackLimit = memRegisters.stackTop;
    else
        memRegisters.stackLimit = poly_stack->Offset(poly_stack->p_space);
}

// Called as part of the call of an IO function.
void PowerPCDependent::StartIOCall(void)
{
    // Set the return address to be the contents of the link register.
    // This is a real return address which is safe for the p_pc field but
    // not allowed to remain in a register field.  We have to add the
    // return offset there.  Because this may be a retry of the call we may
    // already have put in the offset in which case we have to remove it before we
    // put it into the pc.
    poly_stack->p_pc = (byte*)(poly_stack->PS_LR.AsUnsigned() & ~RETURNOFFSET);
    poly_stack->PS_LR = PolyWord::FromUnsigned(poly_stack->PS_LR.AsUnsigned() | RETURNOFFSET);
}

// IO Functions called indirectly from assembly code.
void PowerPCDependent::CallIO0(Handle (*ioFun)(void))
{
    StartIOCall();
    try {
        Handle result = (*ioFun)();
        poly_stack->PS_R3 = result->Word();
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

void PowerPCDependent::CallIO1(Handle (*ioFun)(Handle))
{
    StartIOCall();
    Handle saved1 = gSaveVec->push(poly_stack->PS_R3);
    try {
        Handle result = (*ioFun)(saved1);
        poly_stack->PS_R3 = result->Word();
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

void PowerPCDependent::CallIO2(Handle (*ioFun)(Handle, Handle))
{
    StartIOCall();
    Handle saved1 = gSaveVec->push(poly_stack->PS_R3);
    Handle saved2 = gSaveVec->push(poly_stack->PS_R4);
    try {
        Handle result = (*ioFun)(saved2, saved1);
        poly_stack->PS_R3 = result->Word();
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

void PowerPCDependent::CallIO3(Handle (*ioFun)(Handle, Handle, Handle))
{
    StartIOCall();
    Handle saved1 = gSaveVec->push(poly_stack->PS_R3);
    Handle saved2 = gSaveVec->push(poly_stack->PS_R4);
    Handle saved3 = gSaveVec->push(poly_stack->PS_R5);
    try {
        Handle result = (*ioFun)(saved3, saved2, saved1);
        poly_stack->PS_R3 = result->Word();
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

void PowerPCDependent::CallIO4(Handle (*ioFun)(Handle, Handle, Handle, Handle))
{
    StartIOCall();
    Handle saved1 = gSaveVec->push(poly_stack->PS_R3);
    Handle saved2 = gSaveVec->push(poly_stack->PS_R4);
    Handle saved3 = gSaveVec->push(poly_stack->PS_R5);
    Handle saved4 = gSaveVec->push(poly_stack->PS_R6);
    try {
        Handle result = (*ioFun)(saved4, saved3, saved2, saved1);
        poly_stack->PS_R3 = result->Word();
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
void PowerPCDependent::CallIO5(Handle (*ioFun)(Handle, Handle, Handle, Handle, Handle))
{
    StartIOCall();
    Handle saved1 = gSaveVec->push(poly_stack->PS_R3);
    Handle saved2 = gSaveVec->push(poly_stack->PS_R4);
    Handle saved3 = gSaveVec->push(poly_stack->PS_R5);
    Handle saved4 = gSaveVec->push(poly_stack->PS_R6);
    Handle saved5 = gSaveVec->push(poly_stack->p_sp[0]);
    try {
        Handle result = (*ioFun)(saved5, saved4, saved3, saved2, saved1);
        poly_stack->PS_R3 = result->Word();
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
Handle PowerPCDependent::CallBackResult(void)
{
    return gSaveVec->push(poly_stack->PS_R3); // Argument to return is in r3.
}

bool PowerPCDependent::GetPCandSPFromContext(SIGNALCONTEXT *scp, PolyWord *&sp, POLYCODEPTR &pc)
{
    if (memRegisters.inRTS)
    {
        sp = poly_stack->p_sp;
        pc = poly_stack->p_pc;
        return true;
    }
    else {
        sp = (PolyWord *)(GPSP(scp));
        pc = (byte *)(NIP(scp));
        return true;
    }
}

void PowerPCDependent::InterruptCodeUsingContext(SIGNALCONTEXT *context)
{
    /* Note: to force an interrupt, we set the stack limit register to
       point at the "end of the stack".*/

    // SetMemRegisters sets memRegisters.stackLimit and this is loaded
    // into the stack limit register in PPCAsmSwitchToPoly.  Once this is
    // loaded "inRTS" is set to false and the register itself can be
    // modified.  We could get an interrupt between setting stackLimit
    // and clearing inRTS so we set stackLimit here to be sure.
    if (poly_stack != 0) // We may get an interrupt before we've started properly.
    {
        memRegisters.stackLimit = poly_stack->Offset(poly_stack->Length()-1); 

        if (! memRegisters.inRTS) /* only if in Poly code */
        {
            PolyWord **sl = (PolyWord **)&(GPSL(context));
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
}

void PowerPCDependent::SetForRetry(int ioCall)
{
    poly_stack->p_pc = PC_RETRY_SPECIAL; /* This value is treated specially. */
}

static PolyWord *get_reg(int rno)
/* Returns a pointer to the register given by the 5 bit value rno. */
{
    /* Registers r0, r1, r2, r26, ML_SL, ML_HL and ML_HP should not be
       needed by the emulation code. */
    switch (rno)
    {
    case 3: return &poly_stack->PS_R3;
    case 4: return &poly_stack->PS_R4;
    case 5: return &poly_stack->PS_R5;
    case 6: return &poly_stack->PS_R6;
    case 7: return &poly_stack->PS_R7;
    case 8: return &poly_stack->PS_R8;
    case 9: return &poly_stack->PS_R9;
    case 10: return &poly_stack->PS_R10;
    case 11: return &poly_stack->PS_R11;
    case 12: return &poly_stack->PS_R12;
    case 14: return &poly_stack->PS_R14;
    case 15: return &poly_stack->PS_R15;
    case 16: return &poly_stack->PS_R16;
    case 17: return &poly_stack->PS_R17;
    case 18: return &poly_stack->PS_R18;
    case 19: return &poly_stack->PS_R19;
    case 20: return &poly_stack->PS_R20;
    case 21: return &poly_stack->PS_R21;
    case 22: return &poly_stack->PS_R22;
    case 23: return &poly_stack->PS_R23;
    case 24: return &poly_stack->PS_R24;
    case 25: return &poly_stack->PS_R25;
    case ML_SP: return (PolyWord*)&poly_stack->p_sp;
    case ML_HR: return (PolyWord*)&poly_stack->p_hr;
    default:
        Crash("Unknown register %d at 0x%8x\n", rno, (int)(poly_stack->p_pc));
    }
}

static void set_CR0(int CR0)
{
    /* insert CR0 into condition register */
    poly_stack->PS_CR0 =
      PolyWord::FromUnsigned(
        (poly_stack->PS_CR0.AsUnsigned() & 0x0fffffff) | (CR0 << 28));
}

// Get the next instruction in the sequence, skipping over any
// branch-forwarding jumps that the code-generator may have inserted.
static POLYUNSIGNED getNextInstr(void)
{
    while (1) {
        POLYUNSIGNED instr = *(POLYUNSIGNED*)poly_stack->p_pc;
        if ((instr & 0xfc000000) == 0x48000000)
        {
            POLYUNSIGNED offset = (instr >> 2) & 0xffffff;
            ASSERT((offset & 0x800000) == 0); // Shouldn't be a jump back.
            poly_stack->p_pc += offset*sizeof(PolyWord);
        }
        else
        {
            poly_stack->p_pc += sizeof(PolyWord); // Skip this instr
            return instr;
        }
    }
}

static void emulate_trap(POLYUNSIGNED instr)
/* Emulate an "addo.", "subfco.", "cmpw" or "cmpwi" instruction */
{
    
    unsigned opcode   = (instr >> 26) & 0x3f;  /*  6 bits */
    unsigned RT       = (instr >> 21) & 0x1f;  /*  5 bits */
    unsigned RA       = (instr >> 16) & 0x1f;  /*  5 bits */
    
    /* added 19/12/95 SPF */
    if (emulate_profiling)
    {
        add_count(poly_stack->p_pc, poly_stack->p_sp, 1);
    }
    
#define OPCODE2_addodot   ((1 << 10) | (266 << 1) | 1)   
#define OPCODE2_subfcodot ((1 << 10) | (8   << 1) | 1)   
#define OPCODE2_cmp       ((0 << 10) | (0   << 1) | 0)
#define OPCODE2_srawi                 ((824 << 1) | 0)
    
    /* We emulate comparisons only if they use CR field 0 */
    if (opcode == 11 && RT == 0)
    {
        /* cmpwi  RA,SI */
        int UI = instr & 0xffff; /* 16 bits */
        
        /* signed comparisons sign-extend the immediate. */
        int exp2_15 = 1 << 15;
        int exp2_16 = 1 << 16;
        int SI = (UI < exp2_15) ? UI : UI - exp2_16;
        
        gSaveVec->init();
        Handle arg1 = gSaveVec->push(*get_reg(RA));
        Handle arg2 = gSaveVec->push(PolyWord::FromSigned(SI));
        
        {
            /* arguments to comp_longc are backwards */
            int res = compareLong(arg2,arg1);
            
            switch (res)
            {
            case 1: /* arg1 > arg2 */
                set_CR0(0x4); /* not LT; GT; not EQ; not SO */
                break;
                
            case 0: /* arg1 = arg2 */
                set_CR0(0x2); /* not LT; not GT; EQ; not SO */
                break;
                
            case -1: /* arg1 < arg2 */
                set_CR0(0x8); /* LT; not GT; not EQ; not SO */
                break;
                
            default:
                Crash("Bad return value 0x%08x from comp_longc\n", res);
                break;
            }
        }
        
    } /* opcode == 11 */
    
    else if (opcode == 31)
    { 
        unsigned RB       = (instr >> 11) & 0x1f;  /*  5 bits */
        unsigned opcode2  = instr & 0x7ff;         /* 11 bits */
        
        gSaveVec->init();
        
        if (opcode2 == OPCODE2_addodot)
        {
            PolyWord a1 = *get_reg(RA);
            PolyWord a2 = *get_reg(RB);
            /* We may have trapped either because one of the arguments
               was long or because of overflow.  In the latter case
               we may well have modified the destination register already.
               That's fine unless RT == RA. */
            if (RB != 12) Crash("RB != regtemp2");
            a2 = PolyWord::FromUnsigned(a2.AsUnsigned() + 1); /* We've removed the tag from this. */
            /* printf("Emulating addition rt=%d, ra=%d, rb=%d\n", RT, RA, RB); */
            if (IS_INT(a1) && IS_INT(a2))
            {
                /* Must have been overflow.  May have to undo the addition. */
                if (RT == RA) a1 = PolyWord::FromUnsigned(a1.AsUnsigned() - (a2.AsUnsigned()-1));
            }
            Handle arg1 = gSaveVec->push(a1);
            Handle arg2 = gSaveVec->push(a2);
            Handle res = add_longc(arg2, arg1);
            
            /* store result in RT */
            *(get_reg(RT)) = DEREFWORD(res);
            
            /* we don't emulate the condition codes properly,
            but we MUST ensure that we reset the SO flag,
            because the compiled code tests this flag.
            If we left it set, the compiled code would loop
            back to re-emulate the instruction, giving us an
            endless succession of traps. SPF 11/8/95
            */
            set_CR0(0x0); /* not LT; not GT; not EQ; not SO */
        }
        
        else if (opcode2 == OPCODE2_subfcodot)
        {
            /* We may have trapped either because one of the arguments
               was long or because of overflow.  In the latter case
               we may well have modified the destination register already.
               That's fine unless RT == RA or RT == RB. */
            PolyWord a1 = *get_reg(RA);
            PolyWord a2 = *get_reg(RB);
            /* printf("Emulating subtraction rt=%d, ra=%d, rb=%d\n", RT, RA, RB); */
            if (RA == 12) a1 = PolyWord::FromUnsigned(a1.AsUnsigned() + 1);
            else if (RB == 12) a2 = PolyWord::FromUnsigned(a2.AsUnsigned() - 1);
            else Crash("RA != regtemp2 and RB != regtemp2");
            if (IS_INT(a1) && IS_INT(a2))
            {
                /* Must have been overflow.  May have to undo the
                    subtraction. */
                if (RT == RB)
                    a2 = PolyWord::FromUnsigned(a2.AsUnsigned() + (a1.AsUnsigned()-1));
                else if (RT == RB)
                    a1 = PolyWord::FromUnsigned((a2.AsUnsigned()+1) - a1.AsUnsigned());
            }
            
            Handle arg1 = gSaveVec->push(a1);
            Handle arg2 = gSaveVec->push(a2);
            /* subf is *reversed* subtraction, but sub_longc
            (currently) expects reversed arguments, so the
            following call ought to be correct. */
            Handle res = sub_longc(arg1, arg2);
            
            /* store result in RT */
            *(get_reg(RT)) = DEREFWORD(res);
            
            /* we don't emulate the condition codes properly,
            but we MUST ensure that we reset the SO flag.
            */
            set_CR0(0x0); /* not LT; not GT; not EQ; not SO */
        }
        
        /* We emulate comparisons only if they use CR field 0 */
        else if (opcode2 == OPCODE2_cmp && RT == 0)
        {
            /* arguments to comp_longc are backwards */
            PolyWord a1 = *get_reg(RA);
            PolyWord a2 = *get_reg(RB);
            Handle arg1 = gSaveVec->push(a1);
            Handle arg2 = gSaveVec->push(a2);
            int res = compareLong(arg2,arg1);
            
            switch (res)
            {
            case 1: /* arg1 > arg2 */
                set_CR0(0x4); /* not LT; GT; not EQ; not SO */
                break;
                
            case 0: /* arg1 = arg2 */
                set_CR0(0x2); /* not LT; not GT; EQ; not SO */
                break;
                
            case -1: /* arg1 < arg2 */
                set_CR0(0x8); /* LT; not GT; not EQ; not SO */
                break;
                
            default:
                Crash("Bad return value 0x%08x from comp_longc\n", res);
                break;
            }
        }
        else if (opcode2 == OPCODE2_srawi)
        {
            /* Start of multiply sequence.  poly_stack->p_pc now
                points at the multiply. */
            PolyWord a1 = *get_reg(RT); /* srawi has regs reversed. */
            PolyWord a2 = *get_reg(12); /* The second arg is in regtemp2. */
            POLYUNSIGNED mullwInstr = getNextInstr();
            assert(RA == 11); /* destination of srawi. */
            /* Should be mullwo. rt,r12,r11 */
            assert((mullwInstr & 0xfc1fffff) == 0x7C0C5DD7);
            RT = (mullwInstr >> 21) & 0x1f;
            Handle arg1 = gSaveVec->push(a1);
            Handle arg2 = gSaveVec->push(PolyWord::FromUnsigned(a2.AsUnsigned()+1)); /* This has been untagged. */
            Handle res = mult_longc(arg2, arg1);
            *(get_reg(RT)) = PolyWord::FromUnsigned(DEREFWORD(res).AsUnsigned() - 1); /* Next instr will tag it. */
            set_CR0(0x0); /* Clear overflow bit. */
        }
        
        else
        {
            Crash("Can't emulate instruction: 0x%08x\n",instr);
        }
        
    } /* opcode == 31 */
    
    else /* opcode != 11 && opcode != 31 */
    {
        Crash("Can't emulate instruction: 0x%08x\n",instr);
    } 
    
}

void PowerPCDependent::ArbitraryPrecisionTrap(void)
{
    emulate_trap(getNextInstr());
}

void PowerPCDependent::HeapOverflowTrap(void)
{
    // When we come here we have attempted to allocate some memory by
    // decrementing the heap pointer register.  We have compared it against
    // the heap base and found there is insufficient space.  That may be a
    // fake trap for heap profiling or it may be a real trap.
    // R12 should contain the length word.
    POLYUNSIGNED lengthWord = poly_stack->PS_R12.AsUnsigned();
    // We need one extra word for the length word itself.
    POLYUNSIGNED wordsNeeded = OBJ_OBJECT_LENGTH(lengthWord) + 1;
    
    if (store_profiling)
        add_count(poly_stack->p_pc, poly_stack->p_sp, wordsNeeded);
    else if (allocSpace->pointer >= allocSpace->bottom && ! (userOptions.debug & DEBUG_REGION_CHECK)) 
        Crash ("Spurious heap-limit trap");
    
    // Do we have a genuine out-of-heap problem, or did we only
    // interrupt to profile the heap?
    if (allocSpace->pointer < allocSpace->bottom)
    {
        allocSpace->pointer += wordsNeeded; // Adjust the heap pointer back
        ASSERT(allocSpace->pointer >= allocSpace->bottom); // It should previously have been right.
        // See if we have another space to satisfy the request.
        allocSpace = gMem.GetAllocSpace(wordsNeeded);
        if (allocSpace == 0)
        {
            if (! QuickGC(wordsNeeded)) /* Garbage-collect. */
            {
                fprintf(stderr,"Run out of store - interrupting console processes\n");
                processes->interrupt_console_processes();
                throw IOException(EXC_RETRY);
            }
        }
        allocWords = wordsNeeded; // Actually allocate it in SetMemRegisters.
    }
}

// This is the number of words in the call sequence
#define MAKE_CALL_SEQUENCE_WORDS 5

#ifdef MACOSX
// Mac OS X uses an older assembler syntax.
#define MAKE_IO_CALL_SEQUENCE(ioNum, result) \
{ \
    __asm__ __volatile__ ( "bl 1f \n" \
           "li r0,%1 \n" \
           "stw r0,4(r13) \n" \
           "lwz r0,44(r13) \n" \
           "mtctr r0 \n" \
           "bctr \n" \
           "1: mflr r0 \n" \
           "stw r0,%0" \
           :"=m"(result) \
           :"i"(ioNum) \
           ); \
}

#else
// Linux
#define MAKE_IO_CALL_SEQUENCE(ioNum, result) \
{ \
    __asm__ __volatile__ ( "bl 1f;" \
           "li 0,%1;" \
           "stw 0,4(13);" \
           "lwz 0,44(13);" \
           "mtctr 0;" \
           "bctr;" \
           "1: mflr 0;" \
           "stw 0,%0" \
           :"=m"(result) \
           :"i"(ioNum) \
           ); \
}

#endif

#ifdef MACOSX
// Mac OS X uses an older assembler syntax.
#define MAKE_EXTRA_CALL_SEQUENCE(exNum, result) \
{ \
    __asm__ __volatile__ ( "bl 1f \n" \
           "li r0,%1 \n" \
           "stw r0,8(r13) \n" \
           "lwz r0,44(r13) \n" \
           "mtctr r0 \n" \
           "bctr \n" \
           "1: mflr r0 \n" \
           "stw r0,%0" \
           :"=m"(result) \
           :"i"(exNum) \
           ); \
}

#else
// Linux
#define MAKE_EXTRA_CALL_SEQUENCE(exNum, result) \
{ \
    __asm__ __volatile__ ( "bl 1f;" \
           "li 0,%1;" \
           "stw 0,8(13);" \
           "lwz 0,44(13);" \
           "mtctr 0;" \
           "bctr;" \
           "1: mflr 0;" \
           "stw 0,%0" \
           :"=m"(result) \
           :"i"(exNum) \
           ); \
}

#endif


static void add_function_to_io_area(int x, int (*y)())
{
    add_word_to_io_area(x, PolyWord::FromUnsigned((POLYUNSIGNED)y));
}

void PowerPCDependent::InitInterfaceVector(void)
{
    memRegisters.inRTS = 1; // We start off in the RTS
     
    unsigned char *codeAddr;
    
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_exit, codeAddr);
    add_word_to_io_area(POLY_SYS_exit, PolyWord::FromCodePtr(codeAddr));
   
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_install_root, codeAddr);
    add_word_to_io_area(POLY_SYS_install_root, PolyWord::FromCodePtr(codeAddr));

    MAKE_IO_CALL_SEQUENCE(POLY_SYS_strconcat, codeAddr);    
    add_word_to_io_area(POLY_SYS_strconcat, PolyWord::FromCodePtr(codeAddr));
    
    add_function_to_io_area(POLY_SYS_alloc_store, alloc_store);
    
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_chdir, codeAddr);
    add_word_to_io_area(POLY_SYS_chdir, PolyWord::FromCodePtr(codeAddr));
    
    add_function_to_io_area(POLY_SYS_get_length, get_length_a);
    
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_get_flags, codeAddr);    
    add_word_to_io_area(POLY_SYS_get_flags, PolyWord::FromCodePtr(codeAddr));
   
    add_function_to_io_area(POLY_SYS_str_compare, str_compare);
    add_function_to_io_area(POLY_SYS_teststreq, teststreq);
    add_function_to_io_area(POLY_SYS_teststrneq, teststrneq);
    add_function_to_io_area(POLY_SYS_teststrgtr, teststrgtr);
    add_function_to_io_area(POLY_SYS_teststrlss, teststrlss);
    add_function_to_io_area(POLY_SYS_teststrgeq, teststrgeq);
    add_function_to_io_area(POLY_SYS_teststrleq, teststrleq);
     
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_exception_trace, codeAddr);    
    add_word_to_io_area(POLY_SYS_exception_trace, PolyWord::FromCodePtr(codeAddr));

    add_function_to_io_area(POLY_SYS_lockseg, locksega);
    
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_profiler, codeAddr);
    add_word_to_io_area(POLY_SYS_profiler, PolyWord::FromCodePtr(codeAddr));
    
    add_function_to_io_area(POLY_SYS_is_short, is_shorta);
    add_function_to_io_area(POLY_SYS_aplus, add_long);
    add_function_to_io_area(POLY_SYS_aminus, sub_long);
    add_function_to_io_area(POLY_SYS_amul, mult_long);
    add_function_to_io_area(POLY_SYS_adiv, div_long);
    add_function_to_io_area(POLY_SYS_amod, rem_long);
    add_function_to_io_area(POLY_SYS_aneg, neg_long);
    add_function_to_io_area(POLY_SYS_equala, equal_long);
    add_function_to_io_area(POLY_SYS_ora, or_long);
    add_function_to_io_area(POLY_SYS_anda, and_long);
    add_function_to_io_area(POLY_SYS_xora, xor_long);
    
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_Real_str, codeAddr);
    add_word_to_io_area(POLY_SYS_Real_str, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_Real_geq, codeAddr);
    add_word_to_io_area(POLY_SYS_Real_geq, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_Real_leq, codeAddr);
    add_word_to_io_area(POLY_SYS_Real_leq, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_Real_gtr, codeAddr);
    add_word_to_io_area(POLY_SYS_Real_gtr, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_Real_lss, codeAddr);
    add_word_to_io_area(POLY_SYS_Real_lss, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_Real_eq, codeAddr);
    add_word_to_io_area(POLY_SYS_Real_eq, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_Real_neq, codeAddr);
    add_word_to_io_area(POLY_SYS_Real_neq, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_Real_Dispatch, codeAddr);
    add_word_to_io_area(POLY_SYS_Real_Dispatch, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_Add_real, codeAddr);
    add_word_to_io_area(POLY_SYS_Add_real, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_Sub_real, codeAddr);
    add_word_to_io_area(POLY_SYS_Sub_real, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_Mul_real, codeAddr);
    add_word_to_io_area(POLY_SYS_Mul_real, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_Div_real, codeAddr);
    add_word_to_io_area(POLY_SYS_Div_real, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_Neg_real, codeAddr);
    add_word_to_io_area(POLY_SYS_Neg_real, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_Repr_real, codeAddr);
    add_word_to_io_area(POLY_SYS_Repr_real, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_conv_real, codeAddr);
    add_word_to_io_area(POLY_SYS_conv_real, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_real_to_int, codeAddr);
    add_word_to_io_area(POLY_SYS_real_to_int, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_int_to_real, codeAddr);
    add_word_to_io_area(POLY_SYS_int_to_real, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_sqrt_real, codeAddr);
    add_word_to_io_area(POLY_SYS_sqrt_real, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_sin_real, codeAddr);
    add_word_to_io_area(POLY_SYS_sin_real, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_cos_real, codeAddr);
    add_word_to_io_area(POLY_SYS_cos_real, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_arctan_real, codeAddr);
    add_word_to_io_area(POLY_SYS_arctan_real, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_exp_real, codeAddr);
    add_word_to_io_area(POLY_SYS_exp_real, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_ln_real, codeAddr);
    add_word_to_io_area(POLY_SYS_ln_real, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_io_operation, codeAddr);
    add_word_to_io_area(POLY_SYS_io_operation, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_fork_process, codeAddr);
    add_word_to_io_area(POLY_SYS_fork_process, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_choice_process, codeAddr);
    add_word_to_io_area(POLY_SYS_choice_process, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_int_process, codeAddr);
    add_word_to_io_area(POLY_SYS_int_process, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_send_on_channel, codeAddr);
    add_word_to_io_area(POLY_SYS_send_on_channel, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_receive_on_channel, codeAddr);
    add_word_to_io_area(POLY_SYS_receive_on_channel, PolyWord::FromCodePtr(codeAddr));

    add_function_to_io_area(POLY_SYS_offset_address, offset_address);
    add_function_to_io_area(POLY_SYS_shift_right_word, shift_right_word);
    add_function_to_io_area(POLY_SYS_word_neq, word_neq);
    add_function_to_io_area(POLY_SYS_not_bool, not_bool);
    add_function_to_io_area(POLY_SYS_string_length, string_length);
    add_function_to_io_area(POLY_SYS_int_eq, int_eq);
    add_function_to_io_area(POLY_SYS_int_neq, int_neq);
    add_function_to_io_area(POLY_SYS_int_geq, int_geq);
    add_function_to_io_area(POLY_SYS_int_leq, int_leq);
    add_function_to_io_area(POLY_SYS_int_gtr, int_gtr);
    add_function_to_io_area(POLY_SYS_int_lss, int_lss);
    
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_string_sub, codeAddr);
    add_word_to_io_area(POLY_SYS_string_sub, PolyWord::FromCodePtr(codeAddr)); // This is really redundant.
    
    add_function_to_io_area(POLY_SYS_or_word, or_word);
    add_function_to_io_area(POLY_SYS_and_word, and_word);
    add_function_to_io_area(POLY_SYS_xor_word, xor_word);
    add_function_to_io_area(POLY_SYS_shift_left_word, shift_left_word);
    add_function_to_io_area(POLY_SYS_word_eq, word_eq);
    add_function_to_io_area(POLY_SYS_load_byte, load_byte);
    add_function_to_io_area(POLY_SYS_load_word, load_word);
    add_function_to_io_area(POLY_SYS_is_big_endian, is_big_endian);
    add_function_to_io_area(POLY_SYS_bytes_per_word, bytes_per_word);
    
    add_function_to_io_area(POLY_SYS_assign_byte, assign_byte);
    add_function_to_io_area(POLY_SYS_assign_word, assign_word);

    MAKE_IO_CALL_SEQUENCE(POLY_SYS_objsize, codeAddr);
    add_word_to_io_area(POLY_SYS_objsize, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_showsize, codeAddr);
    add_word_to_io_area(POLY_SYS_showsize, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_timing_dispatch, codeAddr);
    add_word_to_io_area(POLY_SYS_timing_dispatch, PolyWord::FromCodePtr(codeAddr));
    
    
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_interrupt_console_processes, codeAddr);
    add_word_to_io_area(POLY_SYS_interrupt_console_processes, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_install_subshells, codeAddr);
    add_word_to_io_area(POLY_SYS_install_subshells, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_XWindows, codeAddr);
    add_word_to_io_area(POLY_SYS_XWindows, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_full_gc, codeAddr);
    add_word_to_io_area(POLY_SYS_full_gc, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_stack_trace, codeAddr);
    add_word_to_io_area(POLY_SYS_stack_trace, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_foreign_dispatch, codeAddr);
    add_word_to_io_area(POLY_SYS_foreign_dispatch, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_callcode_tupled, codeAddr);
    add_word_to_io_area(POLY_SYS_callcode_tupled, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_process_env, codeAddr);
    add_word_to_io_area(POLY_SYS_process_env, PolyWord::FromCodePtr(codeAddr));
    
    add_function_to_io_area(POLY_SYS_set_string_length, &set_string_length_a); /* DCJM 28/2/01 */
    add_function_to_io_area(POLY_SYS_get_first_long_word, &get_first_long_word_a); /* DCJM 28/2/01 */
    
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_shrink_stack, codeAddr);
    add_word_to_io_area(POLY_SYS_shrink_stack, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_code_flags, codeAddr);
    add_word_to_io_area(POLY_SYS_code_flags, PolyWord::FromCodePtr(codeAddr));
    
    add_function_to_io_area(POLY_SYS_shift_right_arith_word, shift_right_arith_word); /* DCJM 10/10/99 */
    add_function_to_io_area(POLY_SYS_int_to_word,      int_to_word);        /* DCJM 10/10/99 */

    MAKE_IO_CALL_SEQUENCE(POLY_SYS_set_code_constant, codeAddr); /* DCJM 2/1/01 */
    add_word_to_io_area(POLY_SYS_set_code_constant, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_poly_specific, codeAddr); /* DCJM 19/6/06 */
    add_word_to_io_area(POLY_SYS_poly_specific, PolyWord::FromCodePtr(codeAddr));
    
    add_function_to_io_area(POLY_SYS_move_bytes,       move_bytes);        /* DCJM 10/10/99 */
    add_function_to_io_area(POLY_SYS_move_words,       move_words);        /* DCJM 10/10/99 */
    add_function_to_io_area(POLY_SYS_mul_word,         mul_word);        /* DCJM 10/10/99 */
    add_function_to_io_area(POLY_SYS_plus_word,        plus_word);        /* DCJM 10/10/99 */
    add_function_to_io_area(POLY_SYS_minus_word,       minus_word);        /* DCJM 10/10/99 */
    add_function_to_io_area(POLY_SYS_div_word,         div_word);        /* DCJM 10/10/99 */
    add_function_to_io_area(POLY_SYS_mod_word,         mod_word);        /* DCJM 10/10/99 */
    add_function_to_io_area(POLY_SYS_word_geq,         word_geq);
    add_function_to_io_area(POLY_SYS_word_leq,         word_leq);
    add_function_to_io_area(POLY_SYS_word_gtr,         word_gtr);
    add_function_to_io_area(POLY_SYS_word_lss,         word_lss);
    
    // This is now a "closure" like all the other entries.
    add_function_to_io_area(POLY_SYS_raisex,           raisex);

    MAKE_IO_CALL_SEQUENCE(POLY_SYS_io_dispatch, codeAddr);
    add_word_to_io_area(POLY_SYS_io_dispatch, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_network, codeAddr);
    add_word_to_io_area(POLY_SYS_network, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_os_specific, codeAddr);
    add_word_to_io_area(POLY_SYS_os_specific, PolyWord::FromCodePtr(codeAddr));
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_signal_handler, codeAddr);
    add_word_to_io_area(POLY_SYS_signal_handler, PolyWord::FromCodePtr(codeAddr));

    // Entries for special cases.  These are generally, but not always, called from
    // compiled code.
    MAKE_EXTRA_CALL_SEQUENCE(RETURN_HEAP_OVERFLOW, memRegisters.heapOverflow);
    MAKE_EXTRA_CALL_SEQUENCE(RETURN_STACK_OVERFLOW, memRegisters.stackOverflow);
    MAKE_EXTRA_CALL_SEQUENCE(RETURN_STACK_OVERFLOWEX, memRegisters.stackOverflowEx);
    MAKE_EXTRA_CALL_SEQUENCE(RETURN_RAISE_DIV, memRegisters.raiseDiv);
    MAKE_EXTRA_CALL_SEQUENCE(RETURN_ARB_EMULATION, memRegisters.arbEmulation);
    
    // Set the raiseException entry to point to the assembly code.
    memRegisters.raiseException = (byte*)raisex;

    // Entry point to save the state for an IO call.  This is the common entry
    // point for all the return and IO-call cases.
    memRegisters.ioEntry = (byte*)PPCSaveStateAndReturn;
    
}

// Build an ML code segment on the heap to hold a copy of a piece of code
Handle PowerPCDependent::BuildCodeSegment(const byte *code, unsigned codeWords, char functionName)
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

// We need the kill-self code in a little function.
Handle PowerPCDependent::BuildKillSelfCode(void)
{
    byte *codeAddr;
    MAKE_IO_CALL_SEQUENCE(POLY_SYS_kill_self, codeAddr);
    return BuildCodeSegment(codeAddr, MAKE_CALL_SEQUENCE_WORDS, 'K');
}

void PowerPCDependent::SetException(StackObject *stack, poly_exn *exc)
/* Set up the stack of a process to raise an exception. */
{
    // This IO entry has changed from being the code to being a closure
    // like all the other entries.
    stack->PS_R24 = (PolyObject*)IoEntry(POLY_SYS_raisex);
    stack->p_pc   = PC_RETRY_SPECIAL;
    stack->PS_R3  = (PolyWord)exc; /* r3 to exception data */
}

void PowerPCDependent::ResetSignals(void)
{
  /* restore default signal handling in child process after a "fork". */
  signal(SIGFPE,  SIG_DFL);
#ifdef SIGEMT
  signal(SIGEMT,  SIG_DFL);
#endif
  signal(SIGTRAP, SIG_DFL);
  signal(SIGILL,  SIG_DFL);

  /* "just in case" */
  signal(SIGBUS,  SIG_DFL);
  signal(SIGSEGV, SIG_DFL);
}


// Call a piece of compiled code.  Note: this doesn't come via CallIO1
// so StartIOCall has not been called.
void PowerPCDependent::CallCodeTupled(void)
{
    // The eventual return address is in the link register - leave it there
    // but call StartIOCall to make sure it's tagged before any possible G.C.
    StartIOCall();
    PolyObject *argTuple = poly_stack->PS_R3.AsObjPtr();
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
        // First argument is in r3
        poly_stack->PS_R3 = argv->Get(0);
        // Second arg, if there is one, goes into r4 etc.
        if (argCount > 1)
            poly_stack->PS_R4 = argv->Get(1);
        if (argCount > 2)
            poly_stack->PS_R5 = argv->Get(2);
        if (argCount > 3)
            poly_stack->PS_R6 = argv->Get(3);
        // Remaining args go on the stack.
        for (POLYUNSIGNED i = 4; i < argCount; i++)
            *(--poly_stack->p_sp) = argv->Get(i+2);
    }
    // The closure goes into the closure reg.
    poly_stack->PS_R24 = DEREFWORD(closure);
    // First word of closure is entry point.
    poly_stack->p_pc = DEREFHANDLE(closure)->Get(0).AsCodePtr(); // pc points to the start of the code
}

// Set up a special handler that will trace any uncaught exception within a function.
void PowerPCDependent::SetExceptionTrace(void)
{
    // Save the return address for when we've called the function.
    *(--poly_stack->p_sp) =
        PolyWord::FromUnsigned(poly_stack->PS_LR.AsUnsigned() | RETURNOFFSET);
    *(--poly_stack->p_sp) = PolyWord::FromStackAddr(poly_stack->p_hr); // Save previous handler.
    *(--poly_stack->p_sp) = TAGGED(0); // Push special handler address.
    *(--poly_stack->p_sp) = TAGGED(0); // Push "catch all" exception id.
    poly_stack->p_hr = poly_stack->p_sp; // This is the new handler.
    Handle fun = gSaveVec->push(poly_stack->PS_R3); // Argument - function to call and trace
    poly_stack->PS_R24 = DEREFWORD(fun); // r24 must contain the closure
    poly_stack->p_pc = DEREFHANDLE(fun)->Get(0).AsCodePtr(); // pc points to the start of the code

    // We have to use a special entry here that can be recognised by the exception
    // unwinding code because we want to know the stack pointer that is in effect
    // at the time the exception is raised.  If we simply put a normal handler in here
    // that handler would be called after the stack was unwound.
    byte *codeAddr;
#ifdef MACOSX
    __asm__ __volatile__ (
     "bl 1f \n"
        "lwz     r0,12(r27)\n"  // Restore old return address
        "lwz     r31,8(r27)\n"  // Restore old handler
        "mtlr    r0\n"
        "addi    r27,r27,16\n"  // Pop stack
        "blr\n"                 // Return to exception_trace's caller
    "1: mflr r0\n"
    "stw r0,%0"
    :"=m"(codeAddr)
    );
#else
    __asm__ __volatile__ (
     "bl 1f; "
        "lwz     0,12(27); "  // Restore old return address
        "lwz     31,8(27); "  // Restore old handler
        "mtlr    0; "
        "addi    27,27,16; "  // Pop stack
        "blr; "               // Return to exception_trace's caller
    "1: mflr 0; "
    "stw 0,%0"
    :"=m"(codeAddr)
    );
#endif
    Handle retCode = BuildCodeSegment(codeAddr, 5 /* Code is 5 words */, 'R');

    // Set the link register so that if the traced function returns normally (i.e. without
    // raising an exception) it will enter the "return" code which will remove this handler.
    poly_stack->PS_LR = PolyWord::FromUnsigned(retCode->Word().AsUnsigned() | RETURNOFFSET);
    poly_stack->PS_R3 = TAGGED(0); // Give the function we're calling a unit argument.
}

void PowerPCDependent::ScanConstantsWithinCode(PolyObject *addr, PolyObject */*old*/,
                                               POLYUNSIGNED length, ScanAddress *process)
{
    PolyWord *pt = (PolyWord*)addr;
    PolyWord *end;
    POLYUNSIGNED unused;
    /* Find the end of the code (before the constants). */
    addr->GetConstSegmentForCode(length, end, unused);
    /* Find the end of the code (before the constants). */
    end -= 3;
    assert(end->AsUnsigned() == 0); /* This should be the marker word. */

    while (pt != end)
    {
        POLYUNSIGNED instr = (*pt).AsUnsigned();
        pt++;
        if ((instr & 0xfc1f0000) == 0x3c000000) /* addis rd,r0. */
        {
            POLYUNSIGNED reg = (instr >> 21) & 31;
            /* Ignore the unchecked registers. */
            if (reg == 11 || reg == 12) continue;
            /* Next must be an ADD or OR instruction. */
            POLYUNSIGNED instr2 = (*pt).AsUnsigned();
            assert((instr2 & 0xfc000000) == 0x38000000 || (instr2 & 0xfc000000) == 0x60000000);
            bool isSigned = (instr2 & 0xfc000000) == 0x38000000; /* Add instruction. */
            process->ScanConstant((byte*)(pt-1),
                isSigned ? PROCESS_RELOC_PPCDUAL16SIGNED :  PROCESS_RELOC_PPCDUAL16UNSIGNED);
            pt++;
        }
    }
}

/* Store a constant at a specific address in the code.  This is used by
   the code generator.  It needs to be built into the RTS because we
   have to split the value in order to store it into two instructions.
   Separately the two values might well be invalid addresses. */
void PowerPCDependent::SetCodeConstant(Handle/*data*/, Handle constant, Handle offseth, Handle base)
{
    /* The offset is a byte count. */
    POLYUNSIGNED offset = get_C_ulong(DEREFWORD(offseth));
    PolyWord *pointer = DEREFHANDLE(base)->Offset(offset/sizeof(PolyWord));
    POLYUNSIGNED c = DEREFWORD(constant).AsUnsigned(); // N.B.  This may well really be an address.
    unsigned hi = c >> 16, lo = c & 0xffff;
    POLYUNSIGNED instr1 = pointer[0].AsUnsigned();
    POLYUNSIGNED instr2 = pointer[1].AsUnsigned();
    bool isSigned = (instr2 & 0xfc000000) == 0x38000000; /* Add instruction. */
    if (isSigned && (lo & 0x8000)) hi++; /* Adjust the for sign extension */
    assert((offset & 3) == 0);
    // The first word must be addis rd,r0,0.
    assert((instr1 & 0xfc1fffff) == 0x3c000000);
    // and the next must be addi rd,rd,1 or ori rd,rd,1
    assert((instr2 & 0xfc00ffff) == 0x38000001 || (instr2 & 0xfc00ffff) == 0x60000001);
    pointer[0] = PolyWord::FromUnsigned(pointer[0].AsUnsigned() | hi);
    pointer[1] = PolyWord::FromUnsigned((pointer[1].AsUnsigned() & 0xffff0000) | lo);
}


// On the PPC it's important to flush the cache.  The PPC has separate data and instruction
// caches and there is no internal synchronisation.  This actually does two things: it
// flushes the data cache to memory and also clears the instruction prefetch cache.
void PowerPCDependent::FlushInstructionCache(void *p, POLYUNSIGNED bytes)
{
#if (1)
    byte *q = (byte*)p;
    // Apparently some processors have 16 byte cache lines and others 32.
#define CACHE_LINE_SIZE   16
    while (bytes > 0)
    {
        // Flush the data cache
        __asm__ __volatile("dcbf 0,%0" :: "r"(q));
        __asm__ __volatile("sync");
        __asm__ __volatile("icbi 0,%0" :: "r"(q));
        q += CACHE_LINE_SIZE;
        if (bytes <= CACHE_LINE_SIZE)
            break;
        bytes -= CACHE_LINE_SIZE;
    }
    __asm__ __volatile("sync");
    __asm__ __volatile("isync");
#else
    MD_flush_instruction_cache(p, bytes);
#endif
}

extern "C" int registerMaskVector[];

int PowerPCDependent::GetIOFunctionRegisterMask(int ioCall)
{
    return registerMaskVector[ioCall];
}

static PowerPCDependent ppcDependent;

MachineDependent *machineDependent = &ppcDependent;


