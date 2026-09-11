#pragma once
#include "core.hpp"

namespace ax {
std::vector<std::pair<std::string,Bytes>> routerProbeCommands();
Json parseRouterProbe(const Bytes& output);
Json adviseRouter(const Json& snapshot, const fs::path& firmware = {});
std::string routerAdviceText(const Json& result);
Json routerCheckTests();
// Encoding is shared by the framed probe and SSH host-key fingerprints.
std::string base64Encode(const Bytes& bytes);
Bytes base64Decode(const std::string& text);
}
