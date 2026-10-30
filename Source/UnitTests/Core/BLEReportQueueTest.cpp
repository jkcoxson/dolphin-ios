// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <future>
#include <thread>

#include <gtest/gtest.h>

#include "Core/HW/WiimoteReal/BLEReportQueue.h"

using WiimoteReal::BLEReportQueue;

TEST(BLEReportQueue, CompleteReportsPreserveHIDFraming)
{
  BLEReportQueue queue;
  const std::array<uint8_t, 4> input{0x30, 0x12, 0x34, 0x56};
  ASSERT_TRUE(queue.PushInput(input));
  std::array<uint8_t, 23> bytes{};
  ASSERT_EQ(queue.Read(bytes.data()), 5);
  EXPECT_EQ(bytes[0], 0xa1);
  EXPECT_TRUE(std::equal(input.begin(), input.end(), bytes.begin() + 1));
  EXPECT_EQ(queue.Read(bytes.data()), -1);

  const std::array<uint8_t, 3> output{0xa2, 0x11, 0x10};
  ASSERT_TRUE(queue.Write(output));
  uint64_t token = 0;
  ASSERT_EQ(queue.PeekOutput(bytes, &token), 2u);
  EXPECT_EQ(bytes[0], 0x11);
  EXPECT_EQ(bytes[1], 0x10);
  // Backpressure retains the command until acknowledged.
  uint64_t repeated_token = 0;
  ASSERT_EQ(queue.PeekOutput(bytes, &repeated_token), 2u);
  EXPECT_EQ(token, repeated_token);
  queue.CompleteOutput(token, true);
  EXPECT_EQ(queue.PeekOutput(bytes, &token), 0u);
}

TEST(BLEReportQueue, OldCompletionCannotConsumeANewSessionCommand)
{
  BLEReportQueue queue;
  const std::array<uint8_t, 3> output{0xa2, 0x15, 0};
  std::array<uint8_t, 22> bytes{};
  uint64_t old_token = 0, new_token = 0;
  ASSERT_TRUE(queue.Write(output));
  ASSERT_EQ(queue.PeekOutput(bytes, &old_token), 2u);
  queue.Reset();
  ASSERT_TRUE(queue.Write(output));
  ASSERT_EQ(queue.PeekOutput(bytes, &new_token), 2u);
  ASSERT_NE(old_token, new_token);
  queue.CompleteOutput(old_token, false);
  queue.CompleteOutput(old_token, true);
  EXPECT_TRUE(queue.IsConnected());
  EXPECT_EQ(queue.PeekOutput(bytes, &new_token), 2u);
}

TEST(BLEReportQueue, DisconnectWakesReaderAndRejectsLateInputAndOutput)
{
  BLEReportQueue queue;
  auto reader = std::async(std::launch::async, [&] {
    std::array<uint8_t, 23> bytes{};
    int result;
    do { result = queue.Read(bytes.data()); } while (result < 0);
    return result;
  });
  queue.Close();
  ASSERT_EQ(reader.wait_for(std::chrono::seconds(1)), std::future_status::ready);
  EXPECT_EQ(reader.get(), 0);
  EXPECT_FALSE(queue.PushInput(std::array<uint8_t, 3>{0x30, 0, 0}));
  EXPECT_FALSE(queue.Write(std::array<uint8_t, 3>{0xa2, 0x15, 0}));
  EXPECT_FALSE(queue.IsConnected());
}

TEST(BLEReportQueue, WakeupDoesNotDisconnect)
{
  BLEReportQueue queue;
  queue.Wakeup();
  std::array<uint8_t, 23> bytes{};
  EXPECT_EQ(queue.Read(bytes.data()), -1);
  EXPECT_TRUE(queue.IsConnected());
}

TEST(BLEReportQueue, InvalidReportsAndInsufficientDestinationAreRejected)
{
  BLEReportQueue queue;
  EXPECT_FALSE(queue.PushInput({}));
  EXPECT_FALSE(queue.PushInput(std::array<uint8_t, 23>{}));
  EXPECT_FALSE(queue.Write(std::array<uint8_t, 3>{0, 0x15, 0}));
  EXPECT_FALSE(queue.Write(std::array<uint8_t, 1>{0xa2}));
  ASSERT_TRUE(queue.Write(std::array<uint8_t, 3>{0xa2, 0x15, 0}));
  uint64_t token = 0;
  std::array<uint8_t, 1> bytes{};
  EXPECT_EQ(queue.PeekOutput(bytes, &token), 0u);
  EXPECT_TRUE(queue.IsConnected());
}

TEST(BLEReportQueue, OverflowAndFailedDeliveryFailClosed)
{
  BLEReportQueue input;
  for (int i = 0; i < 256; ++i)
    ASSERT_TRUE(input.PushInput(std::array<uint8_t, 3>{0x30, 0, 0}));
  EXPECT_FALSE(input.PushInput(std::array<uint8_t, 3>{0x30, 0, 0}));
  std::array<uint8_t, 23> bytes{};
  EXPECT_EQ(input.Read(bytes.data()), 0);

  BLEReportQueue output;
  for (int i = 0; i < 64; ++i)
    ASSERT_TRUE(output.Write(std::array<uint8_t, 3>{0xa2, 0x15, 0}));
  EXPECT_FALSE(output.Write(std::array<uint8_t, 3>{0xa2, 0x15, 0}));
  EXPECT_FALSE(output.IsConnected());

  BLEReportQueue rejected;
  ASSERT_TRUE(rejected.Write(std::array<uint8_t, 3>{0xa2, 0x15, 0}));
  uint64_t token = 0;
  ASSERT_EQ(rejected.PeekOutput(bytes, &token), 2u);
  rejected.CompleteOutput(token, false);
  EXPECT_FALSE(rejected.IsConnected());
}
