// One background thread for the network work of the settings page and the update check (images, GitHub
// releases), so the presence worker is never held up by it. It sleeps while there is nothing to do.
#pragma once
#include <functional>
#include <string>

namespace jobs {

void Start();
void Stop();                                   // call web::Abort() first: running downloads then end at once
void Post(std::function<void()> job);

// Small images for the settings page: downloaded once, kept in config::CacheDir() for maxAgeDays.
void FetchImage(const std::string& key, const std::wstring& url, const std::wstring& cacheName, int maxAgeDays);
void FetchDiscordAssets(const std::wstring& appId);          // keys "asset:aimp", "asset:play", "asset:pause"
bool TakeImage(const std::string& key, std::string& bytes);  // new data for 'key' (once); empty bytes = failed
std::wstring AppName(const std::wstring& appId);             // name of an own Discord application ("" = unknown)
void ForgetImages();                                         // settings page closed: free the memory

}  // namespace jobs
