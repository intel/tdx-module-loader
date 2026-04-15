# TDX Module Loader

This repository contains a reference UEFI application for loading the Intel® Trust Domain Extensions (TDX) module. The application can be executed from the UEFI shell or invoked from a bootloader such as GRUB.

## Background

The Intel® TDX module has grown in size, making it too large to be embedded within the BIOS/UEFI firmware (IFWI). Consequently, the TDX module must be loaded at a later stage in the boot process.

This UEFI application provides a mechanism to load the TDX module from the boot environment. A typical boot flow is as follows:

1.  The system firmware (BIOS/IFWI) loads the SEAMLDR module.
2.  A bootloader (e.g., GRUB) chain-loads this UEFI application (`TdxModuleLoader.efi`), which then invokes the SEAMLDR interface to load the TDX module (`TDX-BLOB`).
3.  The bootloader proceeds to launch the operating system.

## Prerequisites and Assumptions

Before integrating this loader into a platform boot flow, confirm the following:

1.  The platform firmware includes and initializes SEAMLDR support.
2.  The platform and firmware configuration are compatible with Intel® TDX enablement.
3.  The target boot path can execute a UEFI application before the final OS kernel launch.
4.  The EFI-accessible storage contains both `TdxModuleLoader.efi` and `TDX-BLOB`.

## How to Build the UEFI Application

Building this application requires a standard C development environment (`gcc`, `binutils`, `nasm`) and the EDK2 build system.

1.  Set up the EDK2 development environment. The source code is available at [tianocore/edk2](https://github.com/tianocore/edk2).

2.  Place the `UEFIReferenceAppPkg` directory from this project into the EDK2 workspace. The directory structure should look like this:

    ```
    edk2/
    ├── BaseTools/
    ├── Conf/
    ├── UEFIReferenceAppPkg/         <-- This project
    │   ├── IncludePrivate/
    │   ├── TdxModuleLoader/
    │   ├── UEFIReferenceAppPkg.dec
    │   └── UEFIReferenceAppPkg.dsc
    ├── ...
    └── edksetup.sh
    ```

3.  Build the project. The following commands illustrate one Linux-based setup:

    ```bash
    ## Ensure the required build tools (gcc, binutils, nasm) are installed.
    # Navigate to the edk2 directory
    cd edk2

    # Build EDK2 BaseTools
    make -C BaseTools -jN

    # Set up the EDK2 build environment
    source ./edksetup.sh

    # Build the TdxModuleLoader application
    build -p UEFIReferenceAppPkg/UEFIReferenceAppPkg.dsc -a X64 -t GCC -b RELEASE
    ```

4.  The compiled UEFI application will be located at: `Build/UEFIReferenceAppPkg/RELEASE_GCC/X64/TdxModuleLoader.efi`.

### Build Notes

-  The command example uses GCC and X64; adjust toolchain for your environment.

## TDX Module Configuration

`TDX-BLOB` should be placed on the EFI System Partition (ESP) or a filesystem accessible from the UEFI environment.

TDX module blobs can be obtained from [TDX-Module binaries](https://github.com/intel/confidential-computing.tdx.tdx-module.binaries/tree/main/joined_files).

### Configuration Notes

-  Keep `TdxModuleLoader.efi` and `TDX-BLOB` in the same directory.

## Bootloader Integration

Any bootloader capable of launching UEFI applications can invoke `TdxModuleLoader.efi` before the final OS kernel is launched. The exact implementation is platform- and distribution-dependent.

### General Requirements

1.  Place `TdxModuleLoader.efi` and `TDX-BLOB` on EFI-accessible storage.
2.  Ensure the boot entry executes `TdxModuleLoader.efi` before kernel handoff.
3.  Preserve a fallback boot path in case loader execution fails.

### GRUB Integration Guidance

GRUB integration is platform- and distribution-dependent, so this README does not prescribe a full `/etc/grub.d/` script. In general, the GRUB entry should locate the EFI partition and invoke `TdxModuleLoader.efi`. This loader is expected to return to its caller after attempting the TDX module load, but integrators should still verify that their GRUB and firmware combination resumes the remaining boot commands after `chainloader`.

For simplicity, the example below assumes that `TdxModuleLoader.efi` is located at the root of the EFI partition and that the existing kernel and initrd paths are already correct for your system.

Example GRUB menu entry before inserting the TDX loader step:

```bash
menuentry 'Linux' {
    search --no-floppy --fs-uuid --set=root <boot-partition-uuid>
    linux /vmlinuz root=UUID=<root-filesystem-uuid> <kernel-args>
    initrd /initrd.img
}
```

Example GRUB menu entry with the TDX loader step added ahead of kernel launch:

```bash
menuentry 'Linux with TDX module load' {
    search --no-floppy --fs-uuid --set=root <efi-partition-uuid>
    chainloader /TdxModuleLoader.efi
    boot

    search --no-floppy --fs-uuid --set=root <boot-partition-uuid>
    linux /vmlinuz root=UUID=<root-filesystem-uuid> <kernel-args>
    initrd /initrd.img
}
```

### Integration Notes

-  Adjust the EFI partition UUID, boot partition UUID, root filesystem identifier, kernel paths, initrd paths, and kernel arguments to match your platform layout.
-  Validate the behavior of `chainloader` on the target platform. Some environments may not resume the remaining GRUB commands after transferring control to the EFI application.
