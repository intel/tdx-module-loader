#include <VmxContext.h>
#include <TopologyMgr.h>
#include <Seamldr.h>

EFI_STATUS
EFIAPI
PopulateVmxContextWithCapabilitiesLpWorker (
  IN MP_DISPATCH_CONTEXT *Context
  )
{
  EFI_STATUS Status = EFI_INVALID_PARAMETER;
  UINTN      EnabledProcessors = 0;

  UINT64 *VmxBasic = NULL;

  EnabledProcessors = MpGetNumberOfEnabledProcessors ();

  if (!Context ||
      !Context->DispatchBuffer ||
      Context->Length != 1 ||
      (Context->ProcessorIndex >= EnabledProcessors) ||
      !Context->DispatchBuffer->Address ||
      Context->DispatchBuffer->Size != sizeof(*VmxBasic) * EnabledProcessors) {
    goto EFI_EXIT;
  }

  VmxBasic = (UINT64 *)Context->DispatchBuffer->Address;
  VmxBasic[Context->ProcessorIndex] = AsmReadMsr64 (MSR_IA32_VMX_BASIC);
  Status = EFI_SUCCESS;

EFI_EXIT:
  if (Context && Context->ReturnValue && (Context->ProcessorIndex < EnabledProcessors)) {
    Context->ReturnValue[Context->ProcessorIndex] = Status;
  }

  return Status;
}

EFI_STATUS
EFIAPI
PopulateVmxContextWithCapabilities (
  OUT TDX_MODULE_LDR_VMX_CONTEXT    *VmxContext
  )
{
  EFI_STATUS Status = EFI_INVALID_PARAMETER;
  UINTN      Index  = 0;
  UINTN      EnabledProcessors = 0;

  UINT64              *VmxBasicMsr = NULL;
  MP_DISPATCH_CONTEXT DispatchContext = { 0 };
  MP_DISPATCH_BUFFER  DispatchBuffer;

  if (!VmxContext ||
      !VmxContext->VmxOnRegion ||
      !VmxContext->VmxBasicMsr) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid VMX context or region pointers\n", __func__));
    goto EFI_EXIT;
  }

  EnabledProcessors = MpGetNumberOfEnabledProcessors ();
  VmxBasicMsr = AllocateZeroPool (sizeof(*VmxBasicMsr) * EnabledProcessors);
  if (!VmxBasicMsr) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to allocate memory for VMX basic MSR values\n", __func__));
    Status = EFI_OUT_OF_RESOURCES;
    goto EFI_EXIT;
  }

  ZeroMem (&DispatchBuffer, sizeof(DispatchBuffer));
  DispatchBuffer.Address = (VOID *)VmxBasicMsr;
  DispatchBuffer.Size    = sizeof(*VmxBasicMsr) * EnabledProcessors;

  DispatchContext.ReturnValue = AllocateZeroPool (sizeof(EFI_STATUS) * EnabledProcessors);
  if (!DispatchContext.ReturnValue) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to allocate memory for dispatch return values\n", __func__));
    Status = EFI_OUT_OF_RESOURCES;
    goto EFI_EXIT;
  }
  DispatchContext.DispatchBuffer = &DispatchBuffer;
  DispatchContext.Length         = 1;

  Status = MpDispatch ((EFI_AP_PROCEDURE)PopulateVmxContextWithCapabilitiesLpWorker, &DispatchContext);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to dispatch to all processors: %r\n", __func__, Status));
    goto EFI_EXIT;
  }

  for (Index = 0; Index < EnabledProcessors; Index++) {
    if (EFI_ERROR (DispatchContext.ReturnValue[Index])) {
      DEBUG ((DEBUG_ERROR, "%a: Failed to populate VMX capabilities on enabled CPU[%03x]: %r\n", __func__, Index, DispatchContext.ReturnValue[Index]));
      Status = DispatchContext.ReturnValue[Index];
      goto EFI_EXIT;
    }
  }

  for (Index = 0; Index < EnabledProcessors; Index++) {
    VmxContext->VmxBasicMsr[Index] = VmxBasicMsr[Index];
  }

EFI_EXIT:
  if (DispatchContext.ReturnValue) {
    FreePool (DispatchContext.ReturnValue);
  }
  if (VmxBasicMsr) {
    FreePool (VmxBasicMsr);
  }

  return Status;
}

EFI_STATUS
EFIAPI
PopulateVmxContextWithRegions (
  OUT TDX_MODULE_LDR_VMX_CONTEXT    *VmxContext
  )
{
  EFI_STATUS Status = EFI_INVALID_PARAMETER;
  UINTN      Index  = 0;
  UINTN      Processor = 0;
  UINTN      EnabledProcessors = 0;
  UINTN      CurrentRegionSize = 0;
  UINTN      CurrentRegionSizeInPages = 0;
  UINT64     *CurrentRegion = NULL;

  MSR_IA32_VMX_BASIC_REGISTER CurrentVmxBasic;

  if (!VmxContext ||
      !VmxContext->VmxBasicMsr) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid VMX context or region/basic pointers\n", __func__));
    goto EFI_EXIT;
  }

  EnabledProcessors = MpGetNumberOfEnabledProcessors ();
  for (Index = 0; Index < EnabledProcessors; Index++) {
    Status = MpGetEnabledProcessorNumber (Index, &Processor);
    if (EFI_ERROR (Status)) {
      goto EFI_EXIT;
    }

    CurrentVmxBasic.Uint64 = VmxContext->VmxBasicMsr[Index];
    CurrentRegionSize = CurrentVmxBasic.Bits.VmcsSize;
    if (CurrentRegionSize == 0) {
      DEBUG ((DEBUG_ERROR, "%a: Invalid VMX region size on CPU[%03x]\n", __func__, Processor));
      Status = EFI_DEVICE_ERROR;
      goto EFI_EXIT;
    }

    CurrentRegionSizeInPages = EFI_SIZE_TO_PAGES (CurrentRegionSize);

    VmxContext->VmxOnRegion[Index] = AllocatePages (CurrentRegionSizeInPages);
    if (!VmxContext->VmxOnRegion[Index]) {
      DEBUG ((DEBUG_ERROR, "%a: Failed to allocate memory for VMX region on CPU[%03x]\n", __func__, Processor));
      Status = EFI_OUT_OF_RESOURCES;
      goto EFI_EXIT;
    }

    CurrentRegion = (UINT64 *)VmxContext->VmxOnRegion[Index];
    ZeroMem(CurrentRegion, EFI_PAGES_TO_SIZE (CurrentRegionSizeInPages));
    CurrentRegion[0] = CurrentVmxBasic.Uint64 & 0x000000007FFFFFFF; // VMCS revision identifier
  }

  Status = EFI_SUCCESS;

EFI_EXIT:
  return Status;
}

EFI_STATUS
EFIAPI
CreateVmxContext (
  OUT TDX_MODULE_LDR_VMX_CONTEXT    *VmxContext
  )
{
  EFI_STATUS Status            = EFI_INVALID_PARAMETER;
  UINTN      EnabledProcessors = 0;

  if (!VmxContext) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid VMX context", __func__));
    goto EFI_EXIT;
  }

  EnabledProcessors = MpGetNumberOfEnabledProcessors ();
  VmxContext->VmxOnRegion = AllocateZeroPool (sizeof(VOID*) * EnabledProcessors);
  if (!VmxContext->VmxOnRegion) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to allocate memory for VMX region pointers", __func__));
    Status = EFI_OUT_OF_RESOURCES;
    goto EFI_EXIT;
  }

  VmxContext->VmxBasicMsr = AllocateZeroPool (sizeof(UINT64) * EnabledProcessors);
  if (!VmxContext->VmxBasicMsr) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to allocate memory for VMX basic MSR values", __func__));
    Status = EFI_OUT_OF_RESOURCES;
    goto EFI_EXIT;
  }

  Status = PopulateVmxContextWithCapabilities (VmxContext);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to populate VMX context with capabilities: %r\n", __func__, Status));
    goto EFI_EXIT;
  }

  Status = PopulateVmxContextWithRegions (VmxContext);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to populate VMX context with regions: %r\n", __func__, Status));
    goto EFI_EXIT;
  }

EFI_EXIT:
  return Status;
}

VOID
EFIAPI
DestroyVmxContext (
  IN TDX_MODULE_LDR_VMX_CONTEXT    *VmxContext
  )
{
  UINTN      Index  = 0;
  UINTN      EnabledProcessors = 0;

  MSR_IA32_VMX_BASIC_REGISTER VmxBasic;

  if (!VmxContext) {
    return;
  }

  if (!VmxContext->VmxOnRegion && !VmxContext->VmxBasicMsr) {
    return;
  }

  EnabledProcessors = MpGetNumberOfEnabledProcessors ();
  for (Index = 0; Index < EnabledProcessors; Index++) {
    if (VmxContext->VmxOnRegion && VmxContext->VmxOnRegion[Index]) {
      VmxBasic.Uint64 = VmxContext->VmxBasicMsr[Index];
      FreePages (VmxContext->VmxOnRegion[Index], EFI_SIZE_TO_PAGES (VmxBasic.Bits.VmcsSize));
      VmxContext->VmxOnRegion[Index] = NULL;
    }
  }

  if (VmxContext->VmxOnRegion) {
    FreePool (VmxContext->VmxOnRegion);
    VmxContext->VmxOnRegion = NULL;
  }

  if (VmxContext->VmxBasicMsr) {
    FreePool (VmxContext->VmxBasicMsr);
    VmxContext->VmxBasicMsr = NULL;
  }

  return;
}

VOID
EFIAPI
ConfigureCrsForVmx (
  VOID
  )
{
  UINT64 Cr0       = 0;
  UINT64 Cr0Fixed1 = 0;
  UINT64 Cr0Fixed0 = 0;

  UINT64 Cr4       = 0;
  UINT64 Cr4Fixed1 = 0;
  UINT64 Cr4Fixed0 = 0;

  Cr0 = AsmReadCr0 ();
  Cr0Fixed0 = AsmReadMsr64 (MSR_IA32_VMX_CR0_FIXED0);
  Cr0Fixed1 = AsmReadMsr64 (MSR_IA32_VMX_CR0_FIXED1);
  Cr0 = (Cr0 | Cr0Fixed0) & Cr0Fixed1;
  AsmWriteCr0 (Cr0);

  Cr4 = AsmReadCr4 ();
  Cr4Fixed0 = AsmReadMsr64 (MSR_IA32_VMX_CR4_FIXED0);
  Cr4Fixed1 = AsmReadMsr64 (MSR_IA32_VMX_CR4_FIXED1);
  Cr4 = (Cr4 | Cr4Fixed0) & Cr4Fixed1;
  AsmWriteCr4 (Cr4);
}

VOID
EFIAPI
SetCr4Vmxe (
  IN BOOLEAN Enable
  )
{
  UINT64 Cr4 = AsmReadCr4 ();

  if (Enable) {
    Cr4 |= CR4_VMXE_MASK;
  } else {
    Cr4 &= ~CR4_VMXE_MASK;
  }

  AsmWriteCr4 (Cr4);
}

EFI_STATUS
EFIAPI
VmxOnHelper (
  IN UINT64 VmxonRegionPhysicalAddress
  )

{
  union {
    struct { UINT8 fail_invalid, fail_valid; } b;
    UINT16 raw;
  } status = {0};

  if (!VmxonRegionPhysicalAddress) {
    return EFI_PROTOCOL_ERROR; // VmFailInvalid
  }

  __asm__ __volatile__ (
    "movq %[VmxonRegion], %%rax\n\t"
    "vmxon (%%rax)\n\t"
    "setc  %%al\n\t"   // CF=1: VmFailInvalid
    "setz  %%bl\n\t"   // ZF=1: VmFailValid
    "movb %%al, %[fail_invalid]\n\t"
    "movb %%bl, %[fail_valid]\n\t"
    : [fail_invalid] "=r" (status.b.fail_invalid),
      [fail_valid]   "=r" (status.b.fail_valid)
    : [VmxonRegion] "r" (VmxonRegionPhysicalAddress)
    : "rax", "rbx", "memory", "cc"
  );

  if (status.b.fail_invalid) {
    return EFI_PROTOCOL_ERROR; // VmFailInvalid
  } else if (status.b.fail_valid) {
    return EFI_LOAD_ERROR; // VmFailValid
  } else {
    return EFI_SUCCESS;
  }
}

EFI_STATUS
EFIAPI
VmxOffHelper (
  VOID
  )
{
  union {
    struct { UINT8 fail_invalid, fail_valid; } b;
    UINT16 raw;
  } status = {0};

  __asm__ __volatile__ (
    "vmxoff\n\t"
    "setc  %%al\n\t"   // CF=1: VmFailInvalid
    "setz  %%bl\n\t"   // ZF=1: VmFailValid
    "movb %%al, %[fail_invalid]\n\t"
    "movb %%bl, %[fail_valid]\n\t"
    : [fail_invalid] "=r" (status.b.fail_invalid),
      [fail_valid]   "=r" (status.b.fail_valid)
    :
    : "rax", "rbx", "memory", "cc"
  );

  if (status.b.fail_invalid) {
    return EFI_PROTOCOL_ERROR; // VmFailInvalid
  } else if (status.b.fail_valid) {
    return EFI_LOAD_ERROR; // VmFailValid
  } else {
    return EFI_SUCCESS;
  }
}

EFI_STATUS
EFIAPI
LaunchTdxModuleLpWorker (
  IN MP_DISPATCH_CONTEXT *Context
  )
{
  EFI_STATUS Status = EFI_INVALID_PARAMETER;
  UINT64     VmxOnRegion = 0;
  UINTN      EnabledProcessors = 0;
  UINT64  *ReturnSeamcallStatus = NULL;
  EFI_STATUS VmxStatus = EFI_SUCCESS;
  UINT64  SeamcallStatus = 0;

  TDX_MODULE_LDR_VMX_CONTEXT *VmxContext = NULL;

  EnabledProcessors = MpGetNumberOfEnabledProcessors ();

  if (!Context ||
      !Context->DispatchBuffer ||
      Context->Length != 2 || // One buffer for VMX context, one for Seamcall status
      (Context->ProcessorIndex >= EnabledProcessors) ||
      !Context->DispatchBuffer[0].Address ||
      Context->DispatchBuffer[0].Size != sizeof(*VmxContext) ||
      !Context->DispatchBuffer[1].Address ||
      Context->DispatchBuffer[1].Size != sizeof(UINT64) * EnabledProcessors) {
    goto EFI_EXIT;
  }

  VmxContext = (TDX_MODULE_LDR_VMX_CONTEXT *)Context->DispatchBuffer[0].Address;
  ReturnSeamcallStatus = (UINT64 *)Context->DispatchBuffer[1].Address;

  VmxOnRegion = (UINT64)VmxContext->VmxOnRegion[Context->ProcessorIndex];

  ConfigureCrsForVmx();
  VmxStatus = VmxOnHelper ((UINT64)&VmxOnRegion);
  if (EFI_ERROR (VmxStatus)) {
    goto CLEAR_VMXE;
  }

  SeamcallStatus = SeamldrInstallModule ();
  ReturnSeamcallStatus[Context->ProcessorIndex] = SeamcallStatus;
  if (SeamcallStatus != 0) {
    Status = EFI_DEVICE_ERROR;
  } else {
    Status = EFI_SUCCESS;
  }

  VmxStatus = VmxOffHelper ();
  if (EFI_ERROR (VmxStatus)) {
    goto CLEAR_VMXE;
  }

CLEAR_VMXE:
  SetCr4Vmxe (FALSE);
EFI_EXIT:
  if (Context && Context->ReturnValue && (Context->ProcessorIndex < EnabledProcessors)) {
    Context->ReturnValue[Context->ProcessorIndex] = Status | VmxStatus;
  }

  return Status;
}

EFI_STATUS
EFIAPI
LaunchTdxModule (
  IN TDX_MODULE_LDR_VMX_CONTEXT    *VmxContext
  )
{
  EFI_STATUS Status = EFI_INVALID_PARAMETER;
  UINTN      Index  = 0;
  UINTN      Processor = 0;
  UINTN      EnabledProcessors = 0;
  UINT64     *SeamcallStatusBuffer = NULL;

  MP_DISPATCH_CONTEXT DispatchContext = { 0 };
  MP_DISPATCH_BUFFER  DispatchBuffer[2];

  if (!VmxContext) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid VMX context or region pointers\n", __func__));
    goto EFI_EXIT;
  }

  EnabledProcessors = MpGetNumberOfEnabledProcessors ();

  ZeroMem (&DispatchBuffer, sizeof(DispatchBuffer));
  DispatchBuffer[0].Address = (VOID *)VmxContext;
  DispatchBuffer[0].Size    = sizeof(*VmxContext);

  SeamcallStatusBuffer = AllocateZeroPool (sizeof(UINT64) * EnabledProcessors);
  if (!SeamcallStatusBuffer) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to allocate memory for Seamcall status buffer\n", __func__));
    Status = EFI_OUT_OF_RESOURCES;
    goto EFI_EXIT;
  }
  DispatchBuffer[1].Address = (VOID *)SeamcallStatusBuffer;
  DispatchBuffer[1].Size    = sizeof(UINT64) * EnabledProcessors;

  DispatchContext.ReturnValue   = AllocateZeroPool (sizeof(EFI_STATUS) * EnabledProcessors);
  if (!DispatchContext.ReturnValue) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to allocate memory for dispatch return values\n", __func__));
    Status = EFI_OUT_OF_RESOURCES;
    goto EFI_EXIT;
  }
  DispatchContext.DispatchBuffer = DispatchBuffer;
  DispatchContext.Length         = ARRAY_SIZE (DispatchBuffer);

  Status = MpDispatch ((EFI_AP_PROCEDURE)LaunchTdxModuleLpWorker, &DispatchContext);
  if (EFI_ERROR (Status)) {
      goto EFI_EXIT;
  }

  for (Index = 0; Index < EnabledProcessors; Index++) {
    Status = MpGetEnabledProcessorNumber (Index, &Processor);
    if (EFI_ERROR (Status)) {
      goto EFI_EXIT;
    }

    if (EFI_ERROR (DispatchContext.ReturnValue[Index])) {
      DEBUG ((
        DEBUG_ERROR,
        "%a: Failed to launch TDX module on CPU[%03x]: Status %r, SeamcallStatus 0x%llx\n",
        __func__,
        Processor,
        DispatchContext.ReturnValue[Index],
        SeamcallStatusBuffer[Index]
        ));
      Status = DispatchContext.ReturnValue[Index];
      goto EFI_EXIT;
    }
  }

  Status = EFI_SUCCESS;

EFI_EXIT:
  if (DispatchContext.ReturnValue) {
    FreePool (DispatchContext.ReturnValue);
  }

  if (SeamcallStatusBuffer) {
    FreePool (SeamcallStatusBuffer);
  }

  return Status;
}