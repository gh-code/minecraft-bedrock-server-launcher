//
// Copyright (c) 2024 Gary Huang (ghuang dot nctu at gmail dot com)
//

#include "mainwindow.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QPushButton>

MainWindow::MainWindow(bedrock_server& server_, QWidget *parent)
    : server(server_)
    , QWidget(parent)
{
    auto list_button = new QPushButton(tr("List players"), this);
    backup_button = new QPushButton(tr("Backup"), this);

    auto layout = new QVBoxLayout;
    layout->addWidget(list_button);
    layout->addWidget(backup_button);
    setLayout(layout);

    connect(list_button, SIGNAL(clicked()), this, SLOT(list()));
    connect(backup_button, SIGNAL(clicked()), this, SLOT(backup()));

    setWindowTitle(tr("Main Window"));
}

void MainWindow::exit_app()
{
    QApplication::exit();
}