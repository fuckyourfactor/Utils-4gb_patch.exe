# 4GB Patch — Windows C++ reimplementation

This project reconstructs the behavior and classic Windows dialog of the supplied `4gb_patch.exe` (Visual C++ 2010-era PE32 application). It is independent source code: the original executable is not needed to build or run it.

## Build

Open `4GBPatch.sln` in Visual Studio 2022, select **Release | Win32**, and build. The output is `bin/Release/4gb_patch.exe`. A GitHub Actions workflow also builds the application and publishes a numbered release automatically on each push to `main` or `master`.

## Usage

Run the application and select an `.exe`. The tool creates `selected.exe.Backup` (without overwriting an existing backup), enables `IMAGE_FILE_LARGE_ADDRESS_AWARE` (bit `0x0020` of the PE COFF Characteristics), and recalculates the PE checksum with `MapFileAndCheckSumW`. It shows the original-style dialog with **OK**, **Another File**, and a website link. Command-line usage with one executable path patches that path directly without showing the success dialog.

The target must be a valid PE32 or PE32+ file. This tool does not magically increase memory or guarantee that a particular application can use 4 GB of virtual address space. Modifying a signed executable invalidates its Authenticode signature.

## Fidelity and safety

The dialog dimensions, controls, captions, icon, cursor, navigation, and core patching routine were reconstructed from static inspection of the user-provided executable. The icon and cursor are extracted from that provided file. Unlike the original, this reimplementation validates PE headers, checks I/O errors, and avoids modifying a file when its backup cannot be created. The original executable ran and displayed native Windows dialogs; rendering can vary with Windows version and DPI. Build and runtime behavior have not been verified on Windows in this environment.
