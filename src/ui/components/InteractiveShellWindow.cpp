#include "InteractiveShellWindow.h"
#include "PythonShellController.h"
#include "PythonEnvironmentManager.h"
#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QSyntaxHighlighter>
#include <QStyleFactory>
#include <QTableWidget>
#include <QTextBlock>
#include <QVBoxLayout>

namespace atom::ui {
namespace {
class PythonHighlighter : public QSyntaxHighlighter {
public:
    explicit PythonHighlighter(QTextDocument* document) : QSyntaxHighlighter(document) {}
    void highlightBlock(const QString& text) override {
        const auto highlight = [this, &text](const QString& pattern, const QColor& color) {
            auto matches = QRegularExpression(pattern).globalMatch(text);
            while (matches.hasNext()) {
                const auto match = matches.next();
                setFormat(match.capturedStart(), match.capturedLength(), color);
            }
        };
        highlight("\\b(and|as|assert|async|await|break|class|continue|def|del|elif|else|except|False|finally|for|from|global|if|import|in|is|lambda|None|nonlocal|not|or|pass|raise|return|True|try|while|with|yield)\\b", QColor("#6d3696"));
        highlight("\\b[0-9]+(?:\\.[0-9]+)?\\b", QColor("#966011"));
        highlight("\\b(STRUCT_[0-9]+|studio|np|ase)\\b", QColor("#155b95"));
        highlight(R"(('([^'\\]|\\.)*'|"([^"\\]|\\.)*"))", QColor("#28704b"));
        highlight("#.*$", QColor("#777b83"));
    }
};

class PythonEditor : public QPlainTextEdit {
public:
    using QPlainTextEdit::QPlainTextEdit;
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Tab && !event->modifiers()) {
            insertPlainText("    ");
            return;
        }
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !event->modifiers()) {
            const auto before = textCursor().block().text().left(textCursor().positionInBlock());
            const auto indent = QRegularExpression("^\\s*").match(before).captured();
            insertPlainText('\n' + indent + (before.trimmed().endsWith(':') ? "    " : ""));
            return;
        }
        QPlainTextEdit::keyPressEvent(event);
    }
};

const QStringList examples{
    "# Modify a loaded structure, then click Run.\n# Changes are published after a successful run.\nprint(STRUCT_0)\nSTRUCT_0.positions[:, 2] += 0.5\n",
    "from ase.build import bulk\n\n# studio.add returns the registered Atoms object and prints its STRUCT_N name.\ncopper = studio.add(bulk('Cu', cubic=True) * (2, 2, 2))\nstudio.show(copper)\n",
    "from ase.build import bulk\nfrom ase.calculators.emt import EMT\nfrom ase.optimize import BFGS\n\n# Self-contained example; creates a new copper document.\natoms = studio.add(bulk('Cu', cubic=True) * (2, 2, 2))\natoms.rattle(stdev=0.1, seed=7)\natoms.calc = EMT()\noptimizer = BFGS(atoms, logfile='-')\noptimizer.attach(studio.update, interval=1, atoms=atoms)\noptimizer.run(fmax=0.05, steps=200)\n",
    "from ase.calculators.emt import EMT\nfrom ase.optimize import BFGS\n\n# EMT example for a loaded copper structure. Choose a suitable\n# calculator for your material before running.\nSTRUCT_0.calc = EMT()\noptimizer = BFGS(STRUCT_0, logfile='-')\noptimizer.attach(studio.update, interval=1, atoms=STRUCT_0)\noptimizer.run(fmax=0.05, steps=200)\n"
};
}

InteractiveShellWindow::InteractiveShellWindow(PythonShellController* controller)
    : m_controller(controller) {
    setObjectName("interactiveShellWindow");
    setWindowTitle(tr("Atom Studio — Interactive Python"));
    resize(940, 820);
    setMinimumSize(660, 520);
    setAttribute(Qt::WA_QuitOnClose, false);
    // QQuickStyle does not style QWidget windows. Use a local light palette so
    // macOS dark appearance cannot turn text white on this window's light surface.
    auto* widgetStyle = QStyleFactory::create("Fusion");
    widgetStyle->setParent(this);
    setStyle(widgetStyle);
    QPalette palette = widgetStyle->standardPalette();
    for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        palette.setColor(group, QPalette::Window, QColor("#f5f5f7"));
        palette.setColor(group, QPalette::Base, Qt::white);
        palette.setColor(group, QPalette::AlternateBase, QColor("#f0f1f4"));
        palette.setColor(group, QPalette::Button, QColor("#e2e3e8"));
        const QColor text(group == QPalette::Disabled ? "#90939b" : "#22242a");
        for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
            palette.setColor(group, role, text);
        palette.setColor(group, QPalette::Highlight, QColor("#d8e4f2"));
        palette.setColor(group, QPalette::HighlightedText, QColor("#22242a"));
    }
    setPalette(palette);
    setStyleSheet("QWidget { color: #22242a; } QWidget:disabled { color: #90939b; }"
                  "QMainWindow, QWidget#shellContent { background: #f5f5f7; }"
                  "QPushButton { padding: 5px 12px; background: #e2e3e8; border: 1px solid #c7c9d1; border-radius: 4px; }"
                  "QPushButton:hover { background: #d8dae0; } QPushButton:pressed { background: #c7c9d1; }"
                  "QPushButton:disabled { background: #f0f1f4; color: #90939b; }"
                  "QComboBox, QSpinBox { background: white; border: 1px solid #c7c9d1; padding: 4px; }"
                  "QComboBox QAbstractItemView { background: white; selection-background-color: #d8e4f2; }"
                  "QHeaderView::section { background: #e8e9ed; color: #22242a; padding: 3px; border: 1px solid #d7d9df; }"
                  "QPlainTextEdit, QTableWidget { background: white; color: #22242a; border: 1px solid #c7c9d1; }");
    auto* content = new QWidget;
    content->setObjectName("shellContent");
    setCentralWidget(content);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);
    auto* title = new QLabel(tr("Interactive Python"));
    QFont titleFont = title->font(); titleFont.setPointSize(18); titleFont.setBold(true); title->setFont(titleFont);
    layout->addWidget(title);
    auto* help = new QLabel(tr("Loaded working structures are ASE Atoms named STRUCT_N. Variables persist between runs.\n"
                              "Use studio.update(atoms) during loops, or attach it to an ASE optimizer for live visualization."));
    help->setWordWrap(true);
    layout->addWidget(help);
    auto* environmentRow = new QHBoxLayout;
    auto* environmentName = new QLabel;
    environmentName->setObjectName("shellEnvironmentName");
    auto* packages = new QPushButton(tr("Manage Packages"));
    environmentRow->addWidget(environmentName, 1); environmentRow->addWidget(packages);
    layout->addLayout(environmentRow);
    const auto updateEnvironment = [=] { environmentName->setText(tr("Environment: %1").arg(controller->environment()->name())); };
    connect(controller->environment(), &PythonEnvironmentManager::stateChanged, this, updateEnvironment);
    connect(packages, &QPushButton::clicked, controller, &PythonShellController::openPackages);
    updateEnvironment();
    m_structures = new QTableWidget(0, 4);
    m_structures->setObjectName("shellStructures");
    m_structures->setHorizontalHeaderLabels({tr("Variable"), tr("Source"), tr("Atoms"), tr("View")});
    m_structures->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_structures->verticalHeader()->hide();
    m_structures->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_structures->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_structures->setMaximumHeight(125);
    layout->addWidget(m_structures);

    auto* toolbar = new QHBoxLayout;
    m_run = new QPushButton(tr("Run  ⌘/Ctrl+Enter")); m_run->setObjectName("shellRun");
    m_stop = new QPushButton(tr("Stop")); m_stop->setObjectName("shellStop");
    m_restart = new QPushButton(tr("Restart Python"));
    toolbar->addWidget(m_run); toolbar->addWidget(m_stop); toolbar->addWidget(m_restart);
    toolbar->addStretch();
    auto* example = new QComboBox;
    example->addItems({tr("Insert example…"), tr("Modify STRUCT_0"), tr("Create copper"),
                       tr("Live relaxation demo"), tr("Relax loaded copper")});
    toolbar->addWidget(example);
    auto* open = new QPushButton(tr("Open .py"));
    auto* save = new QPushButton(tr("Save .py"));
    toolbar->addWidget(open); toolbar->addWidget(save);
    layout->addLayout(toolbar);

    auto* splitter = new QSplitter(Qt::Vertical);
    m_editor = new PythonEditor;
    m_editor->setObjectName("shellEditor");
    const auto mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_editor->setFont(mono);
    m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_editor->setTabStopDistance(QFontMetricsF(mono).horizontalAdvance(' ') * 4);
    m_editor->setPlainText("# STRUCT_N contains the current working geometry of document N.\n# Start with an example above, or write your own ASE code.\n\n");
    new PythonHighlighter(m_editor->document());
    splitter->addWidget(m_editor);
    auto* outputPanel = new QWidget;
    auto* outputLayout = new QVBoxLayout(outputPanel);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    auto* outputHeader = new QHBoxLayout;
    outputHeader->addWidget(new QLabel(tr("Output")));
    outputHeader->addStretch();
    auto* clear = new QPushButton(tr("Clear output"));
    outputHeader->addWidget(clear);
    outputLayout->addLayout(outputHeader);
    m_output = new QPlainTextEdit;
    m_output->setObjectName("shellOutput");
    m_output->setFont(mono); m_output->setReadOnly(true);
    m_output->setMaximumBlockCount(3000);
    outputLayout->addWidget(m_output);
    splitter->addWidget(outputPanel);
    splitter->setSizes({400, 200});
    layout->addWidget(splitter, 1);

    auto* settings = new QHBoxLayout;
    m_chooseDirectory = new QPushButton(tr("Working directory…"));
    m_directory = new QLabel;
    m_directory->setTextInteractionFlags(Qt::TextSelectableByMouse);
    settings->addWidget(m_chooseDirectory); settings->addWidget(m_directory, 1);
    settings->addWidget(new QLabel(tr("Live updates/s")));
    m_fps = new QSpinBox; m_fps->setRange(1, 30); m_fps->setValue(controller->fps());
    settings->addWidget(m_fps);
    layout->addLayout(settings);
    m_status = new QLabel;
    m_status->setObjectName("shellStatus");
    layout->addWidget(m_status);

    connect(m_run, &QPushButton::clicked, this, [this] { m_controller->run(m_editor->toPlainText()); });
    auto* shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(shortcut, &QShortcut::activated, m_run, &QPushButton::click);
    connect(m_stop, &QPushButton::clicked, controller, &PythonShellController::stop);
    connect(m_restart, &QPushButton::clicked, controller, &PythonShellController::restart);
    connect(clear, &QPushButton::clicked, m_output, &QPlainTextEdit::clear);
    connect(m_chooseDirectory, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getExistingDirectory(this, tr("Python Working Directory"), m_controller->directory());
        if (!path.isEmpty()) m_controller->setDirectory(path);
    });
    connect(m_fps, &QSpinBox::valueChanged, controller, &PythonShellController::setFps);
    connect(example, &QComboBox::activated, this, [this, example](int index) {
        if (index > 0) {
            auto cursor = m_editor->textCursor();
            cursor.movePosition(QTextCursor::End);
            cursor.insertText('\n' + examples[index - 1]);
            m_editor->setTextCursor(cursor);
        }
        example->setCurrentIndex(0);
    });
    connect(open, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("Open Python Script"), m_controller->directory(), "Python (*.py);;All Files (*)");
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            // Append rather than discard an unsaved editor buffer.
            m_editor->appendPlainText(QString::fromUtf8(file.readAll()));
        }
    });
    connect(save, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getSaveFileName(this, tr("Save Python Script"), m_controller->directory() + "/script.py", "Python (*.py)");
        if (path.isEmpty()) return;
        QSaveFile file(path);
        const auto code = m_editor->toPlainText().toUtf8();
        if (!file.open(QIODevice::WriteOnly) || file.write(code) != code.size() || !file.commit())
            m_output->appendPlainText(tr("Could not save script: %1").arg(file.errorString()));
    });
    connect(controller, &PythonShellController::output, this, [this](const QString& text, bool error) {
        auto cursor = m_output->textCursor(); cursor.movePosition(QTextCursor::End);
        QTextCharFormat format; format.setForeground(error ? QColor("#a52a2a") : QColor("#33353c"));
        cursor.insertText(text, format); m_output->setTextCursor(cursor);
        if (m_output->document()->characterCount() > 300000) {
            cursor.movePosition(QTextCursor::Start);
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, 100000);
            cursor.removeSelectedText();
        }
        m_output->ensureCursorVisible();
    });
    connect(controller, &PythonShellController::stateChanged, this, &InteractiveShellWindow::refresh);
    connect(controller, &PythonShellController::structuresChanged, this, &InteractiveShellWindow::refreshStructures);
    // QWidget palettes/styles do not consistently inherit through native Qt
    // containers on macOS. Resolve the local palette for every editor control.
    for (auto* child : findChildren<QWidget*>()) {
        child->setStyle(widgetStyle);
        child->setPalette(palette);
    }
    refresh(); refreshStructures();
}

void InteractiveShellWindow::refresh() {
    const bool running = m_controller->running();
    m_run->setEnabled(!running); m_stop->setEnabled(running);
    m_restart->setEnabled(!running);
    m_chooseDirectory->setEnabled(!running); m_fps->setEnabled(!running);
    m_directory->setText(m_controller->directory());
    m_status->setText(m_controller->status());
}

void InteractiveShellWindow::refreshStructures() {
    const auto entries = m_controller->structures();
    m_structures->setRowCount(entries.size());
    for (int row = 0; row < entries.size(); ++row) {
        const auto entry = entries[row].toMap();
        const QStringList values{entry["name"].toString(), entry["file"].toString(), entry["atoms"].toString(),
                                 entry["active"].toBool() ? tr("Visible") : ""};
        for (int column = 0; column < values.size(); ++column)
            m_structures->setItem(row, column, new QTableWidgetItem(values[column]));
    }
}
}
