//
// info.c - print data about local devices
//
// This is a tool to print some information about the physical device.
//
#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>

#define VOLK_IMPLEMENTATION
#include "volk.h"

// not really needed. I'm using because it enables Vim's ctrl-n
#include "Vulkan-Headers/include/vulkan/vulkan_core.h"

#define _(X) assert(VK_SUCCESS == (X));

static VkInstance vlk_ins;

static void vlk_create_instance() {
  VkApplicationInfo app = {
    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .apiVersion = VK_API_VERSION_1_1,
  };
  VkInstanceCreateInfo info = (VkInstanceCreateInfo) {
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pApplicationInfo = &app,
  };

#ifdef __APPLE__
  // MoltenVK kinda requires this extension/flag. It works without it, but the
  // validation layer will complain.
  const char * ext[] = {
    VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
    VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
  };
  info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  info.enabledExtensionCount = 2;
  info.ppEnabledExtensionNames = ext;
#endif

  _(vkCreateInstance(&info, NULL, &vlk_ins));
  volkLoadInstance(vlk_ins);
}

int main() {
  _(volkInitialize());

  vlk_create_instance();

  // Required to use thousands separator
  setlocale(LC_NUMERIC, "");

  VkPhysicalDevice pd[16];
  uint32_t pdsz = 16;
  _(vkEnumeratePhysicalDevices(vlk_ins, &pdsz, pd));
  for (int i = 0; i < pdsz; i++) {
    VkPhysicalDeviceSubgroupProperties subg = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES,
    };
    VkPhysicalDeviceMaintenance3Properties prop3 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES,
      .pNext = &subg,
    };
    VkPhysicalDeviceProperties2 prop = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
      .pNext = &prop3,
    };
    vkGetPhysicalDeviceProperties2(pd[i], &prop);
    printf("%s\n", prop.properties.deviceName);
    printf("-- Max Compute Work Group Invocations: %d\n", prop.properties.limits.maxComputeWorkGroupInvocations);
    printf("-- Max Compute Work Group Size: %d %d %d\n",
        prop.properties.limits.maxComputeWorkGroupSize[0],
        prop.properties.limits.maxComputeWorkGroupSize[1],
        prop.properties.limits.maxComputeWorkGroupSize[2]);
    printf("-- Max Memory Allocation Size: %'llu\n", prop3.maxMemoryAllocationSize);
    printf("-- Subgroup Size: %u\n", subg.subgroupSize);
    printf("\n");
  }

  // Vulkan will complain if you leave stuff behind
  vkDestroyInstance(vlk_ins, NULL);
}
