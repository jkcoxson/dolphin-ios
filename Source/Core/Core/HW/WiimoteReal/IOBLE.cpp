// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/WiimoteReal/IOBLE.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "Core/HW/WiimoteReal/BLEReportQueue.h"
#include "Core/HW/WiimoteCommon/WiimoteReport.h"

namespace
{
struct Remote
{
  uint64_t handle;
  uint8_t player;
  std::atomic<bool> active{false};
  std::atomic<bool> power_off_requested{false};
  WiimoteReal::BLEReportQueue reports;
};
std::mutex s_mutex;
std::map<uint64_t, std::shared_ptr<Remote>> s_remotes;
uint64_t s_next_handle = 0;

std::shared_ptr<Remote> Find(uint64_t handle)
{
  std::lock_guard lock(s_mutex);
  const auto it = s_remotes.find(handle);
  return it == s_remotes.end() ? nullptr : it->second;
}

class WiimoteBLE final : public WiimoteReal::Wiimote
{
public:
  explicit WiimoteBLE(std::shared_ptr<Remote> remote) : m_remote(std::move(remote)) {}
  ~WiimoteBLE() override { Shutdown(); }
  std::string GetId() const override { return "BLE/" + std::to_string(m_remote->handle); }
  std::optional<unsigned int> GetPreferredSlot() const override { return m_remote->player; }
  bool IsConnected() const override { return m_connected && m_remote->reports.IsConnected(); }
  void EventLinked() override
  {
    WiimoteReal::Wiimote::EventLinked();
    m_status_after_hid_link_requested = false;
  }
  void PrepareInput(WiimoteEmu::DesiredWiimoteState* state, SensorBarState sensor_bar_state) override
  {
    // This callback begins only when the emulated HID interrupt channel is ready.
    // An earlier transport status can be lost before that channel exists.
    if (!m_status_after_hid_link_requested)
    {
      m_status_after_hid_link_requested = true;
      WiimoteCommon::OutputReportRequestStatus status{};
      QueueReport(status);
    }
    WiimoteReal::Wiimote::PrepareInput(state, sensor_bar_state);
  }
  void EventGuestDisconnect() override { m_remote->power_off_requested = true; }

private:
  bool ConnectInternal() override
  {
    m_remote->reports.Reset();
    m_connected = m_remote->reports.IsConnected();
    m_remote->active = m_connected.load();
    return m_connected;
  }
  void DisconnectInternal() override
  {
    m_remote->active = false;
    m_connected = false;
    m_remote->reports.Reset();
  }
  int IORead(u8* data) override { return m_remote->reports.Read(data); }
  int IOWrite(const u8* data, size_t size) override
  {
    return m_remote->reports.Write({data, size}) ? static_cast<int>(size) : 0;
  }
  void IOWakeup() override { m_remote->reports.Wakeup(); }
  std::shared_ptr<Remote> m_remote;
  std::atomic<bool> m_connected{false};
  bool m_status_after_hid_link_requested = false;
};
}

uint64_t DOLWiiBLEAdd(uint8_t player)
{
  if (player >= 4)
    return 0;
  std::lock_guard lock(s_mutex);
  auto remote = std::make_shared<Remote>();
  remote->handle = ++s_next_handle;
  remote->player = player;
  s_remotes.emplace(remote->handle, remote);
  return remote->handle;
}

void DOLWiiBLERemove(uint64_t handle)
{
  std::lock_guard lock(s_mutex);
  const auto it = s_remotes.find(handle);
  if (it == s_remotes.end())
    return;
  it->second->reports.Close();
  s_remotes.erase(it);
}

bool DOLWiiBLEHealthy(uint64_t handle)
{
  const auto remote = Find(handle);
  return remote && remote->reports.IsConnected();
}

bool DOLWiiBLETakePowerOffRequest(uint64_t handle)
{
  const auto remote = Find(handle);
  return remote && remote->power_off_requested.exchange(false);
}

void DOLWiiBLEReceive(uint64_t handle, const uint8_t* data, size_t size)
{
  // A paired remote can remain awake while no Dolphin port uses it. Do not
  // accumulate reports until a native consumer has opened this transport.
  if (const auto remote = Find(handle); remote && remote->active && data)
    remote->reports.PushInput({data, size});
}

size_t DOLWiiBLEPeekOutput(uint64_t handle, uint8_t* data, size_t capacity, uint64_t* token)
{
  if (const auto remote = Find(handle); remote && data && token)
    return remote->reports.PeekOutput({data, capacity}, token);
  return 0;
}

void DOLWiiBLECompleteOutput(uint64_t handle, uint64_t token, bool success)
{
  if (const auto remote = Find(handle))
    remote->reports.CompleteOutput(token, success);
}

void WiimoteReal::WiimoteScannerBLE::FindWiimotes(std::vector<Wiimote*>& found, Wiimote*&)
{
  // Do not hold the registry lock while taking Dolphin's known-ID lock.
  std::vector<std::shared_ptr<Remote>> remotes;
  {
    std::lock_guard lock(s_mutex);
    for (const auto& [handle, remote] : s_remotes)
      remotes.push_back(remote);
  }
  for (const auto& remote : remotes)
  {
    if (remote->reports.IsConnected() && IsNewWiimote("BLE/" + std::to_string(remote->handle)))
      found.push_back(new WiimoteBLE(remote));
  }
}
