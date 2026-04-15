#include <Uefi.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <HandleFile.h>
#include <TopologyMgr.h>
#include <VmxContext.h>
#include <Seamldr.h>

#define TDX_BLOB_FILE_NAME  L"TDX-BLOB"

EFI_STATUS
EFIAPI
IsRunningInVm (
  VOID
  )
{
  UINT32 RegEcx;

  AsmCpuid (1, 0, 0, &RegEcx, 0);

  //
  // Bit 31 of ECX indicates whether the code is running inside a VM.
  //
  return (RegEcx & BIT31) != 0;
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_STATUS Status = EFI_SUCCESS;
  TDX_MODULE_LDR_VMX_CONTEXT    VmxContext = {0};
  CHAR16                        *ResolvedModulePath = NULL;

  if (IsRunningInVm ()) {
    Print (L"Running inside a VM. Exiting.\n");
    return EFI_SUCCESS;
  }

  /* Initialize the MP topology information */
  Status = MpTopologyConstructor ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to initialize Topology Manager: %r\n", __func__, Status));
    goto RECLAIM_EXIT;
  }

  /*
   * Load TDX-BLOB from the same filesystem path as the current UEFI application.
   */
  Status = ResolveFilePathRelativeToLoadedImage (
             ImageHandle,
             TDX_BLOB_FILE_NAME,
             &ResolvedModulePath
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to resolve TDX-BLOB path from the loaded image: %r\n", __func__, Status));
    goto RECLAIM_EXIT;
  }

  Status = CreateSeamldrParams (ResolvedModulePath);
  if (ResolvedModulePath != NULL) {
    FreePool (ResolvedModulePath);
    ResolvedModulePath = NULL;
  }
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to fill SEAMLDR parameters by the provided module: %r\n", __func__, Status));
    goto RECLAIM_EXIT;
  }

  /* Create VMX context */
  Status = CreateVmxContext (&VmxContext);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to create VMX context: %r\n", __func__, Status));
    goto RECLAIM_EXIT;
  }

  /* Execute VMXON-SEAMCALL-VMXOFF sequence on all LPs */
  Status = LaunchTdxModule (&VmxContext);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to launch TDX module: %r\n", Status));
    goto RECLAIM_EXIT;
  }

RECLAIM_EXIT:
  if (ResolvedModulePath != NULL) {
    FreePool (ResolvedModulePath);
    ResolvedModulePath = NULL;
  }

  /* Destroy VMX context */
  DestroyVmxContext (&VmxContext);

  DestroySeamldrParams ();

  /* Deinitialize the MP topology information */
  MpTopologyDestructor ();

  if (EFI_ERROR (Status)) {
    Print (L"TDX module load failed: %r\n", Status);
  } else {
    Print (L"TDX module launched successfully.\n");
  }

  return EFI_SUCCESS;
}