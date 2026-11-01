#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace fvb {
namespace subprocess {

// Running another program (ffmpeg, for the desktop audio). Mac and Linux only.
// Programs are found on the PATH and given their arguments directly, with no
// shell in between, so file names with spaces or quotes are safe.

// Runs a program and hands its output over in pieces as it arrives. Return false
// from onOutput to stop the program early. Returns its exit code, or -1 if it
// couldn't be started or was stopped early.
int runReadingOutput(const std::vector<std::string>& args,
                     const std::function<bool(const char* data, size_t length)>& onOutput);

// Runs a program, sends it `input`, and waits for it to finish. Returns its exit
// code, or -1 if it couldn't be started.
int runWithInput(const std::vector<std::string>& args, const char* input, size_t length);

// True if the program can be run (tried with `-version`, which ffmpeg understands).
bool isAvailable(const std::string& program);

}  // namespace subprocess
}  // namespace fvb
