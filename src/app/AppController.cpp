#include "AppController.h"
#include "ui/MainWindow.h"
#include "ui/RegionEditorWindow.h"
#include "ui/SettingsDialog.h"
#include "ui/MiniFloatBar.h"
#include "ui/TrayIconManager.h"

#include <QApplication>
#include <QMessageBox>
#include <QDebug>

AppController::AppController(QObject* parent)
    : QObject(parent)
{
    qDebug() << "Creating MainWindow...";
    m_mainWindow = new MainWindow();
    qDebug() << "Creating RegionEditorWindow...";
    m_regionEditor = new RegionEditorWindow();
    qDebug() << "Creating SettingsDialog...";
    m_settingsDialog = new SettingsDialog(m_mainWindow);
    qDebug() << "Creating MiniFloatBar...";
    m_miniFloatBar = new MiniFloatBar();
    qDebug() << "Creating TrayIconManager...";
    m_trayManager = new TrayIconManager(this);
    qDebug() << "Setting mock regions...";

    // Mock initial regions cho game Elden Ring mẫu
    EZTranslator::TranslationRegion r1;
    r1.id = "roi_dialog_box";
    r1.name = QString::fromUtf8("Khung đối thoại chính");
    r1.orderNumber = 1;
    r1.normalizedRect = {0.22, 0.68, 0.56, 0.16};
    r1.tagColor = QColor(37, 99, 235);

    EZTranslator::TranslationRegion r2;
    r2.id = "roi_item_tooltip";
    r2.name = QString::fromUtf8("Bảng thông tin trang bị");
    r2.orderNumber = 2;
    r2.normalizedRect = {0.03, 0.14, 0.18, 0.66};
    r2.tagColor = QColor(59, 130, 246);

    m_regions.append(r1);
    m_regions.append(r2);

    initConnections();
}

AppController::~AppController()
{
    delete m_mainWindow;
    delete m_regionEditor;
    delete m_miniFloatBar;
}

void AppController::initConnections()
{
    // MainWindow signals
    connect(m_mainWindow, &MainWindow::requestStartTranslation, this, &AppController::onStartTranslation);
    connect(m_mainWindow, &MainWindow::requestOpenRegionEditor, this, &AppController::onOpenRegionEditor);
    connect(m_mainWindow, &MainWindow::requestOpenSettings, this, &AppController::onOpenSettings);

    // RegionEditorWindow signals
    connect(m_regionEditor, &RegionEditorWindow::saved, this, &AppController::onRegionEditorSaved);
    connect(m_regionEditor, &RegionEditorWindow::cancelled, this, &AppController::onRegionEditorCancelled);

    // MiniFloatBar signals
    connect(m_miniFloatBar, &MiniFloatBar::requestStopTranslation, this, &AppController::onStopTranslation);
    connect(m_miniFloatBar, &MiniFloatBar::requestOpenSettings, this, &AppController::onOpenSettings);

    // TrayIconManager signals
    connect(m_trayManager, &TrayIconManager::requestShowMainWindow, this, &AppController::onShowMainWindow);
    connect(m_trayManager, &TrayIconManager::requestToggleTranslation, this, &AppController::onToggleTranslation);
    connect(m_trayManager, &TrayIconManager::requestOpenSettings, this, &AppController::onOpenSettings);
    connect(m_trayManager, &TrayIconManager::requestQuit, this, &AppController::onQuit);

    // SettingsDialog signals
    connect(m_settingsDialog, &SettingsDialog::settingsSaved, this, &AppController::onSettingsSaved);
}

void AppController::start()
{
    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();
}

void AppController::onStartTranslation()
{
    m_state = EZTranslator::TranslationState::Running;
    m_mainWindow->onTranslationStarted();
    m_mainWindow->hide();

    m_miniFloatBar->setRunning(true);
    m_miniFloatBar->show();

    m_trayManager->updateState(true);
}

void AppController::onStopTranslation()
{
    m_state = EZTranslator::TranslationState::Idle;
    m_mainWindow->onTranslationStopped();
    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();

    m_miniFloatBar->setRunning(false);
    m_miniFloatBar->hide();

    m_trayManager->updateState(false);
}

void AppController::onToggleTranslation()
{
    if (m_state == EZTranslator::TranslationState::Running) {
        onStopTranslation();
    } else {
        onStartTranslation();
    }
}

void AppController::onOpenRegionEditor()
{
    m_regionEditor->loadRegions(m_regions);
    m_regionEditor->show();
    m_regionEditor->raise();
    m_regionEditor->activateWindow();
}

void AppController::onRegionEditorSaved(const QList<EZTranslator::TranslationRegion>& regions)
{
    m_regions = regions;
    m_regionEditor->hide();
    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();
}

void AppController::onRegionEditorCancelled()
{
    m_regionEditor->hide();
    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();
}

void AppController::onOpenSettings()
{
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void AppController::onSettingsSaved(const EZTranslator::AppSettings& settings)
{
    m_settings = settings;
}

void AppController::onShowMainWindow()
{
    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();
}

void AppController::onQuit()
{
    qApp->quit();
}
