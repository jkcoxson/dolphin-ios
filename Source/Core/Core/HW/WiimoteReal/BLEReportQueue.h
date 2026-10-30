// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <span>
#include <vector>

namespace WiimoteReal
{
// One transport per bridge slot. All accesses, including shutdown and completion,
// are synchronized: CoreBluetooth runs on main, Dolphin I/O on a worker thread.
class BLEReportQueue
{
public:
  bool IsConnected() const
  {
    std::lock_guard lock(m_mutex);
    return m_connected;
  }
  void Close()
  {
    std::lock_guard lock(m_mutex);
    m_connected = false;
    m_input.clear();
    m_output.clear();
    m_ready.notify_all();
  }
  void Wakeup()
  {
    std::lock_guard lock(m_mutex);
    m_wakeup = true;
    m_ready.notify_all();
  }
  void Reset()
  {
    std::lock_guard lock(m_mutex);
    m_input.clear();
    m_output.clear();
    // Never reuse output tokens: an ATT completion may outlive a Dolphin device.
    m_wakeup = true;
    m_ready.notify_all();
  }
  bool PushInput(std::span<const uint8_t> report)
  {
    std::lock_guard lock(m_mutex);
    if (!m_connected || report.empty() || report.size() > 22)
      return false;
    if (m_input.size() >= 256)
    {
      // Losing ACKs or memory replies corrupts the protocol. Fail this remote
      // rather than silently dropping a command reply or replaying stale input.
      m_connected = false;
      m_ready.notify_all();
      return false;
    }
    m_input.emplace_back(report.begin(), report.end());
    m_ready.notify_one();
    return true;
  }
  int Read(uint8_t* data)
  {
    std::unique_lock lock(m_mutex);
    m_ready.wait_for(lock, std::chrono::milliseconds(20),
                     [&] { return !m_connected || m_wakeup || !m_input.empty(); });
    m_wakeup = false;
    if (!m_connected)
      return 0;
    if (m_input.empty())
      return -1;
    data[0] = 0xa1;
    const auto& report = m_input.front();
    std::copy(report.begin(), report.end(), data + 1);
    const int size = static_cast<int>(report.size()) + 1;
    m_input.pop_front();
    return size;
  }
  bool Write(std::span<const uint8_t> report)
  {
    std::lock_guard lock(m_mutex);
    if (!m_connected || report.size() < 2 || report.size() > 23 || report[0] != 0xa2)
      return false;
    if (m_output.size() >= 64)
    {
      m_connected = false;
      m_ready.notify_all();
      return false;
    }
    m_output.push_back({++m_next_token, {report.begin() + 1, report.end()}});
    return true;
  }
  size_t PeekOutput(std::span<uint8_t> data, uint64_t* token)
  {
    std::lock_guard lock(m_mutex);
    if (!m_connected || m_output.empty() || data.size() < m_output.front().data.size())
      return 0;
    const auto& report = m_output.front();
    *token = report.token;
    std::copy(report.data.begin(), report.data.end(), data.begin());
    return report.data.size();
  }
  void CompleteOutput(uint64_t token, bool success)
  {
    std::lock_guard lock(m_mutex);
    if (m_output.empty() || m_output.front().token != token)
      return;
    if (success)
      m_output.pop_front();
    else
    {
      m_connected = false;
      m_ready.notify_all();
    }
  }

private:
  struct Output
  {
    uint64_t token;
    std::vector<uint8_t> data;
  };
  mutable std::mutex m_mutex;
  std::condition_variable m_ready;
  bool m_connected = true;
  bool m_wakeup = false;
  uint64_t m_next_token = 0;
  std::deque<std::vector<uint8_t>> m_input;
  std::deque<Output> m_output;
};
}
