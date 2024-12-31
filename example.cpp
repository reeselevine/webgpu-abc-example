#include <webgpu/webgpu_cpp.h>
#include <iostream>
#include <filesystem>
#include <fstream>

using namespace wgpu;

Device device = nullptr;
Adapter adapter = nullptr;
Instance instance = nullptr;
PipelineLayout VAPipelineLayout = nullptr;
ComputePipeline VAPipeline = nullptr;
Buffer ABuffer = nullptr;
Buffer BBuffer = nullptr;
Buffer CBuffer = nullptr;
Buffer CReadBuffer = nullptr;
BindGroup VABindGroup = nullptr;
BindGroupLayout VABindGroupLayout = nullptr;
std::unique_ptr<wgpu::ErrorCallback> uncapturedErrorCallback;
std::string deviceName;
const int vec_size = 131072;
const int wg_size = 128;

void deviceLostCallback(WGPUDeviceLostReason reason, WGPUStringView message, void* /* pUserData*/) {
  std::cout << "Device lost: reason " << reason;
  if (message.length > 0) std::cout << " (message: " << message.data << ")";
  std::cout << std::endl;
};

ShaderModule loadShader(const std::filesystem::path& path, Device device) {

  std::ifstream file(path);
  if (!file.is_open()) {
    return nullptr;
  }
  file.seekg(0, std::ios::end);
  size_t size = file.tellg();
  std::string shaderSource(size, ' ');
  file.seekg(0);
  file.read(shaderSource.data(), size);

  ShaderSourceWGSL shaderCodeDesc{};
  ShaderModuleDescriptor shaderModuleDescriptor{
    .nextInChain = &shaderCodeDesc};
  StringView code;
  code.data = shaderSource.c_str();
  code.length = shaderSource.length();
  shaderCodeDesc.code = code;
  //std::cout << shaderSource.data() << std::endl;
  //std::cout << "try this!\n";
  return device.CreateShaderModule(&shaderModuleDescriptor);
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
  bindGroupDesc.entries = entries.data();
  VABindGroup = device.CreateBindGroup(&bindGroupDesc);
}

void initVAComputePipeline() {
  // Load compute shader
  ShaderModule VAShaderModule = loadShader("../vec_add.wgsl", device);

  // Create compute pipeline layout
  PipelineLayoutDescriptor pipelineLayoutDesc;
  pipelineLayoutDesc.bindGroupLayoutCount = 1;
  pipelineLayoutDesc.bindGroupLayouts = &VABindGroupLayout;
  VAPipelineLayout = device.CreatePipelineLayout(&pipelineLayoutDesc);

  // Create compute pipeline
  ComputePipelineDescriptor computePipelineDesc;
  std::vector<ConstantEntry> constants(2);
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
  VAPipeline = device.CreateComputePipeline(&computePipelineDesc);
}

void initBuffers() {
  BufferDescriptor ABufDesc;
  ABufDesc.mappedAtCreation = false;
  ABufDesc.size = vec_size * sizeof(int);
  ABufDesc.usage = BufferUsage::Storage | BufferUsage::CopyDst;
  ABuffer = device.CreateBuffer(&ABufDesc);

  BufferDescriptor BBufDesc;
  BBufDesc.mappedAtCreation = false;
  BBufDesc.size = vec_size * sizeof(int);
  BBufDesc.usage = BufferUsage::Storage | BufferUsage::CopyDst;
  BBuffer = device.CreateBuffer(&BBufDesc);

  BufferDescriptor CBufDesc;
  CBufDesc.mappedAtCreation = false;
  CBufDesc.size = vec_size * sizeof(int);
  CBufDesc.usage = BufferUsage::Storage | BufferUsage::CopyDst | BufferUsage::CopySrc;
  CBuffer = device.CreateBuffer(&CBufDesc);

  BufferDescriptor CReadBufDesc;
  CReadBufDesc.mappedAtCreation = false;
  CReadBufDesc.size = vec_size * sizeof(int);
  CReadBufDesc.usage = BufferUsage::CopyDst | BufferUsage::MapRead;
  CReadBuffer = device.CreateBuffer(&CReadBufDesc);

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
  VABindGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDesc);
}

void run() {
  Queue queue = device.GetQueue();
  std::vector<uint32_t> A_host;
  std::vector<uint32_t> B_host;
  for (int i = 0; i < vec_size; i++) {
    A_host.push_back(1);
    B_host.push_back(2);
  }
  queue.WriteBuffer(ABuffer, 0, A_host.data(), A_host.size() * sizeof(uint32_t));
  queue.WriteBuffer(BBuffer, 0, B_host.data(), B_host.size() * sizeof(uint32_t));

  CommandEncoder encoder = device.CreateCommandEncoder();
  ComputePassEncoder VAComputePass = encoder.BeginComputePass();
  VAComputePass.SetPipeline(VAPipeline);
  VAComputePass.SetBindGroup(0, VABindGroup, 0, nullptr);
  VAComputePass.DispatchWorkgroups(vec_size / wg_size, 1, 1);
  VAComputePass.End();

  encoder.CopyBufferToBuffer(CBuffer, 0, CReadBuffer, 0, vec_size * 4);
  CommandBuffer commands = encoder.Finish();
  queue.Submit(1, &commands);

  instance.ProcessEvents();
  
  WaitStatus waitStatus = WaitStatus::Unknown;
  MapAsyncStatus readStatus = MapAsyncStatus::Unknown;
  waitStatus = instance.WaitAny(
    CReadBuffer.MapAsync(MapMode::Read, 0, vec_size * 4, CallbackMode::AllowSpontaneous,
      [&readStatus](wgpu::MapAsyncStatus status, wgpu::StringView) {
        readStatus = status;
      }),
    UINT64_MAX);

  const uint* output = (const uint*)CReadBuffer.GetConstMappedRange(0, vec_size * 4);
  for (int i = 0; i < vec_size; i++) {
    assert(output[i] == 3);
  }
  std::cout << "passed the test!" << std::endl;
  CReadBuffer.Unmap();
}

void start() {
  AdapterInfo info;
  adapter.GetInfo(&info);
  deviceName = info.description.data;
  std::cout << "using device: " << deviceName << std::endl;
  initVABindGroupLayout();
  initVAComputePipeline();
  initBuffers();
  initVABindGroup();
  run();
}

int main() {
  InstanceFeatures features;
  features.timedWaitAnyEnable = true;
  InstanceDescriptor descriptor;
  descriptor.features = features;
  instance = wgpu::CreateInstance(&descriptor);

  RequestAdapterStatus adapterStatus;
  Adapter adapterResult = nullptr;

  WaitStatus waitStatus = instance.WaitAny(
    instance.RequestAdapter(
      nullptr, CallbackMode::AllowSpontaneous,
      [&adapterStatus, &adapterResult](RequestAdapterStatus s, Adapter adapter,
                         StringView message) {
        adapterStatus = s;
        adapterResult = std::move(adapter);
      }),
    UINT64_MAX);

  adapter = adapterResult;

  RequestDeviceStatus deviceStatus;
  Device deviceResult = nullptr;
  DeviceDescriptor deviceDescriptor;
  deviceDescriptor.SetDeviceLostCallback(CallbackMode::AllowSpontaneous, 
    [](const Device& device, DeviceLostReason reason, const char* message) {
      std::cout << "Device lost! Reason: " << static_cast<int>(reason)
                << ", Message: " << message << "\n";
    });
  waitStatus = instance.WaitAny(
    adapter.RequestDevice(
      &deviceDescriptor, CallbackMode::AllowSpontaneous,
      [&deviceStatus, &deviceResult](RequestDeviceStatus s,
                         Device device, StringView message) {
        deviceStatus = s;
        deviceResult = std::move(device);
      }),
    UINT64_MAX);
  
  device = deviceResult;

  start();

  return 0;
}
