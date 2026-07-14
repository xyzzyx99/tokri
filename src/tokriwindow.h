#ifndef TOKRIWINDOW_H
#define TOKRIWINDOW_H

#include "closebutton.h"

#include <QByteArray>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QListView>
#include <QMainWindow>
#include <QMimeData>
#include <QPainter>

QT_BEGIN_NAMESPACE
namespace Ui { class TokriWindow; }
QT_END_NAMESPACE

class QAction;
class QActionGroup;
class QAbstractItemModel;
class QAbstractItemView;
class QModelIndex;
class QMenu;
class QStackedWidget;
class QTreeView;
class QWidget;
class ListItemDelegate;

class TokriWindow : public QMainWindow
{
    Q_OBJECT

public:
    enum class ViewMode {
        SmallIcons = 0,
        MediumIcons,
        LargeIcons,
        Details
    };

    TokriWindow(QWidget *parent = nullptr);
    ~TokriWindow();

    void deleteSelectedItems();
    void setFileModels(QAbstractItemModel *iconModel,
                       QAbstractItemModel *detailsModel,
                       const QModelIndex &iconRoot);
    void setIconRootIndex(const QModelIndex &iconRoot);
    Ui::TokriWindow *uiHandle();
    void sleep();
    void wakeUp();

public slots:
    void onShakeDetect();

private:
    QAbstractItemView *activeView() const;
    QAction *addViewModeMenu(QMenu *menu);
    bool clipboardHasPasteableData() const;
    void configureDetailsView();
    void configureIconView();
    void copySelectedItems();
    void createModeBar();
    void init();
    void keyPressEvent(QKeyEvent *e) override;
    void moveNearCursor();
    void openItem(QString filePath);
    void paintEvent(QPaintEvent *) override;
    void pasteClipboard();
    void renderCloseButton();
    void resizeEvent(QResizeEvent *e) override;
    void selectAllItems();
    QModelIndexList selectedRows() const;
    void setDropping(bool status);
    void setViewMode(ViewMode mode);
    void showContextMenu(QAbstractItemView *view, const QPoint &pos);
    void showEvent(QShowEvent *e) override;

    Ui::TokriWindow *ui;
    bool mDropping = false;
    CloseButton *mCloseButton = nullptr;
    ListItemDelegate *mListDelegate = nullptr;
    QTreeView *mDetailsView = nullptr;
    QStackedWidget *mViewStack = nullptr;
    QWidget *mModeBar = nullptr;
    QActionGroup *mModeActions = nullptr;
    QByteArray mDetailsHeaderState;
    bool mRestoringDetailsHeader = false;
    ViewMode mViewMode = ViewMode::MediumIcons;
};
#endif // TOKRIWINDOW_H
