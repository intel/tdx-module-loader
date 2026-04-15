#ifndef HANDLE_FILE_H_
#define HANDLE_FILE_H_

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Protocol/DevicePath.h>
#include <Guid/FileInfo.h>

// On EFI_SUCCESS, FileHandle is guaranteed to be non-NULL.
EFI_STATUS
EFIAPI
SimpleOpenFileByName (
  IN  CHAR16          *FileName,
  OUT EFI_FILE_HANDLE *FileHandle
  );

EFI_STATUS
EFIAPI
SimpleGetFileSize (
  IN  EFI_FILE_HANDLE     FileHandle,
  OUT UINT64               *FileSize
  );

EFI_STATUS
EFIAPI
SimpleLoadFile (
  IN EFI_FILE_HANDLE     FileHandle,
  IN UINT8               *Buffer,
  IN UINTN               BufferSize
  );

EFI_STATUS
EFIAPI
ResolveFilePathRelativeToLoadedImage (
  IN  EFI_HANDLE ImageHandle,
  IN  CONST CHAR16 *FileName,
  OUT CHAR16     **ResolvedPath
  );

#endif // HANDLE_FILE_H_