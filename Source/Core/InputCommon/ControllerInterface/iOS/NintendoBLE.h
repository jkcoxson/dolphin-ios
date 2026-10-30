// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
// Bit order matches the NSO GameCube profile, not Apple's positional ABXY mapping.
enum DOLGameCubeButton
{
  DOLGC_A = 1 << 0, DOLGC_B = 1 << 1, DOLGC_X = 1 << 2, DOLGC_Y = 1 << 3,
  DOLGC_L = 1 << 4, DOLGC_R = 1 << 5, DOLGC_Z = 1 << 6, DOLGC_ZL = 1 << 7,
  DOLGC_START = 1 << 8, DOLGC_HOME = 1 << 9, DOLGC_CAPTURE = 1 << 10,
  DOLGC_UP = 1 << 11, DOLGC_DOWN = 1 << 12, DOLGC_LEFT = 1 << 13, DOLGC_RIGHT = 1 << 14
};
typedef struct DOLGameCubeState
{
  uint32_t buttons;
  double stick_x, stick_y, cstick_x, cstick_y;
  double trigger_l, trigger_r;
} DOLGameCubeState;

// Attach a session to one of four permanent, zero-based player slots.
// Returns zero if the slot is occupied or invalid. Removal only empties the slot.
uint64_t DOLGameCubeBLEAdd(uint8_t player);
void DOLGameCubeBLERemove(uint64_t handle);
void DOLGameCubeBLEUpdate(uint64_t handle, DOLGameCubeState state);
bool DOLGameCubeBLERumble(uint64_t handle);
#ifdef __cplusplus
}
namespace ciface::iOS
{
void PopulateNintendoBLEDevices();
}
#endif
