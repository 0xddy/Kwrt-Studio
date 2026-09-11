#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <atomic>
#include <stdexcept>
#include <cstdint>
#include "json.hpp"

namespace ax {
namespace fs = std::filesystem;
using Bytes = std::string;
using Json = nlohmann::json;
struct Error : std::runtime_error { using std::runtime_error::runtime_error; };
void need(bool condition, const std::string& message);
std::wstring wide(const std::string& value);
std::string utf8(const std::wstring& value);
Bytes read(const fs::path& path, size_t limit = 600ull*1024*1024);
void write(const fs::path& path, const Bytes& value);
void writeNew(const fs::path& path, const Bytes& value);
uint32_t be32(const Bytes& b, size_t p);
uint32_t le32(const Bytes& b, size_t p);
uint64_t le64(const Bytes& b, size_t p);
void put32(Bytes& b, size_t p, uint32_t value);
Bytes cells(std::initializer_list<uint32_t> values);
std::string hex(const Bytes& bytes);
Bytes hash(const Bytes& bytes, const wchar_t* algorithm = BCRYPT_SHA256_ALGORITHM);
std::string sha(const Bytes& bytes);
std::string shaFile(const fs::path& path);
uint32_t crc(const Bytes& bytes, bool complement = true);
std::string crypt512(const std::string& password, const std::string& salt);
std::string randomToken();
Json readJson(const fs::path& path);
void validateProfile(const Json& profile);
Bytes renderInit(const Json& profile, Bytes initTemplate);

struct Fdt {
    struct Node {
        std::string path;
        std::vector<std::pair<std::string,Bytes>> props;
    };
    std::vector<Node> nodes;
    Bytes reservations;
    uint32_t bootCpu = 0;
    static Fdt parse(const Bytes& blob);
    Bytes encode() const;
    bool has(const std::string& path) const;
    Bytes get(const std::string& path, const std::string& prop) const;
    void set(const std::string& path, const std::string& prop, const Bytes& value);
    void add(const std::string& path);
    std::map<std::string,std::map<std::string,Bytes>> map() const;
};

struct Entry {
    std::string name, link;
    char type = '0';
    uint32_t mode = 0644, uid = 0, gid = 0, major = 0, minor = 0;
    uint64_t mtime = 0;
    Bytes data;
};
using Archive = std::map<std::string,Entry>;
Archive untar(const Bytes& tar);
Bytes tar(const Archive& files, size_t padBlock = 512);
Json manifest(const Archive& files);
struct Firmware {Json metadata; Archive files; std::string archiveRoot;};
Json firmwareMetadata(const Bytes& bytes);
Firmware unpackFirmware(const Bytes& bytes);
Bytes appendMetadata(Bytes bytes, const Json& metadata);
Entry& regular(Archive& files, const std::string& path);
Json packageList(Archive& files);
std::vector<std::string> applyFirmwarePresentation(Archive& files,const Json& profile);
Json detectNetworkDefaults(const Archive& files);

struct Request {
    fs::path input, outputDirectory, appDirectory;
    Json profile;
    unsigned minimumDataMiB = 16;
};
struct Progress {int percent; std::string text;};
using Notify = std::function<void(const Progress&)>;
struct Result {fs::path firmware; Json report;};
Result convert(const Request& request, const Notify& notify, const std::atomic_bool& cancel);
Json parseInstalledPackages(const Bytes& database, const std::string& manager);
Json readFirmwarePackages(const fs::path& input, const fs::path& appDirectory, const Notify& notify, const std::atomic_bool& cancel);
Json selfTest();
}
