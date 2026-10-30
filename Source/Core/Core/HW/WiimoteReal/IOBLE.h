// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
// Handles identify a connection generation, never a reusable player slot.
uint64_t DOLWiiBLEAdd(uint8_t player);
void DOLWiiBLERemove(uint64_t handle);
bool DOLWiiBLEHealthy(uint64_t handle);
void DOLWiiBLEReceive(uint64_t handle, const uint8_t* data, size_t size);
size_t DOLWiiBLEPeekOutput(uint64_t handle, uint8_t* data, size_t capacity, uint64_t* token);
void DOLWiiBLECompleteOutput(uint64_t handle, uint64_t token, bool success);
#ifdef __cplusplus
}

#ifdef IPHONEOS
#include "Core/HW/WiimoteReal/WiimoteReal.h"
namespace WiimoteReal
{
class WiimoteScannerBLE final : public WiimoteScannerBackend
{
public:
  bool IsReady() const override { return true; }
  void FindWiimotes(std::vector<Wiimote*>&, Wiimote*&) override;
  void Update() override {}
  void RequestStopSearching() override {}
};
}
#endif
#endif
