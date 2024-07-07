//
// Copyright (c) 2024 Gary Huang (ghuang dot nctu at gmail dot com)
//

#ifndef GH_SYSTEM_SHELL_HPP
#define GH_SYSTEM_SHELL_HPP

#include <gh/system/router.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>

#include <boost/process/async.hpp>
#include <boost/process/async_pipe.hpp>
#include <boost/process/child.hpp>
#include <boost/process/io.hpp>

#include <boost/regex.hpp>

#include <iostream>
#include <memory>

namespace gh {
namespace system {

namespace bp = boost::process;

inline void
fail(boost::system::error_code ec, char const* what)
{
    std::cerr << what << ": " << ec.message() << "\n";
}

class wrapper : public std::enable_shared_from_this<wrapper>
{
    bp::async_pipe& ap_;
    boost::asio::streambuf buffer_;
    router& router_;

public:
    wrapper(router& r, bp::async_pipe& ap)
    : ap_(ap)
    , buffer_(128)
    , router_(r)
    { }

    auto run() -> void
    {
        do_read();
    }

    auto do_read() -> void
    {
        using namespace std::placeholders;
        boost::asio::async_read_until(ap_, buffer_, '\n',
            std::bind(
                &wrapper::on_read,
                shared_from_this(),
                _1, _2));
    }

    auto on_read(
        boost::system::error_code ec,
        std::size_t size) -> void
    {
        if (ec) {
            return fail(ec, "read");
        }

        std::string line;
        if (std::getline(std::istream(&buffer_), line)) {
            std::cout << line << "\n";
            const auto& rules = router_.get_table();
            for (const auto& rule : rules) {
                boost::regex pattern(std::string(rule.first));
                boost::smatch matches;
                if (boost::regex_search(line, matches, pattern)) {
                    rule.second(router::Matches{matches.begin(), matches.end()});
                    break;
                }
            }
        }

        do_read();
    }
};

/** Shell for interactive from user inputs and auto command.
 */
class shell : public router
{
    bp::opstream os;
    bp::async_pipe ap;
    bp::child c;
    std::thread t;

public:
    /// Command is read from string
    shell(const char* command, boost::asio::io_service &ios)
    : os()
    , ap(ios)
    , c(command,
        bp::std_in < os,
        bp::std_out > ap,
        bp::std_err > ap)
    {
        std::make_shared<wrapper>(*this, ap)->run();

        // must be interactive
        // if (!isatty(fileno(stdin))) { /* error */ }

        // listen to terminal input
        t = std::thread([this]() {
            while (this->running()) {
                std::string line;
                // wait for input from tty
                if (std::getline(std::cin, line) && !line.empty()) {
                    // record input history

                    // pass input to child process
                    this->prompt() << line << std::endl;
                }
            }
        });
    }

    /// Destructor
    ~shell()
    {
        if (t.joinable()) {
            t.join();
        }
    }

    auto running() -> bool { return c.running(); }
    auto terminate() -> void { c.terminate(); t.detach(); }
    auto prompt() -> std::ostream& { return os; }
    auto exit_code() -> int { c.exit_code(); }
};

} // namespace system
} // namespace gh

#endif // GH_SYSTEM_SHELL_HPP