#pragma once
#include "core.hpp"

namespace ax {
struct Gateway { std::string address, adapter; uint64_t metric=0; };
std::vector<Gateway> localGateways();
struct SshOptions {
    std::string address, username="root", password, fingerprint;
    uint16_t port=22;
};
struct HostKeyRequired : Error {
    std::string fingerprint;
    bool changed;
    HostKeyRequired(std::string value,bool mismatch):Error("SSH 设备身份需要确认"),fingerprint(std::move(value)),changed(mismatch){}
};
// Always runs the fixed read-only probe; no caller-provided remote command.
Bytes queryRouter(const SshOptions& options,const std::atomic_bool& cancel);
}
