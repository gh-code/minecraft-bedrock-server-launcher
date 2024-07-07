//
// Copyright (c) 2024 Gary Huang (ghuang dot nctu at gmail dot com)
//

#include "mainwindow.h"

#include <QApplication>

#include <gh/game/minecraft/server.hpp>
#include <gh/system/timer.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio/signal_set.hpp>

#include <csignal>

void MainWindow::backup()
{
    typedef std::remove_reference<decltype(server)>::type server_type;
    std::thread(std::bind(&server_type::backup, &server)).detach();
}

void MainWindow::control()
{
    server.prompt() << "control" << std::endl;
}

template<class Timer>
class echo_timer
{
    Timer& timer_;

public:
    echo_timer(Timer& t)
    : timer_(t)
    {}

    auto start() -> void
    {
        std::cout << "Backup timer is started\n";
        timer_.start();
    }

    auto stop() -> void
    {
        std::cout << "Backup timer is stopped\n";
        timer_.stop();
    }
};

template<class Timer>
auto make_echo_timer(Timer& t) -> echo_timer<Timer>
{ return t; }

auto main(int argc, char* argv[]) -> int
{
    using namespace gh::game::minecraft;
    using Matches = bedrock::server::Matches;

    auto const command = "../current/bedrock_server.exe";

    QApplication app(argc, argv);

    ::size_t num_players = 0;

    boost::asio::io_service ios;
    bedrock::server server(command, ios);

    MainWindow window(server);

    gh::system::timer tm_(ios, std::chrono::minutes(5), [&window](){
        window.backup_button->click();
        return true;
    });
    auto tm = make_echo_timer(tm_);

    server.get("Player connected: ([^,]+), xuid: (\\d{16})$",
        [&tm,&num_players](Matches matches){
            auto const& user = matches[1];
            auto const& xuid = matches[2];
            if (num_players == 0) {
                tm.start();
            }
            ++num_players;
        });
    server.get("Player disconnected: ([^,]+), xuid: (\\d{16}),",
        [&tm,&num_players](Matches matches){
            auto const& user = matches[1];
            auto const& xuid = matches[2];
            if (num_players > 0) {
                --num_players;
                if (num_players == 0) {
                    tm.stop();
                }
            }
        });

    boost::asio::signal_set ss(ios, SIGINT, SIGTERM);
    ss.async_wait([&](boost::system::error_code ec, int signal_number){
        app.exit();
    });

    window.show();

    // event loops
    std::thread t([&ios]() { ios.run(); });
    app.exec();

    // server.prompt() << "stop" << std::endl;
    server.terminate();
    ios.stop();

    t.join(); // wait for ios

    puts("exit gracefully");

    return server.exit_code();
}