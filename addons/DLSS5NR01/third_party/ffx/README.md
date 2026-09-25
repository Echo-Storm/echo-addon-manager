# AMD FidelityFX API headers (FSR 3.1)

The C headers of AMD's FidelityFX API, as published in AMD's FidelityFX SDK v1.1.4
(https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK, tag v1.1.4, commit c6efa6bf7f2027b3ec94f28578bb5965eabb9e55,
folder ffx-api/include/ffx_api), unchanged. They are AMD's, under the MIT licence in LICENSE.txt, which allows them to be
copied here.

The FSR 3 Upscaler loads AMD's prebuilt, AMD-signed runtime (amd_fidelityfx_dx12.dll, from the same release's
PrebuiltSignedDLL folder) at run time from its fsr folder. tools/fetch_ffx_sdk.ps1 downloads it, checks its SHA-256 and AMD's
signature, and puts it in external/ffx/bin (not committed); the build copies it next to the addon.
