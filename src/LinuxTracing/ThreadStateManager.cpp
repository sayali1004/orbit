// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ThreadStateManager.h"

#include <absl/container/flat_hash_map.h>
#include <absl/meta/type_traits.h>
#include <sys/types.h>

#include <algorithm>
#include <utility>

#include "GrpcProtos/capture.pb.h"
#include "OrbitBase/Logging.h"

namespace orbit_linux_tracing {

using orbit_grpc_protos::ThreadStateSlice;

// Note: Since we use PerfEventProcessor to process perf_event_open events in order, OnNewTask,
// OnSchedWakeup, OnSchedSwitchIn, OnSchedSwitchOut are expected to be called in order of
// timestamp. But the initial thread states are retrieved (and OnInitialState is called) after the
// perf_event_open file descriptors have been enabled, so that we don't lose thread states between
// retrieving the initial states and enabling the file descriptors. It is then common for some of
// the first tracepoint events to have a timestamp lower than the timestamp of initial retrieval. In
// all these cases, we discard the previous known state (the one retrieved at the beginning, with a
// larger timestamp) and replace it with the thread state carried by the tracepoint.

void ThreadStateManager::OnInitialState(uint64_t timestamp_ns, pid_t tid,
                                        ThreadStateSlice::ThreadState state) {
  ORBIT_CHECK(!tid_open_states_.contains(tid));
  tid_open_states_.emplace(tid, OpenState{state, timestamp_ns});
}

void ThreadStateManager::OnNewTask(uint64_t timestamp_ns, pid_t tid, pid_t was_created_by_tid,
                                   pid_t was_created_by_pid) {
  static constexpr ThreadStateSlice::ThreadState kNewState = ThreadStateSlice::kRunnable;

  if (auto open_state_it = tid_open_states_.find(tid);
      open_state_it != tid_open_states_.end() &&
      timestamp_ns >= open_state_it->second.begin_timestamp_ns) {
    ORBIT_ERROR("Processed task:task_newtask but thread %d was already known", tid);
    return;
  }
  tid_open_states_.insert_or_assign(
      tid, OpenState{kNewState, timestamp_ns, orbit_grpc_protos::ThreadStateSlice::kCreated,
                     was_created_by_tid, was_created_by_pid});
}

std::optional<ThreadStateSlice> ThreadStateManager::OnSchedWakeup(uint64_t timestamp_ns, pid_t tid,
                                                                  pid_t was_unblocked_by_tid,
                                                                  pid_t was_unblocked_by_pid,
                                                                  bool has_wakeup_callstack) {
  static constexpr ThreadStateSlice::ThreadState kNewState = ThreadStateSlice::kRunnable;

  auto open_state_it = tid_open_states_.find(tid);
  if (open_state_it == tid_open_states_.end()) {
    ORBIT_ERROR("Processed sched:sched_wakeup but previous state of thread %d is unknown", tid);
    tid_open_states_.insert_or_assign(
        tid, OpenState{kNewState, timestamp_ns, orbit_grpc_protos::ThreadStateSlice::kUnblocked,
                       was_unblocked_by_tid, was_unblocked_by_pid, has_wakeup_callstack});
    return std::nullopt;
  }

  const OpenState& open_state = open_state_it->second;
  if (timestamp_ns < open_state.begin_timestamp_ns) {
    // As noted above, overwrite the thread state retrieved at the beginning.
    tid_open_states_.insert_or_assign(
        tid, OpenState{kNewState, timestamp_ns, orbit_grpc_protos::ThreadStateSlice::kUnblocked,
                       was_unblocked_by_tid, was_unblocked_by_pid, has_wakeup_callstack});
    return std::nullopt;
  }

  if (open_state.state == kNewState || open_state.state == ThreadStateSlice::kRunning) {
    // It seems to be somewhat common for a thread to receive a wakeup
    // while already in runnable or running state: disregard the state change.
    return std::nullopt;
  }

  if (open_state.state == ThreadStateSlice::kZombie ||
      open_state.state == ThreadStateSlice::kDead) {
    ORBIT_ERROR("Processed sched:sched_wakeup for thread %d but unexpected previous state %s", tid,
                ThreadStateSlice::ThreadState_Name(open_state.state));
  }

  ThreadStateSlice slice;
  slice.set_tid(tid);
  slice.set_thread_state(open_state.state);
  slice.set_duration_ns(timestamp_ns - open_state.begin_timestamp_ns);
  slice.set_end_timestamp_ns(timestamp_ns);
  slice.set_wakeup_reason(open_state.wakeup_reason);
  slice.set_wakeup_tid(open_state.wakeup_tid);
  slice.set_wakeup_pid(open_state.wakeup_pid);
  if (open_state.has_wakeup_or_switch_out_callstack) {
    slice.set_switch_out_or_wakeup_callstack_status(
        orbit_grpc_protos::ThreadStateSlice::kWaitingForCallstack);
  } else {
    slice.set_switch_out_or_wakeup_callstack_status(
        orbit_grpc_protos::ThreadStateSlice::kNoCallstack);
  }
  tid_open_states_.insert_or_assign(
      tid, OpenState{kNewState, timestamp_ns, orbit_grpc_protos::ThreadStateSlice::kUnblocked,
                     was_unblocked_by_tid, was_unblocked_by_pid, has_wakeup_callstack});
  return slice;
}

std::optional<ThreadStateSlice> ThreadStateManager::OnSchedSwitchIn(uint64_t timestamp_ns,
                                                                    pid_t tid) {
  static constexpr ThreadStateSlice::ThreadState kNewState = ThreadStateSlice::kRunning;

  auto open_state_it = tid_open_states_.find(tid);
  if (open_state_it == tid_open_states_.end()) {
    ORBIT_ERROR("Processed sched:sched_switch(in) but previous state of thread %d is unknown", tid);
    tid_open_states_.insert_or_assign(tid, OpenState{kNewState, timestamp_ns});
    return std::nullopt;
  }

  const OpenState& open_state = open_state_it->second;
  if (timestamp_ns < open_state.begin_timestamp_ns) {
    tid_open_states_.insert_or_assign(tid, OpenState{kNewState, timestamp_ns});
    return std::nullopt;
  }

  if (open_state.state == kNewState) {
    // No state change: do nothing and don't overwrite the previous begin timestamp.
    return std::nullopt;
  }

  // Don't print an error even if open_state.state != ThreadStateSlice::kRunnable: it seems to be
  // sometimes possible for a thread to go from a non-runnable state directly to running, skipping
  // the sched:sched_wakeup event.

  ThreadStateSlice slice;
  slice.set_tid(tid);
  slice.set_thread_state(open_state.state);
  slice.set_duration_ns(timestamp_ns - open_state.begin_timestamp_ns);
  slice.set_end_timestamp_ns(timestamp_ns);
  slice.set_wakeup_reason(open_state.wakeup_reason);
  slice.set_wakeup_tid(open_state.wakeup_tid);
  slice.set_wakeup_pid(open_state.wakeup_pid);
  if (open_state.has_wakeup_or_switch_out_callstack) {
    slice.set_switch_out_or_wakeup_callstack_status(
        orbit_grpc_protos::ThreadStateSlice::kWaitingForCallstack);
  } else {
    slice.set_switch_out_or_wakeup_callstack_status(
        orbit_grpc_protos::ThreadStateSlice::kNoCallstack);
  }
  tid_open_states_.insert_or_assign(tid, OpenState{kNewState, timestamp_ns});
  return slice;
}

std::optional<ThreadStateSlice> ThreadStateManager::OnSchedSwitchOut(
    uint64_t timestamp_ns, pid_t tid, ThreadStateSlice::ThreadState new_state,
    bool has_switch_out_callstack) {
  auto open_state_it = tid_open_states_.find(tid);
  if (open_state_it == tid_open_states_.end()) {
    ORBIT_ERROR("Processed sched:sched_switch(out) but previous state of thread %d is unknown",
                tid);
    tid_open_states_.insert_or_assign(tid,
                                      OpenState{new_state, timestamp_ns, has_switch_out_callstack});
    return std::nullopt;
  }

  const OpenState& open_state = open_state_it->second;
  if (timestamp_ns < open_state.begin_timestamp_ns) {
    tid_open_states_.insert_or_assign(tid,
                                      OpenState{new_state, timestamp_ns, has_switch_out_callstack});
    return std::nullopt;
  }

  // As we are switching out of a CPU, if the previous state was kRunnable, assume it was kRunning.
  // This is because when we retrieve the initial thread states we have no way to distinguish
  // between kRunnable and kRunning. After all, for the OS they are the same state.
  ThreadStateSlice::ThreadState adjusted_open_state_state = open_state.state;
  if (adjusted_open_state_state == ThreadStateSlice::kRunnable) {
    adjusted_open_state_state = ThreadStateSlice::kRunning;
  }

  if (adjusted_open_state_state != ThreadStateSlice::kRunning) {
    ORBIT_ERROR("Processed sched:sched_switch(out) for thread %d but unexpected previous state %s",
                tid, ThreadStateSlice::ThreadState_Name(adjusted_open_state_state));
    if (adjusted_open_state_state == new_state) {
      // No state change: do nothing and don't overwrite the previous begin timestamp.
      return std::nullopt;
    }
  }

  ThreadStateSlice slice;
  slice.set_tid(tid);
  slice.set_thread_state(adjusted_open_state_state);
  slice.set_duration_ns(timestamp_ns - open_state.begin_timestamp_ns);
  slice.set_end_timestamp_ns(timestamp_ns);
  slice.set_wakeup_reason(open_state.wakeup_reason);
  slice.set_wakeup_tid(open_state.wakeup_tid);
  slice.set_wakeup_pid(open_state.wakeup_pid);

  // Note: If the thread exits but the new_state is kZombie instead of kDead,
  // the switch to kDead will never be reported.
  tid_open_states_.insert_or_assign(tid,
                                    OpenState{new_state, timestamp_ns, has_switch_out_callstack});
  return slice;
}

std::vector<ThreadStateSlice> ThreadStateManager::OnCaptureFinished(uint64_t timestamp_ns) {
  std::vector<ThreadStateSlice> slices;
  for (const auto& [tid, open_state] : tid_open_states_) {
    ThreadStateSlice slice;
    slice.set_tid(tid);
    slice.set_thread_state(open_state.state);
    slice.set_duration_ns(timestamp_ns - open_state.begin_timestamp_ns);
    slice.set_end_timestamp_ns(timestamp_ns);
    slice.set_wakeup_reason(open_state.wakeup_reason);
    slice.set_wakeup_tid(open_state.wakeup_tid);
    slice.set_wakeup_pid(open_state.wakeup_pid);
    if (open_state.has_wakeup_or_switch_out_callstack) {
      slice.set_switch_out_or_wakeup_callstack_status(
          orbit_grpc_protos::ThreadStateSlice::kWaitingForCallstack);
    } else {
      slice.set_switch_out_or_wakeup_callstack_status(
          orbit_grpc_protos::ThreadStateSlice::kNoCallstack);
    }
    slices.emplace_back(std::move(slice));
  }
  return slices;
}

}  // namespace orbit_linux_tracing
