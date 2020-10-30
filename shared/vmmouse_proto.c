/*
 * Copyright 1999-2006 by VMware, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the copyright holder(s)
 * and author(s) shall not be used in advertising or otherwise to promote
 * the sale, use or other dealings in this Software without prior written
 * authorization from the copyright holder(s) and author(s).
 */

/*
 * vmmouse_proto.c --
 *
 *      The communication protocol between the guest and the vmmouse
 *      virtual device.
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "vmmouse_proto.h"


/*
 *----------------------------------------------------------------------------
 *
 * VMMouseProtoInOut --
 *
 *      Send a low-bandwidth basic request (16 bytes) to vmware, and return its
 *      reply (24 bytes).
 *
 * Results:
 *      Host-side response returned in cmd IN/OUT parameter.
 *
 * Side effects:
 *      Pokes the communication port.
 *
 *----------------------------------------------------------------------------
 */

static void
VMMouseProtoInOut(VMMouseProtoCmd *cmd) // IN/OUT
{
#ifdef __x86_64__
   uint64_t dummy;

   __asm__ __volatile__(
        "pushq %%rax"           "\n\t"
        "movq 40(%%rax), %%rdi" "\n\t"
        "movq 32(%%rax), %%rsi" "\n\t"
        "movq 24(%%rax), %%rdx" "\n\t"
        "movq 16(%%rax), %%rcx" "\n\t"
        "movq  8(%%rax), %%rbx" "\n\t"
        "movq   (%%rax), %%rax" "\n\t"
        "inl %%dx, %%eax"       "\n\t"  /* NB: There is no inq instruction */
        "xchgq %%rax, (%%rsp)"  "\n\t"
        "movq %%rdi, 40(%%rax)" "\n\t"
        "movq %%rsi, 32(%%rax)" "\n\t"
        "movq %%rdx, 24(%%rax)" "\n\t"
        "movq %%rcx, 16(%%rax)" "\n\t"
        "movq %%rbx,  8(%%rax)" "\n\t"
        "popq          (%%rax)"
      : "=a" (dummy)
      : "0" (cmd)
      /*
       * vmware can modify the whole VM state without the compiler knowing
       * it. So far it does not modify EFLAGS. --hpreg
       */
      : "rbx", "rcx", "rdx", "rsi", "rdi", "memory"
   );
#elif defined __i386__
   uint32_t dummy;

   __asm__ __volatile__(
        "pushl %%ebx"           "\n\t"
        "pushl %%eax"           "\n\t"
        "movl 20(%%eax), %%edi" "\n\t"
        "movl 16(%%eax), %%esi" "\n\t"
        "movl 12(%%eax), %%edx" "\n\t"
        "movl  8(%%eax), %%ecx" "\n\t"
        "movl  4(%%eax), %%ebx" "\n\t"
        "movl   (%%eax), %%eax" "\n\t"
        "inl %%dx, %%eax"       "\n\t"
        "xchgl %%eax, (%%esp)"  "\n\t"
        "movl %%edi, 20(%%eax)" "\n\t"
        "movl %%esi, 16(%%eax)" "\n\t"
        "movl %%edx, 12(%%eax)" "\n\t"
        "movl %%ecx,  8(%%eax)" "\n\t"
        "movl %%ebx,  4(%%eax)" "\n\t"
        "popl          (%%eax)" "\n\t"
        "popl           %%ebx"
      : "=a" (dummy)
      : "0" (cmd)
      /*
       * vmware can modify the whole VM state without the compiler knowing
       * it. So far it does not modify EFLAGS. --hpreg
       */
      : "ecx", "edx", "esi", "edi", "memory"
   );
#elif defined __aarch64__
#define X86_IO_MAGIC          0x86
#define X86_IO_W7_SIZE_SHIFT  0
#define X86_IO_W7_DIR         (1 << 2)
#define X86_IO_W7_WITH        (1 << 3)
   __asm__ __volatile__(
      "ldp x4, x5, [%0, 8 * 4] \n\t"
      "ldp x2, x3, [%0, 8 * 2] \n\t"
      "ldp x0, x1, [%0       ] \n\t"
      "mov x7, %1              \n\t"
      "movk x7, %2, lsl #32    \n\t"
      "mrs xzr, mdccsr_el0     \n\t"
      "stp x4, x5, [%0, 8 * 4] \n\t"
      "stp x2, x3, [%0, 8 * 2] \n\t"
      "stp x0, x1, [%0       ]     "
      :
      : "r" (cmd),
        "M" (X86_IO_W7_WITH | X86_IO_W7_DIR | 2 << X86_IO_W7_SIZE_SHIFT),
        "i" (X86_IO_MAGIC)
      : "x0", "x1", "x2", "x3", "x4", "x5", "x7", "memory"
   );
#else
#error "VMMouse is only supported on i386, amd64, and aarch64."
#endif
}


/*
 *-----------------------------------------------------------------------------
 *
 * VMMouseProto_SendCmd --
 *
 *      Send a request (16 bytes) to vmware, and synchronously return its
 *      reply (24 bytes).
 *
 * Result:
 *      None
 *
 * Side-effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
VMMouseProto_SendCmd(VMMouseProtoCmd *cmd) // IN/OUT
{
   cmd->in.magic = VMMOUSE_PROTO_MAGIC;
   cmd->in.port = VMMOUSE_PROTO_PORT;

   VMMouseProtoInOut(cmd);
}
