//
// Copyright (c) 2024 Gary Huang (ghuang dot nctu at gmail dot com)
//

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QWidget>
#include <QPushButton>

namespace gh {
namespace game {
namespace minecraft {
namespace bedrock {
class server;
} // namespace bedrock
} // namespace minecraft
} // namespace game
} // namespace gh

using bedrock_server = gh::game::minecraft::bedrock::server;

class MainWindow : public QWidget
{
    Q_OBJECT
public:
    MainWindow(bedrock_server& server, QWidget *parent = nullptr);

    QPushButton* backup_button;

private:
    bedrock_server& server;

private slots:
    void exit_app();
    void backup();
    void control();
};

#endif // MAINWINDOW_H