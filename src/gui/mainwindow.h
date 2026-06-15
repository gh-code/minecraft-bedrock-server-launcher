//
// Copyright (c) 2026 Gary Huang (ghuang dot nctu at gmail dot com)
//

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QWidget>
#include <QPushButton>

class QComboBox;

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

    void loadSettings();

    QComboBox* backup_methods;
    QPushButton* backup_button;

private:
    bedrock_server& server;
    bool isBackingUp = false;

private slots:
    void saveSettings();
    void exit_app();
    void backup();
    void list();
    void onBackupFinished();
};

#endif // MAINWINDOW_H