#pragma once
#include "core.hpp"
#include <memory>

namespace ax {
// The common engine knows archive formats, hashes and profiles. Every fact
// about flash geometry, board aliases, boot code and ports belongs here.
struct AdapterInfo {
    std::string id, device, sourceLayout, targetLayout, outputBoard;
    std::vector<std::string> distributions;
};
struct KernelPatch { Bytes bytes; std::string executableSha256; };
struct RootPatch { std::vector<std::string> changed, added; };
class DeviceAdapter {
public:
    virtual ~DeviceAdapter() = default;
    virtual AdapterInfo info() const = 0;
    virtual bool matches(const Json& metadata) const = 0;
    virtual void validateInput(const Firmware& firmware) const = 0;
    virtual KernelPatch transformKernel(const Bytes& kernel) const = 0;
    virtual void verifyOutputKernel(const Bytes& kernel) const = 0;
    virtual RootPatch transformFilesystem(Archive& files, const Json& profile, uint32_t epoch) const = 0;
    virtual uint64_t availableDataBytes(uint64_t kernelBytes, uint64_t rootBytes) const = 0;
    virtual Json outputMetadata(Json original) const = 0;
    virtual Json partitionDescription() const = 0;
    // Optional advisory only; never participates in conversion eligibility.
    virtual Json routerAdvice(const Json&) const { return nullptr; }
};
const std::vector<std::unique_ptr<DeviceAdapter>>& deviceAdapters();
const DeviceAdapter* findAdapter(const Json& metadata);
Json inspectFirmware(const fs::path& path);
Json adapterCatalog();
std::unique_ptr<DeviceAdapter> makeRedmiAx6000Adapter();
}
