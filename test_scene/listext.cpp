// Lists the OpenXR extensions the active (or XR_RUNTIME_JSON) runtime offers.
#include <openxr/openxr.h>
#include <cstdio>
#include <vector>
int main() {
    uint32_t n = 0;
    if (XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr, 0, &n, nullptr))) { printf("enumerate failed\n"); return 1; }
    std::vector<XrExtensionProperties> e(n, { XR_TYPE_EXTENSION_PROPERTIES });
    xrEnumerateInstanceExtensionProperties(nullptr, n, &n, e.data());
    for (auto& x : e) printf("%s\n", x.extensionName);
}
