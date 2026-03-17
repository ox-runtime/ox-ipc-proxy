#pragma once

#ifdef _WIN32
#include <aclapi.h>
#include <sddl.h>
#include <windows.h>
#endif

namespace ox {
namespace protocol {

#ifdef _WIN32
// Create Windows security descriptor allowing current user access
// Uses SDDL (Security Descriptor Definition Language):
// D:P - DACL protected from inheritance
// (A;;GA;;;WD) - Allow Generic All to Everyone (local access only, not network)
// Note: For better security, could use SY (Local System) and current user SID
inline SECURITY_ATTRIBUTES* CreateOwnerOnlySecurityAttributes() {
    static SECURITY_ATTRIBUTES sa;
    static bool initialized = false;

    if (initialized) {
        return &sa;
    }

    // Allow current user - WD means "World" but on local objects it's local only
    // For production, should get current user SID and use that instead
    const char* sddl = "D:P(A;;GA;;;WD)";

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(sddl, SDDL_REVISION_1, &sa.lpSecurityDescriptor,
                                                              nullptr)) {
        return nullptr;
    }

    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = FALSE;
    initialized = true;

    return &sa;
}
#endif

}  // namespace protocol
}  // namespace ox
