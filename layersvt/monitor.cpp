/*
 * Vulkan
 *
 * Copyright (C) 2016 Valve Corporation
 * Copyright (C) 2016 LunarG, Inc.
 * Copyright (C) 2016 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Author: Chris Forbes <chrisforbes@google.com>
 * Author: Tony Barbour <tony@lunarg.com>
 */
#include <vk_loader_platform.h>
#include "vk_layer_data.h"
#include "vk_layer_extension_utils.h"
#include "vk_layer_table.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unordered_map>
#include <vk_dispatch_table_helper.h>
#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#if (!defined(VK_USE_PLATFORM_XCB_KHR) && !defined(VK_USE_PLATFORM_WIN32_KHR) && !defined(VK_USE_PLATFORM_ANDROID_KHR))
#warning "Monitor layer only has code for XCB, Windows and Android at this time"
#endif

#ifdef ANDROID
#include <android/log.h>
#endif

#define TITLE_LENGTH 1000
#define FPS_LENGTH 24
struct layer_data {
    VkLayerDispatchTable *device_dispatch_table;
    VkLayerInstanceDispatchTable *instance_dispatch_table;

    PFN_vkQueuePresentKHR pfnQueuePresentKHR;
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    HWND hwnd;
#elif defined(VK_USE_PLATFORM_XCB_KHR)
    xcb_connection_t *connection;
    xcb_window_t xcb_window;
    bool xcb_fps;
#endif
    char base_title[TITLE_LENGTH];

    VkPhysicalDevice gpu;
    VkDevice device;

    PFN_vkSetDeviceLoaderData pfn_dev_init;
    int lastFrame;
    time_t lastTime;
    float fps;
    int frame;
};

static std::unordered_map<void *, layer_data *> layer_data_map;

template layer_data *GetLayerDataPtr<layer_data>(void *data_key, std::unordered_map<void *, layer_data *> &data_map);

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice gpu, const VkDeviceCreateInfo *pCreateInfo,
                                                              const VkAllocationCallbacks *pAllocator, VkDevice *pDevice) {
    VkLayerDeviceCreateInfo *chain_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);

    assert(chain_info->u.pLayerInfo);
    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr fpGetDeviceProcAddr = chain_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    PFN_vkCreateDevice fpCreateDevice = (PFN_vkCreateDevice)fpGetInstanceProcAddr(NULL, "vkCreateDevice");
    if (fpCreateDevice == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Advance the link info for the next element on the chain
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

    VkResult result = fpCreateDevice(gpu, pCreateInfo, pAllocator, pDevice);
    if (result != VK_SUCCESS) {
        return result;
    }

    layer_data *my_device_data = GetLayerDataPtr(get_dispatch_key(*pDevice), layer_data_map);

    // Setup device dispatch table
    my_device_data->device_dispatch_table = new VkLayerDispatchTable;
    layer_init_device_dispatch_table(*pDevice, my_device_data->device_dispatch_table, fpGetDeviceProcAddr);

    // store the loader callback for initializing created dispatchable objects
    chain_info = get_chain_info(pCreateInfo, VK_LOADER_DATA_CALLBACK);
    if (chain_info) {
        my_device_data->pfn_dev_init = chain_info->u.pfnSetDeviceLoaderData;
    } else {
        my_device_data->pfn_dev_init = NULL;
    }

    my_device_data->gpu = gpu;
    my_device_data->device = *pDevice;
    my_device_data->frame = 0;
    my_device_data->lastFrame = 0;
    my_device_data->fps = 0.0;
    time(&my_device_data->lastTime);

    // Get our WSI hooks in
    VkLayerDispatchTable *pTable = my_device_data->device_dispatch_table;
    my_device_data->pfnQueuePresentKHR = (PFN_vkQueuePresentKHR)pTable->GetDeviceProcAddr(*pDevice, "vkQueuePresentKHR");

    return result;
}

VK_LAYER_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator) {
    dispatch_key key = get_dispatch_key(device);
    layer_data *my_data = GetLayerDataPtr(key, layer_data_map);
    VkLayerDispatchTable *pTable = my_data->device_dispatch_table;
    pTable->DeviceWaitIdle(device);
    pTable->DestroyDevice(device, pAllocator);
    delete pTable;
    layer_data_map.erase(key);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                                                                const VkAllocationCallbacks *pAllocator, VkInstance *pInstance) {
    VkLayerInstanceCreateInfo *chain_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);

    assert(chain_info->u.pLayerInfo);
    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkCreateInstance fpCreateInstance = (PFN_vkCreateInstance)fpGetInstanceProcAddr(NULL, "vkCreateInstance");
    if (fpCreateInstance == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Advance the link info for the next element on the chain
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

    VkResult result = fpCreateInstance(pCreateInfo, pAllocator, pInstance);
    if (result != VK_SUCCESS) return result;

    layer_data *my_data = GetLayerDataPtr(get_dispatch_key(*pInstance), layer_data_map);
    my_data->instance_dispatch_table = new VkLayerInstanceDispatchTable;
    layer_init_instance_dispatch_table(*pInstance, my_data->instance_dispatch_table, fpGetInstanceProcAddr);

    return result;
}

VK_LAYER_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator) {
    dispatch_key key = get_dispatch_key(instance);
    layer_data *my_data = GetLayerDataPtr(key, layer_data_map);
    VkLayerInstanceDispatchTable *pTable = my_data->instance_dispatch_table;
    pTable->DestroyInstance(instance, pAllocator);
    delete pTable;
    layer_data_map.erase(key);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo) {
    layer_data *my_data = GetLayerDataPtr(get_dispatch_key(queue), layer_data_map);

    time_t now;
    time(&now);
    float seconds = (float)difftime(now, my_data->lastTime);

    if (seconds > 0.5) {
        char str[TITLE_LENGTH + FPS_LENGTH];
        char fpsstr[FPS_LENGTH];
        layer_data *my_instance_data = GetLayerDataPtr(get_dispatch_key(my_data->gpu), layer_data_map);
        my_data->fps = (my_data->frame - my_data->lastFrame) / seconds;
        my_data->lastFrame = my_data->frame;
        my_data->lastTime = now;
        sprintf(fpsstr, "   FPS = %.2f", my_data->fps);
        strcpy(str, my_instance_data->base_title);
        strcat(str, fpsstr);
#if defined(VK_USE_PLATFORM_WIN32_KHR)
        SetWindowText(my_instance_data->hwnd, str);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
        if (my_instance_data->xcb_fps) {
            xcb_change_property(my_instance_data->connection, XCB_PROP_MODE_REPLACE, my_instance_data->xcb_window, XCB_ATOM_WM_NAME,
                                XCB_ATOM_STRING, 8, strlen(str), str);
            xcb_flush(my_instance_data->connection);
        }
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
        __android_log_print(ANDROID_LOG_INFO, "monitor", "%s\n", str);
#endif
    }
    my_data->frame++;

    VkResult result = my_data->pfnQueuePresentKHR(queue, pPresentInfo);
    return result;
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceToolPropertiesEXT(
    VkPhysicalDevice physicalDevice, uint32_t *pToolCount, VkPhysicalDeviceToolPropertiesEXT *pToolProperties) {
    static const VkPhysicalDeviceToolPropertiesEXT monitor_layer_tool_props = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TOOL_PROPERTIES_EXT,
        nullptr,
        "Monitor Layer",
        "1",
        VK_TOOL_PURPOSE_PROFILING_BIT_EXT | VK_TOOL_PURPOSE_ADDITIONAL_FEATURES_BIT_EXT,
        "The VK_LAYER_LUNARG_monitor utility layer prints the real-time frames-per-second value to the application's title bar.",
        "VK_LAYER_LUNARG_monitor"};

    auto original_pToolProperties = pToolProperties;
    if (pToolProperties != nullptr) {
        *pToolProperties = monitor_layer_tool_props;
        pToolProperties = ((*pToolCount > 1) ? &pToolProperties[1] : nullptr);
        (*pToolCount)--;
    }

    layer_data *my_data = GetLayerDataPtr(get_dispatch_key(physicalDevice), layer_data_map);
    VkResult result =
        my_data->instance_dispatch_table->GetPhysicalDeviceToolPropertiesEXT(physicalDevice, pToolCount, pToolProperties);

    if (original_pToolProperties != nullptr) {
        pToolProperties = original_pToolProperties;
    }

    (*pToolCount)++;

    return result;
}

#if defined(VK_USE_PLATFORM_WIN32_KHR)
VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateWin32SurfaceKHR(VkInstance instance,
                                                                       const VkWin32SurfaceCreateInfoKHR *pCreateInfo,
                                                                       const VkAllocationCallbacks *pAllocator,
                                                                       VkSurfaceKHR *pSurface) {
    layer_data *my_data = GetLayerDataPtr(get_dispatch_key(instance), layer_data_map);
    my_data->hwnd = pCreateInfo->hwnd;
    GetWindowText(my_data->hwnd, my_data->base_title, TITLE_LENGTH);

    VkResult result = my_data->instance_dispatch_table->CreateWin32SurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
    return result;
}
#elif defined(VK_USE_PLATFORM_XCB_KHR)
VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateXcbSurfaceKHR(VkInstance instance,
                                                                     const VkXcbSurfaceCreateInfoKHR *pCreateInfo,
                                                                     const VkAllocationCallbacks *pAllocator,
                                                                     VkSurfaceKHR *pSurface) {
    xcb_get_property_cookie_t cookie;
    xcb_get_property_reply_t *reply;
    xcb_atom_t property = XCB_ATOM_WM_NAME;
    xcb_atom_t type = XCB_ATOM_STRING;

    layer_data *my_data = GetLayerDataPtr(get_dispatch_key(instance), layer_data_map);
    my_data->xcb_window = pCreateInfo->window;
    my_data->connection = pCreateInfo->connection;
    cookie = xcb_get_property(my_data->connection, 0, my_data->xcb_window, property, type, 0, 0);
    if ((reply = xcb_get_property_reply(my_data->connection, cookie, NULL))) {
        my_data->xcb_fps = true;
        int len = xcb_get_property_value_length(reply);
        if (len > TITLE_LENGTH) {
            my_data->xcb_fps = false;
        } else if (len > 0) {
            strcpy(my_data->base_title, (char *)xcb_get_property_value(reply));
        } else {
            // No window title - make base title null string
            my_data->base_title[0] = 0;
        }
    }

    VkResult result = my_data->instance_dispatch_table->CreateXcbSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
    return result;
}
#endif

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice dev, const char *funcName) {
#define ADD_HOOK(fn) \
    if (!strncmp(#fn, funcName, sizeof(#fn))) return (PFN_vkVoidFunction)fn

    ADD_HOOK(vkGetDeviceProcAddr);
    ADD_HOOK(vkDestroyDevice);
    ADD_HOOK(vkQueuePresentKHR);
#undef ADD_HOOK

    if (dev == NULL) return NULL;

    layer_data *dev_data;
    dev_data = GetLayerDataPtr(get_dispatch_key(dev), layer_data_map);
    VkLayerDispatchTable *pTable = dev_data->device_dispatch_table;

    if (pTable->GetDeviceProcAddr == NULL) return NULL;
    return pTable->GetDeviceProcAddr(dev, funcName);
}

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char *funcName) {
#define ADD_HOOK(fn) \
    if (!strncmp(#fn, funcName, sizeof(#fn))) return (PFN_vkVoidFunction)fn

    ADD_HOOK(vkCreateInstance);
    ADD_HOOK(vkCreateDevice);
    ADD_HOOK(vkDestroyInstance);
    ADD_HOOK(vkGetInstanceProcAddr);
    ADD_HOOK(vkGetPhysicalDeviceToolPropertiesEXT);
    ADD_HOOK(vkEnumerateInstanceLayerProperties);
    ADD_HOOK(vkEnumerateDeviceLayerProperties);
    ADD_HOOK(vkEnumerateInstanceExtensionProperties);
    ADD_HOOK(vkEnumerateDeviceExtensionProperties);
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    ADD_HOOK(vkCreateWin32SurfaceKHR);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
    ADD_HOOK(vkCreateXcbSurfaceKHR);
#endif
#undef ADD_HOOK

    if (instance == NULL) return NULL;

    layer_data *instance_data;
    instance_data = GetLayerDataPtr(get_dispatch_key(instance), layer_data_map);
    VkLayerInstanceDispatchTable *pTable = instance_data->instance_dispatch_table;

    if (pTable->GetInstanceProcAddr == NULL) return NULL;
    return pTable->GetInstanceProcAddr(instance, funcName);
}

static const VkLayerProperties global_layer = {
    "VK_LAYER_LUNARG_monitor",      // layerName
    VK_MAKE_VERSION(1, 0, 68),      // specVersion (clamped to final 1.0 spec version)
    1,                              // implementationVersion
    "Layer: monitor",               // description
};

VKAPI_ATTR VkResult VKAPI_CALL EnumerateInstanceLayerProperties(uint32_t *pCount, VkLayerProperties *pProperties) {
    return util_GetLayerProperties(1, &global_layer, pCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t *pCount,
                                                              VkLayerProperties *pProperties) {
    return util_GetLayerProperties(1, &global_layer, pCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateInstanceExtensionProperties(const char *pLayerName, uint32_t *pCount,
                                                                    VkExtensionProperties *pProperties) {
    if (pLayerName && !strcmp(pLayerName, global_layer.layerName)) return util_GetExtensionProperties(0, NULL, pCount, pProperties);

    return VK_ERROR_LAYER_NOT_PRESENT;
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char *pLayerName,
                                                                  uint32_t *pCount, VkExtensionProperties *pProperties) {
    if (pLayerName && !strcmp(pLayerName, global_layer.layerName))
    {
        return util_GetExtensionProperties(0, NULL, pCount, pProperties);
    }

    assert(physicalDevice);
    dispatch_key key = get_dispatch_key(physicalDevice);
    layer_data *my_data = GetLayerDataPtr(key, layer_data_map);
    VkResult result = my_data->instance_dispatch_table->EnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pCount, pProperties);
    return result;
}

// loader-layer interface v0

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(uint32_t *pCount,
                                                                                  VkLayerProperties *pProperties) {
    return EnumerateInstanceLayerProperties(pCount, pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t *pCount,
                                                                                VkLayerProperties *pProperties) {
    // the layer command handles VK_NULL_HANDLE just fine internally
    assert(physicalDevice == VK_NULL_HANDLE);
    return EnumerateDeviceLayerProperties(VK_NULL_HANDLE, pCount, pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char *pLayerName, uint32_t *pCount,
                                                                                      VkExtensionProperties *pProperties) {
    return EnumerateInstanceExtensionProperties(pLayerName, pCount, pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice,
                                                                                    const char *pLayerName, uint32_t *pCount,
                                                                                    VkExtensionProperties *pProperties) {
    return EnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pCount, pProperties);
}
