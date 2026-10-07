// Update check: asks GitHub for the newest release, downloads its .aimppack (SHA-256 checked) and opens it with
// AIMP, which installs the plugin update; once the new plugin file is in place, AIMP is restarted to load it.
// Runs on the jobs thread.
#pragma once
#include <cstdint>
#include <string>

namespace update {

enum class Phase { Idle, Checking, UpToDate, Available, Downloading, Opened, Restarting, Installed, Failed };

struct State {
    Phase        phase = Phase::Idle;
    std::wstring version;   // newest version found (Available / Downloading / Opened / Restarting / Installed)
    std::wstring detail;    // Failed: reason; Opened: path of the downloaded package
};

State Get();
void Check();     // "Check now" (async)
void Install();   // "Install" (async): download the newest release and open it in AIMP
void Tick();      // jobs thread: automatic check when it is due (see Config::updateFrequency)
bool TakeNotification(std::wstring& text);   // main thread: text for AIMP's display, once per new version
void SetPluginsDir(const std::wstring& dir);  // main thread, at start: AIMP's plugin folder
uint64_t WatchInstall();   // jobs thread: has AIMP installed the opened package? ms until the next look, 0 = done
bool TakeRestart();        // main thread: true once when AIMP has to be restarted to load the installed update
void RestartFailed();      // main thread: AIMP could not be restarted - the user is asked to do it
void RestartingFor();      // main thread, right before the restart: remembered, so the popup can say why
void StartupNotice();      // jobs thread, a few seconds after the start: new version installed -> popup
bool TakePopup(std::wstring& title, std::wstring& text);   // main thread: the "update installed" window, once
void PopupFailed();        // main thread: no message window available -> notice in AIMP's display

}  // namespace update
