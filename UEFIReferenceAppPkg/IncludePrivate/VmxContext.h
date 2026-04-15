#ifndef VMX_CONTEXT_H_
#define VMX_CONTEXT_H_

#include <Uefi.h>
#include <Library/MemoryAllocationLib.h>
#include <Pi/PiDxeCis.h>
#include <Protocol/MpService.h>
#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Register/Intel/ArchitecturalMsr.h>

#define CR4_VMXE_MASK       (1 << 13)

typedef struct {
  VOID   **VmxOnRegion;
  UINT64 *VmxBasicMsr;
} TDX_MODULE_LDR_VMX_CONTEXT;

EFI_STATUS
EFIAPI
CreateVmxContext (
  OUT TDX_MODULE_LDR_VMX_CONTEXT    *VmxContext
  );

VOID
EFIAPI
DestroyVmxContext (
  IN TDX_MODULE_LDR_VMX_CONTEXT    *VmxContext
  );

EFI_STATUS
EFIAPI
LaunchTdxModule (
  IN TDX_MODULE_LDR_VMX_CONTEXT    *VmxContext
  );

#endif // VMX_CONTEXT_H_