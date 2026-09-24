#include "engine/dlaa_model.h"
#include <d3d12.h>
#include "nvsdk_ngx.h"
#include "nvsdk_ngx_defs.h"
#include "nvsdk_ngx_params.h"

namespace dlaa {

namespace {
NVSDK_NGX_Parameter* g_params = nullptr;   // DLSS's own parameter block (the capability block is Neural Rendering's)
unsigned g_preset = 0;
int g_lastResult = NVSDK_NGX_Result_Success;

NVSDK_NGX_Parameter* Params() {
    if (!g_params) { const NVSDK_NGX_Result r = NVSDK_NGX_D3D12_AllocateParameters(&g_params); if (NVSDK_NGX_FAILED(r)) { g_lastResult = r; g_params = nullptr; } }
    return g_params;
}
} // namespace

void SetPreset(unsigned preset) { g_preset = preset; }

void* __cdecl Create(ID3D12GraphicsCommandList* cmd, void*, const NrCreateParams* p) {
    NVSDK_NGX_Parameter* params = Params();
    if (!params) return nullptr;
    params->Set(NVSDK_NGX_Parameter_CreationNodeMask, 1u);
    params->Set(NVSDK_NGX_Parameter_VisibilityNodeMask, 1u);
    params->Set(NVSDK_NGX_Parameter_Width, p->width);
    params->Set(NVSDK_NGX_Parameter_Height, p->height);
    params->Set(NVSDK_NGX_Parameter_OutWidth, p->width);     // DLAA: the output is the input's size
    params->Set(NVSDK_NGX_Parameter_OutHeight, p->height);
    params->Set(NVSDK_NGX_Parameter_PerfQualityValue, static_cast<int>(NVSDK_NGX_PerfQuality_Value_DLAA));
    params->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, static_cast<int>(NVSDK_NGX_DLSS_Feature_Flags_None));
    params->Set(NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects, 0);
    params->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, g_preset);
    NVSDK_NGX_Handle* handle = nullptr;
    g_lastResult = NVSDK_NGX_D3D12_CreateFeature(cmd, NVSDK_NGX_Feature_SuperSampling, params, &handle);
    return NVSDK_NGX_SUCCEED(static_cast<NVSDK_NGX_Result>(g_lastResult)) ? handle : nullptr;
}

int __cdecl Evaluate(ID3D12GraphicsCommandList* cmd, void* feature, void*, const NrEvalParams* p) {
    NVSDK_NGX_Parameter* params = Params();
    if (!params || !feature) return NVSDK_NGX_Result_FAIL_InvalidParameter;
    params->Set(NVSDK_NGX_Parameter_Color, p->color);
    params->Set(NVSDK_NGX_Parameter_Output, p->output);
    params->Set(NVSDK_NGX_Parameter_Depth, p->depth);
    params->Set(NVSDK_NGX_Parameter_MotionVectors, p->mvec);
    params->Set(NVSDK_NGX_Parameter_Jitter_Offset_X, 0.0f);
    params->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, 0.0f);
    params->Set(NVSDK_NGX_Parameter_MV_Scale_X, p->mvScaleX);
    params->Set(NVSDK_NGX_Parameter_MV_Scale_Y, p->mvScaleY);
    params->Set(NVSDK_NGX_Parameter_Reset, static_cast<int>(p->reset));
    params->Set(NVSDK_NGX_Parameter_Sharpness, 0.0f);
    params->Set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, 1.0f);
    params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, p->width);
    params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, p->height);
    g_lastResult = NVSDK_NGX_D3D12_EvaluateFeature_C(cmd, static_cast<NVSDK_NGX_Handle*>(feature), params, nullptr);
    return NVSDK_NGX_SUCCEED(static_cast<NVSDK_NGX_Result>(g_lastResult)) ? 1 : g_lastResult;
}

void __cdecl Release(void* feature) { if (feature) NVSDK_NGX_D3D12_ReleaseFeature(static_cast<NVSDK_NGX_Handle*>(feature)); }

int __cdecl LastResult(int) { return g_lastResult; }

void Shutdown() {
    if (g_params) NVSDK_NGX_D3D12_DestroyParameters(g_params);
    g_params = nullptr;
}

} // namespace dlaa
