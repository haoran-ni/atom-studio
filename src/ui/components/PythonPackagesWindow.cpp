#include "PythonPackagesWindow.h"
#include "PythonShellController.h"
#include "PythonEnvironmentManager.h"
#include <QComboBox>
#include <QFontDatabase>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleFactory>
#include <QTableWidget>
#include <QVBoxLayout>

namespace atom::ui {
PythonPackagesWindow::PythonPackagesWindow(PythonShellController* shell) {
    auto* manager = shell->environment();
    setObjectName("pythonPackagesWindow");
    setWindowTitle(tr("ATOM-STUDIO — Python Packages"));
    setAttribute(Qt::WA_QuitOnClose, false);
    resize(820, 740); setMinimumSize(620, 520);
    auto* content = new QWidget; setCentralWidget(content);
    auto* layout = new QVBoxLayout(content); layout->setContentsMargins(16, 16, 16, 16);
    auto* intro = new QLabel(tr("Install packages into the environment used by Interactive Python.\n"
        "Changes persist across app launches. Installing or switching environments clears shell variables; loaded structures are retained."));
    intro->setWordWrap(true); layout->addWidget(intro);
    auto* environmentRow = new QHBoxLayout;
    environmentRow->addWidget(new QLabel(tr("Environment")));
    auto* environments = new QComboBox; environments->setObjectName("pythonEnvironments");
    auto* create = new QPushButton(tr("New…"));
    environmentRow->addWidget(environments, 1); environmentRow->addWidget(create);
    layout->addLayout(environmentRow);
    auto* path = new QLabel; path->setWordWrap(true); path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    path->setObjectName("pythonEnvironmentPath"); layout->addWidget(path);
    auto* installRow = new QHBoxLayout;
    auto* package = new QLineEdit; package->setObjectName("pythonPackageName"); package->setPlaceholderText(tr("PyPI package name"));
    auto* version = new QLineEdit; version->setObjectName("pythonPackageVersion"); version->setPlaceholderText(tr("Version (optional)")); version->setMaximumWidth(160);
    auto* install = new QPushButton(tr("Install / Upgrade")); install->setObjectName("pythonPackageInstall");
    installRow->addWidget(package, 1); installRow->addWidget(version); installRow->addWidget(install);
    layout->addLayout(installRow);
    auto* presetsRow = new QHBoxLayout;
    presetsRow->addWidget(new QLabel(tr("Package shortcuts:")));
    auto* mace = new QPushButton(tr("MACE")); auto* uma = new QPushButton(tr("UMA / FAIRChem"));
    presetsRow->addWidget(mace); presetsRow->addWidget(uma); presetsRow->addStretch(); layout->addLayout(presetsRow);
    auto* note = new QLabel(tr("MACE installs mace-torch; UMA installs fairchem-core. Model files are downloaded separately.\n"
        "For UMA, obtain model access on Hugging Face, then activate this environment in your terminal and run hf auth login. See Tutorial for examples."));
    note->setWordWrap(true); layout->addWidget(note);
    auto* table = new QTableWidget(0, 3); table->setObjectName("pythonPackages");
    table->setHorizontalHeaderLabels({tr("Package"), tr("Version"), tr("Location")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch); table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection); layout->addWidget(table, 1);
    auto* actions = new QHBoxLayout;
    auto* refresh = new QPushButton(tr("Refresh")); auto* check = new QPushButton(tr("Check Dependencies"));
    auto* remove = new QPushButton(tr("Uninstall Selected")); remove->setObjectName("pythonPackageUninstall");
    auto* cancel = new QPushButton(tr("Cancel Operation"));
    actions->addWidget(refresh); actions->addWidget(check); actions->addWidget(remove); actions->addStretch(); actions->addWidget(cancel);
    layout->addLayout(actions);
    auto* log = new QPlainTextEdit; log->setObjectName("pythonPackageOutput"); log->setReadOnly(true);
    log->setMaximumBlockCount(2000); log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(log, 1);
    auto* status = new QLabel; status->setObjectName("pythonPackageStatus"); status->setWordWrap(true); layout->addWidget(status);
    auto* footer = new QHBoxLayout;
    auto* tutorial = new QPushButton(tr("Tutorial"));
    footer->addStretch(); footer->addWidget(tutorial);
    layout->addLayout(footer);
    const auto update = [=] {
        const bool idle = !manager->busy() && !manager->sessionRunning();
        for (auto* button : {create, install, check, mace, uma}) button->setEnabled(idle);
        refresh->setEnabled(!manager->busy()); cancel->setEnabled(manager->busy());
        environments->setEnabled(idle); package->setEnabled(idle); version->setEnabled(idle);
        const QSignalBlocker blocker(environments);
        environments->clear(); environments->addItems(manager->environments()); environments->setCurrentText(manager->name());
        path->setText(manager->path());
        status->setText(manager->sessionRunning() ? tr("Python is running. Stop the script before changing packages.") : manager->status());
        const auto packages = manager->packages();
        // Keep selection stable across status updates.
        const QString selected = table->currentRow() >= 0 ? table->item(table->currentRow(), 0)->text() : QString{};
        table->setRowCount(packages.size());
        for (int i = 0; i < packages.size(); ++i) {
            const auto entry = packages[i].toObject();
            table->setItem(i, 0, new QTableWidgetItem(entry["name"].toString()));
            table->setItem(i, 1, new QTableWidgetItem(entry["version"].toString()));
            auto* location = new QTableWidgetItem(entry["local"].toBool() ? tr("This environment") : tr("Bundled (read-only)"));
            location->setData(Qt::UserRole, entry["local"].toBool()); table->setItem(i, 2, location);
            if (entry["name"].toString() == selected) table->selectRow(i);
        }
        const int row = table->currentRow();
        remove->setEnabled(idle && row >= 0 && table->item(row, 2)->data(Qt::UserRole).toBool());
    };
    connect(manager, &PythonEnvironmentManager::stateChanged, this, update);
    connect(table, &QTableWidget::itemSelectionChanged, this, [=] {
        const int row = table->currentRow(); auto* item = row >= 0 ? table->item(row, 2) : nullptr;
        remove->setEnabled(!manager->busy() && !manager->sessionRunning() && item && item->data(Qt::UserRole).toBool());
    });
    connect(manager, &PythonEnvironmentManager::output, this, [=](const QString& text) {
        log->moveCursor(QTextCursor::End); log->insertPlainText(text); log->ensureCursorVisible();
    });
    connect(environments, &QComboBox::textActivated, manager, &PythonEnvironmentManager::select);
    connect(create, &QPushButton::clicked, this, [=, this] {
        bool ok; const auto name = QInputDialog::getText(this, tr("New Python Environment"), tr("Name (letters, digits, _ or -):"), QLineEdit::Normal, {}, &ok);
        if (ok) manager->select(name.trimmed());
    });
    connect(install, &QPushButton::clicked, this, [=] { manager->install(package->text(), version->text()); });
    connect(package, &QLineEdit::returnPressed, install, &QPushButton::click);
    connect(mace, &QPushButton::clicked, this, [=] { package->setText("mace-torch"); version->clear(); });
    connect(uma, &QPushButton::clicked, this, [=] { package->setText("fairchem-core"); version->clear(); });
    connect(remove, &QPushButton::clicked, this, [=] { if (table->currentRow() >= 0) manager->uninstall(table->item(table->currentRow(), 0)->text()); });
    connect(refresh, &QPushButton::clicked, manager, &PythonEnvironmentManager::refresh);
    connect(check, &QPushButton::clicked, manager, &PythonEnvironmentManager::check);
    connect(cancel, &QPushButton::clicked, manager, &PythonEnvironmentManager::cancel);
    connect(tutorial, &QPushButton::clicked, shell, &PythonShellController::openTutorial);
    auto* style = QStyleFactory::create("Fusion"); style->setParent(this);
    auto palette = style->standardPalette();
    for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        palette.setColor(group, QPalette::Window, QColor("#f5f5f7"));
        palette.setColor(group, QPalette::Base, Qt::white);
        palette.setColor(group, QPalette::Button, QColor("#e2e3e8"));
        const QColor text(group == QPalette::Disabled ? "#90939b" : "#22242a");
        for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) palette.setColor(group, role, text);
        palette.setColor(group, QPalette::Highlight, QColor("#d8e4f2"));
        palette.setColor(group, QPalette::HighlightedText, QColor("#22242a"));
    }
    setStyle(style); setPalette(palette);
    for (auto* child : findChildren<QWidget*>()) { child->setStyle(style); child->setPalette(palette); }
    setStyleSheet("QPushButton { padding: 5px 10px; background: #e2e3e8; border: 1px solid #c7c9d1; border-radius: 4px; }"
        "QPushButton:hover { background: #d8dae0; } QPushButton:disabled { background: #f0f1f4; color: #90939b; }"
        "QLineEdit, QComboBox { background: white; border: 1px solid #c7c9d1; padding: 5px; }"
        "QComboBox QAbstractItemView { background: white; color: #22242a; selection-background-color: #d8e4f2; }"
        "QHeaderView::section { background: #e8e9ed; color: #22242a; padding: 4px; border: 1px solid #d7d9df; }");
    update();
}
}
