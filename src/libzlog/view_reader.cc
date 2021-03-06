#include "libzlog/view_reader.h"
#include "include/zlog/backend.h"
#include "log_backend.h"
#include <iostream>

namespace zlog {

ViewReader::ViewReader(
    const Options& options,
    const std::shared_ptr<LogBackend> backend) :
  shutdown_(false),
  backend_(backend),
  options_(options),
  view_(nullptr),
  refresh_timeout_(std::chrono::milliseconds(options_.max_refresh_timeout_ms)),
  refresh_thread_(std::thread(&ViewReader::refresh_entry_, this))
{
  assert(backend);
}

ViewReader::~ViewReader()
{
  {
    std::lock_guard<std::mutex> lk(lock_);
    if (shutdown_) {
      assert(refresh_waiters_.empty());
      assert(!refresh_thread_.joinable());
      return;
    }
  }

  shutdown();

  std::lock_guard<std::mutex> lk(lock_);
  assert(shutdown_);
  assert(refresh_waiters_.empty());
  assert(!refresh_thread_.joinable());
}

void ViewReader::shutdown()
{
  {
    std::lock_guard<std::mutex> lk(lock_);
    shutdown_ = true;
  }
  refresh_cond_.notify_one();
  refresh_thread_.join();
}

void ViewReader::refresh_entry_()
{
  while (true) {
    {
      std::unique_lock<std::mutex> lk(lock_);

      std::chrono::milliseconds timeout;
      if (refresh_waiters_.empty()) {
        // no waiters: jump directly to a long delay. when a new waiter arrives
        // they'll signal the thread immediately. there are no waiters when the
        // log object is intially created, but that case is handled by doing a
        // manual refresh during setup.
        timeout = std::chrono::milliseconds(options_.max_refresh_timeout_ms);
      } else {
        timeout = std::min(refresh_timeout_,
            std::chrono::milliseconds(options_.max_refresh_timeout_ms));
      }

      const auto status = refresh_cond_.wait_for(lk, timeout);

      if (status == std::cv_status::timeout) {
        refresh_timeout_ = std::chrono::milliseconds(timeout.count() * 2);
      }

      if (shutdown_) {
        for (auto waiter : refresh_waiters_) {
          waiter->done = true;
          waiter->cond.notify_one();
        }
        refresh_waiters_.clear();
        break;
      }
    }

    refresh_view();

    const auto current_view = view();
    if (!current_view) {
      continue;
    }

    std::lock_guard<std::mutex> lk(lock_);
    for (auto it = refresh_waiters_.begin(); it != refresh_waiters_.end();) {
      auto waiter = *it;
      if (current_view->epoch() > waiter->epoch) {
        waiter->done = true;
        waiter->cond.notify_one();
        it = refresh_waiters_.erase(it);
      } else {
        it++;
      }
    }
  }
}

std::shared_ptr<const VersionedView> ViewReader::view() const
{
  std::lock_guard<std::mutex> lk(lock_);
  return view_;
}

void ViewReader::wait_for_newer_view(const uint64_t epoch, bool wakeup)
{
  std::unique_lock<std::mutex> lk(lock_);
  if (shutdown_) {
    return;
  }
  // TODO: is it necessary to hold the lock while initializing the waiter object
  // that will be read by the refresher thread?
  RefreshWaiter waiter(epoch);
  wakeup = wakeup || refresh_waiters_.empty();
  refresh_waiters_.emplace_back(&waiter);
  if (wakeup) {
    refresh_timeout_ = std::chrono::milliseconds(
        options_.min_refresh_timeout_ms);
    refresh_cond_.notify_one();
  }
  waiter.cond.wait(lk, [&waiter] { return waiter.done; });
}

std::unique_ptr<VersionedView> ViewReader::get_latest_view() const
{
  std::map<uint64_t, std::string> views;
  int ret = backend_->ReadViews(0, 1, &views);
  if (ret) {
    std::cerr << "get_latest_view failed to read view " << ret << std::endl;
    return nullptr;
  }

  const auto it = views.crbegin();
  if (it == views.crend()) {
    std::cerr << "get_latest_view no views found" << std::endl;
    // this would happen if there are no views
    return nullptr;
  }

  return std::unique_ptr<VersionedView>(
      new VersionedView(it->first, it->second));
}

void ViewReader::refresh_view()
{
  auto latest_view = get_latest_view();
  if (!latest_view) {
    std::cerr << "refresh_view failed to get latest view" << std::endl;
    return;
  }
  assert(!latest_view->seq);

  std::lock_guard<std::mutex> lk(lock_);

  if (view_) {
    assert(latest_view->epoch() >= view_->epoch());
    if (latest_view->epoch() == view_->epoch()) {
      return;
    }
  }

  // if the latest view has a sequencer config and token that matches this log
  // client instance, then we will become a sequencer / exclusive writer.
  if (latest_view->seq_config() &&
      latest_view->seq_config()->token() == backend_->token()) {

    // there are two cases for initializing the new view's sequencer:
    //
    //   1) reuse sequencer from previous view
    //   2) create a new sequencer instance
    //
    // if a previous view has a sequencer with the same token, then we might be
    // able to reuse it. however, if the previous view that we have and the
    // latest view are separated by views with _other_ sequencers in the log,
    // but which we haven't observed, then we need to take that into account.
    // in order to catch this scenario, we also check that the previous view has
    // an initialization epoch that matches the epoch in the latest view's
    // sequencer config.
    //
    // the sequencer config in a view either copied or a new sequencer config is
    // proposed. whenver a sequencer config is successfully proposed, it's
    // initialization epoch will be unique (even for different proposals from
    // the same log client). so, if the token and the initialization epoch are
    // equal, then we can be assured that the sequencer hasn't changed and we
    // can reuse the state.
    //
    if (view_ &&
        view_->seq_config() &&
        view_->seq_config()->token() == backend_->token() &&
        view_->seq_config()->epoch() == latest_view->seq_config()->epoch()) {
      //
      // note about thread safety. here we copy the pointer to the existing
      // sequencer which may be in-use concurrently. it wouldn't be sufficient
      // to create a new sequencer object initialized with the existing state
      // (we could miss updates to the seq state until all new threads saw the
      // new view) unless concurrent updates were blocked by a lock, but that
      // would introduce a lock on the i/o path.
      //
      assert(view_->seq);
      latest_view->seq = view_->seq;
    } else {
      // create a new instance for this sequencer
      latest_view->seq = std::make_shared<Sequencer>(latest_view->epoch(),
          latest_view->seq_config()->position());
    }
  }

  view_ = std::move(latest_view);
}

}
