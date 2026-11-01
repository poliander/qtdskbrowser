#include "basictoken.h"
#include "cpclistingview.h"
#include "dskimage.h"

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QIcon>
#include <QKeySequence>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

// Highlights the whole row the mouse is currently over, instead of just the
// single cell QTreeWidget would otherwise highlight on its own.
class RowHoverHighlighter : public QObject
{
public:
    explicit RowHoverHighlighter(QTreeWidget *tree)
        : QObject(tree)
        , m_tree(tree)
    {
        tree->setMouseTracking(true);
        tree->viewport()->installEventFilter(this);
        QObject::connect(tree, &QTreeWidget::itemEntered, this, [this](QTreeWidgetItem *item, int) {
            setHoveredItem(item);
        });
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_tree->viewport() && event->type() == QEvent::Leave)
            setHoveredItem(nullptr);
        return QObject::eventFilter(watched, event);
    }

private:
    void setHoveredItem(QTreeWidgetItem *item)
    {
        if (item == m_hoveredItem)
            return;
        applyHighlight(m_hoveredItem, false);
        m_hoveredItem = item;
        applyHighlight(m_hoveredItem, true);
    }

    void applyHighlight(QTreeWidgetItem *item, bool highlight)
    {
        if (!item)
            return;
        QBrush brush;
        if (highlight) {
            QColor color = m_tree->palette().color(QPalette::Highlight);
            color.setAlpha(60);
            brush = color;
        }
        for (int col = 0; col < m_tree->columnCount(); ++col)
            item->setBackground(col, brush);
    }

    QTreeWidget *m_tree;
    QTreeWidgetItem *m_hoveredItem = nullptr;
};

// Populates the navigation tree: one top-level "USER n" node per user level
// present on the disk, each with its files as children. The file's index
// into image.files() is stashed on each child item so it can be looked back
// up (e.g. on double-click) without re-deriving it from the tree position.
void showDirectory(QTreeWidget *tree, const DskImage &image)
{
    tree->clear();

    const QVector<DskFileEntry> &files = image.files();
    QMap<int, QVector<int>> fileIndicesByUser; // QMap keeps user numbers in ascending order
    for (int i = 0; i < files.size(); ++i)
        fileIndicesByUser[files.at(i).user].append(i);

    for (auto it = fileIndicesByUser.constBegin(); it != fileIndicesByUser.constEnd(); ++it) {
        QTreeWidgetItem *userItem = new QTreeWidgetItem(tree);
        userItem->setText(0, QString("USER %1").arg(it.key()));

        for (int fileIndex : it.value()) {
            const DskFileEntry &file = files.at(fileIndex);
            QString filename = file.extension.isEmpty() ? file.name
                                                          : file.name + "." + file.extension;
            QTreeWidgetItem *fileItem = new QTreeWidgetItem(userItem);
            fileItem->setText(0, filename);
            fileItem->setData(0, Qt::UserRole, fileIndex);

            // Only .BAS files can actually be opened; grey out the rest a bit
            // so it is clear at a glance which ones can't.
            if (file.extension.compare("BAS", Qt::CaseInsensitive) != 0)
                fileItem->setForeground(0, tree->palette().color(QPalette::Disabled, QPalette::Text));
        }
    }

    tree->expandAll();
}

// Shortens a DSK file's name for display as a tab label, keeping only its
// beginning when it would otherwise be too long.
QString tabTextForFile(const QString &filePath)
{
    static const int maxLength = 20;
    QString name = QFileInfo(filePath).fileName();
    if (name.length() > maxLength)
        name = name.left(maxLength) + "…";
    return name;
}

// The text shown in the status bar below the tabs for a file: its size and
// any attributes it has.
QString statusTextForFile(const DskFileEntry &file)
{
    QStringList attributes;
    if (file.readOnly)
        attributes << "Read-only";
    if (file.system)
        attributes << "System";
    if (file.protectedFile)
        attributes << "Protected";

    QString text = QString("%1 bytes").arg(file.size);
    if (!attributes.isEmpty())
        text += " — " + attributes.join(", ");
    return text;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.setWindowTitle("qtdskbrowser");
    window.resize(900, 600);

    QIcon appIcon = QIcon::fromTheme("media-floppy",
        app.style()->standardIcon(QStyle::SP_DriveFDIcon));
    app.setWindowIcon(appIcon);
    window.setWindowIcon(appIcon);

    QIcon openIcon = QIcon::fromTheme("document-open",
        app.style()->standardIcon(QStyle::SP_DialogOpenButton));

    QToolButton *placeholderButton = new QToolButton;
    placeholderButton->setIcon(openIcon);
    placeholderButton->setIconSize(QSize(96, 96));
    placeholderButton->setText("Open disk image...");
    placeholderButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    placeholderButton->setAutoRaise(true);

    QWidget *placeholder = new QWidget;
    QVBoxLayout *placeholderLayout = new QVBoxLayout(placeholder);
    placeholderLayout->addStretch();
    placeholderLayout->addWidget(placeholderButton, 0, Qt::AlignCenter);
    placeholderLayout->addStretch();

    // Left: a navigation panel listing the DSK's directory, with a header
    // showing which file is open and a button to close it.
    QLabel *fileNameLabel = new QLabel;
    QFont fileNameFont = fileNameLabel->font();
    fileNameFont.setBold(true);
    fileNameLabel->setFont(fileNameFont);

    QIcon closeFileIcon = QIcon::fromTheme("document-close",
        app.style()->standardIcon(QStyle::SP_DialogCloseButton));
    QToolButton *closeFileButton = new QToolButton;
    closeFileButton->setIcon(closeFileIcon);
    closeFileButton->setAutoRaise(true);
    closeFileButton->setToolTip("Close disk image");

    QWidget *navHeader = new QWidget;
    QHBoxLayout *navHeaderLayout = new QHBoxLayout(navHeader);
    navHeaderLayout->setContentsMargins(6, 4, 4, 4);
    navHeaderLayout->addWidget(fileNameLabel);
    navHeaderLayout->addStretch();
    navHeaderLayout->addWidget(closeFileButton);

    QTreeWidget *tree = new QTreeWidget;
    tree->setHeaderHidden(true);
    tree->setColumnCount(1);
    tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    new RowHoverHighlighter(tree);

    QWidget *navPanel = new QWidget;
    QVBoxLayout *navPanelLayout = new QVBoxLayout(navPanel);
    navPanelLayout->setContentsMargins(0, 0, 0, 0);
    navPanelLayout->setSpacing(0);
    navPanelLayout->addWidget(navHeader);
    navPanelLayout->addWidget(tree, 1);

    // Right: the opened BAS files, grouped in tabs - like an editor area.
    QTabWidget *tabs = new QTabWidget;
    tabs->setTabsClosable(true);

    // A status bar below the tabs, showing the active file's size and
    // attributes; visible only while a file is actually open.
    QLabel *statusLabel = new QLabel;
    statusLabel->setContentsMargins(6, 2, 6, 2);
    statusLabel->setVisible(false);
    auto updateStatusLabel = [tabs, statusLabel](int index) {
        if (index < 0) {
            statusLabel->setVisible(false);
            return;
        }
        statusLabel->setText(tabs->tabBar()->tabData(index).toString());
        statusLabel->setVisible(true);
    };
    QObject::connect(tabs, &QTabWidget::currentChanged, statusLabel, updateStatusLabel);

    QWidget *editorArea = new QWidget;
    QVBoxLayout *editorAreaLayout = new QVBoxLayout(editorArea);
    editorAreaLayout->setContentsMargins(0, 0, 0, 0);
    editorAreaLayout->setSpacing(0);
    editorAreaLayout->addWidget(tabs, 1);
    editorAreaLayout->addWidget(statusLabel);

    QSplitter *splitter = new QSplitter;
    splitter->addWidget(navPanel);
    splitter->addWidget(editorArea);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({ 320, 580 });

    QStackedWidget *outerStack = new QStackedWidget;
    outerStack->addWidget(placeholder);
    outerStack->addWidget(splitter);
    outerStack->setCurrentWidget(placeholder);
    window.setCentralWidget(outerStack);

    static DskImage currentImage;

    auto closeFile = [outerStack, placeholder, tabs, fileNameLabel, &window]() {
        while (tabs->count() > 0) {
            QWidget *page = tabs->widget(0);
            tabs->removeTab(0);
            page->deleteLater();
        }
        currentImage = DskImage();
        fileNameLabel->clear();
        fileNameLabel->setToolTip(QString());
        outerStack->setCurrentWidget(placeholder);
        window.setWindowTitle("qtdskbrowser");
    };

    QObject::connect(closeFileButton, &QToolButton::clicked, &window, closeFile);

    auto closeTabAt = [tabs](int index) {
        QWidget *page = tabs->widget(index);
        tabs->removeTab(index);
        page->deleteLater();
    };

    QObject::connect(tabs, &QTabWidget::tabCloseRequested, tabs, closeTabAt);

    QAction *closeTabAction = new QAction(&window);
    closeTabAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
    window.addAction(closeTabAction);
    QObject::connect(closeTabAction, &QAction::triggered, &window, [tabs, closeTabAt]() {
        int index = tabs->currentIndex();
        if (index >= 0)
            closeTabAt(index);
    });

    QObject::connect(tree, &QTreeWidget::itemDoubleClicked, tabs, [tabs, updateStatusLabel](QTreeWidgetItem *item, int) {
        if (!item->parent())
            return; // a "USER n" group node, not a file

        int fileIndex = item->data(0, Qt::UserRole).toInt();
        const DskFileEntry &fileEntry = currentImage.files().at(fileIndex);
        if (fileEntry.extension.compare("BAS", Qt::CaseInsensitive) != 0)
            return;

        QString title = fileEntry.name + "." + fileEntry.extension;
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->tabText(i) == title) {
                tabs->setCurrentIndex(i);
                return;
            }
        }

        bool isTokenized = false;
        QByteArray content = currentImage.fileContent(fileIndex, &isTokenized);
        QString text = isTokenized ? detokenizeBasic(content) : QString::fromLatin1(content);

        CpcListingView *viewer = new CpcListingView;
        viewer->setText(text);

        QScrollArea *scrollArea = new QScrollArea;
        scrollArea->setWidgetResizable(true);
        scrollArea->setWidget(viewer);

        int index = tabs->addTab(scrollArea, title);
        tabs->tabBar()->setTabData(index, statusTextForFile(fileEntry));
        tabs->setCurrentIndex(index);
        updateStatusLabel(index); // in case addTab() already made this tab current, before tab data was set
    });

    auto openFile = [&window, tree, outerStack, splitter, tabs, fileNameLabel](const QString &filePath) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(&window, "qtdskbrowser", "Could not open file:\n" + filePath);
            return;
        }
        QByteArray rawData = file.readAll();

        DskImage image;
        QString errorMessage;
        if (!image.load(rawData, &errorMessage)) {
            QMessageBox::warning(&window, "qtdskbrowser", errorMessage);
            return;
        }

        // Close any previously opened file tabs; they belong to the file that
        // is about to be replaced.
        while (tabs->count() > 0) {
            QWidget *page = tabs->widget(0);
            tabs->removeTab(0);
            page->deleteLater();
        }

        fileNameLabel->setText(tabTextForFile(filePath));
        fileNameLabel->setToolTip(filePath);

        currentImage = std::move(image);
        showDirectory(tree, currentImage);
        outerStack->setCurrentWidget(splitter);
        window.setWindowTitle("qtdskbrowser - " + filePath);
    };

    QMenu *fileMenu = window.menuBar()->addMenu("File");

    QAction *openAction = fileMenu->addAction(openIcon, "Open disk image...");
    QObject::connect(openAction, &QAction::triggered, &window, [&window, openFile]() {
        QString filePath = QFileDialog::getOpenFileName(&window, "Open File", QString(),
            "DSK Files (*.dsk);;All Files (*)");
        if (!filePath.isEmpty())
            openFile(filePath);
    });

    QObject::connect(placeholderButton, &QToolButton::clicked, openAction, &QAction::trigger);

    fileMenu->addSeparator();

    QIcon quitIcon = QIcon::fromTheme("application-exit",
        app.style()->standardIcon(QStyle::SP_DialogCloseButton));
    QAction *quitAction = fileMenu->addAction(quitIcon, "Quit");
    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);

    window.showMaximized();

    const QStringList args = app.arguments();
    if (args.size() > 1)
        openFile(args.at(1));

    return app.exec();
}
