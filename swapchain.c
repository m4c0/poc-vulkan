#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define VOLK_IMPLEMENTATION
#include "volk.h"

#ifdef __APPLE__
#include "Vulkan-Headers/include/vulkan/vulkan_metal.h"
#endif

// not really needed. I'm using because it enables Vim's ctrl-n
#include "Vulkan-Headers/include/vulkan/vulkan_core.h"

static const VkExtent2D vlk_ext = { 300, 200 };

static VkCommandBuffer vlk_cb;
static VkCommandPool vlk_cpool;
static VkDevice vlk_dev;
static VkFramebuffer vlk_fb;
static VkInstance vlk_ins;
static VkPhysicalDevice vlk_pd;
static VkQueue vlk_q;
static VkRenderPass vlk_rp;
static VkSwapchainKHR vlk_swc;
static unsigned vlk_qf;
static unsigned vlk_swc_count;

static void vlk_check(VkResult r, const char * msg) {
  if (r == VK_SUCCESS) return;
  fprintf(stderr, "Vulkan call failed (code=%d): %s\n", r, msg);
  exit(1);
}
#define _(X) vlk_check((X), #X)

static void vlk_create_instance() {
  const char * ext[] = {
    0, // Platform-specific
    VK_KHR_SURFACE_EXTENSION_NAME,
    // Next two are only used by OSX
    VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
    VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
  };

  VkApplicationInfo app = {
    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .apiVersion = VK_API_VERSION_1_0,
  };
  VkInstanceCreateInfo info = (VkInstanceCreateInfo) {
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pApplicationInfo = &app,
    .ppEnabledExtensionNames = ext,
    .enabledExtensionCount = 2,
  };

#ifdef __APPLE__
  ext[0] = VK_EXT_METAL_SURFACE_EXTENSION_NAME;

  // MoltenVK kinda requires this extension/flag. It works without it, but the
  // validation layer will complain.
  info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  info.enabledExtensionCount += 2;
#endif

  _(vkCreateInstance(&info, NULL, &vlk_ins));
  volkLoadInstance(vlk_ins);
}

static void vlk_find_physical_device() {
  VkPhysicalDevice pd[16];
  uint32_t pdsz = 16;
  _(vkEnumeratePhysicalDevices(volkGetLoadedInstance(), &pdsz, pd));
  for (int i = 0; i < pdsz; i++) {
    VkQueueFamilyProperties qp[16];
    uint32_t qpsz = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(pd[i], &qpsz, qp);
    for (vlk_qf = 0; vlk_qf < qpsz; vlk_qf++) {
      if ((qp[vlk_qf].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0) continue;
      if ((qp[vlk_qf].queueFlags & VK_QUEUE_TRANSFER_BIT) == 0) continue;
      vlk_pd = pd[i];
      return;
    }
  }
  assert(0);
}

static void vlk_create_device() {
  const char * ext[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    "VK_KHR_portability_subset",
  };

  const float pri = 1.0f;
  VkDeviceQueueCreateInfo q = (VkDeviceQueueCreateInfo) {
    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    .queueCount = 1,
    .pQueuePriorities = &pri,
    .queueFamilyIndex = vlk_qf,
  };
  VkDeviceCreateInfo info = (VkDeviceCreateInfo) {
    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .queueCreateInfoCount = 1,
    .pQueueCreateInfos = &q,
    .ppEnabledExtensionNames = ext,
    .enabledExtensionCount = 1,
  };

#ifdef __APPLE__
  // It would be more "Vulkan-idiomatic" to test if the current instance has
  // the portability flag.
  info.ppEnabledExtensionNames = ext;
  info.enabledExtensionCount++;
#endif

  _(vkCreateDevice(vlk_pd, &info, NULL, &vlk_dev));
  volkLoadDevice(vlk_dev);

  vkGetDeviceQueue(vlk_dev, vlk_qf, 0, &vlk_q);
}

static void vlk_create_render_pass() {
  VkSubpassDescription subpass = {0};
  VkRenderPassCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .subpassCount = 1,
    .pSubpasses = &subpass,
  };
  _(vkCreateRenderPass(vlk_dev, &info, NULL, &vlk_rp));
}

static void vlk_create_swapchain() {
  VkSwapchainCreateInfoKHR info = {
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
  };
  _(vkCreateSwapchainKHR(vlk_dev, &info, NULL, &vlk_swc));

  _(vkGetSwapchainImagesKHR(vlk_dev, vlk_swc, &vlk_swc_count, 0));
}

static void vlk_create_framebuffer() {
  VkFramebufferCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
    .renderPass = vlk_rp,
    .width = vlk_ext.width,
    .height = vlk_ext.height,
    .layers = 1,
  };
  _(vkCreateFramebuffer(vlk_dev, &info, NULL, &vlk_fb));
}

static void vlk_create_command_pool() {
  VkCommandPoolCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
  };
  _(vkCreateCommandPool(vlk_dev, &info, NULL, &vlk_cpool));
}

static void vlk_create_command_buffer() {
  VkCommandBufferAllocateInfo info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = vlk_cpool,
    .commandBufferCount = 1,
  };
  _(vkAllocateCommandBuffers(vlk_dev, &info, &vlk_cb));
}

static void vlk_begin_command_buffer() {
  VkCommandBufferBeginInfo info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
  };
  vkBeginCommandBuffer(vlk_cb, &info);
}
static void vlk_end_command_buffer() {
  vkEndCommandBuffer(vlk_cb);
}

static void vlk_cmd_begin_render_pass() {
  VkRenderPassBeginInfo rp = {
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    .renderPass = vlk_rp,
    .framebuffer = vlk_fb,
    .renderArea = (VkRect2D) { .extent = vlk_ext },
    .clearValueCount = 1,
    .pClearValues = (VkClearValue[]) {
      (VkClearValue) { .color = {{ 1, 0, 0, 1 }} },
    },
  };
  vkCmdBeginRenderPass(vlk_cb, &rp, VK_SUBPASS_CONTENTS_INLINE);
}
static void vlk_cmd_end_render_pass() {
  vkCmdEndRenderPass(vlk_cb);
}

static void vlk_submit() {
  VkSubmitInfo info = {
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pCommandBuffers = &vlk_cb,
    .commandBufferCount = 1,
  };
  _(vkQueueSubmit(vlk_q, 1, &info, NULL));
}

void vlk_init() {
  _(volkInitialize());

  vlk_create_instance();
  vlk_find_physical_device();
  vlk_create_device();
  vlk_create_command_pool();
  vlk_create_command_buffer();
  vlk_create_swapchain();
  vlk_create_render_pass();
  vlk_create_framebuffer();
}

void vlk_frame() {
  vlk_begin_command_buffer();
  vlk_cmd_begin_render_pass();
  // Render pass is only used to draw something
  vlk_cmd_end_render_pass();
  vlk_end_command_buffer();
  vlk_submit();
}

void vlk_deinit() {
  vkDeviceWaitIdle(vlk_dev);
  vkDestroyCommandPool(vlk_dev, vlk_cpool, NULL);
  vkDestroyDevice(vlk_dev, NULL);
  vkDestroyInstance(volkGetLoadedInstance(), NULL);
}
