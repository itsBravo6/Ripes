#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "aboutwidget.h"
#include "defines.h"
#include "edittab.h"
#include "loaddialog.h"
#include "memorytab.h"
#include "parser.h"
#include "processorhandler.h"
#include "processortab.h"
#include "registerwidget.h"

#include "fancytabbar/fancytabbar.h"

#include <QDesktopServices>
#include <QFileDialog>
#include <QIcon>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTextStream>

namespace Ripes {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), m_ui(new Ui::MainWindow) {
    m_ui->setupUi(this);
    setWindowTitle("Ripes");
    setWindowIcon(QIcon(":/icons/logo.svg"));
    showMaximized();

    // Create tabs
    m_stackedTabs = new QStackedWidget(this);
    m_ui->centrallayout->addWidget(m_stackedTabs);

    auto* tb = addToolBar("Edit");
    tb->setVisible(false);
    m_editTab = new EditTab(tb, this);
    m_stackedTabs->insertWidget(0, m_editTab);

    tb = addToolBar("Processor");
    tb->setVisible(true);
    m_processorTab = new ProcessorTab(tb, this);
    m_stackedTabs->insertWidget(1, m_processorTab);

    tb = addToolBar("Processor");
    tb->setVisible(false);
    m_memoryTab = new MemoryTab(tb, this);
    m_stackedTabs->insertWidget(2, m_memoryTab);

    // Setup tab bar
    m_ui->tabbar->addFancyTab(QIcon(":/icons/binary-code.svg"), "Editor");
    m_ui->tabbar->addFancyTab(QIcon(":/icons/cpu.svg"), "Processor");
    m_ui->tabbar->addFancyTab(QIcon(":/icons/ram-memory.svg"), "Memory");
    connect(m_ui->tabbar, &FancyTabBar::activeIndexChanged, m_stackedTabs, &QStackedWidget::setCurrentIndex);
    m_ui->tabbar->setActiveIndex(0);

    setupMenus();

    // setup and connect widgets
    connect(m_processorTab, &ProcessorTab::update, this, &MainWindow::updateMemoryTab);
    connect(this, &MainWindow::update, m_processorTab, &ProcessorTab::restart);
    connect(this, &MainWindow::updateMemoryTab, m_memoryTab, &MemoryTab::update);
    connect(m_stackedTabs, &QStackedWidget::currentChanged, m_memoryTab, &MemoryTab::update);
    connect(m_editTab, &EditTab::programChanged, ProcessorHandler::get(), &ProcessorHandler::loadProgram);

    connect(ProcessorHandler::get(), &ProcessorHandler::reqProcessorReset, m_processorTab, &ProcessorTab::reset);
    connect(ProcessorHandler::get(), &ProcessorHandler::reqReloadProgram, m_editTab, &EditTab::emitProgramChanged);
    connect(ProcessorHandler::get(), &ProcessorHandler::print, m_processorTab, &ProcessorTab::printToLog);
    connect(ProcessorHandler::get(), &ProcessorHandler::exit, m_processorTab, &ProcessorTab::processorFinished);

    connect(m_ui->actionAbout, &QAction::triggered, this, &MainWindow::about);
    connect(m_ui->actionOpen_wiki, &QAction::triggered, this, &MainWindow::wiki);
}

void MainWindow::setupMenus() {
    // Edit actions
    const QIcon newIcon = QIcon(":/icons/file.svg");
    auto* newAction = new QAction(newIcon, "New Program", this);
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::newProgramTriggered);
    m_ui->menuFile->addAction(newAction);

    const QIcon loadIcon = QIcon(":/icons/loadfile.svg");
    auto* loadAction = new QAction(loadIcon, "Load Program", this);
    loadAction->setShortcut(QKeySequence::Open);
    connect(loadAction, &QAction::triggered, [=] { this->loadFileTriggered(); });
    m_ui->menuFile->addAction(loadAction);

    m_ui->menuFile->addSeparator();

    auto* examplesMenu = m_ui->menuFile->addMenu("Load Example...");
    setupExamplesMenu(examplesMenu);

    m_ui->menuFile->addSeparator();

    const QIcon saveIcon = QIcon(":/icons/save.svg");
    auto* saveAction = new QAction(saveIcon, "Save File", this);
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveFilesTriggered);
    m_ui->menuFile->addAction(saveAction);

    const QIcon saveAsIcon = QIcon(":/icons/saveas.svg");
    auto* saveAsAction = new QAction(saveAsIcon, "Save File As...", this);
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::saveFilesAsTriggered);
    m_ui->menuFile->addAction(saveAsAction);

    m_ui->menuFile->addSeparator();

    const QIcon exitIcon = QIcon(":/icons/cancel.svg");
    auto* exitAction = new QAction(exitIcon, "Exit", this);
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &MainWindow::close);
    m_ui->menuFile->addAction(exitAction);
}

MainWindow::~MainWindow() {
    delete m_ui;
}

void MainWindow::setupExamplesMenu(QMenu* parent) {
    const auto assemblyExamples = QDir(":/examples/assembly/").entryList(QDir::Files);

    if (!assemblyExamples.isEmpty()) {
        for (const auto& fileName : assemblyExamples) {
            parent->addAction(fileName, [=] {
                LoadFileParams parms;
                parms.filepath = QString(":/examples/assembly/") + fileName;
                parms.type = FileType::Assembly;
                m_editTab->loadFile(parms);
            });
        }
    }
}

void MainWindow::exit() {
    close();
}

void MainWindow::loadFileTriggered() {
    LoadDialog diag;
    if (!diag.exec())
        return;

    m_editTab->loadFile(diag.getParams());
}

void MainWindow::about() {
    AboutWidget about;
    about.exec();
}

void MainWindow::wiki() {
    QDesktopServices::openUrl(QUrl(QString("https://github.com/mortbopet/Ripes/wiki")));
}

namespace {
inline QString removeFileExt(const QString& file) {
    int lastPoint = file.lastIndexOf(".");
    return file.left(lastPoint);
}
void writeTextFile(QFile& file, const QString& data) {
    if (file.open(QIODevice::WriteOnly)) {
        QTextStream stream(&file);
        stream << data;
        file.close();
    }
}

void writeBinaryFile(QFile& file, const QByteArray& data) {
    if (file.open(QIODevice::WriteOnly)) {
        file.write(data);
        file.close();
    }
}

}  // namespace

void MainWindow::saveFilesTriggered() {
    if (m_currentFile.isEmpty()) {
        saveFilesAsTriggered();
    }

    // if (m_ui->actionSave_Source->isChecked()) {
    {
        QFile file(m_currentFile);
        writeTextFile(file, m_editTab->getAssemblyText());
    }
    //}

    // QAction* binaryStoreAction = m_binaryStoreAction->checkedAction();
    // if (binaryStoreAction == m_ui->actionSave_as_flat_binary) {
    {
        QFile file(removeFileExt(m_currentFile) + ".bin");
        writeBinaryFile(file, m_editTab->getBinaryData());
    }
}

void MainWindow::saveFilesAsTriggered() {
    QFileDialog dialog(this);
    dialog.setNameFilter("*.as *.s");
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix(".s");
    dialog.setModal(true);
    if (dialog.exec()) {
        m_currentFile = dialog.selectedFiles()[0];
        saveFilesTriggered();
    }
}

void MainWindow::newProgramTriggered() {
    QMessageBox mbox;
    mbox.setWindowTitle("New Program...");
    mbox.setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    if (!m_editTab->getAssemblyText().isEmpty() && m_currentFile.isEmpty()) {
        // User wrote a program but did not save it to a file yet
        mbox.setText("Save program before creating new file?");
        auto ret = mbox.exec();
        switch (ret) {
            case QMessageBox::Yes: {
                saveFilesAsTriggered();
                break;
            }
            case QMessageBox::No: {
                break;
            }
            case QMessageBox::Cancel: {
                return;
            }
        }
    } else if (!m_currentFile.isEmpty()) {
        // User previously stored a program but may have updated in the meantime - prompt to ask whether the program
        // should be stored to the current file name
        mbox.setText(QString("Save program \"%1\" before creating new file?").arg(m_currentFile));
        auto ret = mbox.exec();
        switch (ret) {
            case QMessageBox::Yes: {
                saveFilesTriggered();
                break;
            }
            case QMessageBox::No: {
                break;
            }
            case QMessageBox::Cancel: {
                return;
            }
        }
    }
    m_currentFile.clear();
    m_editTab->newProgram();
}  // namespace Ripes
}  // namespace Ripes
