#include <string>
#include <iostream>

#define WEBGPU_CPP_IMPLEMENTATION
#include <webgpu/webgpu.hpp>
using namespace wgpu;

#include "webgpu-utils.cpp"

wgpu::Device device = nullptr;
wgpu::Instance instance = nullptr;
wgpu::PipelineLayout VAPipelineLayout = nullptr;
wgpu::ComputePipeline VAPipeline = nullptr;
wgpu::Buffer ABuffer = nullptr;
wgpu::Buffer BBuffer = nullptr;
wgpu::Buffer CBuffer = nullptr;
wgpu::Buffer CReadBuffer = nullptr;
wgpu::BindGroup VABindGroup = nullptr;
wgpu::BindGroupLayout VABindGroupLayout = nullptr;
std::unique_ptr<wgpu::ErrorCallback> uncapturedErrorCallback;
std::string deviceName;
const int vec_size = 4096;
const int wg_size = 128;

bool initDevice() {
  instance = createInstance(InstanceDescriptor{});
  if (!instance) {
    std::cerr << "Could not initialize WebGPU!" << std::endl;
    return false;
  }
  RequestAdapterOptions options = RequestAdapterOptions{};
  Adapter adapter = instance.requestAdapter(options);
  AdapterInfo info;
  adapter.getInfo(&info);
  deviceName = info.description.data;
  DeviceDescriptor deviceDescriptor = {};
  deviceDescriptor.deviceLostCallback = deviceLostCallback;
  device = adapter.requestDevice(deviceDescriptor);
  uncapturedErrorCallback = errorCallback(device);
  return true;
}

void initVABindGroup() {
  // Create compute bind group
  std::vector<BindGroupEntry> entries;


  BindGroupEntry AEntry;
  AEntry.binding = 0;
  AEntry.buffer = ABuffer;
  AEntry.offset = 0;
  AEntry.size = vec_size * sizeof(int);

  entries.push_back(AEntry);

  BindGroupEntry BEntry;
  BEntry.binding = 1;
  BEntry.buffer = BBuffer;
  BEntry.offset = 0;
  BEntry.size = vec_size * sizeof(int);

  entries.push_back(BEntry);

  BindGroupEntry CEntry;
  CEntry.binding = 2;
  CEntry.buffer = CBuffer;
  CEntry.offset = 0;
  CEntry.size = vec_size * sizeof(int);

  entries.push_back(CEntry);

  BindGroupDescriptor bindGroupDesc;
  bindGroupDesc.layout = VABindGroupLayout;
  bindGroupDesc.entryCount = (uint32_t)entries.size();
  bindGroupDesc.entries = (WGPUBindGroupEntry*)entries.data();
  VABindGroup = device.createBindGroup(bindGroupDesc);
}

void initVAComputePipeline() {
  // Load compute shader
  ShaderModule VAShaderModule = loadShader("../vec_add.wgsl", device);

  // Create compute pipeline layout
  PipelineLayoutDescriptor pipelineLayoutDesc;
  pipelineLayoutDesc.bindGroupLayoutCount = 1;
  pipelineLayoutDesc.bindGroupLayouts = (WGPUBindGroupLayout*)&VABindGroupLayout;
  VAPipelineLayout = device.createPipelineLayout(pipelineLayoutDesc);

  // Create compute pipeline
  ComputePipelineDescriptor computePipelineDesc;
  std::vector<ConstantEntry> constants(2, Default);
  StringView sv_wg_size;
  std::string s_wg_size = "wg_size";
  sv_wg_size.data = s_wg_size.data();
  sv_wg_size.length = s_wg_size.length();
  constants[0].key = sv_wg_size;
  constants[0].value = wg_size;
  StringView sv_vec_size;
  std::string s_vec_size = "vec_size";
  sv_vec_size.data = s_vec_size.c_str();
  sv_vec_size.length = s_vec_size.length();
  constants[1].key = sv_vec_size;
  constants[1].value = vec_size;
  computePipelineDesc.compute.constantCount = (uint32_t)constants.size();
  computePipelineDesc.compute.constants = constants.data();
  StringView sv_entryPoint;
  std::string s_entryPoint = "vec_add";
  sv_entryPoint.data = s_entryPoint.c_str();
  sv_entryPoint.length = s_entryPoint.length();
  computePipelineDesc.compute.entryPoint = sv_entryPoint;
  computePipelineDesc.compute.module = VAShaderModule;
  computePipelineDesc.layout = VAPipelineLayout;
  VAPipeline = device.createComputePipeline(computePipelineDesc);
}

void initBuffers() {
  BufferDescriptor ABufDesc;
  ABufDesc.mappedAtCreation = false;
  ABufDesc.size = vec_size * sizeof(int);
  ABufDesc.usage = BufferUsage::Storage | BufferUsage::CopyDst;
  ABuffer = device.createBuffer(ABufDesc);

  BufferDescriptor BBufDesc;
  BBufDesc.mappedAtCreation = false;
  BBufDesc.size = vec_size * sizeof(int);
  BBufDesc.usage = BufferUsage::Storage | BufferUsage::CopyDst;
  BBuffer = device.createBuffer(BBufDesc);

  BufferDescriptor CBufDesc;
  CBufDesc.mappedAtCreation = false;
  CBufDesc.size = vec_size * sizeof(int);
  CBufDesc.usage = BufferUsage::Storage | BufferUsage::CopyDst | BufferUsage::CopySrc;
  CBuffer = device.createBuffer(CBufDesc);

  BufferDescriptor CReadBufDesc;
  CReadBufDesc.mappedAtCreation = false;
  CReadBufDesc.size = vec_size * sizeof(int);
  CReadBufDesc.usage = BufferUsage::CopyDst | BufferUsage::MapRead;
  CReadBuffer = device.createBuffer(CReadBufDesc);

}


void initVABindGroupLayout() {

  // Create bind group layout
  std::vector<BindGroupLayoutEntry> bindings;

  BindGroupLayoutEntry AEntry;
  AEntry.binding = 0;
  AEntry.buffer.type = BufferBindingType::Storage;
  AEntry.visibility = ShaderStage::Compute;

  bindings.push_back(AEntry);

  BindGroupLayoutEntry BEntry;
  BEntry.binding = 1;
  BEntry.buffer.type = BufferBindingType::Storage;
  BEntry.visibility = ShaderStage::Compute;

  bindings.push_back(BEntry);

  BindGroupLayoutEntry CEntry;
  CEntry.binding = 2;
  CEntry.buffer.type = BufferBindingType::Storage;
  CEntry.visibility = ShaderStage::Compute;

  bindings.push_back(CEntry);

  BindGroupLayoutDescriptor bindGroupLayoutDesc;
  bindGroupLayoutDesc.entryCount = (uint32_t)bindings.size();
  bindGroupLayoutDesc.entries = bindings.data();
  VABindGroupLayout = device.createBindGroupLayout(bindGroupLayoutDesc);
}

void teardown() {
  wgpuBindGroupRelease(VABindGroup);
  wgpuBufferRelease(ABuffer);
  wgpuBufferRelease(CReadBuffer);
  wgpuBufferRelease(CBuffer);
  wgpuBufferRelease(BBuffer);
  wgpuComputePipelineRelease(VAPipeline);
  wgpuPipelineLayoutRelease(VAPipelineLayout);
  wgpuBindGroupLayoutRelease(VABindGroupLayout);
  wgpuDeviceRelease(device);
  wgpuInstanceRelease(instance);
}

void run() {
  Queue queue = device.getQueue();
  std::vector<uint32_t> A_host;
  std::vector<uint32_t> B_host;
  for (int i = 0; i < vec_size; i++) {
    A_host.push_back(1);
    B_host.push_back(2);
  }
  queue.writeBuffer(ABuffer, 0, A_host.data(), A_host.size() * sizeof(uint32_t));
  queue.writeBuffer(BBuffer, 0, B_host.data(), B_host.size() * sizeof(uint32_t));

  CommandEncoder encoder = device.createCommandEncoder(CommandEncoderDescriptor{});
  ComputePassEncoder VAComputePass = encoder.beginComputePass(ComputePassDescriptor{});
  VAComputePass.setPipeline(VAPipeline);
  VAComputePass.setBindGroup(0, VABindGroup, 0, nullptr);
  VAComputePass.dispatchWorkgroups(vec_size / wg_size, 1, 1);
  VAComputePass.end();

  encoder.copyBufferToBuffer(CBuffer, 0, CReadBuffer, 0, vec_size * 4);
  CommandBuffer commands = encoder.finish(CommandBufferDescriptor{});
  queue.submit(commands);
  bool done = false;

  auto handle = CReadBuffer.mapAsync(MapMode::Read, 0, vec_size * 4, [&](BufferMapAsyncStatus status) {
    if (status == BufferMapAsyncStatus::Success) {
      const uint* output = (const uint*)CReadBuffer.getConstMappedRange(0, vec_size * 4);
      for (int i = 0; i < vec_size; i++) {
        assert(output[i] == 3);
      }
      std::cout << "passed the test!" << std::endl;
      CReadBuffer.unmap();
    }
    done = true;
    });
  while (!done)
    // Checks for ongoing asynchronous operations and call their callbacks if needed
    instance.processEvents();
  return;
}

int main() {

  if (!initDevice()) return -1;
  std::cout << "using device: " << deviceName << std::endl;
  initVABindGroupLayout();
  initVAComputePipeline();
  initBuffers();
  initVABindGroup();
  run();

  teardown();
}
