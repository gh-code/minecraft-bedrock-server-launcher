//
// Copyright (c) 2024 Gary Huang (ghuang dot nctu at gmail dot com)
//

#ifndef GH_SYSTEM_TIMER_HPP
#define GH_SYSTEM_TIMER_HPP

#include <boost/asio/io_service.hpp>
#include <boost/asio/steady_timer.hpp>

#include <functional>

namespace gh {
namespace system {

class timer
{
public:
    typedef boost::asio::steady_timer::duration duration;

public:
    template<typename Callback>
    timer(
        boost::asio::io_service &ios,
        const duration &expiry_time,
        Callback&& callback
    )
        : timer_(ios, expiry_time)
        , interval_(expiry_time)
        , callback_(std::move(callback))
    { }

    auto start() -> void
    { 
        timer_.async_wait([=](boost::system::error_code ec) {
            if (!ec && callback_()) {
                run();
            }
        });
    }

    auto stop() -> void
    { timer_.cancel(); }

    auto run() -> void
    {
        timer_.expires_from_now(interval_);
        timer_.async_wait([=](boost::system::error_code ec) {
            if (!ec && callback_()) {
                run();
            }
        });
    }

private:
    boost::asio::steady_timer timer_;
    duration interval_;
    std::function<bool()> callback_;
};

} // namespace system
} // namespace gh

#endif // GH_SYSTEM_TIMER_HPP