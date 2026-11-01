#pragma once

#include <string>
#include <vector>

namespace fvb {

// The version of this firmware. Change it by hand in the pull request that gets
// a release ready: the release workflow (.github/workflows/release.yml) reads it
// from this line and refuses to publish a version that already exists. Keep the
// line in exactly this form so the workflow can find it.
constexpr const char* kFirmwareVersion = "0.1.0";

// The version the box says it's running: in logs, on the start-up screen, in
// /status and when checking for updates. The same as kFirmwareVersion, except
// on the desktop build after a pretend update (see setPretendFirmwareVersion).
const std::string& firmwareVersion();

// Desktop build only. Installing an update can't really swap the laptop's
// program, so after a pretend install the desktop box acts as if it were
// running `version` instead. An empty string goes back to kFirmwareVersion.
void setPretendFirmwareVersion(const std::string& version);

namespace version {

// Reads "1.3", "1.3.0" or "v1.3.0" into its numbers. False for anything else,
// including empty parts ("1..3"), letters ("1.3-beta") and more than four parts.
bool parse(const std::string& text, std::vector<int>& out);

// True if `candidate` is a later version than `current`. "1.3" and "1.3.0" are
// the same version. False if either can't be read, so a garbled release is
// never offered.
bool isNewer(const std::string& candidate, const std::string& current);

}  // namespace version
}  // namespace fvb
