#ifndef SEAM_LDR_H_
#define SEAM_LDR_H_

#include <Uefi.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiLib.h>

#define SEAMLDR_PARAMS_NUM_MOD_PAGES 496
#define SEAMLDR_PARAMS_NUM_SIG_PAGES 1
#define TDX_IMAGE_VERSION_2          0x200
#define DIV_ROUND_UP(X, Y) (((X) + (Y) - 1) / (Y))

#pragma pack(push, 1)

typedef struct {
  UINT32 version;
  UINT32 scenario; // 0: Load, 1: Update
  UINT64 sigstruct_pa; // 4 KiB aligned PA to TdxSeamSig
  UINT8  reserved[104];
  UINT64 num_module_pages; // Module size in 4 KiB pages (valid: 1-496 for Version := 0)
  UINT64 mod_pages_pa_list[SEAMLDR_PARAMS_NUM_MOD_PAGES];
} SEAMLDR_PARAMS_t;

typedef struct {
  UINT16  Version;
  UINT16  Checksum;
  UINT8   Signature[8]; // "TDX-BLOB"
  UINT32  SigstructNumPages;
  UINT32  ModuleNumPages;
  UINT8   Reserved[4076];
} TDX_IMAGE_HEADER_t;

typedef struct {
  TDX_IMAGE_HEADER_t Header;
  UINT8              Payload[];
} TDX_IMAGE_t;

#pragma pack(pop)

EFI_STATUS
EFIAPI
CreateSeamldrParams (
  IN  CHAR16*              ModulePath
  );

VOID
EFIAPI
DestroySeamldrParams (
  VOID
  );

UINT64
EFIAPI
SeamldrInstallModule (
  VOID
  );

#endif // SEAM_LDR_H_