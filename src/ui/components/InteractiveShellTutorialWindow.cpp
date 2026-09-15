#include "InteractiveShellTutorialWindow.h"
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QStyle>
#include <QStyleFactory>
#include <QTextBrowser>
#include <QTextCursor>
#include <QVBoxLayout>

namespace atom::ui {
InteractiveShellTutorialWindow::InteractiveShellTutorialWindow() {
    setObjectName("interactiveShellTutorialWindow");
    setWindowTitle(tr("ATOM-STUDIO — Interactive Shell Tutorial"));
    setAttribute(Qt::WA_QuitOnClose, false);
    resize(880, 780);
    setMinimumSize(560, 420);

    auto* content = new QWidget;
    setCentralWidget(content);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(16, 16, 16, 16);
    auto* toolbar = new QHBoxLayout;
    auto* contents = new QPushButton(tr("Contents"));
    auto* search = new QLineEdit;
    search->setObjectName("tutorialSearch");
    search->setPlaceholderText(tr("Find in tutorial…"));
    search->setAccessibleName(tr("Find in tutorial"));
    search->setClearButtonEnabled(true);
    auto* previous = new QPushButton(tr("Previous"));
    auto* next = new QPushButton(tr("Next"));
    toolbar->addWidget(contents);
    toolbar->addWidget(search, 1);
    toolbar->addWidget(previous);
    toolbar->addWidget(next);
    layout->addLayout(toolbar);

    auto* browser = new QTextBrowser;
    browser->setObjectName("shellTutorialText");
    browser->setAccessibleName(tr("Interactive Shell Tutorial"));
    browser->setReadOnly(true);
    browser->setOpenExternalLinks(false);
    auto font = browser->font();
    font.setPointSize(12);
    browser->setFont(font);
    browser->document()->setDocumentMargin(24);
    browser->document()->setDefaultStyleSheet(QString("pre, code { font-family: '%1'; }")
        .arg(QFontDatabase::systemFont(QFontDatabase::FixedFont).family()));
    browser->setSource(QUrl("qrc:/help/interactive-shell.html"));
    layout->addWidget(browser, 1);
    auto* status = new QLabel(tr("Select text or code to copy. Use Command/Ctrl+F to search."));
    status->setWordWrap(true);
    layout->addWidget(status);

    const auto find = [browser, search, status](bool backwards) {
        if (search->text().isEmpty()) return;
        const auto flags = backwards ? QTextDocument::FindBackward : QTextDocument::FindFlags{};
        bool found = browser->find(search->text(), flags);
        if (!found) {
            const auto saved = browser->textCursor();
            auto cursor = saved;
            cursor.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
            browser->setTextCursor(cursor);
            found = browser->find(search->text(), flags);
            if (!found) browser->setTextCursor(saved);
        }
        status->setText(found ? tr("Match found. Use Previous or Next to continue (wraps at the ends).")
                              : tr("No matches for “%1”.").arg(search->text()));
    };
    connect(next, &QPushButton::clicked, this, [find] { find(false); });
    connect(previous, &QPushButton::clicked, this, [find] { find(true); });
    connect(search, &QLineEdit::returnPressed, this, [find] { find(false); });
    connect(search, &QLineEdit::textChanged, this, [previous, next, status](const QString& text) {
        previous->setEnabled(!text.isEmpty());
        next->setEnabled(!text.isEmpty());
        status->setText(tr("Select text or code to copy. Use Command/Ctrl+F to search."));
    });
    previous->setEnabled(false);
    next->setEnabled(false);
    connect(contents, &QPushButton::clicked, this, [browser] { browser->scrollToAnchor("contents"); });
    auto* findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, this, [search] { search->setFocus(); search->selectAll(); });

    // Resolve a local light style for the text window, including native containers.
    auto* style = QStyleFactory::create("Fusion");
    style->setParent(this);
    auto palette = style->standardPalette();
    palette.setColor(QPalette::Window, QColor("#f5f5f7"));
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::Text, QColor("#22242a"));
    palette.setColor(QPalette::WindowText, QColor("#22242a"));
    palette.setColor(QPalette::ButtonText, QColor("#22242a"));
    palette.setColor(QPalette::Highlight, QColor("#d8e4f2"));
    palette.setColor(QPalette::HighlightedText, QColor("#22242a"));
    setStyle(style);
    setPalette(palette);
    setStyleSheet("QWidget { color: #22242a; } QWidget:disabled { color: #90939b; }"
                  "QTextBrowser, QLineEdit { background: white; border: 1px solid #c7c9d1; }"
                  "QLineEdit { padding: 5px; }"
                  "QPushButton { padding: 5px 12px; background: #e2e3e8; border: 1px solid #c7c9d1; border-radius: 4px; }"
                  "QPushButton:hover { background: #d8dae0; }");
    for (auto* child : findChildren<QWidget*>()) {
        child->setStyle(style);
        child->setPalette(palette);
    }
}
}
