//
// Copyright (c) 2024 Gary Huang (ghuang dot nctu at gmail dot com)
//

#include "mainwindow.h"
#include "backupWorker.h"

#include <gh/game/minecraft/server.hpp>

#include <QApplication>
#include <QHBoxLayout>
#include <QPushButton>
#include <QProgressDialog>
#include <QThread>

MainWindow::MainWindow(bedrock_server& server_, QWidget *parent)
    : server(server_)
    , QWidget(parent)
{
    auto list_button = new QPushButton(tr("List players"), this);
    list_button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    backup_button = new QPushButton(tr("Backup"), this);
    backup_button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto layout = new QVBoxLayout;
    layout->addWidget(list_button);
    layout->addWidget(backup_button);
    setLayout(layout);

    connect(list_button, SIGNAL(clicked()), this, SLOT(list()));
    connect(backup_button, SIGNAL(clicked()), this, SLOT(backup()));

    setWindowTitle(tr("Control Panel"));
}

void MainWindow::backup()
{
    if (isBackingUp) {
        return;
    }
    isBackingUp = true;
    backup_button->setEnabled(false);

    QProgressDialog* progressDialog = new QProgressDialog(tr("Backing up..."), tr("Cancel"), 0, 100, this);
    progressDialog->setWindowModality(Qt::WindowModal);
    progressDialog->setValue(0);
    progressDialog->show();

    typedef std::remove_reference<decltype(server)>::type server_type;
    auto backupFunc = std::bind(&server_type::backup, &server);

    QThread* thread = new QThread;
    BackupWorker* worker = new BackupWorker(std::move(backupFunc));

    worker->moveToThread(thread);

    connect(thread, &QThread::started, worker, &BackupWorker::doWork);
    connect(worker, &BackupWorker::progressUpdated, progressDialog, &QProgressDialog::setValue);

    connect(worker, &BackupWorker::finished, progressDialog, &QProgressDialog::close);
    connect(worker, &BackupWorker::finished, thread, &QThread::quit);
    connect(worker, &BackupWorker::finished, worker, &BackupWorker::deleteLater);
    connect(thread, &QThread::finished, this, &MainWindow::onBackupFinished);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);

    connect(progressDialog, &QProgressDialog::canceled, this, [worker,thread,progressDialog]() {
        worker->stopWork();
        thread->quit();
        progressDialog->close();
        progressDialog->deleteLater();
    });

    thread->start();
}

void MainWindow::onBackupFinished()
{
    isBackingUp = false;
    backup_button->setEnabled(true);
}

void MainWindow::exit_app()
{
    QApplication::exit();
}