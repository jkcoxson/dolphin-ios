// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>
#include <mutex>
#include <string>

#include "InputCommon/ControllerInterface/iOS/NintendoBLE.h"

namespace ciface::iOS
{
// A mapping targets a permanent slot. Handles belong to individual BLE sessions,
// so delayed input, disconnects, and output cannot affect the next occupant.
class NintendoBLESlot
{
public:
  using Clock = std::chrono::steady_clock;
  explicit NintendoBLESlot(unsigned int index) : m_index(index) {}
  std::string GetName() const { return "NSO GC Controller " + std::to_string(m_index + 1); }

  bool Connect(uint64_t handle)
  {
    std::lock_guard lock(m_mutex);
    if (!handle || m_handle)
      return false;
    m_handle = handle;
    m_input = {};
    m_updated = {};
    m_rumble = false;
    return true;
  }
  bool Matches(uint64_t handle) const
  {
    std::lock_guard lock(m_mutex);
    return handle && handle == m_handle;
  }
  bool IsConnected() const
  {
    std::lock_guard lock(m_mutex);
    return m_handle != 0;
  }
  void Disconnect(uint64_t handle)
  {
    std::lock_guard lock(m_mutex);
    if (!handle || handle != m_handle)
      return;
    m_handle = 0;
    m_input = {};
    m_rumble = false;
  }
  void Update(uint64_t handle, DOLGameCubeState input, Clock::time_point now = Clock::now())
  {
    std::lock_guard lock(m_mutex);
    if (!handle || handle != m_handle)
      return;
    m_input = input;
    m_updated = now;
  }
  DOLGameCubeState GetInput(Clock::time_point now = Clock::now()) const
  {
    std::lock_guard lock(m_mutex);
    return m_handle && now - m_updated < std::chrono::milliseconds(500) ? m_input : DOLGameCubeState{};
  }
  void SetRumble(bool active)
  {
    std::lock_guard lock(m_mutex);
    m_rumble = m_handle && active;
  }
  bool GetRumble(uint64_t handle, Clock::time_point now = Clock::now()) const
  {
    std::lock_guard lock(m_mutex);
    return handle && handle == m_handle && m_rumble &&
           now - m_updated < std::chrono::milliseconds(500);
  }

private:
  const unsigned int m_index;
  mutable std::mutex m_mutex;
  uint64_t m_handle = 0;
  DOLGameCubeState m_input{};
  Clock::time_point m_updated{};
  bool m_rumble = false;
};
}
