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
#include <array>
#include <algorithm>
#include <cmath>
#include <filesystem>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {
    const std::string LayerName = "XR_APILAYER_fommil_widescreen";

    std::string dllHome;

    std::ofstream logStream;

    // we only enable ourselves for specific (simracing) titles
    const std::array<const char*, 1> allowedApps = {
        "iRacingSim64DX11"
    };
    // has to be checked on every call, because downstream can cache pointers
    bool enabled = false;

    // this is the fallback after reading the config file
    double targetAspect = 16.0f / 9.0f;

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

        if (aspect >= targetAspect)
            return;

        d desiredHeightTan = widthTan / targetAspect;
        d delta = (heightTan - desiredHeightTan) * 0.5;  // cut from each side
        d maxTrim = std::fabs(tanU) < std::fabs(tanD) ? std::fabs(tanU) : std::fabs(tanD);

        if (delta >= maxTrim)
            return;

        fov.angleUp = static_cast<float>(std::atan(tanU - delta));
        fov.angleDown = static_cast<float>(std::atan(tanD + delta));
    }

    // NOTE: this doesn't seem to ever be called by iRacing. Presumably they do their
    // own calculation of the view configuration, but whatever way it works it does
    // seem to work to only override xrLocateViews. This code is left incase it is
    // required on other titles but is completely untested.
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
                    DebugLog("  --> aspect for %u is %.3f\n", i, curAspect);
                    if (curAspect > targetAspect)
                        continue;
                    const uint32_t newH = static_cast<uint32_t>(
                                               std::lround(static_cast<double>(w) / targetAspect));
                    DebugLog("  --> Res clamp: view %u  %ux%u → %ux%u (aspect %.3f)\n",
                             i, w, h, w, newH, targetAspect);
                    views[i].recommendedImageRectHeight = std::max<uint32_t>(1u, newH);
                }
            }
        }

        DebugLog("<-- fommil_widescreen_xrEnumerateViewConfigurationViews %d\n", res);

        return res;
    }

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

        const char* appName = instanceCreateInfo->applicationInfo.applicationName;
        for (const auto& allowed : allowedApps) {
            if (std::strstr(appName, allowed)) {
                enabled = true;
                break;
            }
        }
        Log("%s for \"%s\"\n", enabled ? "ENABLED" : "DISABLED", appName);

        DebugLog("<-- fommil_widescreen_xrCreateApiLayerInstance %d\n", result);
        return result;
    }

}

extern "C" {

    // entry point for the loader
    XrResult __declspec(dllexport) XRAPI_CALL fommil_widescreen_xrNegotiateLoaderApiLayerInterface(
        const XrNegotiateLoaderInfo* const loaderInfo,
        const char* const apiLayerName,
        XrNegotiateApiLayerRequest* const apiLayerRequest)
    {
        DebugLog("--> (early) fommil_widescreen_xrNegotiateLoaderApiLayerInterface\n");

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
                DebugLog("Failed to locate DLL\n");
            }
        }

        if (!logStream.is_open())
        {
            // std::string logFile = (std::filesystem::path(dllHome) / std::filesystem::path(LayerName + ".log")).string();
            std::string logFile = (std::filesystem::path(getenv("LOCALAPPDATA")) / std::filesystem::path(LayerName + ".log")).string();
            logStream.open(logFile, std::ios_base::ate);
            Log("dllHome is \"%s\"\n", dllHome.c_str());
        }

        std::filesystem::path config = std::filesystem::path(getenv("LOCALAPPDATA")) / std::filesystem::path(LayerName + ".ini");
        if (std::filesystem::exists(config)) {
            wchar_t buffer[64];
            GetPrivateProfileStringW(L"Settings", L"aspect", L"", buffer, 64, config.c_str());
            double aspect = std::stod(buffer);
            if (aspect >= 1 && aspect <= 3) {
                targetAspect = aspect;
            }
        }
        Log("target aspect is %f\n", targetAspect);

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

        apiLayerRequest->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
        apiLayerRequest->layerApiVersion = XR_CURRENT_API_VERSION;
        apiLayerRequest->getInstanceProcAddr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(fommil_widescreen_xrGetInstanceProcAddr);
        apiLayerRequest->createApiLayerInstance = reinterpret_cast<PFN_xrCreateApiLayerInstance>(fommil_widescreen_xrCreateApiLayerInstance);

        DebugLog("<-- fommil_widescreen_xrNegotiateLoaderApiLayerInterface\n");

        Log("%s layer is active\n", LayerName.c_str());

        return XR_SUCCESS;
    }

}
