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
#include "view/base_view.hpp"
namespace Presenter {
    class OutputPresenter final {
      public:
        OutputPresenter(Model::ServerLoop& loop, View::BaseView& view) : loop_(loop), view_(view)
        {
            listener_ = loop_.append_listener([this](Model::ServerLoopEvent event) {
                if (event == Model::ServerLoopEvent::status_updated) {
                    const auto& s = loop_.get_status();
                    view_.display_server_status(s.fps, s.num_players, s.max_players, s.current_map);
                }
            });
        }
        ~OutputPresenter() { loop_.remove_listener(listener_); }
        OutputPresenter(const OutputPresenter&) = delete;
        OutputPresenter& operator=(const OutputPresenter&) = delete;
      private:
        Model::ServerLoop& loop_;
        View::BaseView& view_;
        Model::ServerLoop::Handle listener_{};
    };
}
