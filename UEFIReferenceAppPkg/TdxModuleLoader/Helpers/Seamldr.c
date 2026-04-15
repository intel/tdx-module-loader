#include <Seamldr.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <HandleFile.h>
#include <AsmSeamcall.h>

STATIC SEAMLDR_PARAMS_t* mSeamldrParams = NULL;
STATIC SEAM_BLOB_HEADER_t* mTdxBlob = NULL;
STATIC UINTN mBlobSize = 0;

BOOLEAN
EFIAPI
VerifyChecksum (
  VOID
  )
{
  UINT32 size = mTdxBlob->len;
  UINT16 checksum = 0;
  UINT16 *ptr = NULL;

  /* Handle the last byte if the size is odd */
  if (size % 2) {
    checksum += *((UINT8*)mTdxBlob + size - 1);
    size--;
  }

  ptr = (UINT16*)mTdxBlob;
  for (UINTN Index = 0; Index < size; Index += 2) {
    checksum += *ptr;
    ptr++;
  }

  return (checksum == 0);
}

EFI_STATUS
EFIAPI
SanityCheckTdxBlob (
  IN UINTN ModuleSize,
  IN UINTN SigSize
  )
{
  if (!mTdxBlob) {
    DEBUG ((DEBUG_ERROR, "%a: TDX module blob is not loaded\n", __func__));
    return EFI_INVALID_PARAMETER;
  }

  if (mBlobSize != mTdxBlob->len) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid TDX module blob: size mismatch (expected %u, got %llu)\n", __func__, mTdxBlob->len, mBlobSize));
    return EFI_LOAD_ERROR;
  }

  if (CompareMem (mTdxBlob->signature, "TDX-BLOB", 8)) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid TDX module blob: bad signature\n", __func__));
    return EFI_LOAD_ERROR;
}

  if (ModuleSize == 0) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid TDX module blob: empty module payload\n", __func__));
    return EFI_LOAD_ERROR;
  }

  if (ModuleSize > SEAMLDR_PARAMS_NUM_MOD_PAGES * SIZE_4KB) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid TDX module blob: module size too large (%u bytes)\n", __func__, ModuleSize));
    return EFI_LOAD_ERROR;
  }

  if (SigSize != SIZE_4KB) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid TDX module blob: invalid signature size (%u bytes)\n", __func__, SigSize));
    return EFI_LOAD_ERROR;
  }

  if (!VerifyChecksum ()) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid TDX module blob: bad checksum\n", __func__));
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
  UINTN           ModuleSize = 0;
  UINTN           SigSize = 0;
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

  if (FileSize < sizeof(SEAM_BLOB_HEADER_t)) {
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

  mTdxBlob = (SEAM_BLOB_HEADER_t*)Buffer;
  mBlobSize = FileSize;

  ModuleSize = mBlobSize - mTdxBlob->offset_of_module;
  NumberModPages = DIV_ROUND_UP(ModuleSize, SIZE_4KB);
  SigSize = mTdxBlob->offset_of_module - sizeof(SEAM_BLOB_HEADER_t);

  Status = SanityCheckTdxBlob (ModuleSize, SigSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid TDX module blob in file %s: %r\n", __func__, ModulePath, Status));
    goto EFI_EXIT;
  }

  mSeamldrParams = (SEAMLDR_PARAMS_t*)AllocatePages (EFI_SIZE_TO_PAGES (sizeof(*mSeamldrParams)));
  if (!mSeamldrParams) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to allocate memory for SEAMLDR parameters\n", __func__));
    Status = EFI_OUT_OF_RESOURCES;
    goto EFI_EXIT;
  }

  // version and reserved bits must be 0
  ZeroMem (mSeamldrParams, sizeof(*mSeamldrParams));

  mSeamldrParams->scenario = 0; // Load

  mSeamldrParams->sigstruct_pa = (UINT64)mTdxBlob->data;
  mSeamldrParams->num_module_pages = NumberModPages;

  Ptr = (UINT8*)mTdxBlob + mTdxBlob->offset_of_module;

  /*
   * sigstruct_pa and mod_pages_pa_list[] must be 4K-aligned. The well-designed
   * TdxBlob can ensure they are aligned once TdxBlob is 4k-aligned.
   */
  for (UINTN Index = 0; Index < mSeamldrParams->num_module_pages; Index++) {
    mSeamldrParams->mod_pages_pa_list[Index] = (UINT64)Ptr;
    Ptr += SIZE_4KB;
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

  if (mTdxBlob) {
    FreePages (mTdxBlob, EFI_SIZE_TO_PAGES (mBlobSize));
    mTdxBlob = NULL;
    mBlobSize = 0;
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