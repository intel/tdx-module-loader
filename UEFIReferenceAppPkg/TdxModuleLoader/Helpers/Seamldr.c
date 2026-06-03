#include <Seamldr.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <HandleFile.h>
#include <AsmSeamcall.h>

STATIC SEAMLDR_PARAMS_t* mSeamldrParams = NULL;
STATIC TDX_IMAGE_t* mTdxImage = NULL;
STATIC UINTN mImageSize = 0;

BOOLEAN
EFIAPI
VerifyChecksum (
  VOID
  )
{
  UINTN   Size;
  UINT16  Checksum = 0;
  UINT16  *Ptr = NULL;

  Size = mImageSize;
  if (Size % sizeof(UINT16)) {
    Checksum += *((UINT8 *)mTdxImage + Size - 1);
    Size--;
  }

  Ptr = (UINT16 *)mTdxImage;
  for (UINTN Index = 0; Index < Size; Index += sizeof(UINT16)) {
    Checksum += *Ptr;
    Ptr++;
  }

  return (Checksum == 0);
}

EFI_STATUS
EFIAPI
SanityCheckTdxImage (
  VOID
  )
{
  UINT64 SigstructSize;
  UINT64 ModuleSize;
  UINT64 ExpectedImageSize;

  if (!mTdxImage) {
    DEBUG ((DEBUG_ERROR, "%a: TDX module image is not loaded\n", __func__));
    return EFI_INVALID_PARAMETER;
  }

  if (mImageSize < sizeof(TDX_IMAGE_HEADER_t)) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid TDX module image: size too small (%llu bytes)\n", __func__, mImageSize));
    return EFI_LOAD_ERROR;
  }

  if (mTdxImage->Header.Version != TDX_IMAGE_VERSION_2) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid TDX module image: unsupported version 0x%x\n", __func__, mTdxImage->Header.Version));
    return EFI_LOAD_ERROR;
  }

  if (CompareMem (mTdxImage->Header.Signature, "TDX-BLOB", sizeof(mTdxImage->Header.Signature))) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid TDX module image: bad signature\n", __func__));
    return EFI_LOAD_ERROR;
  }

  if (mTdxImage->Header.ModuleNumPages == 0) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid TDX module image: empty module payload\n", __func__));
    return EFI_LOAD_ERROR;
  }

  if (mTdxImage->Header.SigstructNumPages > SEAMLDR_PARAMS_NUM_SIG_PAGES ||
      mTdxImage->Header.ModuleNumPages > SEAMLDR_PARAMS_NUM_MOD_PAGES) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid TDX module image: invalid page counts (sigstruct %u, module %u)\n", __func__, mTdxImage->Header.SigstructNumPages, mTdxImage->Header.ModuleNumPages));
    return EFI_LOAD_ERROR;
  }

  if (!IsZeroBuffer (mTdxImage->Header.Reserved, sizeof(mTdxImage->Header.Reserved))) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid TDX module image: reserved bytes must be zero\n", __func__));
    return EFI_LOAD_ERROR;
  }

  if (!VerifyChecksum ()) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid TDX module image: bad checksum\n", __func__));
    return EFI_LOAD_ERROR;
  }

  SigstructSize = (UINT64)mTdxImage->Header.SigstructNumPages * EFI_PAGE_SIZE;
  ModuleSize = (UINT64)mTdxImage->Header.ModuleNumPages * EFI_PAGE_SIZE;
  ExpectedImageSize = sizeof(TDX_IMAGE_HEADER_t) + SigstructSize + ModuleSize;
  if (ExpectedImageSize != mImageSize) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid TDX module image: size mismatch (expected %llu, got %llu)\n", __func__, ExpectedImageSize, mImageSize));
    return EFI_LOAD_ERROR;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
CreateSeamldrParams (
  IN  CHAR16*              ModulePath
  )
{
  EFI_STATUS      Status = EFI_SUCCESS;
  EFI_FILE_HANDLE FileHandle = NULL;
  UINT64          FileSize = 0;
  VOID            *Buffer = NULL;
  UINTN           SigstructSize = 0;
  UINT64          NumberModPages = 0;
  UINT8           *Ptr;

  Status = SimpleOpenFileByName (
              ModulePath,
              &FileHandle
              );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to open module file %s: %r\n", __func__, ModulePath, Status));
    return Status;
  }

  if (FileHandle == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Opened module file %s without a valid file handle\n", __func__, ModulePath));
    return EFI_DEVICE_ERROR;
  }

  Status = SimpleGetFileSize (
              FileHandle,
              &FileSize
              );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to get size of module file %s: %r\n", __func__, ModulePath, Status));
    goto EFI_EXIT;
  }

  if (FileSize < sizeof(TDX_IMAGE_HEADER_t)) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid module file %s: size too small (%llu bytes)\n", __func__, ModulePath, FileSize));
    Status = EFI_LOAD_ERROR;
    goto EFI_EXIT;
  }

  Buffer = AllocatePages (EFI_SIZE_TO_PAGES (FileSize));
  if (!Buffer) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to allocate memory for module file %s\n", __func__, ModulePath));
    Status = EFI_OUT_OF_RESOURCES;
    goto EFI_EXIT;
  }

  ZeroMem (Buffer, EFI_SIZE_TO_PAGES (FileSize) * EFI_PAGE_SIZE);

  Status = SimpleLoadFile (
              FileHandle,
              (UINT8*)Buffer,
              FileSize
              );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to read module file %s: %r\n", __func__, ModulePath, Status));
    goto EFI_EXIT;
  }

  mTdxImage = (TDX_IMAGE_t*)Buffer;
  mImageSize = FileSize;

  Status = SanityCheckTdxImage ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid TDX module image in file %s: %r\n", __func__, ModulePath, Status));
    goto EFI_EXIT;
  }

  NumberModPages = mTdxImage->Header.ModuleNumPages;
  SigstructSize = mTdxImage->Header.SigstructNumPages * EFI_PAGE_SIZE;

  mSeamldrParams = (SEAMLDR_PARAMS_t*)AllocatePages (EFI_SIZE_TO_PAGES (sizeof(*mSeamldrParams)));
  if (!mSeamldrParams) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to allocate memory for SEAMLDR parameters\n", __func__));
    Status = EFI_OUT_OF_RESOURCES;
    goto EFI_EXIT;
  }

  // version and reserved bits must be 0
  ZeroMem (mSeamldrParams, sizeof(*mSeamldrParams));

  mSeamldrParams->scenario = 0; // Load

  mSeamldrParams->sigstruct_pa = (UINT64)mTdxImage->Payload;

  mSeamldrParams->num_module_pages = NumberModPages;

  Ptr = (UINT8*)mTdxImage->Payload + SigstructSize;

  /*
   * sigstruct_pa and mod_pages_pa_list[] must be 4K-aligned. The well-designed
   * TdxImage can ensure they are aligned once TdxImage is 4k-aligned.
   */
  for (UINTN Index = 0; Index < mSeamldrParams->num_module_pages; Index++) {
    mSeamldrParams->mod_pages_pa_list[Index] = (UINT64)Ptr;
    Ptr += EFI_PAGE_SIZE;
  }

EFI_EXIT:
  FileHandle->Close (FileHandle);
  return Status;
}

VOID
EFIAPI
DestroySeamldrParams (
  VOID
  )
{
  if (mSeamldrParams) {
    FreePages (mSeamldrParams, EFI_SIZE_TO_PAGES (sizeof(*mSeamldrParams)));
    mSeamldrParams = NULL;
  }

  if (mTdxImage) {
    FreePages (mTdxImage, EFI_SIZE_TO_PAGES (mImageSize));
    mTdxImage = NULL;
    mImageSize = 0;
  }

}

UINT64
EFIAPI
SeamldrInstallModule (
  VOID
  )
{
  UINT64 SeamcallStatus = 0;

  if (!mSeamldrParams) {
    DEBUG ((DEBUG_ERROR, "%a: SEAMLDR parameters are not initialized\n", __func__));
    return 0;
  }

  SeamcallStatus = AsmSeamcall (ASM_SEAMCALL_LEAF_SEAMLDR_INSTALL, (UINT64)(UINTN)mSeamldrParams);

  return SeamcallStatus;
}