#include "app/Version.h"

namespace fvb {

namespace {
std::string& pretendVersion() {
    static std::string value;
    return value;
}
}  // namespace

const std::string& firmwareVersion() {
    static const std::string built = kFirmwareVersion;
    const std::string& pretend = pretendVersion();
    return pretend.empty() ? built : pretend;
}

void setPretendFirmwareVersion(const std::string& version) { pretendVersion() = version; }

namespace version {

bool parse(const std::string& text, std::vector<int>& out) {
    out.clear();
    size_t i = 0;
    if (!text.empty() && (text[0] == 'v' || text[0] == 'V')) i = 1;
    if (i >= text.size()) return false;
    while (true) {
        const size_t start = i;
        long part = 0;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            part = part * 10 + (text[i] - '0');
            if (part > 100000) return false;
            ++i;
        }
        if (i == start) return false;  // an empty part, or not a number
        out.push_back(static_cast<int>(part));
        if (out.size() > 4) return false;
        if (i == text.size()) return true;
        if (text[i] != '.') return false;
        ++i;
    }
}

bool isNewer(const std::string& candidate, const std::string& current) {
    std::vector<int> a, b;
    if (!parse(candidate, a) || !parse(current, b)) return false;
    while (a.size() < b.size()) a.push_back(0);
    while (b.size() < a.size()) b.push_back(0);
    return a > b;
}

}  // namespace version
}  // namespace fvb
