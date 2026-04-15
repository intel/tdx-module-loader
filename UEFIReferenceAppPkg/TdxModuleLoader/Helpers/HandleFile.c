#include <HandleFile.h>

EFI_STATUS
EFIAPI
ResolveFilePathRelativeToLoadedImage (
  IN  EFI_HANDLE    ImageHandle,
  IN  CONST CHAR16  *FileName,
  OUT CHAR16        **ResolvedPath
  )
{
  EFI_STATUS                Status;
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
  EFI_DEVICE_PATH_PROTOCOL  *Node;
  FILEPATH_DEVICE_PATH      *FilePathNode;
  CHAR16                    *ImagePath;
  UINTN                     ImagePathLen;
  UINTN                     ImagePathCapacity;
  UINTN                     FragmentLen;
  UINTN                     DirLen;
  UINTN                     FileNameLen;
  UINTN                     Index;
  BOOLEAN                   FoundFilePathNode;

  if ((FileName == NULL) || (ResolvedPath == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *ResolvedPath = NULL;

  Status = gBS->OpenProtocol (
                  ImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **)&LoadedImage,
                  ImageHandle,
                  NULL,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (LoadedImage->FilePath == NULL) {
    return EFI_NOT_FOUND;
  }

  FilePathNode      = NULL;
  ImagePath         = NULL;
  ImagePathLen      = 0;
  ImagePathCapacity = 0;
  FoundFilePathNode = FALSE;

  for (Node = LoadedImage->FilePath; !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
    if ((DevicePathType (Node) == MEDIA_DEVICE_PATH) &&
        (DevicePathSubType (Node) == MEDIA_FILEPATH_DP))
    {
      FilePathNode = (FILEPATH_DEVICE_PATH *)Node;
      FragmentLen  = StrLen (FilePathNode->PathName);

      ImagePathCapacity += FragmentLen + 1;
      FoundFilePathNode  = TRUE;
    }
  }

  if (!FoundFilePathNode) {
    return EFI_NOT_FOUND;
  }

  ImagePath = AllocateZeroPool (ImagePathCapacity * sizeof (CHAR16));
  if (ImagePath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  ImagePathLen = 0;
  for (Node = LoadedImage->FilePath; !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
    if ((DevicePathType (Node) != MEDIA_DEVICE_PATH) ||
        (DevicePathSubType (Node) != MEDIA_FILEPATH_DP))
    {
      continue;
    }

    FilePathNode = (FILEPATH_DEVICE_PATH *)Node;
    FragmentLen  = StrLen (FilePathNode->PathName);
    if (FragmentLen == 0) {
      continue;
    }

    if ((ImagePathLen == 0) && (FilePathNode->PathName[0] != L'\\')) {
      ImagePath[ImagePathLen++] = L'\\';
    } else if ((ImagePathLen != 0) &&
               (ImagePath[ImagePathLen - 1] != L'\\') &&
               (FilePathNode->PathName[0] != L'\\'))
    {
      ImagePath[ImagePathLen++] = L'\\';
    }

    CopyMem (ImagePath + ImagePathLen, FilePathNode->PathName, FragmentLen * sizeof (CHAR16));
    ImagePathLen += FragmentLen;
  }

  if (ImagePathLen == 0) {
    Status = EFI_NOT_FOUND;
    goto Exit;
  }

  FileNameLen = StrLen (FileName);
  DirLen = 0;

  for (Index = ImagePathLen; Index > 0; Index--) {
    if (ImagePath[Index - 1] == L'\\') {
      DirLen = Index;
      break;
    }
  }

  if (DirLen == 0) {
    DirLen = 1;
  }

  *ResolvedPath = AllocateZeroPool ((DirLen + FileNameLen + 1) * sizeof (CHAR16));
  if (*ResolvedPath == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  if (DirLen == 1) {
    (*ResolvedPath)[0] = L'\\';
  } else {
    CopyMem (*ResolvedPath, ImagePath, DirLen * sizeof (CHAR16));
  }

  CopyMem (*ResolvedPath + DirLen, FileName, (FileNameLen + 1) * sizeof (CHAR16));

  Status = EFI_SUCCESS;

Exit:
  FreePool (ImagePath);

  return Status;
}

EFI_STATUS
EFIAPI
SimpleOpenFileByName (
  IN  CHAR16          *FileName,
  OUT EFI_FILE_HANDLE *FileHandle
  )
{
  EFI_STATUS                      Status = EFI_SUCCESS;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *SimpleFs;
  EFI_FILE_PROTOCOL               *Volume;
  EFI_HANDLE                      *HandleBuffer;
  UINTN                           NumberHandles;
  UINTN                           Index;

  if ((FileName == NULL) || (FileHandle == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *FileHandle = NULL;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiSimpleFileSystemProtocolGuid,
                  NULL,
                  &NumberHandles,
                  &HandleBuffer
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to locate Simple File System Protocol: %r\n", Status));
    return Status;
  }

  for (Index = 0; Index < NumberHandles; Index++) {
    Status = gBS->HandleProtocol (
                    HandleBuffer[Index],
                    &gEfiSimpleFileSystemProtocolGuid,
                    (VOID**)&SimpleFs
                    );
    if (!EFI_ERROR (Status)) {
      Status = SimpleFs->OpenVolume (SimpleFs, &Volume);
      if (!EFI_ERROR (Status)) {
        Status = Volume->Open (
                          Volume,
                          FileHandle,
                          FileName,
                          EFI_FILE_MODE_READ,
                          0
                          );
        if (!EFI_ERROR (Status)) {
          if (*FileHandle == NULL) {
            DEBUG ((DEBUG_ERROR, "SimpleOpenFileByName - Opened %s without a valid file handle\n", FileName));
            Status = EFI_DEVICE_ERROR;
          }

          Volume->Close (Volume);
          if (!EFI_ERROR (Status)) {
            break;
          }
        } else {
          Volume->Close (Volume);
        }
      }
    }
  }

  if (HandleBuffer) {
    FreePool (HandleBuffer);
  }

  return Status;
}

EFI_STATUS
EFIAPI
SimpleGetFileSize (
  IN EFI_FILE_HANDLE     FileHandle,
  OUT UINT64                *FileSize
  )
{
  EFI_STATUS      Status;
  EFI_FILE_INFO   *FileInfo;
  UINTN           FileInfoSize = 0;

  if ((FileHandle == NULL) || (FileSize == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Status = FileHandle->GetInfo (
                        FileHandle,
                        &gEfiFileInfoGuid,
                        &FileInfoSize,
                        NULL
                        );
  if (Status != EFI_BUFFER_TOO_SMALL) {
    DEBUG ((DEBUG_ERROR, "GetFileSize - Unable to get file info size: %r\n", Status));
    return Status;
  }

  FileInfo = AllocatePool (FileInfoSize);
  if (FileInfo == NULL) {
    DEBUG ((DEBUG_ERROR, "GetFileSize - Unable to allocate memory for file info structure\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  Status = FileHandle->GetInfo (
                        FileHandle,
                        &gEfiFileInfoGuid,
                        &FileInfoSize,
                        FileInfo
                        );
  if (!EFI_ERROR (Status)) {
    *FileSize = FileInfo->FileSize;
  }

  FreePool (FileInfo);

  return Status;
}

EFI_STATUS
EFIAPI
SimpleLoadFile (
  IN EFI_FILE_HANDLE     FileHandle,
  IN UINT8               *Buffer,
  IN UINTN               BufferSize
  )
{
  EFI_STATUS Status;
  UINTN      Offset;
  UINTN      ReadSize;

  if (!FileHandle || !Buffer || !BufferSize) {
    return EFI_INVALID_PARAMETER;
  }

  Offset = 0;
  while (Offset < BufferSize) {
    ReadSize = BufferSize - Offset;
    Status = FileHandle->Read (
                FileHandle,
                &ReadSize,
                Buffer + Offset
                );
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "Failed to read file: %r\n", Status));
      return Status;
    }

    if (ReadSize == 0) {
      DEBUG ((DEBUG_ERROR, "Short read: expected %llu bytes, got %llu bytes\n", BufferSize, Offset));
      return EFI_END_OF_FILE;
    }

    Offset += ReadSize;
  }

  return EFI_SUCCESS;
}