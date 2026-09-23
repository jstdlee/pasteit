#pragma once
#include "platform/platform_services.hpp"
#include <cstdint>
namespace pastit {
class LinuxRecentPathCollector{
public:
 std::vector<PlatformRecentPath> scan_process(std::uint32_t pid)const;
 std::vector<PlatformRecentPath> scan_file_manager_services()const;
 std::vector<PlatformRecentPath> scan_nautilus_service()const;
 std::vector<PlatformRecentPath> scan_shell_history()const;
};
}
