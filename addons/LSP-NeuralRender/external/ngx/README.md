# NVIDIA DLSS SDK (not included)

The build needs the NVIDIA NGX SDK headers and the static import library. NVIDIA's license does
not allow redistributing them in source form, so they are not part of this repository.

1. Download the DLSS SDK from https://developer.nvidia.com/rtx/dlss (or the DLSS GitHub release).
2. Copy these files here:

```
external/ngx/include/nvsdk_ngx.h
external/ngx/include/nvsdk_ngx_defs.h
external/ngx/include/nvsdk_ngx_defs_dlssd.h
external/ngx/include/nvsdk_ngx_helpers.h
external/ngx/include/nvsdk_ngx_helpers_dlssd.h
external/ngx/include/nvsdk_ngx_params.h
external/ngx/lib/nvsdk_ngx_s.lib          (the /MT static library, x64)
```

CMake fails with a clear message if they are missing. Everything in this folder except this
README is ignored by git.
