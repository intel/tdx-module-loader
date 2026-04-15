#ifndef _ASM_SEAMCALL_H
#define _ASM_SEAMCALL_H

#include <Uefi.h>

/**
  @param[in]        Leaf    0x80000000.00000001 [SEAMLDR.INSTALL]
  @param[in,out]    Buffer  A 64-bit PA of buffer, if Leaf set to:
                            SEAMLDR.INSTALL, then SEAMLDR_PARAMS buffer,

  @return    SeamcallStatus    Seamcall execution status, 0x00 if successful or one of the following errors:
                               - 0x80000000 00000000    EBADPARAM,
                               - 0x80000000 00000003    EBADCALL,
                               - 0x80000000 00010002    ENOMEM,
                               - 0x80000000 00010003    EUNSPECERR,
                               - 0x80000000 00010004    EUNSUPCPU,
                               - 0x80000000 00020000    EBADSIG,
                               - 0x80000000 00020001    EBADHASH,
                               - 0x80000000 00030000    EINTERRUPT,
                               - 0x80000000 00030001    ENOENTROPY.
**/
UINT64
EFIAPI
AsmSeamcall (
  IN     UINT64 Leaf,  // RCX
  IN OUT UINT64 Buffer // RDX
  );

#define ASM_SEAMCALL_LEAF_SEAMLDR_INSTALL  0x8000000000000001

#endif // _ASM_SEAMCALL_H