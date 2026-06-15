//
// Copyright (c) 2026 Gary Huang (ghuang dot nctu at gmail dot com)
//

#include "mainwindow.h"
#include "backupWorker.h"

#include <gh/game/minecraft/server.hpp>
#include <gh/system/uploader.hpp>
#include <gh/utility/logger.hpp>

#include <iomanip>

#include <QApplication>
#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QPushButton>
#include <QProgressDialog>
#include <QSettings>
#include <QThread>

MainWindow::MainWindow(bedrock_server& server_, QWidget *parent)
    : server(server_)
    , QWidget(parent)
{  
    auto list_button = new QPushButton(tr("List players"), this);
    list_button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    backup_methods = new QComboBox(this);
    backup_methods->addItem(tr("Default"));
    backup_methods->addItem(tr("Builtin"));
    backup_methods->addItem(tr("Script"));
    backup_button = new QPushButton(tr("Backup"), this);
    backup_button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    loadSettings();

    auto layout = new QVBoxLayout;
    layout->addWidget(list_button);
    layout->addWidget(backup_methods);
    layout->addWidget(backup_button);
    setLayout(layout);

    connect(list_button, SIGNAL(clicked()), this, SLOT(list()));
    connect(backup_methods, SIGNAL(currentIndexChanged(int)), this, SLOT(saveSettings()));
    connect(backup_button, SIGNAL(clicked()), this, SLOT(backup()));

    setWindowTitle(tr("Control Panel"));
}

void MainWindow::saveSettings()
{
    QString iniPath = QCoreApplication::applicationDirPath() + "/config.ini";
    QSettings settings(iniPath, QSettings::IniFormat);
    settings.setIniCodec("UTF-8");

    settings.setValue("UserInterface/BackupMethod", backup_methods->currentText());

    settings.sync();
}

void MainWindow::loadSettings()
{
    QString iniPath = QCoreApplication::applicationDirPath() + "/config.ini";
    if (QFileInfo::exists(iniPath)) {
        QSettings settings(iniPath, QSettings::IniFormat);
        settings.setIniCodec("UTF-8");

        QString backupMethod = settings.value("UserInterface/BackupMethod", tr("Default")).toString();
        int comboBoxIndex = backup_methods->findText(backupMethod);
        if (comboBoxIndex != -1) {
            backup_methods->setCurrentIndex(comboBoxIndex);
        }
    }
}

static std::string currentDateTime()
{
    std::time_t now = std::time(nullptr);
    char buf[20];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    return std::string(buf);
}

class MyLogger : public gh::utility::logger
{
public:
    using logger::logger;

    void flush(const std::string &message) override
    {
        std::string timestamped_message = "[" + currentDateTime() + "] " + message;
        logger::flush(timestamped_message);
    }
};

int builtin_backup(std::function<void(int)> progress_callback, std::atomic<bool>& shouldStop)
{
    const auto input_folder = "../current/";
    const auto compressed_file = "\\\\NAS\\backup\\minecraft.tar.gz";
    bool verbose = false;

    MyLogger shared_logger(std::cerr);
    gh::system::compress_uploader uploader;
    if (verbose) {
        uploader.set_logger(shared_logger);
    }

    uploader.set_progress_callback([&](
            size_t processed_input_bytes,
            size_t total_compressed_bytes,
            size_t total_uploaded_bytes,
            double compression_percent,
            double upload_percent,
            double overall_percent,
            bool finished) {

        if (shouldStop.load()) {
            uploader.cancel();
            return;
        }
        // auto message = shared_logger.stream();
        // message << std::fixed << std::setprecision(1)
        //         << "Progress: input=" << processed_input_bytes
        //         << " bytes (" << compression_percent << "% compressed), "
        //         << "uploaded=" << total_uploaded_bytes
        //         << " bytes (" << upload_percent << "% uploaded, "
        //         << overall_percent << "% overall)";
        progress_callback(static_cast<int>(overall_percent));
        // if (finished) {
        //     message << " [done]";
        // }
        // message << std::endl;
    });

    bool result = uploader.upload(input_folder, compressed_file);
    if (!result) {
        shared_logger.stream() << "[Main] Operation failed." << std::endl;
        return 1;
    }

    return 0;
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

    QThread* thread = new QThread;
    BackupWorker* worker;
    if (backup_methods->currentText() == tr("Script")) {
        worker = new BackupWorker([this](std::function<void(int)>, std::atomic<bool>&) {
            return server.backup();
        });
    } else {
        worker = new BackupWorker(builtin_backup);
    }

    worker->moveToThread(thread);

    connect(thread, &QThread::started, worker, &BackupWorker::doWork);
    connect(worker, &BackupWorker::progressUpdated, progressDialog, &QProgressDialog::setValue);
    connect(progressDialog, &QProgressDialog::canceled, worker, &BackupWorker::stopWork);
    
    connect(worker, &BackupWorker::finished, progressDialog, &QProgressDialog::close);
    connect(worker, &BackupWorker::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, progressDialog, &QProgressDialog::deleteLater);
    connect(thread, &QThread::finished, this, &MainWindow::onBackupFinished);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    
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