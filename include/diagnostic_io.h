#ifndef BO1MW2_DIAGNOSTIC_IO_H
#define BO1MW2_DIAGNOSTIC_IO_H
#include <windows.h>
#include <stdint.h>

/* Small fixed-buffer log helpers: no C/C++ runtime initialization or allocation. */
static inline void DiagnosticWrite(HANDLE file, const char* text) {
    DWORD size = 0, written = 0;
    DWORD error = GetLastError();
    while (text[size]) ++size;
    if (file != INVALID_HANDLE_VALUE) {
        WriteFile(file, text, size, &written, NULL);
        FlushFileBuffers(file);
    }
    SetLastError(error);
}
static inline void DiagnosticHex(HANDLE file, const char* label, uintptr_t value) {
    static const char digits[] = "0123456789abcdef";
    char line[2 + sizeof(uintptr_t) * 2 + 3];
    unsigned n;
    line[0] = '0'; line[1] = 'x';
    for (n = 0; n < sizeof(uintptr_t) * 2; ++n)
        line[2 + n] = digits[(value >> ((sizeof(uintptr_t) * 2 - 1 - n) * 4)) & 15];
    line[2 + sizeof(uintptr_t) * 2] = '\r';
    line[3 + sizeof(uintptr_t) * 2] = '\n';
    line[4 + sizeof(uintptr_t) * 2] = 0;
    DiagnosticWrite(file, label); DiagnosticWrite(file, line);
}
static inline void DiagnosticPath(HANDLE file, const char* label, const wchar_t* path) {
    char utf8[1024];
    DWORD error = GetLastError();
    int result = WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), NULL, NULL);
    DiagnosticWrite(file, label);
    DiagnosticWrite(file, result > 0 ? utf8 : "<path too long or conversion failed>");
    DiagnosticWrite(file, "\r\n");
    SetLastError(error);
}
static inline void DiagnosticError(HANDLE file, const char* label, DWORD error) {
    char message[512];
    DiagnosticHex(file, label, error);
    if (FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                      NULL, error, 0, message, sizeof(message), NULL)) DiagnosticWrite(file, message);
    SetLastError(error);
}
static inline BOOL DiagnosticAppend(wchar_t* path, DWORD capacity, const wchar_t* tail) {
    DWORD n = 0, m = 0;
    while (n < capacity && path[n]) ++n;
    while (tail[m]) ++m;
    if (n == capacity || m >= capacity - n) return FALSE;
    for (m = 0; tail[m]; ++m) path[n + m] = tail[m];
    path[n + m] = 0;
    return TRUE;
}
#endif
