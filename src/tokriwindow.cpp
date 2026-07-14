#include "tokriwindow.h"
#include "./ui_tokriwindow.h"
#include "standardpaths.h"
#include "listitemdelegate.h"
#include "closebutton.h"
#include "dropawarefilesystemmodel.h"
#include "sleekscrollbar.h"

#include <QAbstractProxyModel>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QIcon>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QMouseEvent>
#include <QMimeData>
#include <QSettings>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QTimer>
#include <QTreeView>
#include <QWindow>

#include <functional>

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

QIcon modeIcon(int mode, const QPalette &palette)
{
    // The original Small and Medium toolbar glyphs were assigned in the
    // opposite order. Only swap their artwork; the actual view modes and
    // saved mode values remain unchanged.
    const int iconMode = mode == 0 ? 1 : (mode == 1 ? 0 : mode);

    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(palette.color(QPalette::ButtonText));
    pen.setWidthF(1.4);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    if (iconMode == 3) {
        for (int y : {4, 9, 14}) {
            painter.drawRect(QRectF(2.5, y - 1.5, 3, 3));
            painter.drawLine(QPointF(8, y), QPointF(18, y));
        }
    } else {
        const int cells = iconMode == 0 ? 3 : 2;
        const qreal size = iconMode == 2 ? 7.0
                                             : (iconMode == 1 ? 5.5 : 4.0);
        const qreal gap = iconMode == 2 ? 2.0 : 1.8;
        const qreal total = cells * size + (cells - 1) * gap;
        const qreal start = (20.0 - total) / 2.0;
        for (int row = 0; row < cells; ++row) {
            for (int column = 0; column < cells; ++column) {
                painter.drawRoundedRect(
                    QRectF(start + column * (size + gap),
                           start + row * (size + gap), size, size),
                    1.0, 1.0);
            }
        }
    }
    return QIcon(pixmap);
}

class ResizeHandle final : public QWidget
{
public:
    ResizeHandle(Qt::Edges edges, Qt::CursorShape cursor,
                 QWidget *parent = nullptr)
        : QWidget(parent)
        , mEdges(edges)
    {
        setCursor(cursor);
        setAttribute(Qt::WA_NoSystemBackground);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            if (QWindow *handle = window()->windowHandle()) {
                if (handle->startSystemResize(mEdges)) {
                    event->accept();
                    return;
                }
            }
        }
        QWidget::mousePressEvent(event);
    }

private:
    Qt::Edges mEdges;
};

class DropTreeView final : public QTreeView
{
public:
    using QTreeView::QTreeView;
    std::function<void(bool)> droppingChanged;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (event->source() == this) {
            event->ignore();
            return;
        }
        if (droppingChanged)
            droppingChanged(true);
        QTreeView::dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (event->source() == this)
            event->ignore();
        else
            QTreeView::dragMoveEvent(event);
    }

    void dragLeaveEvent(QDragLeaveEvent *event) override
    {
        if (droppingChanged)
            droppingChanged(false);
        QTreeView::dragLeaveEvent(event);
    }

    void dropEvent(QDropEvent *event) override
    {
        if (droppingChanged)
            droppingChanged(false);
        QTreeView::dropEvent(event);
    }

    void paintEvent(QPaintEvent *event) override
    {
        QTreeView::paintEvent(event);
        if (!model() || model()->rowCount(rootIndex()) > 0)
            return;

        QPainter painter(viewport());
        QPixmap pixmap(QStringLiteral(":/background.png"));
        pixmap = pixmap.scaled(150, 150, Qt::KeepAspectRatio,
                               Qt::SmoothTransformation);
        const QString text = tr(
            "Drop files, folders, text, links, or images here.");
        const QFontMetrics metrics(font());
        const int spacing = 8;
        const int totalHeight = pixmap.height() + spacing + metrics.height();
        const int startY = (height() - totalHeight) / 3;
        painter.drawPixmap(width() / 2 - pixmap.width() / 2,
                           startY, pixmap);
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(QRect(0, startY + pixmap.height() + spacing,
                               width(), metrics.height()),
                         Qt::AlignHCenter, text);
    }
};
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

    createModeBar();
    configureIconView();
    configureDetailsView();

    mCloseButton = new CloseButton(this);
    mCloseButton->raise();
    connect(mCloseButton, &QAbstractButton::clicked,
            this, &TokriWindow::sleep);
    renderCloseButton();

    setMinimumSize(260, 200);
    createResizeHandles();

    QSettings settings(QStringLiteral("Tokri"), QStringLiteral("Tokri"));
    const QSize savedSize = settings.value(
        QStringLiteral("Window/Size")).toSize();
    if (savedSize.isValid())
        resize(savedSize.expandedTo(minimumSize()));
    const int savedMode = settings.value(
        QStringLiteral("View/Mode"),
        static_cast<int>(ViewMode::MediumIcons)).toInt();
    setViewMode(static_cast<ViewMode>(
        qBound(static_cast<int>(ViewMode::SmallIcons), savedMode,
               static_cast<int>(ViewMode::Details))));
}

TokriWindow::~TokriWindow()
{
    QSettings settings(QStringLiteral("Tokri"), QStringLiteral("Tokri"));
    settings.setValue(QStringLiteral("View/Mode"),
                      static_cast<int>(mViewMode));
    settings.setValue(QStringLiteral("Window/Size"), size());
    if (mDetailsView)
        settings.setValue(QStringLiteral("View/DetailsHeader"),
                          mDetailsView->header()->saveState());
    delete ui;
}

void TokriWindow::createModeBar()
{
    mModeBar = new QWidget(ui->centralwidget);
    auto *layout = new QHBoxLayout(mModeBar);
    layout->setContentsMargins(8, 0, 38, 2);
    layout->setSpacing(2);
    layout->addStretch();

    mModeActions = new QActionGroup(this);
    mModeActions->setExclusive(true);

    const QStringList labels = {
        tr("Small icons"), tr("Medium icons"),
        tr("Large icons"), tr("Details")
    };
    for (int i = 0; i < labels.size(); ++i) {
        auto *button = new QToolButton(mModeBar);
        auto *action = new QAction(modeIcon(i, palette()), labels[i], button);
        action->setCheckable(true);
        action->setData(i);
        mModeActions->addAction(action);
        button->setDefaultAction(action);
        button->setAutoRaise(true);
        button->setToolTip(labels[i]);
        layout->addWidget(button);
        connect(action, &QAction::triggered, this, [this, i] {
            setViewMode(static_cast<ViewMode>(i));
        });
    }

    ui->verticalLayout->removeWidget(ui->listView);
    ui->verticalLayout->insertWidget(1, mModeBar);

    mViewStack = new QStackedWidget(ui->centralwidget);
    mViewStack->addWidget(ui->listView);
    ui->verticalLayout->insertWidget(2, mViewStack, 1);
}

void TokriWindow::configureIconView()
{
    ui->listView->setStyleSheet(QStringLiteral(
        "QListView { padding: 0px; margin: 0px; }"));
    ui->listView->setVerticalScrollBar(
        new SleekScrollBar(Qt::Vertical, ui->listView));
    mListDelegate = new ListItemDelegate(ui->listView);
    ui->listView->setItemDelegate(mListDelegate);
    ui->listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->listView->setViewMode(QListView::IconMode);
    ui->listView->setFlow(QListView::LeftToRight);
    ui->listView->setWrapping(true);
    ui->listView->setUniformItemSizes(true);
    ui->listView->setSpacing(8);
    ui->listView->setMouseTracking(true);
    ui->listView->setFocusPolicy(Qt::NoFocus);
    ui->listView->setDropIndicatorShown(false);
    ui->listView->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(ui->listView, &QListView::doubleClicked,
            this, [this](const QModelIndex &index) {
        if (index.isValid()) {
            const QFileInfo info = index.data(
                QFileSystemModel::FileInfoRole).value<QFileInfo>();
            openItem(info.filePath());
        }
    });
    connect(ui->listView, &QWidget::customContextMenuRequested,
            this, [this](const QPoint &pos) {
        showContextMenu(ui->listView, pos);
    });
    connect(ui->listView, &NoInternalDragListView::dropping,
            this, &TokriWindow::setDropping);
}

void TokriWindow::configureDetailsView()
{
    auto *details = new DropTreeView(ui->centralwidget);
    details->droppingChanged = [this](bool status) { setDropping(status); };
    mDetailsView = details;
    mViewStack->addWidget(mDetailsView);

    mDetailsView->setVerticalScrollBar(
        new SleekScrollBar(Qt::Vertical, mDetailsView));
    mDetailsView->setAcceptDrops(true);
    mDetailsView->setDragEnabled(true);
    mDetailsView->setDragDropMode(QAbstractItemView::DragDrop);
    mDetailsView->setDropIndicatorShown(false);
    mDetailsView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mDetailsView->setFocusPolicy(Qt::NoFocus);
    mDetailsView->setFrameShape(QFrame::NoFrame);
    mDetailsView->setRootIsDecorated(false);
    mDetailsView->setItemsExpandable(false);
    mDetailsView->setUniformRowHeights(true);
    mDetailsView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    mDetailsView->setSelectionBehavior(QAbstractItemView::SelectRows);
    mDetailsView->setContextMenuPolicy(Qt::CustomContextMenu);
    mDetailsView->setSortingEnabled(true);
    mDetailsView->setIconSize(QSize(20, 20));
    mDetailsView->header()->setSectionsMovable(true);
    // QTreeView keeps its first column fixed by default even when the other
    // sections are movable. Details is a flat list, so every column should be
    // movable just like in Explorer.
    mDetailsView->header()->setFirstSectionMovable(true);
    mDetailsView->header()->setSectionsClickable(true);
    mDetailsView->header()->setStretchLastSection(false);
    mDetailsView->header()->setSortIndicatorShown(true);

    connect(mDetailsView, &QTreeView::doubleClicked,
            this, [this](const QModelIndex &index) {
        if (index.isValid()) {
            const QFileInfo info = index.data(
                QFileSystemModel::FileInfoRole).value<QFileInfo>();
            openItem(info.filePath());
        }
    });
    connect(mDetailsView, &QWidget::customContextMenuRequested,
            this, [this](const QPoint &pos) {
        showContextMenu(mDetailsView, pos);
    });
}

void TokriWindow::setFileModels(QAbstractItemModel *iconModel,
                                QAbstractItemModel *detailsModel,
                                const QModelIndex &iconRoot)
{
    ui->listView->setModel(iconModel);
    ui->listView->setRootIndex(iconRoot);
    mDetailsView->setModel(detailsModel);

    auto *header = mDetailsView->header();
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->resizeSection(0, 180);
    header->resizeSection(1, 145);
    header->resizeSection(2, 145);
    header->resizeSection(3, 145);
    header->resizeSection(4, 130);
    header->resizeSection(5, 90);

    QSettings settings(QStringLiteral("Tokri"), QStringLiteral("Tokri"));
    const QByteArray state = settings.value(
        QStringLiteral("View/DetailsHeader")).toByteArray();
    if (!state.isEmpty())
        header->restoreState(state);

    // restoreState() can load a state saved before the first section was made
    // movable, so enforce this after restoring the state as well.
    header->setSectionsMovable(true);
    header->setFirstSectionMovable(true);
    mDetailsHeaderState = header->saveState();

    auto saveHeaderState = [this, header] {
        if (mRestoringDetailsHeader)
            return;
        mDetailsHeaderState = header->saveState();
        QSettings settings(QStringLiteral("Tokri"), QStringLiteral("Tokri"));
        settings.setValue(QStringLiteral("View/DetailsHeader"),
                          mDetailsHeaderState);
    };
    connect(header, &QHeaderView::sectionMoved,
            this, [saveHeaderState](int, int, int) { saveHeaderState(); });
    connect(header, &QHeaderView::sectionResized,
            this, [saveHeaderState](int, int, int) { saveHeaderState(); });
    connect(header, &QHeaderView::sortIndicatorChanged,
            this, [saveHeaderState](int, Qt::SortOrder) {
        saveHeaderState();
    });

    // FileDetailsModel currently uses model resets when the source is sorted
    // or refreshed. QHeaderView may return to logical column order during a
    // reset, so restore the user's visual order after the view has processed
    // the reset.
    connect(detailsModel, &QAbstractItemModel::modelAboutToBeReset,
            this, [this, header] {
        if (!mRestoringDetailsHeader)
            mDetailsHeaderState = header->saveState();
    });
    connect(detailsModel, &QAbstractItemModel::modelReset,
            this, [this, header] {
        if (mRestoringDetailsHeader || mDetailsHeaderState.isEmpty())
            return;
        mRestoringDetailsHeader = true;
        QTimer::singleShot(0, this, [this, header] {
            header->restoreState(mDetailsHeaderState);
            header->setSectionsMovable(true);
            header->setFirstSectionMovable(true);
            mRestoringDetailsHeader = false;
        });
    });

    int sortSection = header->sortIndicatorSection();
    if (sortSection < 0)
        sortSection = 1;
    mDetailsView->sortByColumn(sortSection,
                               header->sortIndicatorOrder());
}

void TokriWindow::setIconRootIndex(const QModelIndex &iconRoot)
{
    ui->listView->setRootIndex(iconRoot);
}

QAbstractItemView *TokriWindow::activeView() const
{
    return mViewMode == ViewMode::Details
               ? static_cast<QAbstractItemView *>(mDetailsView)
               : static_cast<QAbstractItemView *>(ui->listView);
}

QModelIndexList TokriWindow::selectedRows() const
{
    auto *view = activeView();
    return view && view->selectionModel()
               ? view->selectionModel()->selectedRows(0)
               : QModelIndexList();
}

void TokriWindow::setViewMode(ViewMode mode)
{
    mViewMode = mode;
    const int value = static_cast<int>(mode);
    if (mModeActions) {
        for (QAction *action : mModeActions->actions())
            action->setChecked(action->data().toInt() == value);
    }

    if (mode == ViewMode::Details) {
        mViewStack->setCurrentWidget(mDetailsView);
    } else {
        int iconExtent = 64;
        QSize grid(100, 106);
        if (mode == ViewMode::SmallIcons) {
            iconExtent = 32;
            grid = QSize(82, 72);
        } else if (mode == ViewMode::LargeIcons) {
            iconExtent = 96;
            grid = QSize(132, 142);
        }
        mListDelegate->setIconExtent(iconExtent);
        ui->listView->setGridSize(grid);
        ui->listView->doItemsLayout();
        ui->listView->viewport()->update();
        mViewStack->setCurrentWidget(ui->listView);
    }

    QSettings settings(QStringLiteral("Tokri"), QStringLiteral("Tokri"));
    settings.setValue(QStringLiteral("View/Mode"), value);
}

QAction *TokriWindow::addViewModeMenu(QMenu *menu)
{
    QMenu *modeMenu = menu->addMenu(tr("Switch Mode"));
    const QStringList labels = {
        tr("Small icons"), tr("Medium icons"),
        tr("Large icons"), tr("Details")
    };
    for (int i = 0; i < labels.size(); ++i) {
        QAction *action = modeMenu->addAction(labels[i]);
        action->setCheckable(true);
        action->setChecked(i == static_cast<int>(mViewMode));
        action->setData(i);
        connect(action, &QAction::triggered, this, [this, i] {
            setViewMode(static_cast<ViewMode>(i));
        });
    }
    return modeMenu->menuAction();
}

void TokriWindow::showContextMenu(QAbstractItemView *view, const QPoint &pos)
{
    const QModelIndexList selected = view->selectionModel()
                                         ? view->selectionModel()->selectedRows(0)
                                         : QModelIndexList();
    const int count = selected.size();

    QMenu menu;
    menu.setPalette(palette());

    QAction *open = nullptr;
    QAction *reveal = nullptr;
    QAction *rename = nullptr;
    QAction *copy = nullptr;
    QAction *del = nullptr;
    QAction *paste = nullptr;

    if (count == 1) {
        open = menu.addAction(tr("&Open"));
        reveal = menu.addAction(tr("Reveal in &Explorer"));
        rename = menu.addAction(tr("&Rename"));
    }
    if (count > 0) {
        copy = menu.addAction(tr("&Copy"));
        del = menu.addAction(tr("&Delete"));
    }

    QAction *selectAll = menu.addAction(tr("Select &All"));
    addViewModeMenu(&menu);
    if (count == 0 && clipboardHasPasteableData())
        paste = menu.addAction(tr("&Paste"));

    QAction *chosen = menu.exec(view->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    auto fileInfoAt = [](const QModelIndex &index) {
        return index.data(QFileSystemModel::FileInfoRole).value<QFileInfo>();
    };

    if (chosen == selectAll) {
        selectAllItems();
    } else if (chosen == paste) {
        pasteClipboard();
    } else if (count == 1 && chosen == open) {
        openItem(fileInfoAt(selected.first()).filePath());
    } else if (count == 1 && chosen == reveal) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(
            fileInfoAt(selected.first()).absolutePath()));
    } else if (count == 1 && chosen == rename) {
        view->edit(selected.first());
    } else if (chosen == copy) {
        copySelectedItems();
    } else if (chosen == del) {
        deleteSelectedItems();
    }
}

bool TokriWindow::clipboardHasPasteableData() const
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    auto *model = sourceFileSystemModel(activeView()->model());
    return mime && !mime->formats().isEmpty() && model
           && model->canDropMimeData(
               mime, Qt::CopyAction, -1, -1, QModelIndex());
}

void TokriWindow::copySelectedItems()
{
    QList<QUrl> urls;
    for (const QModelIndex &index : selectedRows()) {
        const QFileInfo fileInfo = index.data(
            QFileSystemModel::FileInfoRole).value<QFileInfo>();
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
    auto *model = sourceFileSystemModel(activeView()->model());
    if (mime && model && clipboardHasPasteableData())
        model->dropMimeData(mime, Qt::CopyAction, -1, -1, QModelIndex());
}

void TokriWindow::selectAllItems()
{
    if (auto *view = activeView())
        view->selectAll();
}

void TokriWindow::deleteSelectedItems()
{
    for (const QModelIndex &index : selectedRows()) {
        const QFileInfo info = index.data(
            QFileSystemModel::FileInfoRole).value<QFileInfo>();
        if (!info.exists())
            continue;
        if (info.isDir())
            QDir(info.absoluteFilePath()).removeRecursively();
        else
            QFile(info.absoluteFilePath()).moveToTrash();
    }
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
    if (minimized || hidden)
        moveNearCursor();
    if (minimized)
        showNormal();
    else if (hidden)
        show();
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

void TokriWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    renderCloseButton();
    updateResizeHandles();
}

void TokriWindow::createResizeHandles()
{
    auto addHandle = [this](Qt::Edges edges, Qt::CursorShape cursor) {
        auto *handle = new ResizeHandle(edges, cursor, this);
        mResizeHandles.append(handle);
    };

    addHandle(Qt::LeftEdge, Qt::SizeHorCursor);
    addHandle(Qt::RightEdge, Qt::SizeHorCursor);
    addHandle(Qt::TopEdge, Qt::SizeVerCursor);
    addHandle(Qt::BottomEdge, Qt::SizeVerCursor);
    addHandle(Qt::TopEdge | Qt::LeftEdge, Qt::SizeFDiagCursor);
    addHandle(Qt::TopEdge | Qt::RightEdge, Qt::SizeBDiagCursor);
    addHandle(Qt::BottomEdge | Qt::LeftEdge, Qt::SizeBDiagCursor);
    addHandle(Qt::BottomEdge | Qt::RightEdge, Qt::SizeFDiagCursor);

    updateResizeHandles();
}

void TokriWindow::updateResizeHandles()
{
    if (mResizeHandles.size() != 8)
        return;

    constexpr int edgeThickness = 6;
    constexpr int cornerExtent = 14;
    const int middleWidth = qMax(0, width() - 2 * cornerExtent);
    const int middleHeight = qMax(0, height() - 2 * cornerExtent);

    const QRect geometries[] = {
        QRect(0, cornerExtent, edgeThickness, middleHeight),
        QRect(width() - edgeThickness, cornerExtent,
              edgeThickness, middleHeight),
        QRect(cornerExtent, 0, middleWidth, edgeThickness),
        QRect(cornerExtent, height() - edgeThickness,
              middleWidth, edgeThickness),
        QRect(0, 0, cornerExtent, cornerExtent),
        QRect(width() - cornerExtent, 0,
              cornerExtent, cornerExtent),
        QRect(0, height() - cornerExtent,
              cornerExtent, cornerExtent),
        QRect(width() - cornerExtent, height() - cornerExtent,
              cornerExtent, cornerExtent)
    };

    for (int i = 0; i < mResizeHandles.size(); ++i) {
        mResizeHandles[i]->setGeometry(geometries[i]);
        mResizeHandles[i]->raise();
    }

    if (mCloseButton)
        mCloseButton->raise();
}

void TokriWindow::init()
{
    const QString tokriDir = StandardPaths::getPath(StandardPaths::TokriDir);
    QDir dir(tokriDir);
    if (!dir.exists())
        dir.mkpath(tokriDir);
#ifdef Q_OS_MAC
    MacWindowLevel::hideFromDock();
#endif
}

void TokriWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::SelectAll)) {
        selectAllItems();
        event->accept();
    } else if (event->matches(QKeySequence::Copy)) {
        copySelectedItems();
        event->accept();
    } else if (event->matches(QKeySequence::Paste)) {
        pasteClipboard();
        event->accept();
    } else {
        QMainWindow::keyPressEvent(event);
    }
}

void TokriWindow::moveNearCursor()
{
    const QPoint cursor = QCursor::pos();
    const QSize winSize = size();
    QPoint point(cursor.x() + 20, cursor.y() + 20);
    const QRect screen = QGuiApplication::screenAt(cursor)->availableGeometry();

    if (point.x() + winSize.width() > screen.right())
        point.setX(screen.right() - winSize.width());
    if (point.y() + winSize.height() > screen.bottom())
        point.setY(screen.bottom() - winSize.height());
    if (point.x() < screen.left())
        point.setX(screen.left());
    if (point.y() < screen.top())
        point.setY(screen.top());
    move(point);
}

void TokriWindow::onShakeDetect()
{
    wakeUp();
}

void TokriWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    setFocus(Qt::ActiveWindowFocusReason);
#ifdef Q_OS_MAC
    MacWindowLevel::makeAlwaysOnTop(windowHandle());
#endif
}

void TokriWindow::openItem(QString filePath)
{
    if (filePath.endsWith(QStringLiteral(".url.txt"))) {
        QFile file(filePath);
        if (file.open(QIODeviceBase::ReadOnly | QIODeviceBase::Text)) {
            QTextStream in(&file);
            const QString line = in.readLine();
            if (!line.isEmpty()) {
                QDesktopServices::openUrl(line);
                return;
            }
        }
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
}

void TokriWindow::renderCloseButton()
{
    const int margin = 8;
#ifdef Q_OS_MAC
    mCloseButton->move(margin, margin);
#else
    mCloseButton->move(width() - mCloseButton->width() - margin, margin);
#endif
}
