#include "InteractiveShellTutorialWindow.h"
#include <QAbstractTextDocumentLayout>
#include <QFile>
#include <QFontDatabase>
#include <QLabel>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStyle>
#include <QStyleFactory>
#include <QTextBrowser>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace atom::ui {
namespace {
// Let the outer scroll area handle the whole guide, including long code blocks.
class TutorialText : public QTextBrowser {
public:
    explicit TutorialText(const QString& resource, QWidget* parent) : QTextBrowser(parent) {
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        setOpenExternalLinks(false);
        auto textFont = font();
        textFont.setPointSize(12);
        setFont(textFont);
        document()->setDocumentMargin(20);
        document()->setDefaultStyleSheet(QString(
            "h3 { font-size: 13pt; margin-top: 20px; margin-bottom: 8px; }"
            "p, li { line-height: 140%; }"
            "pre { margin-top: 0px; margin-bottom: 0px; }"
            "code, .control { background-color: #f3f3f3; }"
            "pre, code { font-family: '%1'; font-size: 11pt; }")
            .arg(QFontDatabase::systemFont(QFontDatabase::FixedFont).family()));
        connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
                this, [this](const QSizeF& size) {
            const int height = static_cast<int>(std::ceil(size.height()));
            if (height != minimumHeight()) setFixedHeight(height);
        });
        QFile file(resource);
        if (file.open(QIODevice::ReadOnly)) {
            auto html = QString::fromUtf8(file.readAll());
            // QTextDocument does not support CSS padding on <pre>. A table cell
            // provides real padding while keeping the source examples easy to edit.
            html.replace("<pre>", "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"12\""
                                  " bgcolor=\"#f3f3f3\"><tr><td><pre>");
            html.replace("</pre>", "</pre></td></tr></table>");
            setHtml(html);
        }
    }
protected:
    void resizeEvent(QResizeEvent* event) override {
        QTextBrowser::resizeEvent(event);
        document()->setTextWidth(viewport()->width());
    }
};
}

InteractiveShellTutorialWindow::InteractiveShellTutorialWindow() {
    setObjectName("interactiveShellTutorialWindow");
    setWindowTitle(tr("ATOM-STUDIO — Interactive Shell Tutorial"));
    setAttribute(Qt::WA_QuitOnClose, false);
    resize(820, 740);
    setMinimumSize(560, 420);

    auto* style = QStyleFactory::create("Fusion");
    style->setParent(this);
    auto palette = style->standardPalette();
    palette.setColor(QPalette::Window, QColor("#f7f7f7"));
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::Text, QColor("#262626"));
    palette.setColor(QPalette::WindowText, QColor("#262626"));
    palette.setColor(QPalette::ButtonText, QColor("#262626"));
    palette.setColor(QPalette::Highlight, QColor("#dedede"));
    palette.setColor(QPalette::HighlightedText, QColor("#111111"));
    setStyle(style);
    setPalette(palette);

    auto* content = new QWidget;
    setCentralWidget(content);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 24, 24, 16);
    layout->setSpacing(8);
    auto* title = new QLabel(tr("Python Tutorial"));
    auto titleFont = title->font();
    titleFont.setPointSize(22);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);
    auto* subtitle = new QLabel(tr("Open a section to get started. Select any example to copy it."));
    subtitle->setWordWrap(true);
    subtitle->setObjectName("tutorialSubtitle");
    layout->addWidget(subtitle);
    layout->addSpacing(12);

    auto* scroll = new QScrollArea;
    scroll->setObjectName("tutorialScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* sections = new QWidget;
    auto* sectionsLayout = new QVBoxLayout(sections);
    sectionsLayout->setContentsMargins(0, 0, 0, 0);
    sectionsLayout->setSpacing(12);

    const auto addSection = [&](const QString& name, const QString& id, const QString& resource) {
        auto* section = new QFrame;
        section->setObjectName("tutorialSection");
        auto* sectionLayout = new QVBoxLayout(section);
        sectionLayout->setContentsMargins(0, 0, 0, 0);
        sectionLayout->setSpacing(0);
        auto* toggle = new QToolButton;
        toggle->setObjectName(id + "Toggle");
        toggle->setText(name);
        toggle->setCheckable(true);
        toggle->setChecked(false);
        toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        toggle->setIcon(QIcon(":/icons/chevron-right.svg"));
        toggle->setIconSize(QSize(16, 16));
        toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        toggle->setCursor(Qt::PointingHandCursor);
        auto* body = new TutorialText(resource, section);
        body->setObjectName(id + "Text");
        body->setAccessibleName(name);
        body->hide();
        sectionLayout->addWidget(toggle);
        sectionLayout->addWidget(body);
        connect(toggle, &QToolButton::toggled, this, [toggle, body](bool expanded) {
            toggle->setIcon(QIcon(expanded ? ":/icons/chevron-down.svg" : ":/icons/chevron-right.svg"));
            body->setVisible(expanded);
        });
        sectionsLayout->addWidget(section);
    };
    addSection(tr("1. Usage of interactive python shell"), "tutorialUsage", ":/help/interactive-shell.html");
    addSection(tr("2. Environment management"), "tutorialEnvironment", ":/help/python-environments.html");
    addSection(tr("3. Trouble shooting"), "tutorialTroubleshooting", ":/help/python-troubleshooting.html");
    sectionsLayout->addStretch();
    scroll->setWidget(sections);
    layout->addWidget(scroll, 1);

    setStyleSheet(
        "QWidget { color: #262626; }"
        "QLabel#tutorialSubtitle { color: #696969; }"
        "QScrollArea, QScrollArea > QWidget > QWidget { background: #f7f7f7; }"
        "QFrame#tutorialSection { background: white; border: 1px solid #dedede; border-radius: 8px; }"
        "QToolButton { border: none; border-radius: 7px; padding: 16px; text-align: left;"
        " background: white; font-size: 14px; font-weight: 600; }"
        "QToolButton:hover, QToolButton:checked { background: #f0f0f0; }"
        "QToolButton:pressed { background: #e7e7e7; }"
        "QToolButton:focus { border: 1px solid #919191; }"
        "QTextBrowser { background: white; border: none; }");
    for (auto* child : findChildren<QWidget*>()) {
        child->setStyle(style);
        child->setPalette(palette);
    }
}
}
