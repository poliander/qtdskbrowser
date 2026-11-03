#include "detokenizer.h"
#include "basicviewer.h"
#include "dskimage.h"
#include "hexviewer.h"

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QIcon>
#include <QKeySequence>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QLabel>
#include <QMessageBox>
#include <QScreen>
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

// Formats a byte size in KiB with one decimal place and a comma as the
// decimal separator, e.g. "123,4 KiB".
QString formatKib(quint32 bytes)
{
    return QString::number(bytes / 1024.0, 'f', 1).replace('.', ',') + " KiB";
}

// Populates the navigation tree: one top-level "USER n" node per user level
// present on the disk, each with its files as children. The file's index
// into image.files() is stashed on each child item so it can be looked back
// up (e.g. on double-click) without re-deriving it from the tree position.
void showDirectory(QTreeWidget *tree, const DskImage &image)
{
    tree->clear();

    QIcon folderIcon = QIcon::fromTheme("folder", QApplication::style()->standardIcon(QStyle::SP_DirIcon));
    QIcon fileIcon = QIcon::fromTheme("unknown", QApplication::style()->standardIcon(QStyle::SP_FileIcon));

    const QVector<DskFileEntry> &files = image.files();
    QMap<int, QVector<int>> fileIndicesByUser; // QMap keeps user numbers in ascending order
    for (int i = 0; i < files.size(); ++i)
        fileIndicesByUser[files.at(i).user].append(i);

    for (auto it = fileIndicesByUser.constBegin(); it != fileIndicesByUser.constEnd(); ++it) {
        QTreeWidgetItem *userItem = new QTreeWidgetItem(tree);
        userItem->setText(0, QString("USER %1").arg(it.key()));
        userItem->setIcon(0, folderIcon);

        for (int fileIndex : it.value()) {
            const DskFileEntry &file = files.at(fileIndex);
            QString filename = file.extension.isEmpty() ? file.name
                                                          : file.name + "." + file.extension;
            QTreeWidgetItem *fileItem = new QTreeWidgetItem(userItem);
            fileItem->setText(0, filename);
            fileItem->setIcon(0, fileIcon);
            fileItem->setData(0, Qt::UserRole, fileIndex);
            if (file.system)
                fileItem->setForeground(0, tree->palette().color(QPalette::Disabled, QPalette::Text));
            fileItem->setText(1, formatKib(file.size));
            fileItem->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        }
    }

    tree->expandAll();
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

// The text shown in the status bar below the directory tree: how much of the
// disk's capacity is occupied by its files, in KiB (rounded up, so even a
// few used bytes show as at least 1 KB rather than misleadingly as 0).
QString diskUsageText(const DskImage &image)
{
    quint32 usedKb = (image.usedSpace() + 1023) / 1024;
    quint32 totalKb = (image.totalCapacity() + 1023) / 1024;
    quint32 freeKb = totalKb - usedKb;
    return QString("%1 KiB used (%2 KiB free)").arg(usedKb).arg(freeKb);
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.setWindowTitle("qtdskbrowser");

    int windowHeight = QGuiApplication::primaryScreen()->availableGeometry().height() * 2 / 3;
    int windowWidth = windowHeight * 3 / 2;
    window.resize(windowWidth, windowHeight);

    QIcon appIcon = QIcon::fromTheme("media-floppy",
        app.style()->standardIcon(QStyle::SP_DriveFDIcon));
    app.setWindowIcon(appIcon);
    window.setWindowIcon(appIcon);

    QIcon openIcon = QIcon::fromTheme("document-open",
        app.style()->standardIcon(QStyle::SP_DialogOpenButton));

    QIcon saveAsIcon = QIcon::fromTheme("document-save-as",
        app.style()->standardIcon(QStyle::SP_DialogSaveButton));

    QIcon exportIcon = QIcon::fromTheme("document-export",
        app.style()->standardIcon(QStyle::SP_FileLinkIcon));

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

    // Left: a navigation panel listing the DSK's directory.
    QIcon closeFileIcon = QIcon::fromTheme("document-close",
        app.style()->standardIcon(QStyle::SP_DialogCloseButton));

    QTreeWidget *tree = new QTreeWidget;
    tree->setHeaderLabels({ "Name", "Size" });
    // As in Dolphin's list view: the name column stretches to take up
    // whatever width the size column (sized to fit "999,9 KiB") doesn't
    // need, so both track the panel's width as it is resized.
    tree->header()->setStretchLastSection(false);
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree->setAlternatingRowColors(true);
    new RowHoverHighlighter(tree);

    // A status bar below the tree, showing how much of the disk's capacity
    // is used; visible only while a disk image is actually open.
    QLabel *diskUsageLabel = new QLabel;
    diskUsageLabel->setContentsMargins(6, 2, 6, 2);
    diskUsageLabel->setVisible(false);

    QWidget *navPanel = new QWidget;
    QVBoxLayout *navPanelLayout = new QVBoxLayout(navPanel);
    navPanelLayout->setContentsMargins(0, 0, 0, 0);
    navPanelLayout->setSpacing(0);
    navPanelLayout->addWidget(tree, 1);
    navPanelLayout->addWidget(diskUsageLabel);

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

    QAction *closeFileAction = new QAction(closeFileIcon, "Close disk image", &window);
    closeFileAction->setEnabled(false); // no disk image is open yet

    auto closeFile = [outerStack, placeholder, tabs, diskUsageLabel, closeFileAction, &window]() {
        while (tabs->count() > 0) {
            QWidget *page = tabs->widget(0);
            tabs->removeTab(0);
            page->deleteLater();
        }
        currentImage = DskImage();
        diskUsageLabel->setVisible(false);
        closeFileAction->setEnabled(false);
        outerStack->setCurrentWidget(placeholder);
        window.setWindowTitle("qtdskbrowser");
    };

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

        QString title = fileEntry.extension.isEmpty() ? fileEntry.name
                                                        : fileEntry.name + "." + fileEntry.extension;
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->tabText(i) == title) {
                tabs->setCurrentIndex(i);
                return;
            }
        }

        QScrollArea *scrollArea = new QScrollArea;

        // Whether this is a BASIC program is determined by the AMSDOS
        // header's own file type (0 = tokenised, 1 = protected), not by the
        // file's extension - fileContent() already checks that header.
        bool isTokenized = false;
        QByteArray content = currentImage.fileContent(fileIndex, &isTokenized);

        if (isTokenized) {
            QString text = detokenize(content);

            BasicViewer *viewer = new BasicViewer;
            viewer->setText(text);
            scrollArea->setWidgetResizable(true);
            scrollArea->setWidget(viewer);
        } else {
            HexViewer *viewer = new HexViewer;
            viewer->setData(content);
            // The hex dump has a fixed width/height and never wraps or
            // grows, so the scroll area should scroll it rather than
            // resize it. Giving the viewport the same blue background
            // keeps any extra space around it looking consistent.
            scrollArea->setWidgetResizable(false);
            scrollArea->setWidget(viewer);
            scrollArea->viewport()->setStyleSheet("background-color: rgb(0, 0, 170);");
        }

        int index = tabs->addTab(scrollArea, title);
        tabs->tabBar()->setTabData(index, statusTextForFile(fileEntry));
        tabs->setCurrentIndex(index);
        updateStatusLabel(index); // in case addTab() already made this tab current, before tab data was set
    });

    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(tree, &QTreeWidget::customContextMenuRequested, tree, [&window, tree, saveAsIcon, exportIcon](const QPoint &pos) {
        QTreeWidgetItem *item = tree->itemAt(pos);
        if (!item || !item->parent())
            return; // no item under the cursor, or a "USER n" group node

        int fileIndex = item->data(0, Qt::UserRole).toInt();
        const DskFileEntry &fileEntry = currentImage.files().at(fileIndex);
        QString defaultName = fileEntry.extension.isEmpty() ? fileEntry.name
                                                              : fileEntry.name + "." + fileEntry.extension;

        // Whether this is a BASIC program is determined by the AMSDOS
        // header's own file type (0 = tokenised, 1 = protected), not by the
        // file's extension - fileContent() already checks that header.
        bool isTokenized = false;
        currentImage.fileContent(fileIndex, &isTokenized);

        QMenu menu;
        QAction *saveAsAction = menu.addAction(saveAsIcon, "Save as...");
        QObject::connect(saveAsAction, &QAction::triggered, &window, [&window, fileIndex, defaultName]() {
            QString savePath = QFileDialog::getSaveFileName(&window, "Save File As", defaultName);
            if (savePath.isEmpty())
                return;

            QByteArray content = currentImage.fileContent(fileIndex);
            QFile file(savePath);
            if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size()) {
                QMessageBox::warning(&window, "qtdskbrowser", "Could not save file:\n" + savePath);
            }
        });

        if (isTokenized) {
            QAction *exportAction = menu.addAction(exportIcon, "Export...");
            QObject::connect(exportAction, &QAction::triggered, &window, [&window, fileIndex, fileEntry]() {
                QString defaultTextName = fileEntry.name + ".txt";
                QString savePath = QFileDialog::getSaveFileName(&window, "Export File As", defaultTextName);
                if (savePath.isEmpty())
                    return;

                QString text = detokenize(currentImage.fileContent(fileIndex));

                QFile file(savePath);
                if (!file.open(QIODevice::WriteOnly) || file.write(text.toLatin1()) < 0) {
                    QMessageBox::warning(&window, "qtdskbrowser", "Could not export file:\n" + savePath);
                }
            });
        }

        menu.exec(tree->viewport()->mapToGlobal(pos));
    });

    auto openFile = [&window, tree, outerStack, splitter, tabs, diskUsageLabel, closeFileAction](const QString &filePath) {
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

        currentImage = std::move(image);
        showDirectory(tree, currentImage);
        diskUsageLabel->setText(diskUsageText(currentImage));
        diskUsageLabel->setVisible(true);
        closeFileAction->setEnabled(true);
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

    fileMenu->addAction(closeFileAction);
    QObject::connect(closeFileAction, &QAction::triggered, &window, closeFile);

    fileMenu->addSeparator();

    QIcon quitIcon = QIcon::fromTheme("application-exit",
        app.style()->standardIcon(QStyle::SP_DialogCloseButton));
    QAction *quitAction = fileMenu->addAction(quitIcon, "Quit");
    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);

    window.show();

    const QStringList args = app.arguments();
    if (args.size() > 1)
        openFile(args.at(1));

    return app.exec();
}
