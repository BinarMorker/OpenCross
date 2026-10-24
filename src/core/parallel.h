#pragma once
// parallel_for: calls fn(i) for every i in [0, count) on up to hardware_concurrency threads (the calling thread is one of them) and returns
// when every call has finished. The calls must not touch shared state without their own locking.
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <future>
#include <thread>
#include <vector>

namespace oc {

template <class Fn>
void parallel_for(size_t count, Fn&& fn) {
  if (count == 0) return;
  const size_t workers = std::min<size_t>(count, std::max(1u, std::thread::hardware_concurrency()));
  std::atomic<size_t> next{0};
  const auto run = [&] {
    for (size_t i = next.fetch_add(1); i < count; i = next.fetch_add(1)) fn(i);
  };
  std::vector<std::future<void>> jobs;
  for (size_t k = 1; k < workers; ++k) jobs.push_back(std::async(std::launch::async, run));
  run();
  for (auto& job : jobs) job.get();
}

}  // namespace oc
