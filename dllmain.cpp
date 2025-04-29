// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "pch.h"
#include <algorithm>
#include <cmath>

namespace {
    const std::string LayerName = "XR_APILAYER_fommil_widescreen";

    std::string dllHome;

    std::ofstream logStream;

    // we only enable ourselves for specific (simracing) titles
    // this has to be considered for every function, because
    // downstream of us might cache references.
    bool enabled = false;

    // we might want to make this configurable in the future
    constexpr double kTargetAspect = 16.0f / 9.0f;

    // Function pointers to interact with the next layers and/or the OpenXR runtime.
    PFN_xrGetInstanceProcAddr nextXrGetInstanceProcAddr = nullptr;
    PFN_xrLocateViews nextXrLocateViews = nullptr;
    PFN_xrEnumerateViewConfigurationViews nextXrEnumerateViewConfigurationViews = nullptr;

    void Log(const char* fmt, ...);

    void InternalLog(const char* fmt, va_list va)
    {
        char buf[1024];
        _vsnprintf_s(buf, sizeof(buf), fmt, va);
        OutputDebugStringA(buf);
        if (logStream.is_open())
        {
            logStream << buf;
            logStream.flush();
        }
    }

    void Log(const char* fmt, ...)
    {
        va_list va;
        va_start(va, fmt);
        InternalLog(fmt, va);
        va_end(va);
    }

    void DebugLog(const char* fmt, ...)
    {
#ifdef _DEBUG
        va_list va;
        va_start(va, fmt);
        InternalLog(fmt, va);
        va_end(va);
#endif
    }

    // ---------------------------------------------------------------------------
    // ClampVerticalFov – symmetric 16:9 crop (equal cut top & bottom)
    // ---------------------------------------------------------------------------
    // - works entirely in double precision to minimise rounding
    // - first tries an equal-delta trim; if either side can’t spare the delta
    //   it falls back to proportional scaling (rare on real headsets)
    // ---------------------------------------------------------------------------
    static void ClampVerticalFov(XrFovf& fov)
    {
        using d = double;

        d tanL = std::tan(static_cast<d>(fov.angleLeft));   // −
        d tanR = std::tan(static_cast<d>(fov.angleRight));  // +
        d tanU = std::tan(static_cast<d>(fov.angleUp));     // +
        d tanD = std::tan(static_cast<d>(fov.angleDown));   // −

        d widthTan = std::fabs(tanL) + std::fabs(tanR);
        d heightTan = std::fabs(tanU) + std::fabs(tanD);
        d aspect = widthTan / heightTan;

        if (aspect >= kTargetAspect)
            return;

        d desiredHeightTan = widthTan / kTargetAspect;
        d delta = (heightTan - desiredHeightTan) * 0.5;  // cut from each side

        // Can both sides spare that much?
        d maxTrim = std::fabs(tanU) < std::fabs(tanD) ? std::fabs(tanU) : std::fabs(tanD);

        if (delta <= maxTrim)
        {
            tanU -= delta;
            tanD += delta;
        }
        else
        {
            d scale = desiredHeightTan / heightTan;
            tanU *= scale;
            tanD *= scale;
            delta = 0.0;
        }

        // Back to angles (single float conversion)
        fov.angleUp = static_cast<float>(std::atan(tanU));
        fov.angleDown = static_cast<float>(std::atan(tanD));

#ifdef _DEBUG
        d newAspect = widthTan / (std::fabs(tanU) + std::fabs(tanD));
        DebugLog("FOV sym-crop: aspect %.6f → %.6f  delta %.6f%s\n",
            aspect, newAspect, delta,
            delta == 0.0 ? " (scaled)" : "");
#endif
    }

    XRAPI_ATTR XrResult XRAPI_CALL fommil_widescreen_xrEnumerateViewConfigurationViews(
        XrInstance               instance,
        XrSystemId               systemId,
        XrViewConfigurationType  viewConfigurationType,
        uint32_t                 viewCapacityInput,
        uint32_t*                viewCountOutput,
        XrViewConfigurationView* views)
    {
        if (enabled) {
            DebugLog("--> fommil_widescreen_xrEnumerateViewConfigurationViews\n");
        }

        XrResult res = nextXrEnumerateViewConfigurationViews(instance, systemId,
                                                             viewConfigurationType, viewCapacityInput,
                                                             viewCountOutput, views);
        if (!enabled) {
            return res;
        }

        if (res != XR_SUCCESS || views == nullptr) {
            DebugLog("<-- fommil_widescreen_xrEnumerateViewConfigurationViews EARLY %d\n", res);
            return res;
        }

        PFN_xrGetViewConfigurationProperties pfnGetProps = nullptr;
        XrResult props_result = nextXrGetInstanceProcAddr(instance, "xrGetViewConfigurationProperties", reinterpret_cast<PFN_xrVoidFunction*>(&pfnGetProps));
        if (props_result == XR_SUCCESS) {
            DebugLog("  --> got props ref\n");
            XrViewConfigurationProperties props{XR_TYPE_VIEW_CONFIGURATION_PROPERTIES};
            if (pfnGetProps(instance, systemId, viewConfigurationType, &props) == XR_SUCCESS &&
                props.fovMutable == XR_TRUE) {
                DebugLog("  --> got props, and fovMutable is true\n");
                for (uint32_t i = 0; i < *viewCountOutput; ++i) {
                    const uint32_t w = views[i].recommendedImageRectWidth;
                    const uint32_t h = views[i].recommendedImageRectHeight;
                    const double curAspect = static_cast<double>(w) / static_cast<double>(h);
                    DebugLog("  --> aspect for %u is %.3f", i, curAspect);
                    if (curAspect > kTargetAspect)
                        continue;
                    const uint32_t newH = static_cast<uint32_t>(
                                               std::lround(static_cast<double>(w) / kTargetAspect));
                    DebugLog("  --> Res clamp: view %u  %ux%u → %ux%u (aspect %.3f)\n",
                             i, w, h, w, newH, kTargetAspect);
                    views[i].recommendedImageRectHeight = std::max<uint32_t>(1u, newH);
                }
            }
        }

        DebugLog("<-- fommil_widescreen_xrEnumerateViewConfigurationViews %d\n", res);

        return res;
    }

    // Overrides the behavior of xrLocateViews().
    XrResult fommil_widescreen_xrLocateViews(
        const XrSession session,
        const XrViewLocateInfo* const viewLocateInfo,
        XrViewState* const viewState,
        const uint32_t viewCapacityInput,
        uint32_t* const viewCountOutput,
        XrView* const views)
    {
        if (enabled) {
            DebugLog("--> fommil_widescreen_xrLocateViews\n");
        }

        // Call the chain to perform the actual operation.
        XrResult result = nextXrLocateViews(session, viewLocateInfo, viewState, viewCapacityInput, viewCountOutput, views);
        if (!enabled) {
            return result;
        }

        if (result == XR_SUCCESS && viewLocateInfo->viewConfigurationType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
        {
            for (uint32_t i = 0; i < *viewCountOutput; ++i)
                ClampVerticalFov(views[i].fov);
        }

        DebugLog("<-- fommil_widescreen_xrLocateViews %d\n", result);

        return result;
    }

    // Entry point for OpenXR calls.
    XrResult fommil_widescreen_xrGetInstanceProcAddr(
        const XrInstance instance,
        const char* const name,
        PFN_xrVoidFunction* const function)
    {
        DebugLog("--> fommil_widescreen_xrGetInstanceProcAddr \"%s\"\n", name);
        XrResult res = nextXrGetInstanceProcAddr(instance, name, function);

        if (strcmp(name, "xrLocateViews") == 0) {
            nextXrLocateViews = reinterpret_cast<PFN_xrLocateViews>(*function);
            *function = reinterpret_cast<PFN_xrVoidFunction>(
                fommil_widescreen_xrLocateViews);
        }
        else if (strcmp(name, "xrEnumerateViewConfigurationViews") == 0) {
            nextXrEnumerateViewConfigurationViews =
                reinterpret_cast<PFN_xrEnumerateViewConfigurationViews>(*function);
            *function = reinterpret_cast<PFN_xrVoidFunction>(
                fommil_widescreen_xrEnumerateViewConfigurationViews);
        }

        DebugLog("<-- fommil_widescreen_xrGetInstanceProcAddr %d\n", res);
        return res;
    }

    // Entry point for creating the layer.
    XrResult fommil_widescreen_xrCreateApiLayerInstance(
        const XrInstanceCreateInfo* const instanceCreateInfo,
        const struct XrApiLayerCreateInfo* const apiLayerInfo,
        XrInstance* const instance)
    {
        DebugLog("--> fommil_widescreen_xrCreateApiLayerInstance\n");

        if (!apiLayerInfo ||
            apiLayerInfo->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO ||
            apiLayerInfo->structVersion != XR_API_LAYER_CREATE_INFO_STRUCT_VERSION ||
            apiLayerInfo->structSize != sizeof(XrApiLayerCreateInfo) ||
            !apiLayerInfo->nextInfo ||
            apiLayerInfo->nextInfo->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO ||
            apiLayerInfo->nextInfo->structVersion != XR_API_LAYER_NEXT_INFO_STRUCT_VERSION ||
            apiLayerInfo->nextInfo->structSize != sizeof(XrApiLayerNextInfo) ||
            apiLayerInfo->nextInfo->layerName != LayerName ||
            !apiLayerInfo->nextInfo->nextGetInstanceProcAddr ||
            !apiLayerInfo->nextInfo->nextCreateApiLayerInstance)
        {
            Log("xrCreateApiLayerInstance validation failed\n");
            return XR_ERROR_INITIALIZATION_FAILED;
        }

        // Store the next xrGetInstanceProcAddr to resolve the functions no handled by our layer.
        nextXrGetInstanceProcAddr = apiLayerInfo->nextInfo->nextGetInstanceProcAddr;

        // Call the chain to create the instance.
        XrApiLayerCreateInfo chainApiLayerInfo = *apiLayerInfo;
        chainApiLayerInfo.nextInfo = apiLayerInfo->nextInfo->next;
        const XrResult result = apiLayerInfo->nextInfo->nextCreateApiLayerInstance(instanceCreateInfo, &chainApiLayerInfo, instance);
        DebugLog("<-- fommil_widescreen_xrCreateApiLayerInstance %d\n", result);

        const char* appName = instanceCreateInfo->applicationInfo.applicationName;
        enabled = std::strstr(appName, "iRacing") != nullptr;
        Log("[widescreen] %s for \"%s\"\n", enabled ? "ENABLED" : "DISABLED", appName);

        return result;
    }

}

extern "C" {

    // Entry point for the loader.
    XrResult __declspec(dllexport) XRAPI_CALL fommil_widescreen_xrNegotiateLoaderApiLayerInterface(
        const XrNegotiateLoaderInfo* const loaderInfo,
        const char* const apiLayerName,
        XrNegotiateApiLayerRequest* const apiLayerRequest)
    {
        DebugLog("--> (early) fommil_widescreen_xrNegotiateLoaderApiLayerInterface\n");

        // Retrieve the path of the DLL.
        if (dllHome.empty())
        {
            HMODULE module;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&dllHome, &module))
            {
                char path[_MAX_PATH];
                GetModuleFileNameA(module, path, sizeof(path));
                dllHome = std::filesystem::path(path).parent_path().string();
            }
            else
            {
                // Falling back to loading config/writing logs to the current working directory.
                DebugLog("Failed to locate DLL\n");
            }
        }

        // Start logging to file.
        if (!logStream.is_open())
        {
            // std::string logFile = (std::filesystem::path(dllHome) / std::filesystem::path(LayerName + ".log")).string();
            std::string logFile = (std::filesystem::path(getenv("LOCALAPPDATA")) / std::filesystem::path(LayerName + ".log")).string();
            logStream.open(logFile, std::ios_base::ate);
            Log("dllHome is \"%s\"\n", dllHome.c_str());
        }

        DebugLog("--> fommil_widescreen_xrNegotiateLoaderApiLayerInterface\n");

        if (apiLayerName && apiLayerName != LayerName)
        {
            Log("Invalid apiLayerName \"%s\"\n", apiLayerName);
            return XR_ERROR_INITIALIZATION_FAILED;
        }

        if (!loaderInfo ||
            !apiLayerRequest ||
            loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
            loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION ||
            loaderInfo->structSize != sizeof(XrNegotiateLoaderInfo) ||
            apiLayerRequest->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST ||
            apiLayerRequest->structVersion != XR_API_LAYER_INFO_STRUCT_VERSION ||
            apiLayerRequest->structSize != sizeof(XrNegotiateApiLayerRequest) ||
            loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
            loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION ||
            loaderInfo->maxInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
            loaderInfo->maxApiVersion < XR_CURRENT_API_VERSION ||
            loaderInfo->minApiVersion > XR_CURRENT_API_VERSION)
        {
            Log("xrNegotiateLoaderApiLayerInterface validation failed\n");
            return XR_ERROR_INITIALIZATION_FAILED;
        }

        // Setup our layer to intercept OpenXR calls.
        apiLayerRequest->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
        apiLayerRequest->layerApiVersion = XR_CURRENT_API_VERSION;
        apiLayerRequest->getInstanceProcAddr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(fommil_widescreen_xrGetInstanceProcAddr);
        apiLayerRequest->createApiLayerInstance = reinterpret_cast<PFN_xrCreateApiLayerInstance>(fommil_widescreen_xrCreateApiLayerInstance);

        DebugLog("<-- fommil_widescreen_xrNegotiateLoaderApiLayerInterface\n");

        Log("%s layer is active\n", LayerName.c_str());

        return XR_SUCCESS;
    }

}
