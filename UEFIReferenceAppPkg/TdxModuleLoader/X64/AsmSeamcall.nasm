;--------------------------------------------------------------------------------------
; UINT64
; EFIAPI
; AsmSeamcall (
;   IN     UINT64 Leaf,  // RCX
;   IN OUT UINT64 Buffer // RDX
;   );
;
;  Input:       rcx - Leaf    0x80000000.00000001 [SEAMLDR.INSTALL]
;               rdx - Buffer  A 64-bit PA of buffer, if Leaf set to:
;                             SEAMLDR.INSTALL, then SEAMLDR_PARAMS buffer,
;
;  Output:      rax - SeamcallStatus    Execution status, 0x00 if successful or
;                                       one of the following errors:
;                                       - 0x80000000 00000000    EBADPARAM,
;                                       - 0x80000000 00000003    EBADCALL,
;                                       - 0x80000000 00010002    ENOMEM,
;                                       - 0x80000000 00010003    EUNSPECERR,
;                                       - 0x80000000 00010004    EUNSUPCPU,
;                                       - 0x80000000 00020000    EBADSIG,
;                                       - 0x80000000 00020001    EBADHASH,
;                                       - 0x80000000 00030000    EINTERRUPT,
;                                       - 0x80000000 00030001    ENOENTROPY.
;
;--------------------------------------------------------------------------------------

/*
 * TDCALL and SEAMCALL are supported in Binutils >= 2.36.
 */
#define seamcall	db 0x66, 0x0f, 0x01, 0xcf

global ASM_PFX(AsmSeamcall)
ASM_PFX(AsmSeamcall):
; Save GPRs
  pushfq
  push    rsp
  push    rbp
  push    rbx
  push    rcx
  push    rdx
  push    rsi
  push    rdi
  push    r8
  push    r9
  push    r10
  push    r11
  push    r12
  push    r13
  push    r14
  push    r15

; Set input parameters

  mov rax, rcx ; rax := Leaf
  mov rcx, rdx ; rcx := Buffer

; Call SEAMCALL

  cli
  seamcall
  sti

; Restore GPRs
  pop    r15
  pop    r14
  pop    r13
  pop    r12
  pop    r11
  pop    r10
  pop    r9
  pop    r8
  pop    rdi
  pop    rsi
  pop    rdx
  pop    rcx
  pop    rbx
  pop    rbp
  pop    rsp
  popfq

  ret
