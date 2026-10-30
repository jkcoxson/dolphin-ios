// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>

#include "InputCommon/ControllerInterface/iOS/NintendoBLEDevice.h"
#include "InputCommon/ControllerInterface/iOS/NintendoBLESlot.h"

using ciface::iOS::NintendoBLESlot;
using namespace std::chrono_literals;

TEST(NintendoBLEDevice, ListedSlotOnlyReportsConnectedWhileOccupied)
{
  class SlotDevices : public ciface::Core::DeviceContainer
  {
  public:
    explicit SlotDevices(const std::shared_ptr<ciface::Core::Device>& device)
    {
      m_devices.push_back(device);
    }
  };
  auto slot = std::make_shared<NintendoBLESlot>(0);
  auto device = std::make_shared<ciface::iOS::GameCubeBLE>(slot);
  SlotDevices devices(device);
  ciface::Core::DeviceQualifier qualifier;
  qualifier.FromDevice(device.get());
  // This is the same presence query used by EmulatedController::UpdateReferences.
  EXPECT_EQ(devices.FindDevice(qualifier), device);
  EXPECT_FALSE(devices.HasConnectedDevice(qualifier));
  ASSERT_TRUE(slot->Connect(10));
  EXPECT_TRUE(devices.HasConnectedDevice(qualifier));
  slot->Disconnect(10);
  EXPECT_FALSE(devices.HasConnectedDevice(qualifier));
  EXPECT_EQ(devices.FindDevice(qualifier), device);
  ASSERT_TRUE(slot->Connect(20));
  EXPECT_TRUE(devices.HasConnectedDevice(qualifier));
  slot->Disconnect(10);  // A delayed disconnect must not unplug the replacement.
  EXPECT_TRUE(devices.HasConnectedDevice(qualifier));
  slot->Disconnect(20);
  EXPECT_FALSE(devices.HasConnectedDevice(qualifier));
}

TEST(NintendoBLEDevice, ViewerReadsLiveInputWithoutEmulationPolling)
{
  auto slot = std::make_shared<NintendoBLESlot>(0);
  ciface::iOS::GameCubeBLE device(slot);
  auto* button = device.FindInput("Button A");
  auto* stick = device.FindInput("L Stick X+");
  auto* opposite = device.FindInput("L Stick X-");
  auto* trigger = device.FindInput("R Trigger");
  ASSERT_NE(button, nullptr);
  ASSERT_NE(stick, nullptr);
  ASSERT_NE(opposite, nullptr);
  ASSERT_NE(trigger, nullptr);
  EXPECT_EQ(button->GetState(), 0);
  ASSERT_TRUE(slot->Connect(10));
  DOLGameCubeState input{};
  input.buttons = DOLGC_A;
  input.stick_x = 0.75;
  input.trigger_r = 0.5;
  slot->Update(10, input);
  // The iOS viewer and mapping detector read controls without UpdateInput().
  EXPECT_EQ(button->GetState(), 1);
  EXPECT_EQ(stick->GetState(), 0.75);
  EXPECT_EQ(opposite->GetState(), -0.75);
  EXPECT_EQ(trigger->GetState(), 0.5);
  slot->Update(10, {});
  EXPECT_EQ(button->GetState(), 0);
  EXPECT_EQ(stick->GetState(), 0);
  EXPECT_EQ(trigger->GetState(), 0);
}

TEST(NintendoBLEDevice, ExistingMappingFollowsReplacementAndNeutralizesImmediately)
{
  auto slot = std::make_shared<NintendoBLESlot>(0);
  ciface::iOS::GameCubeBLE device(slot);
  auto* button = device.FindInput("Button B");
  ASSERT_NE(button, nullptr);
  ASSERT_TRUE(slot->Connect(10));
  DOLGameCubeState input{};
  input.buttons = DOLGC_B;
  slot->Update(10, input);
  device.UpdateInput();  // Also exercise the normal in-game poll path.
  EXPECT_EQ(button->GetState(), 1);
  slot->Disconnect(10);
  EXPECT_EQ(button->GetState(), 0);
  ASSERT_TRUE(slot->Connect(20));
  slot->Update(20, input);
  EXPECT_EQ(button->GetState(), 1);
  slot->Update(10, {});
  EXPECT_EQ(button->GetState(), 1);
  slot->Update(20, input, NintendoBLESlot::Clock::now() - 1s);
  EXPECT_EQ(button->GetState(), 0);
}

TEST(NintendoBLESlot, NameAndMappingIdentitySurviveReplacement)
{
  NintendoBLESlot slot(0);
  EXPECT_EQ(slot.GetName(), "NSO GC Controller 1");
  EXPECT_EQ(slot.GetInput().buttons, 0u);
  ASSERT_TRUE(slot.Connect(10));
  slot.Disconnect(10);
  ASSERT_TRUE(slot.Connect(20));
  EXPECT_EQ(slot.GetName(), "NSO GC Controller 1");
  EXPECT_EQ(slot.GetInput().buttons, 0u);
}

TEST(NintendoBLESlot, ReplacingOneSlotDoesNotMoveOtherPlayers)
{
  NintendoBLESlot first(0), second(1);
  const auto now = NintendoBLESlot::Clock::now();
  ASSERT_TRUE(first.Connect(10));
  ASSERT_TRUE(second.Connect(20));
  DOLGameCubeState input{};
  input.buttons = DOLGC_A;
  second.Update(20, input, now);
  first.Disconnect(10);
  ASSERT_TRUE(first.Connect(30));
  EXPECT_TRUE(second.Matches(20));
  EXPECT_EQ(second.GetName(), "NSO GC Controller 2");
  EXPECT_EQ(second.GetInput(now).buttons, DOLGC_A);
}

TEST(NintendoBLESlot, OccupiedSlotCannotBeStolen)
{
  NintendoBLESlot slot(0);
  EXPECT_FALSE(slot.Connect(0));
  ASSERT_TRUE(slot.Connect(10));
  EXPECT_FALSE(slot.Connect(20));
  EXPECT_TRUE(slot.Matches(10));
  EXPECT_FALSE(slot.Matches(20));
  EXPECT_FALSE(slot.Matches(0));
}

TEST(NintendoBLESlot, LateCallbacksCannotAffectNewOccupant)
{
  NintendoBLESlot slot(0);
  const auto now = NintendoBLESlot::Clock::now();
  ASSERT_TRUE(slot.Connect(10));
  slot.Disconnect(10);
  ASSERT_TRUE(slot.Connect(20));
  DOLGameCubeState current{};
  current.buttons = DOLGC_B;
  current.trigger_r = 0.5;
  slot.Update(20, current, now);
  DOLGameCubeState late{};
  late.buttons = DOLGC_A;
  slot.Update(10, late, now);
  slot.Disconnect(10);
  EXPECT_TRUE(slot.Matches(20));
  EXPECT_EQ(slot.GetInput(now).buttons, DOLGC_B);
  EXPECT_EQ(slot.GetInput(now).trigger_r, 0.5);
  slot.SetRumble(true);
  EXPECT_FALSE(slot.GetRumble(10, now));
  EXPECT_TRUE(slot.GetRumble(20, now));
}

TEST(NintendoBLESlot, DisconnectNeutralizesEveryControlAndRumble)
{
  NintendoBLESlot slot(0);
  const auto now = NintendoBLESlot::Clock::now();
  ASSERT_TRUE(slot.Connect(10));
  DOLGameCubeState input{DOLGC_L | DOLGC_R, 1, -1, 0.5, -0.5, 0.75, 1};
  slot.Update(10, input, now);
  slot.SetRumble(true);
  ASSERT_TRUE(slot.GetRumble(10, now));
  slot.Disconnect(10);
  const auto neutral = slot.GetInput(now);
  EXPECT_EQ(neutral.buttons, 0u);
  EXPECT_EQ(neutral.stick_x, 0);
  EXPECT_EQ(neutral.stick_y, 0);
  EXPECT_EQ(neutral.cstick_x, 0);
  EXPECT_EQ(neutral.cstick_y, 0);
  EXPECT_EQ(neutral.trigger_l, 0);
  EXPECT_EQ(neutral.trigger_r, 0);
  slot.SetRumble(true);  // An empty slot must not carry rumble to its next occupant.
  ASSERT_TRUE(slot.Connect(20));
  slot.Update(20, input, now);
  EXPECT_FALSE(slot.GetRumble(20, now));
}

TEST(NintendoBLESlot, StaleInputIsNeutralWithoutLosingTheSlot)
{
  NintendoBLESlot slot(3);
  const auto now = NintendoBLESlot::Clock::now();
  ASSERT_TRUE(slot.Connect(10));
  DOLGameCubeState input{};
  input.buttons = DOLGC_A;
  slot.Update(10, input, now);
  slot.SetRumble(true);
  EXPECT_EQ(slot.GetInput(now + 499ms).buttons, DOLGC_A);
  EXPECT_EQ(slot.GetInput(now + 500ms).buttons, 0u);
  EXPECT_FALSE(slot.GetRumble(10, now + 500ms));
  EXPECT_TRUE(slot.Matches(10));
  EXPECT_EQ(slot.GetName(), "NSO GC Controller 4");
  slot.Update(10, input, now + 600ms);
  EXPECT_EQ(slot.GetInput(now + 600ms).buttons, DOLGC_A);
}
