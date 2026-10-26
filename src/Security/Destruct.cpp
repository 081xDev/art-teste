// Single translation unit for the exit-time artifact cleanup.
//
// destruct.hpp holds the whole implementation and is included here and nowhere
// else. Its functions are plain (non-static) so nothing is duplicated across
// translation units, and the heavy headers it needs - winioctl.h, <filesystem>,
// the shlobj/aclapi pair - stay out of every other file in the project.
//
// DestructHook.hpp is the light declaration other files include instead.

#include "DestructHook.hpp"

#include <windows.h>
#include <TlHelp32.h>
#include <winioctl.h>
#include <shlobj.h>
#include <aclapi.h>

#include "../destruct.hpp"
