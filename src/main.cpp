#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <imagehlp.h>
#include <shellapi.h>
#include <cstddef>
#include <string>
#include "../resource.h"

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "imagehlp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace {
constexpr wchar_t kTitle[] = L"4GB Patch";
constexpr wchar_t kSuccess[] = L"Executable successfully patched!";
constexpr wchar_t kFailure[] = L"Couldn't patch executable!";
constexpr wchar_t kWebsite[] = L"http://ntcore.com/4gb_patch.php";
constexpr wchar_t kLink[] = L"www.ntcore.com";
constexpr wchar_t kFileFilter[] = L"All Files (*.*)\0*.*\0Exe Files (*.exe)\0*.exe\0";
HINSTANCE g_instance = nullptr;
HFONT g_linkFont = nullptr;
HCURSOR g_linkCursor = nullptr;
WNDPROC g_previousLinkProc = nullptr;

void ShowError(const wchar_t* message) {
    MessageBoxW(nullptr, message, kTitle, MB_OK | MB_ICONWARNING);
}

bool Seek(HANDLE file, LONGLONG position) {
    LARGE_INTEGER offset{};
    offset.QuadPart = position;
    return SetFilePointerEx(file, offset, nullptr, FILE_BEGIN) != FALSE;
}

template <class T>
bool ReadAt(HANDLE file, LONGLONG position, T& result) {
    DWORD count = 0;
    return Seek(file, position) &&
           ReadFile(file, &result, sizeof(result), &count, nullptr) &&
           count == sizeof(result);
}

template <class T>
bool WriteAt(HANDLE file, LONGLONG position, const T& result) {
    DWORD count = 0;
    return Seek(file, position) &&
           WriteFile(file, &result, sizeof(result), &count, nullptr) &&
           count == sizeof(result);
}

enum class PatchResult { Success, OpenError, ReadError, InvalidExecutable, WriteError };

PatchResult PatchExecutable(const std::wstring& path) {
    const std::wstring backup = path + L".Backup";
    const bool copied = CopyFileW(path.c_str(), backup.c_str(), TRUE) != FALSE;
    const DWORD backupError = copied ? ERROR_SUCCESS : GetLastError();

    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return PatchResult::OpenError;
    if (!copied && backupError != ERROR_FILE_EXISTS && backupError != ERROR_ALREADY_EXISTS) {
        CloseHandle(file);
        return PatchResult::WriteError;
    }

    LARGE_INTEGER size{};
    IMAGE_DOS_HEADER dos{};
    if (!GetFileSizeEx(file, &size) || !ReadAt(file, 0, dos)) {
        CloseHandle(file);
        return PatchResult::ReadError;
    }

    DWORD signature = 0;
    IMAGE_FILE_HEADER header{};
    WORD magic = 0;
    const bool valid = dos.e_magic == IMAGE_DOS_SIGNATURE &&
        dos.e_lfanew >= static_cast<LONG>(sizeof(IMAGE_DOS_HEADER)) &&
        static_cast<LONGLONG>(dos.e_lfanew) + sizeof(DWORD) + sizeof(header) <= size.QuadPart &&
        ReadAt(file, dos.e_lfanew, signature) &&
        signature == IMAGE_NT_SIGNATURE &&
        ReadAt(file, static_cast<LONGLONG>(dos.e_lfanew) + sizeof(DWORD), header) &&
        header.SizeOfOptionalHeader >= offsetof(IMAGE_OPTIONAL_HEADER32, CheckSum) + sizeof(DWORD) &&
        static_cast<LONGLONG>(dos.e_lfanew) + sizeof(DWORD) + sizeof(header) + header.SizeOfOptionalHeader <= size.QuadPart &&
        ReadAt(file, static_cast<LONGLONG>(dos.e_lfanew) + sizeof(DWORD) + sizeof(header), magic) &&
        (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC || magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC);

    if (!valid) {
        CloseHandle(file);
        return PatchResult::InvalidExecutable;
    }

    header.Characteristics |= IMAGE_FILE_LARGE_ADDRESS_AWARE;
    const LONGLONG characteristicsOffset =
        static_cast<LONGLONG>(dos.e_lfanew) + sizeof(DWORD) +
        offsetof(IMAGE_FILE_HEADER, Characteristics);
    if (!WriteAt(file, characteristicsOffset, header.Characteristics) || !FlushFileBuffers(file)) {
        CloseHandle(file);
        return PatchResult::WriteError;
    }

    DWORD oldChecksum = 0;
    DWORD newChecksum = 0;
    if (MapFileAndCheckSumW(const_cast<PWSTR>(path.c_str()), &oldChecksum, &newChecksum) != CHECKSUM_SUCCESS) {
        CloseHandle(file);
        return PatchResult::WriteError;
    }

    const LONGLONG checksumOffset =
        static_cast<LONGLONG>(dos.e_lfanew) + sizeof(DWORD) + sizeof(header) +
        offsetof(IMAGE_OPTIONAL_HEADER32, CheckSum);
    const bool written = WriteAt(file, checksumOffset, newChecksum) && FlushFileBuffers(file);
    CloseHandle(file);
    return written ? PatchResult::Success : PatchResult::WriteError;
}

int PatchAndReport(const std::wstring& path) {
    switch (PatchExecutable(path)) {
    case PatchResult::Success:
        return 1;
    case PatchResult::OpenError:
        ShowError(L"Couldn't open executable!");
        return 0;
    case PatchResult::ReadError:
        ShowError(L"Couldn't read executable!");
        return 0;
    case PatchResult::InvalidExecutable:
    case PatchResult::WriteError:
        ShowError(kFailure);
        return 0;
    }
    return 0;
}

int PickAndPatch(HWND parent = nullptr) {
    wchar_t filename[MAX_PATH]{};
    OPENFILENAMEW options{};
    options.lStructSize = sizeof(options);
    options.hwndOwner = parent;
    options.lpstrFilter = kFileFilter;
    options.lpstrFile = filename;
    options.nMaxFile = MAX_PATH;
    options.lpstrTitle = L"Select Executable...";
    if (!GetOpenFileNameW(&options)) return 2;
    return PatchAndReport(filename);
}

LRESULT CALLBACK LinkWindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(0, 0, 255));
        HGDIOBJ previous = SelectObject(dc, g_linkFont);
        TextOutW(dc, 0, 0, kLink, 14);
        SelectObject(dc, previous);
        EndPaint(hwnd, &paint);
        return 0;
    }
    return CallWindowProcW(g_previousLinkProc, hwnd, message, wparam, lparam);
}

INT_PTR CALLBACK PatchDialog(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_INITDIALOG: {
        g_linkCursor = LoadCursorW(g_instance, MAKEINTRESOURCEW(IDC_LINK_CURSOR));
        g_linkFont = CreateFontW(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                 DEFAULT_PITCH, L"Arial");
        HWND link = GetDlgItem(window, IDC_WEBSITE);
        g_previousLinkProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            link, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(LinkWindowProc)));
        return TRUE;
    }
    case WM_SETCURSOR:
        if (reinterpret_cast<HWND>(wparam) == GetDlgItem(window, IDC_WEBSITE)) {
            SetCursor(g_linkCursor ? g_linkCursor : LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IDC_PATCH_OK:
        case IDCANCEL:
            EndDialog(window, 0);
            return TRUE;
        case IDC_ANOTHER_FILE: {
            const int result = PickAndPatch(window);
            if (result != 2) {
                SetDlgItemTextW(window, IDC_PATCH_STATUS, result == 1 ? kSuccess : kFailure);
                MessageBeep(result == 1 ? MB_ICONASTERISK : MB_ICONEXCLAMATION);
            }
            return TRUE;
        }
        case IDC_WEBSITE:
            if (HIWORD(wparam) == STN_CLICKED) {
                ShellExecuteW(window, L"open", kWebsite, nullptr, nullptr, SW_SHOWNORMAL);
                return TRUE;
            }
            break;
        }
        break;
    case WM_CLOSE:
        EndDialog(window, 0);
        return TRUE;
    case WM_DESTROY:
        if (g_linkFont) {
            DeleteObject(g_linkFont);
            g_linkFont = nullptr;
        }
        return TRUE;
    }
    return FALSE;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    g_instance = instance;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc > 1 && argv) {
        PatchAndReport(argv[1]);
        LocalFree(argv);
        return 0;
    }
    if (argv) LocalFree(argv);

    if (PickAndPatch() == 1) {
        DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_PATCH_DIALOG),
                        nullptr, PatchDialog, 0);
    }
    return 0;
}
