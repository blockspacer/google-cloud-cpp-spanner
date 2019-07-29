// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef GOOGLE_CLOUD_CPP_GOOGLE_CLOUD_SPANNER_INTERNAL_TRANSACTION_IMPL_H_
#define GOOGLE_CLOUD_CPP_GOOGLE_CLOUD_SPANNER_INTERNAL_TRANSACTION_IMPL_H_

#include "google/cloud/spanner/version.h"
#include <google/spanner/v1/transaction.pb.h>
#include <condition_variable>
#include <functional>
#include <mutex>

namespace google {
namespace cloud {
namespace spanner {
inline namespace SPANNER_CLIENT_NS {
namespace internal {

/**
 * The internal representation of a google::cloud::spanner::Transaction.
 */
class TransactionImpl {
 public:
  TransactionImpl(google::spanner::v1::TransactionSelector selector)
      : selector_(std::move(selector)) {
    state_ = selector_.has_begin() ? State::kBegin : State::kDone;
  }

  ~TransactionImpl();

  // Visit the transaction with the given functor, which should use the
  // passed TransactionSelector in its Client::Read()/Client::ExecuteSql()
  // call. If initially selector.has_begin(), and the operation successfully
  // allocates a transaction ID, then the functor should selector.set_id(id).
  // Otherwise the functor should not modify the selector.
  template <typename T>
  T Visit(std::function<T(google::spanner::v1::TransactionSelector&)> f) {
    {
      std::unique_lock<std::mutex> lock(mu_);
      cond_.wait(lock, [this] { return state_ != State::kPending; });
      if (state_ == State::kDone) {
        lock.unlock();
        return f(selector_);
      }
      state_ = State::kPending;
    }
    // selector_.has_begin(), but only one visitor active at a time.
#if __EXCEPTIONS
    try {
#endif
      auto r = f(selector_);
      bool done = false;
      {
        std::lock_guard<std::mutex> lock(mu_);
        state_ = selector_.has_begin() ? State::kBegin : State::kDone;
        done = (state_ == State::kDone);
      }
      if (done) {
        cond_.notify_all();
      } else {
        cond_.notify_one();
      }
      return r;
#if __EXCEPTIONS
    } catch (...) {
      {
        std::lock_guard<std::mutex> lock(mu_);
        state_ = State::kBegin;
      }
      cond_.notify_one();
      throw;
    }
#endif
  }

 private:
  enum class State {
    kBegin,    // waiting for a future visitor to assign a transaction ID
    kPending,  // waiting for an active visitor to assign a transaction ID
    kDone,     // a transaction ID has been assigned (or we are single-use)
  };
  State state_;

  std::mutex mu_;
  std::condition_variable cond_;
  google::spanner::v1::TransactionSelector selector_;
};

}  // namespace internal
}  // namespace SPANNER_CLIENT_NS
}  // namespace spanner
}  // namespace cloud
}  // namespace google

#endif  // GOOGLE_CLOUD_CPP_GOOGLE_CLOUD_SPANNER_INTERNAL_TRANSACTION_IMPL_H_