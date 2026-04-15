#ifndef TOPOLOGY_MGR_H_
#define TOPOLOGY_MGR_H_

#include <Uefi.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Pi/PiDxeCis.h>
#include <Protocol/MpService.h>
#include <Library/UefiLib.h>
#include <Library/DebugLib.h>

typedef struct {
  UINT64 *Address;
  UINT64 Size;
} MP_DISPATCH_BUFFER;

typedef struct {
  EFI_STATUS         *ReturnValue;
  MP_DISPATCH_BUFFER *DispatchBuffer;
  UINT64             Length;
  UINTN              ProcessorIndex;
} MP_DISPATCH_CONTEXT;

EFI_STATUS
EFIAPI
MpTopologyConstructor (
  VOID
  );

EFI_STATUS
EFIAPI
MpTopologyDestructor (
  VOID
  );

UINTN
EFIAPI
MpGetNumberOfEnabledProcessors (
  VOID
  );

EFI_STATUS
EFIAPI
MpGetEnabledProcessorNumber (
  IN  UINTN EnabledProcessorIndex,
  OUT UINTN *ProcessorNumber
  );

EFI_STATUS
EFIAPI
MpDispatch (
  IN EFI_AP_PROCEDURE		  Procedure,
  IN MP_DISPATCH_CONTEXT	*Context
  );

#endif // TOPOLOGY_MGR_H_