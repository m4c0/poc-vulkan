#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __APPLE__
#define VK_USE_PLATFORM_METAL_EXT
#endif

#define VOLK_IMPLEMENTATION
#include "volk.h"

// not really needed. I'm using because it enables Vim's ctrl-n
#include "Vulkan-Headers/include/vulkan/vulkan_core.h"

// TODO: read this from "platform" or surface
static const VkExtent2D vlk_ext = { 300, 200 };

#define MAX_SWAPCHAIN_IMAGES 8
static VkCommandBuffer vlk_cb      [MAX_SWAPCHAIN_IMAGES];
static VkFramebuffer   vlk_fb      [MAX_SWAPCHAIN_IMAGES];
static VkImage         vlk_swc_img [MAX_SWAPCHAIN_IMAGES];
static VkImageView     vlk_swc_iv  [MAX_SWAPCHAIN_IMAGES];

// TODO: multiple "inflights"
static VkFence     vlk_fence;
static VkSemaphore vlk_sema_img;
static VkSemaphore vlk_sema_present;

static VkCommandPool vlk_cpool;
static VkDevice vlk_dev;
static VkInstance vlk_ins;
static VkPhysicalDevice vlk_pd;
static VkQueue vlk_q;
static VkRenderPass vlk_rp;
static VkSurfaceFormatKHR vlk_surf_fmt;
static VkSurfaceKHR vlk_surf;
static VkSwapchainKHR vlk_swc;
static unsigned vlk_qf;
static unsigned vlk_swc_count;

#ifdef __APPLE__
CAMetalLayer * vlk_metal_layer();
#endif

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
  VkAttachmentDescription att = {
    .format      = vlk_surf_fmt.format,
    .samples     = VK_SAMPLE_COUNT_1_BIT,
    .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
    .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
  };

  VkSubpassDescription subpass = {0};

  VkRenderPassCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .attachmentCount = 1,
    .pAttachments = &att,
    .subpassCount = 1,
    .pSubpasses = &subpass,
  };
  _(vkCreateRenderPass(vlk_dev, &info, NULL, &vlk_rp));
}

static void vlk_create_surface() {
#ifdef __APPLE__
  VkMetalSurfaceCreateInfoEXT info = {
    .sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT,
    .pLayer = vlk_metal_layer(),
  };
  _(vkCreateMetalSurfaceEXT(vlk_ins, &info, NULL, &vlk_surf));
#endif

  VkSurfaceCapabilitiesKHR cap;
  _(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vlk_pd, vlk_surf, &cap));

  vlk_swc_count = cap.minImageCount + 1;
  if (vlk_swc_count > cap.maxImageCount && cap.maxImageCount > 0) vlk_swc_count = cap.maxImageCount;
  assert(vlk_swc_count < MAX_SWAPCHAIN_IMAGES);

  // No concensus on docs or the Internet about how to deal with surface
  // formats. Picking the first seems to be enough for most cases.
  uint32_t sz = 1;
  VkResult res = vkGetPhysicalDeviceSurfaceFormatsKHR(vlk_pd, vlk_surf, &sz, &vlk_surf_fmt);
  if (res != VK_INCOMPLETE) vlk_check(res, "vkGetPhysicalDeviceSurfaceFormatsKHR invalid return");
}

static void vlk_create_swapchain() {
  VkSwapchainCreateInfoKHR info = {
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .surface = vlk_surf,
    .minImageCount = vlk_swc_count,
    .imageFormat = vlk_surf_fmt.format,
    .imageColorSpace = vlk_surf_fmt.colorSpace,
    .imageExtent = vlk_ext,
    .imageArrayLayers = 1,
    // In theory we can add more usages as well
    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
    // We can use others modes, if supported, for max FPS or discard frames if
    // CPU faster than GPU
    .presentMode = VK_PRESENT_MODE_FIFO_KHR,
    // Should be "true" unless we want to read clipped parts
    .clipped = VK_TRUE,
  };
  _(vkCreateSwapchainKHR(vlk_dev, &info, NULL, &vlk_swc));

  _(vkGetSwapchainImagesKHR(vlk_dev, vlk_swc, &vlk_swc_count, vlk_swc_img));
}

static void vlk_create_image_views() {
  for (int i = 0; i < vlk_swc_count; i++) {
    VkImageViewCreateInfo info = {
      .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image            = vlk_swc_img[i],
      .viewType         = VK_IMAGE_VIEW_TYPE_2D,
      .format           = vlk_surf_fmt.format,
      .subresourceRange = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount     = 1,
        .layerCount     = 1,
      },
    };
    _(vkCreateImageView(vlk_dev, &info, NULL, vlk_swc_iv + i));
  }
}

static void vlk_create_framebuffer() {
  for (int i = 0; i < vlk_swc_count; i++) {
    VkFramebufferCreateInfo info = {
      .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass      = vlk_rp,
      .attachmentCount = 1,
      .pAttachments    = vlk_swc_iv + i,
      .width           = vlk_ext.width,
      .height          = vlk_ext.height,
      .layers          = 1,
    };
    _(vkCreateFramebuffer(vlk_dev, &info, NULL, vlk_fb + i));
  }
}

static void vlk_create_semaphores() {
  VkSemaphoreCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
  };
  _(vkCreateSemaphore(vlk_dev, &info, NULL, &vlk_sema_img));
  _(vkCreateSemaphore(vlk_dev, &info, NULL, &vlk_sema_present));
}

static void vlk_create_fence() {
  VkFenceCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    .flags = VK_FENCE_CREATE_SIGNALED_BIT,
  };
  _(vkCreateFence(vlk_dev, &info, NULL, &vlk_fence));
}

static void vlk_create_command_pool() {
  VkCommandPoolCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
  };
  _(vkCreateCommandPool(vlk_dev, &info, NULL, &vlk_cpool));
}

static void vlk_create_command_buffer() {
  VkCommandBufferAllocateInfo info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = vlk_cpool,
    .commandBufferCount = vlk_swc_count,
  };
  _(vkAllocateCommandBuffers(vlk_dev, &info, vlk_cb));
}

static void vlk_begin_command_buffer(int i) {
  VkCommandBufferBeginInfo info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
  };
  vkBeginCommandBuffer(vlk_cb[i], &info);
}
static void vlk_end_command_buffer(int i) {
  vkEndCommandBuffer(vlk_cb[i]);
}

static void vlk_cmd_begin_render_pass(int i) {
  VkClearValue clear = {
    .color = {{ 0.1, 0.2, 0.3, 1 }},
  };
  VkRenderPassBeginInfo rp = {
    .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    .renderPass      = vlk_rp,
    .framebuffer     = vlk_fb[i],
    .renderArea      = (VkRect2D) { .extent = vlk_ext },
    .clearValueCount = 1,
    .pClearValues    = &clear
  };
  vkCmdBeginRenderPass(vlk_cb[i], &rp, VK_SUBPASS_CONTENTS_INLINE);
}
static void vlk_cmd_end_render_pass(int i) {
  vkCmdEndRenderPass(vlk_cb[i]);
}

void vlk_init() {
  _(volkInitialize());

  vlk_create_instance();
  vlk_find_physical_device();
  vlk_create_surface();
  vlk_create_device();
  vlk_create_command_pool();
  vlk_create_command_buffer();
  vlk_create_swapchain();
  vlk_create_image_views();
  vlk_create_render_pass();
  vlk_create_framebuffer();
  vlk_create_semaphores();
  vlk_create_fence();
}

void vlk_frame() {
  _(vkWaitForFences(vlk_dev, 1, &vlk_fence, VK_TRUE, ~0UL));
  _(vkResetFences(vlk_dev, 1, &vlk_fence));

  unsigned idx;
  vkAcquireNextImageKHR(vlk_dev, vlk_swc, ~0UL, vlk_sema_img, VK_NULL_HANDLE, &idx);

  vlk_begin_command_buffer(idx);
  vlk_cmd_begin_render_pass(idx);
  // Render pass is only used to draw something via its "clear value"
  vlk_cmd_end_render_pass(idx);
  vlk_end_command_buffer(idx);

  // TODO: confirm if this is the best and document why
  VkPipelineStageFlags stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  // The idea of the wait semaphore is to wait until the swapchain _actually_
  // made the image available
  VkSubmitInfo submit = {
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pCommandBuffers = vlk_cb + idx,
    .commandBufferCount = 1,
    .pWaitSemaphores = &vlk_sema_img,
    .pWaitDstStageMask = &stage,
    .waitSemaphoreCount = 1,
    .pSignalSemaphores = &vlk_sema_present,
    .signalSemaphoreCount = 1,
  };
  // The fence signals we can reuse the current in-flight
  _(vkQueueSubmit(vlk_q, 1, &submit, vlk_fence));

  // Present is entirely async. We don't have control when it finishes. We then
  // use a semaphore to force it to wait until we finished processing the
  // image.
  VkPresentInfoKHR pres = {
    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    .pWaitSemaphores = &vlk_sema_present,
    .waitSemaphoreCount = 1,
    .swapchainCount = 1,
    .pSwapchains = &vlk_swc,
    .pImageIndices = &idx,
  };
  VkResult res = vkQueuePresentKHR(vlk_q, &pres);
  // TODO: deal with suboptimal
  if (res != VK_SUBOPTIMAL_KHR) vlk_check(res, "vkQueuePresentKHR");
}

void vlk_deinit() {
  // TODO: destroy everything we created.
  vkDeviceWaitIdle(vlk_dev);
  vkDestroyCommandPool(vlk_dev, vlk_cpool, NULL);
  vkDestroyDevice(vlk_dev, NULL);
  vkDestroyInstance(volkGetLoadedInstance(), NULL);
}
