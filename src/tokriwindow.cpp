#include "tokriwindow.h"
#include "./ui_tokriwindow.h"
#include "standardpaths.h"
#include "listitemdelegate.h"
#include "closebutton.h"
#include "dropawarefilesystemmodel.h"
#include <QAbstractProxyModel>
#include <QDir>
#include <QMenu>
#include <QDesktopServices>
#include <QKeyEvent>
#include <QKeySequence>
#include <QFileSystemModel>
#include <QApplication>
#include <QClipboard>
#include "sleekscrollbar.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
DropAwareFileSystemModel *sourceFileSystemModel(QAbstractItemModel *model)
{
    while (auto *proxy = qobject_cast<QAbstractProxyModel *>(model))
        model = proxy->sourceModel();

    return qobject_cast<DropAwareFileSystemModel *>(model);
}
}


#ifdef Q_OS_MAC
#include "MacWindowLevel.h"
#endif

TokriWindow::TokriWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::TokriWindow)
{
    ui->setupUi(this);

    init();
    setWindowFlags(windowFlags()
                   | Qt::FramelessWindowHint
                   | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);

    ui->listView->setStyleSheet(R"(
        QListView {
            padding: 0px;
            margin: 0px;
        }
    )");

    mCloseButton = new CloseButton(this);
    mCloseButton->setParent(this);
    mCloseButton->raise();
    connect(
        mCloseButton,
        &QAbstractButton::clicked,
        this,
        &TokriWindow::sleep
        );
    renderCloseButton();


    ui->listView->setVerticalScrollBar(new SleekScrollBar(Qt::Vertical, ui->listView));
    const auto delegate = new ListItemDelegate(ui->listView);
    ui->listView->setItemDelegate(delegate);
    ui->listView->setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(ui->listView, &QListView::doubleClicked,
            this, [this](const QModelIndex &idx){
                if (!idx.isValid())
                    return;

                const QString filePath =
                    idx.data(QFileSystemModel::FileInfoRole)
                        .value<QFileInfo>()
                        .filePath();
                openItem(filePath);
    });

    ui->listView->setViewMode(QListView::IconMode);
    ui->listView->setGridSize({100, 130});
    ui->listView->setFlow(QListView::LeftToRight);
    ui->listView->setWrapping(true);
    ui->listView->setUniformItemSizes(true);
    ui->listView->setSpacing(8);
    ui->listView->setMouseTracking(true);
    ui->listView->setFocusPolicy(Qt::NoFocus);
    ui->listView->setDropIndicatorShown(false);
    ui->listView->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(ui->listView, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                auto *view = ui->listView;
                auto *sel  = view->selectionModel();
                const auto selected = sel->selectedIndexes();
                const int count = selected.size();

                QMenu menu;
                menu.setPalette(this->palette());

                QAction *open = nullptr, *reveal = nullptr, *rename = nullptr;
                QAction *copy = nullptr, *del = nullptr, *selectAll = nullptr;
                QAction *paste = nullptr;

                if (count == 1) {
                    open   = menu.addAction("&Open");
                    reveal= menu.addAction("Reveal in &Explorer");
                    rename= menu.addAction("&Rename");
                }
                if (count > 0) {
                    copy = menu.addAction("&Copy");
                    del  = menu.addAction("&Delete");
                }
                selectAll = menu.addAction("Select &All");
                if (count == 0 && clipboardHasPasteableData())
                    paste = menu.addAction("&Paste");

                QAction *chosen = menu.exec(view->viewport()->mapToGlobal(pos));
                if (!chosen) return;

                auto fileInfoAt = [](const QModelIndex &idx) {
                    return idx.data(QFileSystemModel::FileInfoRole).value<QFileInfo>();
                };

                if (chosen == selectAll) {
                    selectAllItems();
                    return;
                }

                if (chosen == paste) {
                    pasteClipboard();
                    return;
                }

                if (count == 1 && chosen == open) {
                    QString filePath = fileInfoAt(selected[0]).filePath();
                    openItem(filePath);
                    return;
                }

                if (count == 1 && chosen == reveal) {
                    QDesktopServices::openUrl(
                        QUrl::fromLocalFile(fileInfoAt(selected[0]).absolutePath()));
                    return;
                }

                if (count == 1 && chosen == rename) {
                    view->edit(selected[0]);
                    return;
                }

                if (chosen == copy) {
                    copySelectedItems();
                    return;
                }

                if (chosen == del) {
                    for (const auto &idx : selected) {
                        QFileInfo fi(fileInfoAt(idx).filePath());
                        if (!fi.exists())
                            return;

                        if (fi.isDir())
                            QDir(fi.absoluteFilePath()).removeRecursively();
                        else
                            QFile::remove(fi.absoluteFilePath());                    }
                }
            });

    connect(
        ui->listView,
        &NoInternalDragListView::dropping,
        this,
        &TokriWindow::setDropping
        );
}

TokriWindow::~TokriWindow()
{
    delete ui;
}

bool TokriWindow::clipboardHasPasteableData() const
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    auto *model = sourceFileSystemModel(ui->listView->model());

    return mime
           && !mime->formats().isEmpty()
           && model
           && model->canDropMimeData(
               mime, Qt::CopyAction, -1, -1, QModelIndex());
}

void TokriWindow::copySelectedItems()
{
    auto *selectionModel = ui->listView->selectionModel();
    if (!selectionModel)
        return;

    QList<QUrl> urls;
    for (const QModelIndex &index : selectionModel->selectedIndexes()) {
        const QFileInfo fileInfo =
            index.data(QFileSystemModel::FileInfoRole).value<QFileInfo>();
        if (fileInfo.exists())
            urls << QUrl::fromLocalFile(fileInfo.absoluteFilePath());
    }

    if (urls.isEmpty())
        return;

    auto *mime = new QMimeData;
    mime->setUrls(urls);
    QGuiApplication::clipboard()->setMimeData(mime);
}

void TokriWindow::pasteClipboard()
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    auto *model = sourceFileSystemModel(ui->listView->model());
    if (!mime || !model || !clipboardHasPasteableData())
        return;

    model->dropMimeData(mime, Qt::CopyAction, -1, -1, QModelIndex());
}

void TokriWindow::selectAllItems()
{
    ui->listView->selectAll();
}

Ui::TokriWindow *TokriWindow::uiHandle()
{
    return ui;
}

void TokriWindow::sleep()
{
    hide();
}

void TokriWindow::wakeUp()
{
    const bool minimized = isMinimized();
    const bool hidden = !isVisible();

    if (minimized || hidden) {
        moveNearCursor();
    }

    if (minimized) {
        showNormal();
    } else if (hidden) {
        show();
    }

    raise();
    activateWindow();
    setFocus(Qt::ActiveWindowFocusReason);
}

void TokriWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Clear the translucent backing surface before drawing the frame. This
    // prevents partially covered antialiased pixels from accumulating.
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(rect(), Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    const QColor borderColor = palette().color(
        mDropping ? QPalette::Accent : QPalette::Shadow
        );
    const qreal penWidth = mDropping ? 8.0 : 2.0;
    const qreal inset = penWidth / 2.0;
    const QRectF frameRect = QRectF(rect()).adjusted(
        inset, inset, -inset, -inset
        );
    const qreal radius = 16.0 - inset;

    QPen pen(borderColor);
    pen.setWidthF(penWidth);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);

    // Use one rounded-rectangle operation for both the background and border.
    // Separate fill and stroke rectangles create two antialiased corner edges.
    p.setPen(pen);
    p.setBrush(palette().color(QPalette::Window));
    p.drawRoundedRect(frameRect, radius, radius);
}

void TokriWindow::setDropping(bool status)
{
    mDropping = status;
    update();
}

void TokriWindow::resizeEvent(QResizeEvent *e)
{
    QMainWindow::resizeEvent(e);
    renderCloseButton();
}

void TokriWindow::init()
{
    QString tokriDir = StandardPaths::getPath(StandardPaths::TokriDir);
    QDir dir(tokriDir);
    if (!dir.exists()){
        bool success = dir.mkpath(tokriDir);
        if (!success){
            // FIXME handle error
        }
    }
#ifdef Q_OS_MAC
    MacWindowLevel::hideFromDock();
#endif
}

void TokriWindow::keyPressEvent(QKeyEvent *e)
{
    if (e->matches(QKeySequence::SelectAll)) {
        selectAllItems();
        e->accept();
        return;
    }

    if (e->matches(QKeySequence::Copy)) {
        copySelectedItems();
        e->accept();
        return;
    }

    if (e->matches(QKeySequence::Paste)) {
        pasteClipboard();
        e->accept();
        return;
    }

    QMainWindow::keyPressEvent(e);
}

void TokriWindow::moveNearCursor()
{
    const QPoint cursor = QCursor::pos();
    const QSize  winSize = size();
    QPoint p(cursor.x() + 20, cursor.y() + 20);

    const QRect screen = QGuiApplication::screenAt(cursor)->availableGeometry();

    if (p.x() + winSize.width() > screen.right())
        p.setX(screen.right() - winSize.width());
    if (p.y() + winSize.height() > screen.bottom())
        p.setY(screen.bottom() - winSize.height());
    if (p.x() < screen.left())
        p.setX(screen.left());
    if (p.y() < screen.top())
        p.setY(screen.top());

    move(p);
}

void TokriWindow::onShakeDetect()
{
    wakeUp();
}


void TokriWindow::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    setFocus(Qt::ActiveWindowFocusReason);
#ifdef Q_OS_MAC
    MacWindowLevel::makeAlwaysOnTop(windowHandle());
#endif
}

void TokriWindow::openItem(QString filePath) {
    if (filePath.endsWith(".url.txt"))
    {
        QFile file(filePath);
        if (file.open(QIODeviceBase::ReadOnly | QIODeviceBase::Text))
        {
            QTextStream in(&file);
            QString line = in.readLine();
            if (!line.isEmpty())
            {
                qDebug() << "Opening url" << line;
                QDesktopServices::openUrl(line);
                return;
            }
        }
    }

    QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
}

void TokriWindow::renderCloseButton()
{
        const int m = 8;
#ifdef Q_OS_MAC
        mCloseButton->move(m, m);
#else
        mCloseButton->move(width() - mCloseButton->width() - m, m);
#endif
}
