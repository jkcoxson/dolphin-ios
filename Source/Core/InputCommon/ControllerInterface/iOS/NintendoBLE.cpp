// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "InputCommon/ControllerInterface/iOS/NintendoBLE.h"

#include <array>
#include <atomic>

#include "InputCommon/ControllerInterface/ControllerInterface.h"
#include "InputCommon/ControllerInterface/iOS/NintendoBLEDevice.h"

namespace
{
using ControllerState = ciface::iOS::NintendoBLESlot;
using ciface::iOS::GameCubeBLE;
const std::array s_slots{std::make_shared<ControllerState>(0),
                         std::make_shared<ControllerState>(1),
                         std::make_shared<ControllerState>(2),
                         std::make_shared<ControllerState>(3)};
std::atomic<uint64_t> s_next_handle{0};

std::shared_ptr<ControllerState> Find(uint64_t handle)
{
  for (const auto& state : s_slots)
    if (state->Matches(handle))
      return state;
  return nullptr;
}

// Called under ControllerInterface's population lock. Slots exist independently
// of Bluetooth discovery and never change identity when an occupant leaves.
void AddControllerDevice(const std::shared_ptr<ControllerState>& state)
{
  for (const auto& device : g_controller_interface.GetAllDevices())
  {
    const auto* gc = dynamic_cast<const GameCubeBLE*>(device.get());
    if (gc && gc->Matches(state))
      return;
  }
  g_controller_interface.AddDevice(std::make_shared<GameCubeBLE>(state));
}
}

uint64_t DOLGameCubeBLEAdd(uint8_t player)
{
  if (player >= s_slots.size())
    return 0;
  const auto handle = ++s_next_handle;
  if (!s_slots[player]->Connect(handle))
    return 0;
  // EmulatedController caches default-device presence in UpdateReferences().
  // Occupancy changes are hotplug events even though the slot stays listed.
  // Notify only after releasing the slot lock: callbacks query IsValid().
  g_controller_interface.InvokeDevicesChangedCallbacks();
  return handle;
}

void DOLGameCubeBLERemove(uint64_t handle)
{
  if (const auto state = Find(handle))
  {
    state->Disconnect(handle);
    g_controller_interface.InvokeDevicesChangedCallbacks();
  }
}

void DOLGameCubeBLEUpdate(uint64_t handle, DOLGameCubeState input)
{
  if (const auto state = Find(handle))
    state->Update(handle, input);
}

bool DOLGameCubeBLERumble(uint64_t handle)
{
  const auto state = Find(handle);
  return state && state->GetRumble(handle);
}

void ciface::iOS::PopulateNintendoBLEDevices()
{
  for (const auto& state : s_slots)
    AddControllerDevice(state);
}
