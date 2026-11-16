// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/CpuBoost.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>

#include <sys/syscall.h>
#include <unistd.h>

namespace CpuBoost
{
const Config::Info<int> UT_CPU_BOOST{{Config::System::Main, "UbuntuTouch", "CPUBoost"}, 1024};

namespace
{
// struct sched_attr (linux/sched/types.h) with the utilization clamps.
struct SchedAttr
{
  std::uint32_t size;
  std::uint32_t sched_policy;
  std::uint64_t sched_flags;
  std::int32_t sched_nice;
  std::uint32_t sched_priority;
  std::uint64_t sched_runtime;
  std::uint64_t sched_deadline;
  std::uint64_t sched_period;
  std::uint32_t sched_util_min;
  std::uint32_t sched_util_max;
};

constexpr std::uint64_t SCHED_FLAG_KEEP_POLICY = 0x08;
constexpr std::uint64_t SCHED_FLAG_KEEP_PARAMS = 0x10;
constexpr std::uint64_t SCHED_FLAG_UTIL_CLAMP_MIN = 0x20;
}  // namespace

int Apply(int util_min)
{
  SchedAttr attr{};
  attr.size = sizeof(attr);
  attr.sched_flags = SCHED_FLAG_KEEP_POLICY | SCHED_FLAG_KEEP_PARAMS | SCHED_FLAG_UTIL_CLAMP_MIN;
  attr.sched_util_min = static_cast<std::uint32_t>(std::clamp(util_min, 0, 1024));
  attr.sched_util_max = 1024;

  int changed = 0;
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator("/proc/self/task", error))
  {
    const pid_t tid = static_cast<pid_t>(std::stoi(entry.path().filename().string()));
    if (syscall(SYS_sched_setattr, tid, &attr, 0) == 0)
      ++changed;
  }
  return changed;
}
}  // namespace CpuBoost
