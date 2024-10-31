#include <filesystem>
#include <fstream>
#include <webgpu/webgpu.hpp>

using namespace wgpu;

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

  ShaderModuleWGSLDescriptor shaderCodeDesc{};
  shaderCodeDesc.chain.next = nullptr;
  shaderCodeDesc.chain.sType = SType::ShaderSourceWGSL;
  ShaderModuleDescriptor shaderDesc{};
  shaderDesc.nextInChain = &shaderCodeDesc.chain;
  StringView code;
  code.data = shaderSource.c_str();
  code.length = shaderSource.length();
  shaderCodeDesc.code = code;
  //std::cout << shaderSource.data() << std::endl;
  //std::cout << "try this!\n";
  return device.createShaderModule(shaderDesc);
}

std::unique_ptr<wgpu::ErrorCallback> errorCallback(Device device) {
  return device.setUncapturedErrorCallback([](ErrorType type, StringView message) {
    std::cout << "Device error: type " << type;
    if (message.length > 0) std::cout << " (message: " << message.data << ")";
    std::cout << std::endl;
    });
}

void deviceLostCallback(WGPUDeviceLostReason reason, WGPUStringView message, void* /* pUserData*/) {
  std::cout << "Device lost: reason " << reason;
  if (message.length > 0) std::cout << " (message: " << message.data << ")";
  std::cout << std::endl;
};
