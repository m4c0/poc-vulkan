//
// hello.c - compute shader example
//
// This is a "minimal" example of how to use a compute shader (as "minimal" as
// Vulkan can get).
//
// The compute shader takes two buffers. It reads a float from one buffer, adds
// "2.0" then stores in another buffer.
//
// It uses a single descriptor set with two bindings.
//
// Exercise suggestion: try to organise the memory and buffer in a different
// way. For instance, if you try to use single memory for both buffers, you
// have to deal with alignment.
//
// Exercise suggestion: modify the pipeline to replace the single set with
// multiple bindings with multiple sets, etc.
//
#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define VOLK_IMPLEMENTATION
#include "volk.h"

// not really needed. I'm using because it enables Vim's ctrl-n
#include "Vulkan-Headers/include/vulkan/vulkan_core.h"

#define _(X) assert(VK_SUCCESS == (X));

static VkBuffer vlk_buffer_a;
static VkBuffer vlk_buffer_b;
static VkCommandBuffer vlk_cb;
static VkCommandPool vlk_cpool;
static VkDescriptorPool vlk_dpool;
static VkDescriptorSet vlk_dset;
static VkDescriptorSetLayout vlk_dsl;
static VkDevice vlk_dev;
static VkDeviceMemory vlk_memory_a;
static VkDeviceMemory vlk_memory_b;
static VkInstance vlk_ins;
static VkPhysicalDevice vlk_pd;
static VkPipeline vlk_ppl;
static VkPipelineLayout vlk_pl;
static VkQueue vlk_q;
static unsigned vlk_qf;

static void vlk_create_instance() {
  VkApplicationInfo app = {
    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .apiVersion = VK_API_VERSION_1_0,
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
  };
  info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  info.enabledExtensionCount = 1;
  info.ppEnabledExtensionNames = ext;
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
  };

#ifdef __APPLE__
  const char * ext[1] = { "VK_KHR_portability_subset" };
  info.ppEnabledExtensionNames = ext;
  info.enabledExtensionCount = 1;
#endif

  _(vkCreateDevice(vlk_pd, &info, NULL, &vlk_dev));
  volkLoadDevice(vlk_dev);

  vkGetDeviceQueue(vlk_dev, vlk_qf, 0, &vlk_q);
}

static VkShaderModule vlk_create_shader_module() {
  FILE * f = fopen("hello.comp.spv", "rb");
  assert(f);
  assert(0 == fseek(f, 0, SEEK_END));
  long sz = ftell(f);
  assert(sz && (sz % 4 == 0));
  assert(0 == fseek(f, 0, SEEK_SET));
  uint32_t * data = malloc(sz);
  assert(1 == fread(data, sz, 1, f));
  fclose(f);

  VkShaderModuleCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = sz,
    .pCode = data,
  };

  VkShaderModule mod;
  _(vkCreateShaderModule(vlk_dev, &info, NULL, &mod));

  free(data);
  return mod;
}

static void vlk_create_descriptor_set_layouts() {
  VkDescriptorSetLayoutBinding bis[] = {{
    .binding = 0,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
  }, {
    .binding = 1,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
  }};
  VkDescriptorSetLayoutCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount = 2,
    .pBindings = bis,
  };
  _(vkCreateDescriptorSetLayout(vlk_dev, &info, NULL, &vlk_dsl));
}

static void vlk_create_descriptor_pool() {
  VkDescriptorPoolSize pszs[1] = {{
    .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 2,
  }};
  VkDescriptorPoolCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .maxSets = 1,
    .poolSizeCount = 1,
    .pPoolSizes = pszs,
  };
  _(vkCreateDescriptorPool(vlk_dev, &info, NULL, &vlk_dpool));
}

static void vlk_allocate_descriptor_set() {
  VkDescriptorSetAllocateInfo info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .descriptorPool = vlk_dpool,
    .descriptorSetCount = 1,
    .pSetLayouts = &vlk_dsl,
  };
  _(vkAllocateDescriptorSets(vlk_dev, &info, &vlk_dset));

  VkDescriptorBufferInfo b0 = { vlk_buffer_a, 0, VK_WHOLE_SIZE };
  VkDescriptorBufferInfo b1 = { vlk_buffer_b, 0, VK_WHOLE_SIZE };
  VkWriteDescriptorSet wr[] = {{
    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet = vlk_dset,
    .dstBinding = 0,
    .descriptorCount = 1,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .pBufferInfo = &b0,
  }, {
    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet = vlk_dset,
    .dstBinding = 1,
    .descriptorCount = 1,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .pBufferInfo = &b1,
  }};
  vkUpdateDescriptorSets(vlk_dev, 2, wr, 0, NULL);
}

static void vlk_create_pipeline_layouts() {
  VkPipelineLayoutCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = 1,
    .pSetLayouts = &vlk_dsl,
  };
  _(vkCreatePipelineLayout(vlk_dev, &info, NULL, &vlk_pl));
}
static void vlk_create_pipelines() {
  VkShaderModule mod = vlk_create_shader_module();

  VkComputePipelineCreateInfo infos[] = {{
    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .pName = "main",
      .module = mod,
    },
    .layout = vlk_pl,
  }};

  _(vkCreateComputePipelines(vlk_dev, NULL, 1, infos, NULL, &vlk_ppl));
  vkDestroyShaderModule(vlk_dev, mod, NULL);
}

static void vlk_create_buffers() {
  VkBufferCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = sizeof(float),
    .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
  };
  _(vkCreateBuffer(vlk_dev, &info, NULL, &vlk_buffer_a));

  info = (VkBufferCreateInfo) {
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = sizeof(float),
    .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
  };
  _(vkCreateBuffer(vlk_dev, &info, NULL, &vlk_buffer_b));
}

#define F(x, y) (((x) & (y)) == (y))
static void vlk_allocate_memories() {
  VkPhysicalDeviceMemoryProperties props;
  vkGetPhysicalDeviceMemoryProperties(vlk_pd, &props);

  int local = -1, host = -1;
  for (int i = 0; i < props.memoryTypeCount; i++) {
    VkMemoryPropertyFlags flags = props.memoryTypes[i].propertyFlags;
    if (local == -1 && F(flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) local = i;
    if (host == -1 && F(flags, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) host = i;
  }
  assert(local >= 0);
  assert(host >= 0);

  VkMemoryAllocateInfo info = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = sizeof(float),
    .memoryTypeIndex = local,
  };
  _(vkAllocateMemory(vlk_dev, &info, NULL, &vlk_memory_a));
  _(vkBindBufferMemory(vlk_dev, vlk_buffer_a, vlk_memory_a, 0));

  // Note: we could use a single allocation for both buffers, but we have to
  // deal with Vulkan's memory aligment requirements. Or we could bring an
  // entire "vulkan allocation library" as dependency.
  //
  // Breaking in multiple fragments should be fine until you start deallocating
  // those chunks - then you have to deal with classical issues: fragmentation,
  // etc
  info = (VkMemoryAllocateInfo) {
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = sizeof(float),
    .memoryTypeIndex = host,
  };
  _(vkAllocateMemory(vlk_dev, &info, NULL, &vlk_memory_b));
  _(vkBindBufferMemory(vlk_dev, vlk_buffer_b, vlk_memory_b, 0));
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

static void vlk_submit() {
  VkSubmitInfo info = {
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pCommandBuffers = &vlk_cb,
    .commandBufferCount = 1,
  };
  _(vkQueueSubmit(vlk_q, 1, &info, NULL));
}

int main() {
  _(volkInitialize());

  vlk_create_instance();
  vlk_find_physical_device();
  vlk_create_device();
  vlk_create_buffers();
  vlk_allocate_memories();
  vlk_create_descriptor_pool();
  vlk_create_descriptor_set_layouts();
  vlk_allocate_descriptor_set();
  vlk_create_pipeline_layouts();
  vlk_create_pipelines();
  vlk_create_command_pool();
  vlk_create_command_buffer();

  const float k = 67;

  vlk_begin_command_buffer();
  vkCmdUpdateBuffer(vlk_cb, vlk_buffer_a, 0, sizeof(float), &k);
  vkCmdBindPipeline(vlk_cb, VK_PIPELINE_BIND_POINT_COMPUTE, vlk_ppl);
  vkCmdBindDescriptorSets(vlk_cb, VK_PIPELINE_BIND_POINT_COMPUTE, vlk_pl, 0, 1, &vlk_dset, 0, NULL);
  vkCmdDispatch(vlk_cb, 1, 1, 1);
  vlk_end_command_buffer();
  vlk_submit();
  vkDeviceWaitIdle(vlk_dev);

  float * mem;
  _(vkMapMemory(vlk_dev, vlk_memory_b, 0, sizeof(float), 0, (void **)&mem));
  printf("67 + 2 = %f (according to your GPU)\n", *mem);
  assert(*mem == 69 && "output differed from expected");
  vkUnmapMemory(vlk_dev, vlk_memory_b);

  // Vulkan will complain if you leave stuff behind
  vkDestroyDescriptorSetLayout(vlk_dev, vlk_dsl, NULL);
  vkDestroyDescriptorPool(vlk_dev, vlk_dpool, NULL);
  vkDestroyPipelineLayout(vlk_dev, vlk_pl, NULL);
  vkDestroyPipeline(vlk_dev, vlk_ppl, NULL);
  vkDestroyBuffer(vlk_dev, vlk_buffer_a, NULL);
  vkDestroyBuffer(vlk_dev, vlk_buffer_b, NULL);
  vkFreeMemory(vlk_dev, vlk_memory_a, NULL);
  vkFreeMemory(vlk_dev, vlk_memory_b, NULL);
  vkDestroyCommandPool(vlk_dev, vlk_cpool, NULL);
  vkDestroyDevice(vlk_dev, NULL);
  vkDestroyInstance(volkGetLoadedInstance(), NULL);
}
