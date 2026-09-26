#pragma once

#include "core/protocol.hpp"

#include <algorithm>
#include <chrono>
#include <future>
#include <optional>
#include <vector>

namespace pasteit {

// Keeps superseded async requests alive until they finish, so replacing one
// never joins its worker on the UI thread.
class DecisionFutureSlot {
public:
    void replace(std::future<DecisionResponse> next) {
        clear();
        current_.emplace(std::move(next));
    }

    void clear() {
        if (current_.has_value()) {
            retired_.push_back(std::move(*current_));
            current_.reset();
        }
    }

    bool pending() const { return current_.has_value(); }

    std::optional<DecisionResponse> take_ready() {
        if (!current_ || current_->wait_for(std::chrono::milliseconds{0}) != std::future_status::ready) {
            return std::nullopt;
        }
        auto response = current_->get();
        current_.reset();
        return response;
    }

    void reap() {
        std::erase_if(retired_, [](std::future<DecisionResponse>& request) {
            return request.wait_for(std::chrono::milliseconds{0}) == std::future_status::ready;
        });
    }

    void wait_all() {
        if (current_) {
            current_->wait();
        }
        for (auto& request : retired_) {
            request.wait();
        }
    }

private:
    std::optional<std::future<DecisionResponse>> current_;
    std::vector<std::future<DecisionResponse>> retired_;
};

}  // namespace pasteit
