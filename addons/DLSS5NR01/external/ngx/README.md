# NVIDIA DLSS SDK (not in this repository)

The build needs the NVIDIA NGX SDK headers and static library, and the release carries NVIDIA's DLSS runtime and licence. They are NVIDIA's,
under NVIDIA's RTX SDKs licence, which allows them to ship inside an application such as this one but not to be republished as source, so
they are not part of this repository (see NOTICE.md for what the release carries and on what terms).

1. Either run `powershell -File tools\fetch_ngx_sdk.ps1 -AcceptNvidiaLicense` from the repository root (it downloads exactly the files below from
   NVIDIA's public repository, https://github.com/NVIDIA/DLSS, pinned to a commit and checked by SHA-256, and only runs when you accept NVIDIA's licence,
   https://github.com/NVIDIA/DLSS/blob/main/LICENSE.txt; add `-Latest` for NVIDIA's newest commit instead of the pinned one), or download the DLSS SDK yourself from https://developer.nvidia.com/rtx/dlss
   (or the DLSS GitHub release).
2. If you downloaded it yourself, copy these files here:

```
external/ngx/include/nvsdk_ngx.h
external/ngx/include/nvsdk_ngx_defs.h
external/ngx/include/nvsdk_ngx_defs_dlssd.h
external/ngx/include/nvsdk_ngx_helpers.h
external/ngx/include/nvsdk_ngx_helpers_dlssd.h
external/ngx/include/nvsdk_ngx_params.h
external/ngx/lib/nvsdk_ngx_s.lib          (the /MT static library, x64)
external/ngx/bin/nvngx_dlss.dll           (the DLSS runtime, lib/Windows_x86_64/rel in NVIDIA's repository)
external/ngx/LICENSE.txt                  (NVIDIA's licence, shipped as NVIDIA-LICENSE.txt)
```

CMake fails with a clear message if the build files are missing, and the package script if the licence is. Everything in this folder except
this README is ignored by git.
