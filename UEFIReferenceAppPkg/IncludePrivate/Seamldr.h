#ifndef SEAM_LDR_H_
#define SEAM_LDR_H_

#include <Uefi.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiLib.h>

/*
 * mod_pages_pa_list[] spans multiple pages. The last entry of each page
 * points to the next page and does not hold a module page, so the maximum
 * number of module pages is the array size minus one entry per page.
 *
 * SEAMLDR_PARAMS_NUM_PAGES is somewhat arbitrary. It allows modules of up
 * to about 32MB. Increase it if modules outgrow that.
 */
#define SEAMLDR_PARAMS_NUM_PAGES          16
#define SEAMLDR_PARAMS_NUM_MOD_PA_ENTRIES (496 + 512 * (SEAMLDR_PARAMS_NUM_PAGES - 1))
#define SEAMLDR_PARAMS_NUM_MOD_PAGES      (SEAMLDR_PARAMS_NUM_MOD_PA_ENTRIES - SEAMLDR_PARAMS_NUM_PAGES)
#define SEAMLDR_PARAMS_NUM_SIG_PAGES      4
#define TDX_IMAGE_VERSION_2          0x200
#define DIV_ROUND_UP(X, Y) (((X) + (Y) - 1) / (Y))

#pragma pack(push, 1)

typedef struct {
  UINT32 version; // 0: single-page sigstruct, 1: multi-page sigstruct
  UINT32 scenario; // 0: Load, 1: Update
  UINT64 sigstruct_pages_pa_list[SEAMLDR_PARAMS_NUM_SIG_PAGES]; // 4 KiB aligned PAs of TdxSeamSig pages
  UINT8  reserved[80];
  UINT64 num_module_pages; // Module size in 4 KiB pages
  UINT64 mod_pages_pa_list[SEAMLDR_PARAMS_NUM_MOD_PA_ENTRIES];
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

STATIC_ASSERT (
  sizeof (SEAMLDR_PARAMS_t) == SEAMLDR_PARAMS_NUM_PAGES * EFI_PAGE_SIZE,
  "SEAMLDR_PARAMS_t must span exactly SEAMLDR_PARAMS_NUM_PAGES pages"
  );

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