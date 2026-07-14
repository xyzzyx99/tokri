#include "copyworker.h"
#include "dropawarefilesystemmodel.h"
#include "drophandler.h"
#include "themeprovider.h"
#include "tokriwindow.h"
#include "sortfilterproxy.h"
#include "filedetailsmodel.h"
#include "ui_tokriwindow.h"
#include "standardnames.h"
#include "standardpaths.h"

#ifdef Q_OS_WIN
#include "windowsmouseinterceptor.h"
#endif

#ifdef Q_OS_LINUX
#include "linuxmouseinterceptor.h"
#endif

#ifdef Q_OS_MACOS
#include "macosmouseinterceptor.h"
#endif

#include <QAbstractItemView>
#include <QAbstractProxyModel>
#include <QApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QLineEdit>
#include <QMimeDatabase>
#include <QQueue>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QThread>
#include <QVBoxLayout>
#include <QLocalServer>
#include <QLocalSocket>
#include <QShortcut>
#include <QLockFile>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QIcon appIcon(QStringLiteral(":/net.surajyadav.Tokri.png"));
    a.setWindowIcon(appIcon);
    a.setPalette(ThemeProvider::theme());


    QLocalServer server;
    TokriWindow tokriWindow;
    tokriWindow.setWindowIcon(appIcon);
    // Single Instance
    const QString lockFilePath =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                             + "/"
                             + StandardNames::get(StandardNames::LockFile);
    static QLockFile lockFile(lockFilePath);
    lockFile.setStaleLockTime(0);
    QString localServerName = StandardNames::get(StandardNames::LocalServer);

    if (!lockFile.tryLock()) {
        QLocalSocket localSocket;
        localSocket.connectToServer(localServerName);

        if (localSocket.waitForConnected(100)) {
            return 0;
        }
    } else {
        server.removeServer(localServerName);
        server.listen(localServerName);

        QObject::connect(&server, &QLocalServer::newConnection, [&]{
            auto sock = server.nextPendingConnection();
            if (sock) sock->close();
            tokriWindow.onShakeDetect();
        });
    }


    QIcon icon(":/tray.png");
    auto *tray = new QSystemTrayIcon(icon, &a);
    tray->setToolTip("Tokri - Running");
    auto *menu = new QMenu();
    menu->addAction("Show", &tokriWindow, &TokriWindow::wakeUp);
    menu->addAction("Quit", &a, &QCoreApplication::quit);
    menu->setPalette(a.palette());
    tray->setContextMenu(menu);

    QObject::connect(tray, &QSystemTrayIcon::activated,
                     [&](QSystemTrayIcon::ActivationReason r) {
                         if (r == QSystemTrayIcon::DoubleClick)
                             tokriWindow.wakeUp();
                     });

    tray->show();


    QAction *deleteAction = new QAction(&tokriWindow);
    deleteAction->setShortcut(QKeySequence::Delete);
    tokriWindow.addAction(deleteAction);

    // View & Models
    DropAwareFileSystemModel *fsModel = new DropAwareFileSystemModel(&tokriWindow);
    QString rootPath = StandardPaths::getPath(StandardPaths::TokriDir);
    QModelIndex rootIndex = fsModel->setRootPath(rootPath);

    FSSortFilterProxy *sortFilterProxy = new FSSortFilterProxy(&tokriWindow);
    sortFilterProxy->setSourceModel(fsModel);
    sortFilterProxy->setDynamicSortFilter(true);
    sortFilterProxy->sort(FSSortFilterProxy::AddedColumn, Qt::DescendingOrder);

    auto *detailsModel = new FileDetailsModel(&tokriWindow);
    detailsModel->setSourceModel(sortFilterProxy);
    const QModelIndex proxyRoot = sortFilterProxy->mapFromSource(rootIndex);
    detailsModel->setSourceRoot(proxyRoot);
    tokriWindow.setFileModels(sortFilterProxy, detailsModel, proxyRoot);

    TextDropHandler *dropHandler = new TextDropHandler;
    DropAwareFileSystemModel::connect(
        fsModel,
        &DropAwareFileSystemModel::droppedText,
        dropHandler,
        &TextDropHandler::handleTextDrop
        );

    DropAwareFileSystemModel::connect(
        fsModel,
        &DropAwareFileSystemModel::droppedUrl,
        dropHandler,
        &TextDropHandler::handleUrlDrop
        );
    QObject::connect(dropHandler, &TextDropHandler::itemCreated,
                     sortFilterProxy, &FSSortFilterProxy::recordAddedPath,
                     Qt::QueuedConnection);

    CopyWorker *worker = new CopyWorker;
    CopyWorker::connect(
        fsModel,
        &DropAwareFileSystemModel::droppedFile,
        worker,
        &CopyWorker::copyFile,
        Qt::QueuedConnection
        );
    CopyWorker::connect(
        fsModel,
        &DropAwareFileSystemModel::droppedDirectory,
        worker,
        &CopyWorker::copyDirectory,
        Qt::QueuedConnection
        );
    CopyWorker::connect(
        fsModel,
        &DropAwareFileSystemModel::droppedImage,
        worker,
        &CopyWorker::saveImage,
        Qt::QueuedConnection
        );
    CopyWorker::connect(
        fsModel,
        &DropAwareFileSystemModel::droppedImageBytes,
        worker,
        &CopyWorker::saveImageBytes,
        Qt::QueuedConnection
        );


    QTimer *reloadDirectoryDebounce = new QTimer(&tokriWindow);
    reloadDirectoryDebounce->setSingleShot(true);

    bool reset = true;

    QObject::connect(worker, &CopyWorker::copySuccess,
                     sortFilterProxy, &FSSortFilterProxy::recordAddedPath,
                     Qt::QueuedConnection);

    QObject::connect(worker, &CopyWorker::copySuccess,
                     reloadDirectoryDebounce,
                     [&reloadDirectoryDebounce, &reset] {
                         reloadDirectoryDebounce->setInterval(reset ? 500 : 3000);
                         reset = false;
                         reloadDirectoryDebounce->start();
                     },
                     Qt::QueuedConnection);

    QObject::connect(reloadDirectoryDebounce, &QTimer::timeout,
                     fsModel,
                     [&reset, fsModel, sortFilterProxy,
                      detailsModel, &tokriWindow] {
                         reset = true;
                         const QString root = fsModel->rootPath();
                         fsModel->setRootPath(QString());
                         const QModelIndex sourceRoot = fsModel->setRootPath(root);
                         const QModelIndex refreshedRoot =
                             sortFilterProxy->mapFromSource(sourceRoot);
                         detailsModel->setSourceRoot(refreshedRoot);
                         tokriWindow.setIconRootIndex(refreshedRoot);
                     });


    QObject::connect(fsModel, &QFileSystemModel::fileRenamed,
                     sortFilterProxy,
                     [sortFilterProxy](const QString &path,
                                       const QString &oldName,
                                       const QString &newName) {
        const QDir directory(path);
        sortFilterProxy->renameAddedPath(
            directory.filePath(oldName), directory.filePath(newName));
    });

    QThread* th = new QThread;
    CopyWorker::connect(th, &QThread::finished, worker, &QObject::deleteLater);
    TextDropHandler::connect(th, &QThread::finished, worker, &QObject::deleteLater);
    dropHandler->moveToThread(th);
    worker->moveToThread(th);
    th->start();


    QObject::connect(deleteAction, &QAction::triggered,
                     &tokriWindow, &TokriWindow::deleteSelectedItems);



    auto SleepShortcut = new QShortcut(QKeySequence("Escape"), &tokriWindow);
    SleepShortcut->setContext(Qt::WindowShortcut);

    QObject::connect(SleepShortcut,
                     &QShortcut::activated,
                     &tokriWindow,
                     &TokriWindow::sleep);


#ifdef Q_OS_LINUX
    MouseInterceptor *interceptor = new MouseInterceptor;

    QObject::connect(
        interceptor,
        &MouseInterceptor::shakeDetected,
        &tokriWindow,
        &TokriWindow::wakeUp
        );
#endif
#ifdef Q_OS_WIN
    WindowsMouseInterceptor *interceptor = new WindowsMouseInterceptor;
    interceptor->start();

    QObject::connect(
        interceptor,
        &WindowsMouseInterceptor::shakeDetected,
        &tokriWindow,
        &TokriWindow::wakeUp,
        Qt::QueuedConnection
        );
#endif

#ifdef Q_OS_MACOS
    MacOSMouseInterceptor *interceptor = new MacOSMouseInterceptor;

    QObject::connect(
        interceptor,
        &MacOSMouseInterceptor::shakeDetected,
        &tokriWindow,
        &TokriWindow::wakeUp
        );

    interceptor->start();
#endif


    tokriWindow.show();
    int ret = a.exec();

#ifdef Q_OS_WIN
    interceptor->quit();   // stop the event loop inside the thread
    interceptor->wait();   // wait for thread to finish
    delete interceptor;
#endif

    return ret;
}
