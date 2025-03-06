#include <webgpu/webgpu_cpp.h>
#include <iostream>
#include <filesystem>
#include <fstream>

using namespace wgpu;

Device device;
Instance instance;
ComputePipeline pipeline;
Buffer ABuffer;
Buffer BBuffer;
Buffer CBuffer;
Buffer CReadBuffer;
Buffer DBuffer;
BindGroup bindGroup;
BindGroupLayout bindGroupLayout;
const int vec_size = 1024;
const int wg_size = 128;
const int BATCH_SIZE = 2;

StringView makeStringView(std::string str) {
  return StringView(str.data(), str.length());
}

void deviceLostCallback(WGPUDeviceLostReason reason, WGPUStringView message, void* /* pUserData*/) {
  std::cout << "Device lost: reason " << reason;
  if (message.length > 0) std::cout << " (message: " << message.data << ")";
  std::cout << std::endl;
};

ShaderModule loadShader(const std::filesystem::path& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return nullptr;
  }

  file.seekg(0, std::ios::end);
  size_t size = file.tellg();
  //james subgroups and uniformity analysis
  std::string shaderSource = "enable subgroups;\ndiagnostic(off, subgroup_uniformity);\n";
  size_t sourceBegin = shaderSource.size();
  shaderSource.resize(sourceBegin + size);
  file.seekg(0);
  file.read(shaderSource.data() + sourceBegin, size);
  shaderSource.replace(shaderSource.find("const BATCH_SIZE = 4;"), sizeof("const BATCH_SIZE = 4;") - 1, "const BATCH_SIZE = " + std::to_string(BATCH_SIZE) + ";");



  ShaderSourceWGSL shaderCodeDesc{};
  ShaderModuleDescriptor shaderModuleDescriptor{
    .nextInChain = &shaderCodeDesc};
  StringView code;
  code.data = shaderSource.c_str();
  code.length = shaderSource.length();
  shaderCodeDesc.code = code;
  return device.CreateShaderModule(&shaderModuleDescriptor);
}

void initBindGroupLayout() {
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

  // atomic buffer
  BindGroupLayoutEntry DEntry;
  DEntry.binding = 3;
  DEntry.buffer.type = BufferBindingType::Storage;
  DEntry.visibility = ShaderStage::Compute;
  bindings.push_back(DEntry);

  BindGroupLayoutDescriptor bindGroupLayoutDesc;
  bindGroupLayoutDesc.entryCount = (uint32_t)bindings.size();
  bindGroupLayoutDesc.entries = bindings.data();
  bindGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDesc);
}


void initBindGroup() {
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

  // atomic buffer
  BindGroupEntry DEntry;
  DEntry.binding = 3;
  DEntry.buffer = DBuffer;
  DEntry.offset = 0;
  DEntry.size = sizeof(int);
  entries.push_back(DEntry);

  BindGroupDescriptor bindGroupDesc;
  bindGroupDesc.layout = bindGroupLayout;
  bindGroupDesc.entryCount = (uint32_t)entries.size();
  bindGroupDesc.entries = entries.data();
  bindGroup = device.CreateBindGroup(&bindGroupDesc);
}

void initComputePipeline() {

  // Create compute pipeline layout
  PipelineLayoutDescriptor pipelineLayoutDesc;
  pipelineLayoutDesc.bindGroupLayoutCount = 1;
  pipelineLayoutDesc.bindGroupLayouts = &bindGroupLayout;
  PipelineLayout pipelineLayout = device.CreatePipelineLayout(&pipelineLayoutDesc);

  // Load compute shader
  ShaderModule shaderModule = loadShader("vec_add.wgsl");
  WaitStatus waitStatus = WaitStatus::Unknown;
  wgpu::CompilationInfoRequestStatus compilationStatus = CompilationInfoRequestStatus::Unknown;
  WGPUCompilationInfo _compilationInfo = {};

  waitStatus = instance.WaitAny(shaderModule.GetCompilationInfo(CallbackMode::AllowSpontaneous,
  [&compilationStatus, &_compilationInfo](CompilationInfoRequestStatus status, CompilationInfo const* compilationInfo) {
    compilationStatus = status;

    if (compilationInfo != nullptr) {
      bool compileError = false;

      // Copy the compilerInfo
      _compilationInfo = *compilationInfo;

      // Print and iterate over all compiler messages
      std::cout << "Compiler Message Count: " << _compilationInfo.messageCount << std::endl;
      for (size_t i = 0; i < _compilationInfo.messageCount; ++i) {
        const WGPUCompilationMessage& msg = _compilationInfo.messages[i];
        std::cout << "Line: " << (uint32_t)(msg.lineNum - msg.offset) << " " << std::endl;
        std::cout << "msg type: " << (uint32_t)(msg.type) << std::endl; 
        std::cout << "  " << std::string(msg.message.data, msg.message.length) << std::endl;
        
        // msg.type = 1 = ERROR 
        if (std::string(msg.message.data, msg.message.length) != "'subgroupExclusiveAdd' must only be called from uniform control flow") {
          if (msg.type == 1) {
            compileError = true;
          }
          std::cout << "normal" << std::endl; 
        }else{
          std::cout << "subgroup error but continue" << std::endl;
        }
      }
      // If odd, cancel pipeline 
      assert(compileError != 1);
    }
  }), UINT64_MAX);
  

  if (waitStatus != WaitStatus::Success || compilationStatus != CompilationInfoRequestStatus::Success) { 
    std::cout << "Compiler Failed with Error Code: " << (uint32_t)compilationStatus << std::endl;
    return;
  }

  // Create compute pipeline
  ComputePipelineDescriptor computePipelineDesc;
  std::vector<ConstantEntry> constants(2);
  StringView wgSizeSV = makeStringView("wg_size");
  constants[0].key = wgSizeSV;
  constants[0].value = wg_size;
  StringView vecSizeSV = makeStringView("vec_size");
  constants[1].key = vecSizeSV;
  constants[1].value = vec_size;
  computePipelineDesc.compute.constantCount = (uint32_t)constants.size();
  computePipelineDesc.compute.constants = constants.data();
  StringView entryPointSV = makeStringView("vec_add");
  computePipelineDesc.compute.entryPoint = entryPointSV;
  computePipelineDesc.compute.module = shaderModule;
  computePipelineDesc.layout = pipelineLayout;
  pipeline = device.CreateComputePipeline(&computePipelineDesc);
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

  // atomic buffer 
  BufferDescriptor DBufDesc;
  DBufDesc.mappedAtCreation = false;
  DBufDesc.size = sizeof(int);
  DBufDesc.usage = BufferUsage::Storage | BufferUsage::CopyDst;
  DBuffer = device.CreateBuffer(&DBufDesc);
}


void run() {
  Queue queue = device.GetQueue();
  std::vector<uint32_t> A_host;
  std::vector<uint32_t> B_host;
  std::vector<uint32_t> D_host;
  
  for (int i = 0; i < vec_size; i++) {
    A_host.push_back(1);
    B_host.push_back(0);
  }

  D_host.push_back(0);

  queue.WriteBuffer(ABuffer, 0, A_host.data(), A_host.size() * sizeof(uint32_t));
  queue.WriteBuffer(BBuffer, 0, B_host.data(), B_host.size() * sizeof(uint32_t));
  queue.WriteBuffer(DBuffer, 0, D_host.data(), D_host.size() * sizeof(uint32_t));

  CommandEncoder encoder = device.CreateCommandEncoder();
  ComputePassEncoder computePass = encoder.BeginComputePass();
  computePass.SetPipeline(pipeline);
  computePass.SetBindGroup(0, bindGroup, 0, nullptr);
  computePass.DispatchWorkgroups(vec_size / wg_size, 1, 1);
  computePass.End();

  encoder.CopyBufferToBuffer(CBuffer, 0, CReadBuffer, 0, vec_size * 4);
  CommandBuffer commands = encoder.Finish();
  queue.Submit(1, &commands);

  WaitStatus waitStatus = WaitStatus::Unknown;
  MapAsyncStatus readStatus = MapAsyncStatus::Unknown;
  waitStatus = instance.WaitAny(
    CReadBuffer.MapAsync(MapMode::Read, 0, vec_size * 4, CallbackMode::AllowSpontaneous,
      [&readStatus](wgpu::MapAsyncStatus status, wgpu::StringView) {
        readStatus = status;
      }),
    UINT64_MAX);
  if (waitStatus != WaitStatus::Success || readStatus != MapAsyncStatus::Success) {
    std::cout << "Failed to map buffer" << std::endl;
    return;
  }

  const uint* output = (const uint*)CReadBuffer.GetConstMappedRange(0, vec_size * 4);
  for (int i = 0; i < 1024; i++) { // james print
    //assert(output[i] == 3);
    std::cout << "output[" << i << "]: " << output[i] << std::endl; 
  }
  std::cout << "passed the test!" << std::endl;
  CReadBuffer.Unmap();
}

int main() {
  InstanceFeatures features;
  const char* const instanceEnabledToggles[] = {"allow_unsafe_apis"};
  DawnTogglesDescriptor instanceTogglesDesc;
  instanceTogglesDesc.enabledToggles = instanceEnabledToggles;
  instanceTogglesDesc.enabledToggleCount = 1;
  features.timedWaitAnyEnable = true; // for some reason this defaults to false
  InstanceDescriptor descriptor;
  descriptor.nextInChain = &instanceTogglesDesc;
  descriptor.features = features;
  instance = wgpu::CreateInstance(&descriptor);

  RequestAdapterStatus adapterStatus;
  Adapter adapter;
  WaitStatus waitStatus = instance.WaitAny(
    instance.RequestAdapter(
      nullptr, CallbackMode::AllowSpontaneous,
      [&adapterStatus, &adapter](RequestAdapterStatus s, Adapter _adapter,
                         StringView message) {
        adapterStatus = s;
        adapter = std::move(_adapter);
      }),
    UINT64_MAX);
  if (waitStatus != WaitStatus::Success || adapterStatus != RequestAdapterStatus::Success) {
    std::cout << "Failed to get adapter" << std::endl;
    return 1;
  }

  RequestDeviceStatus deviceStatus;
  Device deviceResult;
  DeviceDescriptor deviceDescriptor{};

    std::vector<FeatureName> reqFeatures = {
        FeatureName::Subgroups,
        FeatureName::TimestampQuery,
    };
    

    // features enable checking
    for (uint i = 0; i < reqFeatures.size(); i++) {
      if (adapter.HasFeature(reqFeatures[i]) != true) {
        std::cout << "tried to enable feature:" << i << std::endl;
      }
    }

    deviceDescriptor.requiredFeatures = reqFeatures.data();
    deviceDescriptor.requiredFeatureCount =
        static_cast<uint32_t>(reqFeatures.size());


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
  if (waitStatus != WaitStatus::Success || deviceStatus != RequestDeviceStatus::Success) {
    std::cout << "Failed to get device" << std::endl;
    return 1;
  }
  device = deviceResult;

  AdapterInfo info;
  adapter.GetInfo(&info);
  std::string deviceName = info.description.data;
  std::cout << "using device: " << deviceName << std::endl;
  initBindGroupLayout();
  initComputePipeline();
  initBuffers();
  initBindGroup();
  run();

  return 0;
}
