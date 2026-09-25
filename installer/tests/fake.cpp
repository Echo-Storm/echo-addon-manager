// A DLL that does nothing. The installer's tests need files that look, by their version resource alone, like Lossless Scaling's Lossless.dll or an older
// LS Addon Manager: the installer never loads a DLL, so nothing here has to work.
#include <windows.h>
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
