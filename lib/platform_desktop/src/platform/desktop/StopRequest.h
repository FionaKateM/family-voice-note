#pragma once

namespace fvb {

// Ctrl+C for the laptop build.
//
// The first Ctrl+C asks everything to stop: loops check stopRequested(), and
// DesktopNetwork can be told to give up on a request that's waiting for
// Telegram (see DesktopNetwork::setCancelCheck). The program should then finish
// within a second or two. A second Ctrl+C quits straight away, in case
// something is stuck.
void installStopHandler();
bool stopRequested();
// Asks for a stop without Ctrl+C (e.g. when --ticks runs out).
void requestStop();

}  // namespace fvb
