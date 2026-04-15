#include <TopologyMgr.h>

STATIC EFI_MP_SERVICES_PROTOCOL* mMpService = NULL;

STATIC EFI_PROCESSOR_INFORMATION *mProcInfo        = NULL;
STATIC UINTN                     *mEnabledProcList = NULL;

STATIC UINTN  mNumberProcessors = 0;
STATIC UINTN  mEnabledProcessors = 0;
STATIC UINTN  mSystemBsp        = 0;

EFI_STATUS
EFIAPI
MpTopologyConstructor (
  VOID
  )
{
  EFI_STATUS Status = EFI_SUCCESS;
  UINTN	  Index;
  UINTN	 EnabledProcessorCount;
  UINTN	 NumberOfProcessors;

  if (mProcInfo != NULL &&
    mNumberProcessors != 0) {
    goto EFI_EXIT;
  }

  Status = gBS->LocateProtocol (
                  &gEfiMpServiceProtocolGuid,
                  NULL,
                  (VOID**)&mMpService
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to locate MP Service Protocol: %r\n", __func__, Status));
    goto EFI_EXIT;
  }

  Status = mMpService->GetNumberOfProcessors (
                        mMpService,
                        &NumberOfProcessors,
                        &EnabledProcessorCount
                        );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to get number of processors: %r\n", __func__, Status));
    goto EFI_EXIT;
  }

  mNumberProcessors = NumberOfProcessors;
  mEnabledProcessors = EnabledProcessorCount;

  mProcInfo = AllocateZeroPool (sizeof(EFI_PROCESSOR_INFORMATION) *
                    mNumberProcessors);
  if (mProcInfo == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    DEBUG ((DEBUG_ERROR, "%a: Failed to allocate memory for processor info: %r\n", __func__, Status));
    goto EFI_EXIT;
  }

  mEnabledProcList = AllocateZeroPool (sizeof(UINTN) * mEnabledProcessors);
  if (mEnabledProcList == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    DEBUG ((DEBUG_ERROR, "%a: Failed to allocate memory for enabled processor list: %r\n", __func__, Status));
    goto EXIT_DEALLOCATE;
  }

  EnabledProcessorCount = 0;
  for (Index = 0; Index < mNumberProcessors; Index++) {
    Status = mMpService->GetProcessorInfo (mMpService, Index,
                          &mProcInfo[Index]);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: Failed to get processor info for CPU %u: %r\n",
              __func__, Index, Status));
      goto EXIT_DEALLOCATE;
    }

    if (mProcInfo[Index].StatusFlag & PROCESSOR_AS_BSP_BIT) {
      mSystemBsp = Index;
    }

    if (mProcInfo[Index].StatusFlag & PROCESSOR_ENABLED_BIT) {
      if (EnabledProcessorCount >= mEnabledProcessors) {
        DEBUG ((DEBUG_ERROR, "%a: Enabled processor count exceeds expected topology count\n", __func__));
        Status = EFI_DEVICE_ERROR;
        goto EXIT_DEALLOCATE;
      }

      mEnabledProcList[EnabledProcessorCount++] = Index;
    }
  }

  if (EnabledProcessorCount != mEnabledProcessors) {
    DEBUG ((DEBUG_ERROR, "%a: Enabled processor count mismatch (expected %u, got %u)\n", __func__, mEnabledProcessors, EnabledProcessorCount));
    Status = EFI_DEVICE_ERROR;
    goto EXIT_DEALLOCATE;
  }

EFI_EXIT:
  return Status;

EXIT_DEALLOCATE:
  if (mEnabledProcList != NULL) {
    FreePool (mEnabledProcList);
    mEnabledProcList = NULL;
  }
  if (mProcInfo != NULL) {
    FreePool (mProcInfo);
    mProcInfo = NULL;
  }
  mNumberProcessors = 0;
  mEnabledProcessors = 0;
  mSystemBsp = 0;
  return Status;
}

EFI_STATUS
EFIAPI
MpTopologyDestructor (
  VOID
  )
{
  if (mEnabledProcList != NULL) {
    FreePool (mEnabledProcList);
    mEnabledProcList = NULL;
  }

  if (mProcInfo != NULL) {
    FreePool (mProcInfo);
    mProcInfo = NULL;
  }
  mNumberProcessors = 0;
  mEnabledProcessors = 0;
  mSystemBsp = 0;
  return EFI_SUCCESS;
}

UINTN
EFIAPI
MpGetNumberOfEnabledProcessors (
  VOID
  )
{
  return mEnabledProcessors;
}

EFI_STATUS
EFIAPI
MpGetEnabledProcessorNumber (
  IN  UINTN EnabledProcessorIndex,
  OUT UINTN *ProcessorNumber
  )
{
  if ((ProcessorNumber == NULL) ||
      (mEnabledProcList == NULL) ||
      (EnabledProcessorIndex >= mEnabledProcessors))
  {
    return EFI_INVALID_PARAMETER;
  }

  *ProcessorNumber = mEnabledProcList[EnabledProcessorIndex];
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
MpGetEnabledProcessorIndex (
  IN  UINTN ProcessorNumber,
  OUT UINTN *EnabledProcessorIndex
  )
{
  UINTN Index;

  if (EnabledProcessorIndex == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  for (Index = 0; Index < mEnabledProcessors; Index++) {
    if (mEnabledProcList[Index] == ProcessorNumber) {
      *EnabledProcessorIndex = Index;
      return EFI_SUCCESS;
    }
  }

  return EFI_NOT_FOUND;
}

STATIC
MP_DISPATCH_CONTEXT *
MpBuildPerProcessorContext (
  IN  MP_DISPATCH_CONTEXT *BaseContext,
  OUT MP_DISPATCH_CONTEXT *PerProcessorContext,
  IN  UINTN               ProcessorIndex
  )
{
  if (BaseContext == NULL) {
    return NULL;
  }

  *PerProcessorContext = *BaseContext;
  PerProcessorContext->ProcessorIndex = ProcessorIndex;
  return PerProcessorContext;
}

STATIC
EFI_STATUS
MpDispatchToEnabledProcessors (
  IN EFI_AP_PROCEDURE      Procedure,
  IN MP_DISPATCH_CONTEXT   *Context
  )
{
  EFI_STATUS          Status;
  MP_DISPATCH_CONTEXT PerProcessorContext;
  UINTN               EnabledIndex;
  UINTN               BspEnabledIndex;
  UINTN               Processor;
  BOOLEAN             BspFound;

  BspFound = FALSE;

  for (EnabledIndex = 0; EnabledIndex < mEnabledProcessors; EnabledIndex++) {
    Status = MpGetEnabledProcessorNumber (EnabledIndex, &Processor);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if (Processor == mSystemBsp) {
      BspFound = TRUE;
      continue;
    }

    DEBUG ((DEBUG_INFO, "%a: Starting AP %u\n", __func__, Processor));

    Status = mMpService->StartupThisAP (
                          mMpService,
                          Procedure,
                          Processor,
                          NULL,
                          0,
                          MpBuildPerProcessorContext (Context, &PerProcessorContext, EnabledIndex),
                          NULL
                          );
    if (EFI_ERROR (Status)) {
      if (Status == EFI_NOT_STARTED) {
        // The processor state may have changed since topology enumeration, skip it.
        continue;
      }

      DEBUG ((DEBUG_ERROR, "%a: Failed to start AP %u: %r\n", __func__, Processor, Status));
      return Status;
    }
  }

  if (!BspFound) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to locate BSP in enabled processor list\n", __func__));
    return EFI_DEVICE_ERROR;
  }

  DEBUG ((DEBUG_INFO, "%a Starting BSP %u\n", __func__, mSystemBsp));

  BspEnabledIndex = 0;
  if (Context != NULL) {
    Status = MpGetEnabledProcessorIndex (mSystemBsp, &BspEnabledIndex);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: Failed to resolve BSP enabled index: %r\n", __func__, Status));
      return Status;
    }
  }

  Procedure (MpBuildPerProcessorContext (Context, &PerProcessorContext, BspEnabledIndex));

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
MpDispatch (
  IN EFI_AP_PROCEDURE		  Procedure,
  IN MP_DISPATCH_CONTEXT	*Context
  )
{
  EFI_STATUS Status = EFI_SUCCESS;

  if (!Procedure) {
    return EFI_INVALID_PARAMETER;
  }

  Status = MpDispatchToEnabledProcessors (Procedure, Context);
  return Status;
}