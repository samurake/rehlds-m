/*
 * Half Life 1 SDK License
 * Copyright(c) Valve Corp
 *
 * DISCLAIMER OF WARRANTIES. THE HALF LIFE 1 SDK AND ANY OTHER MATERIAL
 * DOWNLOADED BY LICENSEE IS PROVIDED "AS IS". VALVE AND ITS SUPPLIERS
 * DISCLAIM ALL WARRANTIES WITH RESPECT TO THE SDK, EITHER EXPRESS OR IMPLIED,
 * INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY,
 * NON-INFRINGEMENT, TITLE AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * LIMITATION OF LIABILITY. IN NO EVENT SHALL VALVE OR ITS SUPPLIERS BE LIABLE
 * FOR ANY SPECIAL, INCIDENTAL, INDIRECT, OR CONSEQUENTIAL DAMAGES WHATSOEVER
 * (INCLUDING, WITHOUT LIMITATION, DAMAGES FOR LOSS OF BUSINESS PROFITS,
 * BUSINESS INTERRUPTION, LOSS OF BUSINESS INFORMATION, OR ANY OTHER PECUNIARY
 * LOSS) ARISING OUT OF THE USE OF OR INABILITY TO USE THE ENGINE AND/OR THE
 * SDK, EVEN IF VALVE HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
 *
 * For commercial use, contact: sourceengine@valvesoftware.com
 */

#pragma once
#include "model/server_loop.hpp"
#include "model/userinput_history.hpp"
#include <chrono>
#include <stop_token>

namespace Presenter {
    // Runtime owns all three objects. The input thread is joined before any dies.
    class InputPresenter final {
      public:
        InputPresenter(Model::ServerLoop& loop, Model::UserInputHistory& history)
          : loop_(loop), history_(history) { history_.load(); }
        [[nodiscard]] bool enqueue_input(std::string_view input) const
        {
            if (!loop_.enqueue_input(input)) return false;
            history_.append(input);
            history_.save();
            return true;
        }
        [[nodiscard]] std::vector<std::string> find_command_matches(std::string_view command, std::stop_token stop) const
        {
            auto request = loop_.request_completion(command);
            return request ? request->wait(stop, std::chrono::milliseconds{250}) : std::vector<std::string>{};
        }
        [[nodiscard]] std::optional<std::string> history_previous() const { return history_.previous(); }
        [[nodiscard]] std::optional<std::string> history_next() const { return history_.next(); }
      private:
        Model::ServerLoop& loop_;
        Model::UserInputHistory& history_;
    };
}
