// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <memory>
#include <utility>

#include "InputCommon/ControllerInterface/CoreDevice.h"
#include "InputCommon/ControllerInterface/iOS/NintendoBLESlot.h"

namespace ciface::iOS
{
// iOS's input viewer and mapping detector read GetState() without polling
// UpdateInput(). Read the synchronized slot directly, also keeping retained
// mapping references live across device-list refreshes.
class GameCubeBLE final : public ciface::Core::Device
{
  using ControllerState = NintendoBLESlot;
  class Button final : public Input
  {
  public:
    Button(std::shared_ptr<ControllerState> state, uint32_t mask, std::string name)
        : m_state(std::move(state)), m_mask(mask), m_name(std::move(name)) {}
    std::string GetName() const override { return m_name; }
    ControlState GetState() const override { return (m_state->GetInput().buttons & m_mask) != 0; }
  private:
    std::shared_ptr<ControllerState> m_state;
    uint32_t m_mask;
    std::string m_name;
  };
  class Axis final : public Input
  {
  public:
    Axis(std::shared_ptr<ControllerState> state, double DOLGameCubeState::*member,
         double sign, std::string name)
        : m_state(std::move(state)), m_member(member), m_sign(sign), m_name(std::move(name)) {}
    std::string GetName() const override { return m_name; }
    ControlState GetState() const override { return (m_state->GetInput().*m_member) * m_sign; }
  private:
    std::shared_ptr<ControllerState> m_state;
    double DOLGameCubeState::*m_member;
    double m_sign;
    std::string m_name;
  };
  class Rumble final : public Output
  {
  public:
    explicit Rumble(std::shared_ptr<ControllerState> state) : m_state(std::move(state)) {}
    std::string GetName() const override { return "Rumble"; }
    void SetState(ControlState value) override { m_state->SetRumble(value > 0); }
  private:
    std::shared_ptr<ControllerState> m_state;
  };
public:
  explicit GameCubeBLE(std::shared_ptr<ControllerState> state) : m_state(std::move(state))
  {
    constexpr std::array names{"Button A", "Button B", "Button X", "Button Y", "L Click",
                               "R Click", "R Shoulder", "L Shoulder", "Menu", "Home", "Capture",
                               "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right"};
    for (size_t i = 0; i < names.size(); ++i)
      AddInput(new Button(m_state, 1u << i, names[i]));
    AddInput(new Axis(m_state, &DOLGameCubeState::stick_x, 1, "L Stick X+"));
    AddInput(new Axis(m_state, &DOLGameCubeState::stick_x, -1, "L Stick X-"));
    AddInput(new Axis(m_state, &DOLGameCubeState::stick_y, 1, "L Stick Y+"));
    AddInput(new Axis(m_state, &DOLGameCubeState::stick_y, -1, "L Stick Y-"));
    AddInput(new Axis(m_state, &DOLGameCubeState::cstick_x, 1, "R Stick X+"));
    AddInput(new Axis(m_state, &DOLGameCubeState::cstick_x, -1, "R Stick X-"));
    AddInput(new Axis(m_state, &DOLGameCubeState::cstick_y, 1, "R Stick Y+"));
    AddInput(new Axis(m_state, &DOLGameCubeState::cstick_y, -1, "R Stick Y-"));
    AddInput(new Axis(m_state, &DOLGameCubeState::trigger_l, 1, "L Trigger"));
    AddInput(new Axis(m_state, &DOLGameCubeState::trigger_r, 1, "R Trigger"));
    AddOutput(new Rumble(m_state));
  }
  ~GameCubeBLE() override { m_state->SetRumble(false); }
  std::string GetName() const override { return m_state->GetName(); }
  std::string GetSource() const override { return "NintendoBLE"; }
  std::optional<int> GetPreferredId() const override { return 0; }
  // Empty slots remain selectable and keep existing mapping references alive.
  bool IsVirtualDevice() const override { return true; }
  bool IsValid() const override { return m_state->IsConnected(); }
  int GetSortPriority() const override { return -1; }
  bool Matches(const std::shared_ptr<ControllerState>& state) const { return m_state == state; }
private:
  std::shared_ptr<ControllerState> m_state;
};

}
