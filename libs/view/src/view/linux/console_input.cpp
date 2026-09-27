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

#include "view/console_input.hpp"
#include "view/linux/terminal_settings.hpp"
#include "view/linux/tty_redirect.hpp"
#include "util/logger.hpp"
#include <array>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <poll.h>
#include <stop_token>
#include <unistd.h>

namespace {
    struct WakePipe {
        int fds[2]{-1, -1};
        WakePipe()
        {
            if (::pipe(fds) != 0) return;
            for (int fd : fds) {
                if (::fcntl(fd, F_SETFD, FD_CLOEXEC) == -1 || ::fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
                    ::close(fds[0]); ::close(fds[1]); fds[0] = fds[1] = -1; break;
                }
            }
        }
        ~WakePipe() { for (int fd : fds) if (fd != -1) ::close(fd); }
        void wake() const { if (fds[1] != -1) { const char byte = 1; (void)::write(fds[1], &byte, 1); } }
    };
}
namespace View {
    void ConsoleInput::consume_byte(unsigned char ch)
    {
        if (escape_state_ == 1) {
            escape_state_ = ch == '[' ? 2 : 0;
            if (escape_state_ == 2) return;
        }
        else if (escape_state_ == 2) {
            escape_state_ = 0;
            switch (ch) {
                case 'A': handle_up_arrow(); break;
                case 'B': handle_down_arrow(); break;
                case 'C': handle_right_arrow(); break;
                case 'D': handle_left_arrow(); break;
                case 'F': handle_end(); break;
                case 'H': handle_home(); break;
                case '3': escape_state_ = 3; break;
            }
            return;
        }
        else if (escape_state_ == 3) { escape_state_ = 0; if (ch == '~') handle_delete(); return; }
        switch (ch) {
            case 0: break;
            case '\n': handle_newline(); break;
            case '\r': break; // CRLF pipe input: newline executes once
            case 0x1B: escape_state_ = 1; break;
            case 0x7F:
            case '\b': handle_backspace(); break;
            case '\t': handle_tab(); break;
            default: handle_char(static_cast<char>(ch)); break;
        }
    }
    void ConsoleInput::read_input(std::stop_token stop)
    {
        const ScopedTerminalSettings terminal_settings{};
        const ScopedTtyRedirect tty_redirect{};
        const WakePipe wake_pipe{};
        // stop_callback is destroyed before the pipe, including an already-stopped token.
        std::stop_callback wake{stop, [&wake_pipe] { wake_pipe.wake(); }};
        std::array<pollfd, 2> fds{{{STDIN_FILENO, POLLIN, 0}, {wake_pipe.fds[0], POLLIN, 0}}};
        std::array<unsigned char, 256> buffer{};
        auto escape_deadline = std::chrono::steady_clock::time_point{};
        while (!stop.stop_requested()) {
            if (escape_state_ && std::chrono::steady_clock::now() >= escape_deadline) escape_state_ = 0;
            const auto ready = ::poll(fds.data(), fds.size(), 25);
            if (ready < 0) {
                if (errno == EINTR) continue;
                Util::log_error("Console poll failed (errno {}). Input disabled.", errno); break;
            }
            if (stop.stop_requested()) break;
            if (ready == 0) continue;
            if (fds[0].revents & POLLNVAL) break;
            if (!(fds[0].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            const auto count = ::read(STDIN_FILENO, buffer.data(), buffer.size());
            if (count == 0) break; // EOF: do not spin; server keeps running headless
            if (count < 0) {
                if (errno == EINTR) continue;
                if ((errno == EAGAIN) && !(fds[0].revents & (POLLHUP | POLLERR))) continue;
                Util::log_error("Console read failed (errno {}). Input disabled.", errno); break;
            }
            for (ssize_t i = 0; i < count && !stop.stop_requested(); ++i) {
                consume_byte(buffer[static_cast<std::size_t>(i)]);
                if (escape_state_) escape_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{100};
            }
        }
    }
}
