/* ============================================================
* QupZilla - WebKit based browser
* Copyright (C) 2010-2014  David Rosca <nowrep@gmail.com>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
* ============================================================ */
#include "browserwindow.h"
#include "tabwidget.h"
#include "tabbar.h"
#include "webpage.h"
#include "tabbedwebview.h"
#include "lineedit.h"
#include "history.h"
#include "locationbar.h"
#include "searchtoolbar.h"
#include "websearchbar.h"
#include "pluginproxy.h"
#include "sidebar.h"
#include "downloadmanager.h"
#include "cookiejar.h"
#include "cookiemanager.h"
#include "bookmarkstoolbar.h"
#include "clearprivatedata.h"
#include "sourceviewer.h"
#include "siteinfo.h"
#include "preferences.h"
#include "networkmanager.h"
#include "autofill.h"
#include "networkmanagerproxy.h"
#include "rssmanager.h"
#include "mainapplication.h"
#include "aboutdialog.h"
#include "checkboxdialog.h"
#include "adblockmanager.h"
#include "clickablelabel.h"
#include "docktitlebarwidget.h"
#include "iconprovider.h"
#include "progressbar.h"
#include "adblockicon.h"
#include "closedtabsmanager.h"
#include "statusbarmessage.h"
#include "browsinglibrary.h"
#include "navigationbar.h"
#include "pagescreen.h"
#include "webinspectordockwidget.h"
#include "bookmarksimport/bookmarksimportdialog.h"
#include "qztools.h"
#include "actioncopy.h"
#include "reloadstopbutton.h"
#include "enhancedmenu.h"
#include "navigationcontainer.h"
#include "settings.h"
#include "qzsettings.h"
#include "webtab.h"
#include "speeddial.h"
#include "menubar.h"
#include "qtwin.h"
#include "bookmarkstools.h"
#include "bookmarksmenu.h"
#include "historymenu.h"

#include <QKeyEvent>
#include <QSplitter>
#include <QStatusBar>
#include <QMenuBar>
#include <QTimer>
#include <QShortcut>
#include <QStackedWidget>
#include <QSqlQuery>
#include <QTextCodec>
#include <QFileDialog>
#include <QNetworkRequest>
#include <QDesktopServices>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QWebFrame>
#include <QWebHistory>
#include <QMessageBox>
#include <QDesktopWidget>
#include <QToolTip>
#include <QScrollArea>

#if QT_VERSION < 0x050000
#include "qwebkitversion.h"
#endif

#ifdef QZ_WS_X11
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

#ifndef Q_OS_MAC
#define MENU_RECEIVER this
#else
#include "macmenureceiver.h"
#define MENU_RECEIVER mApp->macMenuReceiver()
#endif

const QString BrowserWindow::WEBKITVERSION = qWebKitVersion();

BrowserWindow::BrowserWindow(Qz::BrowserWindowType type, QUrl startUrl)
    : QMainWindow(0)
    , m_bookmarksMenuChanged(true)
    , m_isClosing(false)
    , m_isStarting(false)
    , m_startingUrl(startUrl)
    , m_windowType(type)
    , m_startTab(0)
    , m_menuBookmarksAction(0)
    , m_actionPrivateBrowsing(0)
    , m_sideBarManager(new SideBarManager(this))
    , m_statusBarMessage(new StatusBarMessage(this))
    , m_usingTransparentBackground(false)
    , m_tabsOnTopState(-1)
{
    setObjectName("mainwindow");
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("QupZilla"));

    if (mApp->isPrivateSession()) {
        setProperty("private", QVariant(true));
    }

    m_isStarting = true;

#ifndef QZ_WS_X11
    setUpdatesEnabled(false);
#endif

    setupUi();
    setupMenu();

    m_hideNavigationTimer = new QTimer(this);
    m_hideNavigationTimer->setInterval(1000);
    m_hideNavigationTimer->setSingleShot(true);
    connect(m_hideNavigationTimer, SIGNAL(timeout()), this, SLOT(hideNavigationSlot()));

    connect(mApp, SIGNAL(reloadSettings()), this, SLOT(loadSettings()));

    QTimer::singleShot(0, this, SLOT(postLaunch()));

    if (mApp->isPrivateSession()) {
        QzTools::setWmClass("QupZilla Browser (Private Window)", this);
    }
    else {
        QzTools::setWmClass("QupZilla Browser", this);
    }
}

void BrowserWindow::openWithTab(WebTab* tab)
{
    m_startTab = tab;
}

void BrowserWindow::postLaunch()
{
#ifdef QZ_WS_X11
    setUpdatesEnabled(false);
#endif

    loadSettings();

    Settings settings;
    int afterLaunch = settings.value("Web-URL-Settings/afterLaunch", 3).toInt();
    bool addTab = true;
    QUrl startUrl;

    switch (afterLaunch) {
    case 0:
        startUrl = QUrl();
        break;

    case 2:
        startUrl = QUrl("qupzilla:speeddial");
        break;

    case 1:
    case 3:
        startUrl = m_homepage;
        break;

    default:
        break;
    }

    switch (m_windowType) {
    case Qz::BW_FirstAppWindow:
        if (mApp->isStartingAfterCrash()) {
            addTab = true;
            startUrl = QUrl("qupzilla:restore");
        }
        else if (afterLaunch == 3 && mApp->restoreManager()) {
            addTab = !mApp->restoreStateSlot(this, mApp->restoreManager()->restoreData());
        }
        else {
            // Pinned tabs are restored in MainApplication::restoreStateSlot
            // Make sure they will be restored also when not restoring session
            m_tabWidget->restorePinnedTabs();
        }
        break;

    case Qz::BW_MacFirstWindow:
#ifdef Q_OS_MAC
        QTimer::singleShot(0, this, SLOT(refreshStateOfAllActions()));
#endif
        m_tabWidget->restorePinnedTabs();
        // fallthrough

    case Qz::BW_NewWindow:
        addTab = true;
        break;

    case Qz::BW_OtherRestoredWindow:
        addTab = false;
        break;
    }

    show();

    if (!m_startingUrl.isEmpty()) {
        startUrl = m_startingUrl;
        addTab = true;
    }

    if (m_startTab) {
        addTab = false;
        m_tabWidget->addView(m_startTab);
    }

    if (addTab) {
        QNetworkRequest request(startUrl);
        request.setRawHeader("X-QupZilla-UserLoadAction", QByteArray("1"));

        m_tabWidget->addView(request, Qz::NT_CleanSelectedTabAtTheEnd);

        if (startUrl.isEmpty() || startUrl.toString() == QLatin1String("qupzilla:speeddial")) {
            locationBar()->setFocus();
        }
    }

    if (m_tabWidget->getTabBar()->normalTabsCount() <= 0 && m_windowType != Qz::BW_OtherRestoredWindow) {
        // Something went really wrong .. add one tab
        QNetworkRequest request(m_homepage);
        request.setRawHeader("X-QupZilla-UserLoadAction", QByteArray("1"));

        m_tabWidget->addView(request, Qz::NT_SelectedTabAtTheEnd);
    }

    aboutToHideEditMenu();

#ifdef Q_OS_MAC
    // Fill menus even if user don't call them
    if (m_windowType == Qz::BW_FirstAppWindow) {
        aboutToShowBookmarksMenu();
        aboutToShowHistoryMostMenu();
        aboutToShowHistoryRecentMenu();
        aboutToShowEncodingMenu();
    }
#endif

    mApp->plugins()->emitMainWindowCreated(this);
    emit startingCompleted();

    m_isStarting = false;
    QMainWindow::setWindowTitle(m_lastWindowTitle);

    setUpdatesEnabled(true);
    raise();
    activateWindow();

    QTimer::singleShot(0, tabWidget()->getTabBar(), SLOT(ensureVisible()));
}

void BrowserWindow::setupUi()
{
    int locationBarWidth;
    int websearchBarWidth;

    Settings settings;
    settings.beginGroup("Browser-View-Settings");
    if (settings.value("WindowMaximised", false).toBool()) {
        resize(800, 550);
        setWindowState(Qt::WindowMaximized);
    }
    else {
        // Let the WM decides where to put new browser window
        if ((m_windowType != Qz::BW_FirstAppWindow && m_windowType != Qz::BW_MacFirstWindow) && mApp->getWindow()) {
#ifdef Q_WS_WIN
            // Windows WM places every new window in the middle of screen .. for some reason
            QPoint p = mApp->getWindow()->geometry().topLeft();
            p.setX(p.x() + 30);
            p.setY(p.y() + 30);

            if (!mApp->desktop()->availableGeometry(mApp->getWindow()).contains(p)) {
                p.setX(mApp->desktop()->availableGeometry(mApp->getWindow()).x() + 30);
                p.setY(mApp->desktop()->availableGeometry(mApp->getWindow()).y() + 30);
            }
            setGeometry(QRect(p, mApp->getWindow()->size()));
#else
            resize(mApp->getWindow()->size());
#endif
        }
        else if (!restoreGeometry(settings.value("WindowGeometry").toByteArray())) {
#ifdef Q_WS_WIN
            setGeometry(QRect(mApp->desktop()->availableGeometry(mApp->getWindow()).x() + 30,
                              mApp->desktop()->availableGeometry(mApp->getWindow()).y() + 30, 800, 550));
#else
            resize(800, 550);
#endif
        }
    }

    locationBarWidth = settings.value("LocationBarWidth", 480).toInt();
    websearchBarWidth = settings.value("WebSearchBarWidth", 140).toInt();
    settings.endGroup();

    QWidget* widget = new QWidget(this);
    widget->setCursor(Qt::ArrowCursor);
    setCentralWidget(widget);

    m_mainLayout = new QVBoxLayout(widget);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);
    m_mainSplitter = new QSplitter(this);
    m_mainSplitter->setObjectName("sidebar-splitter");
    m_mainSplitter->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_tabWidget = new TabWidget(this);
    m_superMenu = new QMenu(this);
    m_navigationBar = new NavigationBar(this);
    m_navigationBar->setSplitterSizes(locationBarWidth, websearchBarWidth);
    m_bookmarksToolbar = new BookmarksToolbar(this);

    m_navigationContainer = new NavigationContainer(this);
    QVBoxLayout* l = new QVBoxLayout(m_navigationContainer);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);
    l->addWidget(m_navigationBar);
    l->addWidget(m_bookmarksToolbar);
    m_navigationContainer->setLayout(l);

    m_mainSplitter->addWidget(m_tabWidget);
    triggerTabsOnTop(tabsOnTop());
    m_mainLayout->addWidget(m_mainSplitter);
    m_mainSplitter->setCollapsible(0, false);

    statusBar()->setObjectName("mainwindow-statusbar");
    statusBar()->setCursor(Qt::ArrowCursor);
    m_progressBar = new ProgressBar(statusBar());
    m_privateBrowsing = new QLabel(this);
    m_privateBrowsing->setPixmap(QPixmap(":/icons/locationbar/privatebrowsing.png"));
    m_privateBrowsing->setVisible(false);
    m_privateBrowsing->setToolTip(tr("Private Browsing Enabled"));
    m_adblockIcon = new AdBlockIcon(this);
    m_ipLabel = new QLabel(this);
    m_ipLabel->setObjectName("statusbar-ip-label");
    m_ipLabel->setToolTip(tr("IP Address of current page"));

    statusBar()->addPermanentWidget(m_progressBar);
    statusBar()->addPermanentWidget(m_ipLabel);
    statusBar()->addPermanentWidget(m_privateBrowsing);
    statusBar()->addPermanentWidget(m_adblockIcon);

    // Workaround for Oxygen tooltips not having transparent background
    QPalette pal = QToolTip::palette();
    QColor col = pal.window().color();
    col.setAlpha(0);
    pal.setColor(QPalette::Window, col);
    QToolTip::setPalette(pal);
}

void BrowserWindow::setupMenu()
{
#ifdef Q_OS_MAC
    if (menuBar()) {
        setupMacMenu();
        setupOtherActions();
        return;
    }
    else {
        mApp->macMenuReceiver()->setMenuBar(new QMenuBar(0));
    }
#else
    setMenuBar(new MenuBar(this));
#endif

    // Standard actions - needed on Mac to be placed correctly in "application" menu
    m_actionAbout = new QAction(QIcon::fromTheme("help-about"), tr("&About QupZilla"), 0);
    m_actionAbout->setMenuRole(QAction::AboutRole);
    connect(m_actionAbout, SIGNAL(triggered()), MENU_RECEIVER, SLOT(aboutQupZilla()));

    m_actionPreferences = new QAction(QIcon::fromTheme("preferences-desktop", QIcon(":/icons/theme/settings.png")), tr("Pr&eferences"), 0);
    m_actionPreferences->setMenuRole(QAction::PreferencesRole);
    m_actionPreferences->setShortcut(QKeySequence(QKeySequence::Preferences));
    connect(m_actionPreferences, SIGNAL(triggered()), MENU_RECEIVER, SLOT(showPreferences()));

    m_actionQuit = new QAction(QIcon::fromTheme("application-exit"), tr("Quit"), 0);
    m_actionQuit->setMenuRole(QAction::QuitRole);

    // QKeySequence::Quit returns an empty sequence on Windows and X11 when running desktop other than Gnome and Kde
    m_actionQuit->setShortcut(actionShortcut(QKeySequence::Quit, Qt::CTRL + Qt::Key_Q));
    connect(m_actionQuit, SIGNAL(triggered()), MENU_RECEIVER, SLOT(quitApp()));

    /*************
     * File Menu *
     *************/
    m_menuFile = new QMenu(tr("&File"));
    m_menuFile->addAction(QIcon::fromTheme("tab-new", QIcon(":/icons/menu/tab-new.png")), tr("New Tab"), MENU_RECEIVER, SLOT(addTab()))->setShortcut(QKeySequence("Ctrl+T"));
    m_menuFile->addAction(QIcon::fromTheme("window-new"), tr("&New Window"), MENU_RECEIVER, SLOT(newWindow()))->setShortcut(QKeySequence("Ctrl+N"));
    m_menuFile->addAction(QIcon::fromTheme("document-open-remote"), tr("Open Location"), MENU_RECEIVER, SLOT(openLocation()))->setShortcut(QKeySequence("Ctrl+L"));
    m_menuFile->addAction(QIcon::fromTheme("document-open"), tr("Open &File..."), MENU_RECEIVER, SLOT(openFile()))->setShortcut(QKeySequence("Ctrl+O"));
    m_actionCloseWindow = m_menuFile->addAction(QIcon::fromTheme("window-close"), tr("Close Window"), MENU_RECEIVER, SLOT(closeWindow()));
    m_actionCloseWindow->setShortcut(QKeySequence("Ctrl+Shift+W"));
    m_menuFile->addSeparator();
    m_menuFile->addAction(QIcon::fromTheme("document-save"), tr("&Save Page As..."), MENU_RECEIVER, SLOT(savePage()))->setShortcut(QKeySequence("Ctrl+S"));
    m_menuFile->addAction(QIcon::fromTheme("image-loading"), tr("Save Page Screen"), MENU_RECEIVER, SLOT(savePageScreen()))->setShortcut(QKeySequence("Ctrl+Shift+S"));
    m_menuFile->addAction(QIcon::fromTheme("mail-message-new"), tr("Send Link..."), MENU_RECEIVER, SLOT(sendLink()));
    m_menuFile->addAction(QIcon::fromTheme("document-print"), tr("&Print..."), MENU_RECEIVER, SLOT(printPage()))->setShortcut(QKeySequence("Ctrl+P"));
    m_menuFile->addSeparator();
    m_menuFile->addAction(tr("Import bookmarks..."), MENU_RECEIVER, SLOT(showBookmarkImport()));
    m_menuFile->addAction(m_actionQuit);
#ifdef Q_OS_MAC // Add standard actions to File Menu (as it won't be ever cleared) and Mac menubar should move them to "application" menu
    m_menuFile->addAction(m_actionAbout);
    m_menuFile->addAction(m_actionPreferences);
#endif
    connect(m_menuFile, SIGNAL(aboutToShow()), MENU_RECEIVER, SLOT(aboutToShowFileMenu()));
    connect(m_menuFile, SIGNAL(aboutToHide()), MENU_RECEIVER, SLOT(aboutToHideFileMenu()));

    /*************
     * Edit Menu *
     *************/
    m_menuEdit = new QMenu(tr("&Edit"));
    m_menuEdit->addAction(QIcon::fromTheme("edit-undo"), tr("&Undo"), MENU_RECEIVER, SLOT(editUndo()))->setShortcut(QKeySequence("Ctrl+Z"));
    m_menuEdit->addAction(QIcon::fromTheme("edit-redo"), tr("&Redo"), MENU_RECEIVER, SLOT(editRedo()))->setShortcut(QKeySequence("Ctrl+Shift+Z"));
    m_menuEdit->addSeparator();
    m_menuEdit->addAction(QIcon::fromTheme("edit-cut"), tr("&Cut"), MENU_RECEIVER, SLOT(editCut()))->setShortcut(QKeySequence("Ctrl+X"));
    m_menuEdit->addAction(QIcon::fromTheme("edit-copy"), tr("C&opy"), MENU_RECEIVER, SLOT(editCopy()))->setShortcut(QKeySequence("Ctrl+C"));
    m_menuEdit->addAction(QIcon::fromTheme("edit-paste"), tr("&Paste"), MENU_RECEIVER, SLOT(editPaste()))->setShortcut(QKeySequence("Ctrl+V"));
    m_menuEdit->addSeparator();
    m_menuEdit->addAction(QIcon::fromTheme("edit-select-all"), tr("Select &All"), MENU_RECEIVER, SLOT(editSelectAll()))->setShortcut(QKeySequence("Ctrl+A"));
    m_menuEdit->addAction(QIcon::fromTheme("edit-find"), tr("&Find"), MENU_RECEIVER, SLOT(searchOnPage()))->setShortcut(QKeySequence("Ctrl+F"));
    m_menuEdit->addSeparator();
#ifdef Q_OS_UNIX
    m_menuEdit->addAction(m_actionPreferences);
#endif
    connect(m_menuEdit, SIGNAL(aboutToShow()), MENU_RECEIVER, SLOT(aboutToShowEditMenu()));
    connect(m_menuEdit, SIGNAL(aboutToHide()), MENU_RECEIVER, SLOT(aboutToHideEditMenu()));

    /*************
     * View Menu *
     *************/
    m_menuView = new QMenu(tr("&View"));
    m_actionShowToolbar = new QAction(tr("&Navigation Toolbar"), MENU_RECEIVER);
    m_actionShowToolbar->setCheckable(true);
    connect(m_actionShowToolbar, SIGNAL(triggered(bool)), MENU_RECEIVER, SLOT(showNavigationToolbar()));
    m_actionShowBookmarksToolbar = new QAction(tr("&Bookmarks Toolbar"), MENU_RECEIVER);
    m_actionShowBookmarksToolbar->setCheckable(true);
    connect(m_actionShowBookmarksToolbar, SIGNAL(triggered(bool)), MENU_RECEIVER, SLOT(showBookmarksToolbar()));
    m_actionShowStatusbar = new QAction(tr("Sta&tus Bar"), MENU_RECEIVER);
    m_actionShowStatusbar->setCheckable(true);
    connect(m_actionShowStatusbar, SIGNAL(triggered(bool)), MENU_RECEIVER, SLOT(showStatusbar()));
#ifndef Q_OS_MAC
    m_actionShowMenubar = new QAction(tr("&Menu Bar"), this);
    m_actionShowMenubar->setCheckable(true);
    connect(m_actionShowMenubar, SIGNAL(triggered(bool)), MENU_RECEIVER, SLOT(showMenubar()));
    m_menuEncoding = new QMenu(this);
#else
    m_menuEncoding = new QMenu(0);
#endif
    m_actionTabsOnTop = new QAction(tr("&Tabs on Top"), MENU_RECEIVER);
    m_actionTabsOnTop->setCheckable(true);
    connect(m_actionTabsOnTop, SIGNAL(triggered(bool)), MENU_RECEIVER, SLOT(triggerTabsOnTop(bool)));
    m_actionShowFullScreen = new QAction(tr("&Fullscreen"), MENU_RECEIVER);
    m_actionShowFullScreen->setCheckable(true);
#ifndef Q_OS_MAC
    m_actionShowFullScreen->setShortcut(QKeySequence("F11"));
#else
    m_actionShowFullScreen->setShortcut(QKeySequence("Ctrl+F11"));
#endif
    connect(m_actionShowFullScreen, SIGNAL(triggered(bool)), MENU_RECEIVER, SLOT(toggleFullScreen()));
    m_actionStop = new QAction(IconProvider::standardIcon(QStyle::SP_BrowserStop), tr("&Stop"), MENU_RECEIVER);
    connect(m_actionStop, SIGNAL(triggered()), MENU_RECEIVER, SLOT(stop()));
    m_actionStop->setShortcut(QKeySequence("Esc"));
    m_actionReload = new QAction(IconProvider::standardIcon(QStyle::SP_BrowserReload), tr("&Reload"), MENU_RECEIVER);
    connect(m_actionReload, SIGNAL(triggered()), MENU_RECEIVER, SLOT(reload()));
    m_actionReload->setShortcut(QKeySequence("F5"));
    QAction* actionEncoding = new QAction(tr("Character &Encoding"), MENU_RECEIVER);
    actionEncoding->setMenu(m_menuEncoding);
    connect(m_menuEncoding, SIGNAL(aboutToShow()), MENU_RECEIVER, SLOT(aboutToShowEncodingMenu()));
    m_actionCaretBrowsing = new QAction(tr("Enable &Caret Browsing"), MENU_RECEIVER);
    m_actionCaretBrowsing->setVisible(false);
    m_actionCaretBrowsing->setCheckable(true);
    m_actionCaretBrowsing->setShortcut(QKeySequence("F7"));
    connect(m_actionCaretBrowsing, SIGNAL(triggered()), MENU_RECEIVER, SLOT(triggerCaretBrowsing()));

#if QTWEBKIT_FROM_2_3
    m_actionCaretBrowsing->setVisible(true);
#endif

    m_toolbarsMenu = new QMenu(tr("Toolbars"));
#ifndef Q_OS_MAC
    m_toolbarsMenu->addAction(m_actionShowMenubar);
#endif
    m_toolbarsMenu->addAction(m_actionShowToolbar);
    m_toolbarsMenu->addAction(m_actionShowBookmarksToolbar);
    m_toolbarsMenu->addSeparator();
    m_toolbarsMenu->addAction(m_actionTabsOnTop);
    QMenu* sidebarsMenu = new QMenu(tr("Sidebars"));
    m_sideBarManager->setSideBarMenu(sidebarsMenu);

    m_menuView->addMenu(m_toolbarsMenu);
    m_menuView->addMenu(sidebarsMenu);
    m_menuView->addAction(m_actionShowStatusbar);
    m_menuView->addSeparator();
    m_menuView->addAction(m_actionStop);
    m_menuView->addAction(m_actionReload);
    m_menuView->addSeparator();
    m_menuView->addAction(QIcon::fromTheme("zoom-in"), tr("Zoom &In"), MENU_RECEIVER, SLOT(zoomIn()))->setShortcut(QKeySequence("Ctrl++"));
    m_menuView->addAction(QIcon::fromTheme("zoom-out"), tr("Zoom &Out"), MENU_RECEIVER, SLOT(zoomOut()))->setShortcut(QKeySequence("Ctrl+-"));
    m_menuView->addAction(QIcon::fromTheme("zoom-original"), tr("Reset"), MENU_RECEIVER, SLOT(zoomReset()))->setShortcut(QKeySequence("Ctrl+0"));
    m_menuView->addSeparator();
    m_menuView->addAction(m_actionCaretBrowsing);
    m_menuView->addAction(actionEncoding);
    m_menuView->addSeparator();
    m_actionPageSource = m_menuView->addAction(QIcon::fromTheme("text-html"), tr("&Page Source"), MENU_RECEIVER, SLOT(showSource()));
    m_actionPageSource->setShortcut(QKeySequence("Ctrl+U"));
    m_actionPageSource->setEnabled(false);
    m_menuView->addAction(m_actionShowFullScreen);
    connect(m_menuView, SIGNAL(aboutToShow()), MENU_RECEIVER, SLOT(aboutToShowViewMenu()));
    connect(m_menuView, SIGNAL(aboutToHide()), MENU_RECEIVER, SLOT(aboutToHideViewMenu()));

    /****************
     * History Menu *
     ****************/

    m_menuHistory = new HistoryMenu();
    m_menuHistory->setMainWindow(this);

    /******************
     * Bookmarks Menu *
     ******************/
    m_menuBookmarks = new BookmarksMenu();
    m_menuBookmarks->setMainWindow(this);

    /**************
     * Tools Menu *
     **************/
    m_menuTools = new QMenu(tr("&Tools"));
    m_menuTools->addAction(tr("&Web Search"), MENU_RECEIVER, SLOT(webSearch()))->setShortcut(QKeySequence("Ctrl+K"));
    m_actionPageInfo = m_menuTools->addAction(QIcon::fromTheme("dialog-information"), tr("Page &Info"), MENU_RECEIVER, SLOT(showPageInfo()));
    m_actionPageInfo->setShortcut(QKeySequence("Ctrl+I"));
    m_actionPageInfo->setEnabled(false);
    m_menuTools->addSeparator();
    m_menuTools->addAction(tr("&Download Manager"), MENU_RECEIVER, SLOT(showDownloadManager()))->setShortcut(QKeySequence("Ctrl+Y"));
    m_menuTools->addAction(tr("&Cookies Manager"), MENU_RECEIVER, SLOT(showCookieManager()));
    m_menuTools->addAction(tr("&AdBlock"), AdBlockManager::instance(), SLOT(showDialog()));
    m_menuTools->addAction(QIcon(":/icons/menu/rss.png"), tr("RSS &Reader"), MENU_RECEIVER,  SLOT(showRSSManager()));
    m_menuTools->addAction(tr("Web In&spector"), MENU_RECEIVER, SLOT(showWebInspector()))->setShortcut(QKeySequence("Ctrl+Shift+I"));
    m_menuTools->addAction(QIcon::fromTheme("edit-clear"), tr("Clear Recent &History"), MENU_RECEIVER, SLOT(showClearPrivateData()))->setShortcut(QKeySequence("Ctrl+Shift+Del"));
    m_actionPrivateBrowsing = new QAction(QIcon(":/icons/locationbar/privatebrowsing.png"), tr("New &Private Window"), MENU_RECEIVER);
    m_actionPrivateBrowsing->setShortcut(QKeySequence("Ctrl+Shift+P"));
    connect(m_actionPrivateBrowsing, SIGNAL(triggered(bool)), mApp, SLOT(startPrivateBrowsing()));
    m_menuTools->addAction(m_actionPrivateBrowsing);
    m_menuTools->addSeparator();
#if !defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
    m_menuTools->addAction(m_actionPreferences);
#endif
    connect(m_menuTools, SIGNAL(aboutToShow()), MENU_RECEIVER, SLOT(aboutToShowToolsMenu()));
    connect(m_menuTools, SIGNAL(aboutToHide()), MENU_RECEIVER, SLOT(aboutToHideToolsMenu()));

    /*************
     * Help Menu *
     *************/
    m_menuHelp = new QMenu(tr("&Help"));
#ifndef Q_OS_MAC
    m_menuHelp->addAction(QIcon(":/icons/menu/qt.png"), tr("About &Qt"), qApp, SLOT(aboutQt()));
    m_menuHelp->addAction(m_actionAbout);
    m_menuHelp->addSeparator();
#endif
    QAction* infoAction = new QAction(QIcon::fromTheme("help-contents"), tr("Information about application"), m_menuHelp);
    infoAction->setData(QUrl("qupzilla:about"));
    infoAction->setShortcut(QKeySequence(QKeySequence::HelpContents));
    connect(infoAction, SIGNAL(triggered()), MENU_RECEIVER, SLOT(loadActionUrlInNewTab()));
    m_menuHelp->addAction(infoAction);
    m_menuHelp->addAction(tr("Configuration Information"), MENU_RECEIVER, SLOT(loadActionUrlInNewTab()))->setData(QUrl("qupzilla:config"));
    m_menuHelp->addAction(tr("Report &Issue"), MENU_RECEIVER, SLOT(loadActionUrlInNewTab()))->setData(QUrl("qupzilla:reportbug"));

    /************
     * Menu Bar *
     ************/
    menuBar()->addMenu(m_menuFile);
    menuBar()->addMenu(m_menuEdit);
    menuBar()->addMenu(m_menuView);
    menuBar()->addMenu(m_menuHistory);
    menuBar()->addMenu(m_menuBookmarks);
    menuBar()->addMenu(m_menuTools);
    menuBar()->addMenu(m_menuHelp);

    /*****************
     * Other Actions *
     *****************/
    setupOtherActions();

#ifndef Q_OS_MAC
    m_superMenu->addAction(m_menuFile->actions().at(0));
    m_superMenu->addAction(m_menuFile->actions().at(1));
    m_superMenu->addAction(m_actionPrivateBrowsing);
    m_superMenu->addAction(m_menuFile->actions().at(3));

    m_superMenu->addSeparator();
    m_superMenu->addAction(m_menuFile->actions().at(7));
    m_superMenu->addAction(m_menuFile->actions().at(8));
    m_superMenu->addAction(m_menuFile->actions().at(10));

    m_superMenu->addSeparator();
    m_superMenu->addAction(m_menuEdit->actions().at(7));
    m_superMenu->addAction(m_menuEdit->actions().at(8));

    m_superMenu->addSeparator();
    m_superMenu->addAction(m_menuHistory->actions().at(3));
    m_superMenu->addAction(m_menuBookmarks->actions().at(2));

    m_superMenu->addSeparator();
    m_superMenu->addAction(m_actionPreferences);

    m_superMenu->addSeparator();
    m_superMenu->addMenu(m_menuView);
    m_superMenu->addMenu(m_menuHistory);
    m_superMenu->addMenu(m_menuBookmarks);
    m_superMenu->addMenu(m_menuTools);

    m_superMenu->addSeparator();
    m_superMenu->addAction(m_actionAbout);
    m_superMenu->addAction(m_menuHelp->actions().at(3));
    m_superMenu->addAction(m_menuHelp->actions().at(4));
    m_superMenu->addAction(m_menuHelp->actions().at(5));

    m_superMenu->addSeparator();
    m_superMenu->addAction(m_actionQuit);
#else
    ActionCopy* copyActionPrivateBrowsing = new ActionCopy(m_actionPrivateBrowsing);
    copyActionPrivateBrowsing->setText(copyActionPrivateBrowsing->text().remove(QLatin1Char('&')));
    mApp->macDockMenu()->addAction(copyActionPrivateBrowsing);
    mApp->macDockMenu()->addAction(m_menuFile->actions().at(1));
    mApp->macDockMenu()->addAction(m_menuFile->actions().at(0));
#endif
}

void BrowserWindow::setupOtherActions()
{
    m_actionRestoreTab = new QAction(QIcon::fromTheme("user-trash"), tr("Restore &Closed Tab"), this);
    m_actionRestoreTab->setShortcut(QKeySequence("Ctrl+Shift+T"));
    connect(m_actionRestoreTab, SIGNAL(triggered()), MENU_RECEIVER, SLOT(restoreClosedTab()));
    addAction(m_actionRestoreTab);

    QShortcut* reloadByPassCacheAction = new QShortcut(QKeySequence("Ctrl+F5"), this);
    QShortcut* reloadByPassCacheAction2 = new QShortcut(QKeySequence("Ctrl+Shift+R"), this);
    connect(reloadByPassCacheAction, SIGNAL(activated()), MENU_RECEIVER, SLOT(reloadByPassCache()));
    connect(reloadByPassCacheAction2, SIGNAL(activated()), MENU_RECEIVER, SLOT(reloadByPassCache()));

    QShortcut* reloadAction = new QShortcut(QKeySequence("Ctrl+R"), this);
    connect(reloadAction, SIGNAL(activated()), MENU_RECEIVER, SLOT(reload()));

    QShortcut* openLocationAction = new QShortcut(QKeySequence("Alt+D"), this);
    connect(openLocationAction, SIGNAL(activated()), MENU_RECEIVER, SLOT(openLocation()));

    QShortcut* closeTabAction = new QShortcut(QKeySequence("Ctrl+W"), this);
    QShortcut* closeTabAction2 = new QShortcut(QKeySequence("Ctrl+F4"), this);
    connect(closeTabAction, SIGNAL(activated()), MENU_RECEIVER, SLOT(closeTab()));
    connect(closeTabAction2, SIGNAL(activated()), MENU_RECEIVER, SLOT(closeTab()));

    // Make shortcuts available even in fullscreen (menu hidden)
    QList<QAction*> actions = menuBar()->actions();
    for (int i = 0; i < actions.size(); ++i) {
        QAction* action = actions.at(i);
        if (action->menu()) {
            actions += action->menu()->actions();
        }
        addAction(action);
    }
}

QKeySequence BrowserWindow::actionShortcut(QKeySequence shortcut, QKeySequence fallBack,
        QKeySequence shortcutRTL, QKeySequence fallbackRTL)
{
    if (isRightToLeft() && (!shortcutRTL.isEmpty() || !fallbackRTL.isEmpty())) {
        return (shortcutRTL.isEmpty() ? fallbackRTL : shortcutRTL);
    }
    else {
        return (shortcut.isEmpty() ? fallBack : shortcut);
    }
}

#ifdef Q_OS_MAC
void BrowserWindow::setupMacMenu()
{
    // menus
    m_menuFile = menuBar()->actions().at(0)->menu();
    m_menuEdit = menuBar()->actions().at(1)->menu();
    m_menuView = menuBar()->actions().at(2)->menu();
    m_menuHistory = qobject_cast<Menu*>(menuBar()->actions().at(3)->menu());
    m_menuBookmarks = qobject_cast<Menu*>(menuBar()->actions().at(4)->menu());
    m_menuTools = menuBar()->actions().at(5)->menu();
    m_menuHelp = menuBar()->actions().at(6)->menu();

    m_toolbarsMenu = m_menuView->actions().at(0)->menu();
    m_menuEncoding = m_menuView->actions().at(12)->menu();

    m_menuHistoryRecent = qobject_cast<Menu*>(m_menuHistory->actions().at(5)->menu());
    m_menuHistoryMost = qobject_cast<Menu*>(m_menuHistory->actions().at(6)->menu());
    m_menuClosedTabs = m_menuHistory->actions().at(7)->menu();

    // actions
    m_actionCloseWindow = m_menuFile->actions().at(5);
    m_actionQuit = m_menuFile->actions().at(13);
    m_actionAbout = m_menuFile->actions().at(14);
    m_actionPreferences = m_menuFile->actions().at(15);

    m_actionShowToolbar = m_menuView->actions().at(0)->menu()->actions().at(0);
    m_actionShowBookmarksToolbar = m_menuView->actions().at(0)->menu()->actions().at(1);
    m_actionTabsOnTop = m_menuView->actions().at(0)->menu()->actions().at(3);
    m_actionShowStatusbar = m_menuView->actions().at(2);
    m_actionStop =  m_menuView->actions().at(4);
    m_actionReload =  m_menuView->actions().at(5);
    m_actionCaretBrowsing = m_menuView->actions().at(11);
    m_actionPageSource =  m_menuView->actions().at(14);
    m_actionShowFullScreen = m_menuView->actions().at(15);

    m_actionPageInfo = m_menuTools->actions().at(1);
    m_actionPrivateBrowsing = m_menuTools->actions().at(9);
}

void BrowserWindow::refreshStateOfAllActions()
{
    mApp->macMenuReceiver()->aboutToShowFileMenu(m_menuFile);
    mApp->macMenuReceiver()->aboutToShowHistoryMenu(m_menuHistory);
    mApp->macMenuReceiver()->aboutToShowBookmarksMenu(m_menuBookmarks);
    mApp->macMenuReceiver()->aboutToShowViewMenu(m_menuView);
    mApp->macMenuReceiver()->aboutToShowEditMenu(m_menuEdit);
    mApp->macMenuReceiver()->aboutToShowToolsMenu(m_menuTools);
}
#endif

void BrowserWindow::loadSettings()
{
    Settings settings;

    //Url settings
    settings.beginGroup("Web-URL-Settings");
    m_homepage = settings.value("homepage", "qupzilla:start").toUrl();
    settings.endGroup();

    QWebSettings* websettings = mApp->webSettings();
    websettings->setAttribute(QWebSettings::JavascriptCanAccessClipboard, true);

    //Browser Window settings
    settings.beginGroup("Browser-View-Settings");
    bool showStatusBar = settings.value("showStatusBar", true).toBool();
    bool showReloadButton = settings.value("showReloadButton", true).toBool();
    bool showHomeButton = settings.value("showHomeButton", true).toBool();
    bool showBackForwardButtons = settings.value("showBackForwardButtons", true).toBool();
    bool showAddTabButton = settings.value("showAddTabButton", false).toBool();
    bool showWebSearchBar = settings.value("showWebSearchBar", true).toBool();
    bool showBookmarksToolbar = settings.value("showBookmarksToolbar", true).toBool();
    bool showNavigationToolbar = settings.value("showNavigationToolbar", true).toBool();
    bool showMenuBar = settings.value("showMenubar", true).toBool();
    bool makeTransparent = settings.value("useTransparentBackground", false).toBool();
    m_sideBarWidth = settings.value("SideBarWidth", 250).toInt();
    m_webViewWidth = settings.value("WebViewWidth", 2000).toInt();
    const QString activeSideBar = settings.value("SideBar", "None").toString();

    // Make sure both menubar and navigationbar are not hidden
    // Fixes #781
    if (!showNavigationToolbar) {
        showMenuBar = true;
        settings.setValue("showMenubar", true);
    }

    settings.endGroup();

    settings.beginGroup("Shortcuts");
    m_useTabNumberShortcuts = settings.value("useTabNumberShortcuts", true).toBool();
    m_useSpeedDialNumberShortcuts = settings.value("useSpeedDialNumberShortcuts", true).toBool();
    settings.endGroup();

    m_adblockIcon->setEnabled(settings.value("AdBlock/enabled", true).toBool());

    statusBar()->setVisible(!isFullScreen() && showStatusBar);
    m_bookmarksToolbar->setVisible(showBookmarksToolbar);
    m_navigationBar->setVisible(showNavigationToolbar);
    menuBar()->setVisible(!isFullScreen() && showMenuBar);

#ifndef Q_OS_MAC
    m_navigationBar->setSuperMenuVisible(!showMenuBar);
#endif
    m_navigationBar->buttonReloadStop()->setVisible(showReloadButton);
    m_navigationBar->buttonHome()->setVisible(showHomeButton);
    m_navigationBar->buttonBack()->setVisible(showBackForwardButtons);
    m_navigationBar->buttonNext()->setVisible(showBackForwardButtons);
    m_navigationBar->searchLine()->setVisible(showWebSearchBar);
    m_navigationBar->buttonAddTab()->setVisible(showAddTabButton);

    m_sideBarManager->showSideBar(activeSideBar, false);

    // Private browsing
    m_privateBrowsing->setVisible(mApp->isPrivateSession());

#ifdef Q_OS_WIN
    if (m_usingTransparentBackground && !makeTransparent) {
        QtWin::extendFrameIntoClientArea(this, 0, 0, 0, 0);
        QtWin::enableBlurBehindWindow(this, false);
        m_tabWidget->getTabBar()->enableBluredBackground(false);
        m_usingTransparentBackground = false;
    }
#endif

    if (!makeTransparent) {
        return;
    }

    // Transparency on X11 (no blur like on Windows)
#ifdef QZ_WS_X11
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground, false);
    QPalette pal = palette();
    QColor bg = pal.window().color();
    bg.setAlpha(180);
    pal.setColor(QPalette::Window, bg);
    setPalette(pal);
    ensurePolished(); // workaround Oxygen filling the background
    setAttribute(Qt::WA_StyledBackground, false);
#endif

#ifdef Q_OS_WIN
    if (QtWin::isCompositionEnabled()) {
        setContentsMargins(0, 0, 0, 0);

        m_usingTransparentBackground = true;

        if (!isFullScreen()) {
            m_tabWidget->getTabBar()->enableBluredBackground(true);
            QtWin::extendFrameIntoClientArea(this);
        }

        //install event filter
        menuBar()->installEventFilter(this);
        m_tabWidget->getTabBar()->installEventFilter(this);
        m_navigationBar->installEventFilter(this);
        m_bookmarksToolbar->installEventFilter(this);
        statusBar()->installEventFilter(this);
        m_navigationContainer->installEventFilter(this);
    }
#endif
}

void BrowserWindow::goForward()
{
    weView()->forward();
}

void BrowserWindow::goBack()
{
    weView()->back();
}

QMenuBar* BrowserWindow::menuBar() const
{
#ifdef Q_OS_MAC
    return mApp->macMenuReceiver()->menuBar();
#else
    return QMainWindow::menuBar();
#endif
}

TabbedWebView* BrowserWindow::weView() const
{
    return weView(m_tabWidget->currentIndex());
}

TabbedWebView* BrowserWindow::weView(int index) const
{
    WebTab* webTab = qobject_cast<WebTab*>(m_tabWidget->widget(index));
    if (!webTab) {
        return 0;
    }

    return webTab->view();
}

LocationBar* BrowserWindow::locationBar() const
{
    return qobject_cast<LocationBar*>(m_tabWidget->locationBars()->currentWidget());
}

Qz::BrowserWindowType BrowserWindow::windowType() const
{
    return m_windowType;
}

void BrowserWindow::popupToolbarsMenu(const QPoint &pos)
{
    aboutToShowViewMenu();
    m_toolbarsMenu->exec(pos);
    aboutToHideViewMenu();
}

bool BrowserWindow::isTransparentBackgroundAllowed()
{
    return m_usingTransparentBackground && !isFullScreen();
}

bool BrowserWindow::tabsOnTop() const
{
    if (m_tabsOnTopState == -1) {
        m_tabsOnTopState = qzSettings->tabsOnTop ? 1 : 0;
    }

    return m_tabsOnTopState == 1;
}

void BrowserWindow::setWindowTitle(const QString &t)
{
    QString title = t;

    if (mApp->isPrivateSession()) {
        title.append(tr(" (Private Browsing)"));
    }

    if (m_isStarting) {
        m_lastWindowTitle = title;
        return;
    }

    QMainWindow::setWindowTitle(title);
}

void BrowserWindow::aboutToShowFileMenu()
{
#ifndef Q_OS_MAC
    m_actionCloseWindow->setEnabled(mApp->windowCount() > 1);
#endif
}

void BrowserWindow::aboutToHideFileMenu()
{
    m_actionCloseWindow->setEnabled(true);
}

void BrowserWindow::aboutToShowViewMenu()
{
    m_actionShowToolbar->setChecked(m_navigationBar->isVisible());
#ifndef Q_OS_MAC
    m_actionShowMenubar->setChecked(menuBar()->isVisible());
#else
    m_sideBarManager->setSideBarMenu(m_menuView->actions().at(1)->menu());
#endif

    m_actionShowStatusbar->setChecked(statusBar()->isVisible());
    m_actionShowBookmarksToolbar->setChecked(m_bookmarksToolbar->isVisible());
    m_actionTabsOnTop->setChecked(tabsOnTop());

    m_actionPageSource->setEnabled(true);

#if QTWEBKIT_FROM_2_3
    m_actionCaretBrowsing->setChecked(mApp->webSettings()->testAttribute(QWebSettings::CaretBrowsingEnabled));
#endif
}

void BrowserWindow::aboutToHideViewMenu()
{
#ifndef Q_OS_MAC
    m_actionPageSource->setEnabled(false);
#endif
}

void BrowserWindow::aboutToShowEditMenu()
{
    WebView* view = weView();

    m_menuEdit->actions().at(0)->setEnabled(view->pageAction(QWebPage::Undo)->isEnabled());
    m_menuEdit->actions().at(1)->setEnabled(view->pageAction(QWebPage::Redo)->isEnabled());
    // Separator
    m_menuEdit->actions().at(3)->setEnabled(view->pageAction(QWebPage::Cut)->isEnabled());
    m_menuEdit->actions().at(4)->setEnabled(view->pageAction(QWebPage::Copy)->isEnabled());
    m_menuEdit->actions().at(5)->setEnabled(view->pageAction(QWebPage::Paste)->isEnabled());
    // Separator
    m_menuEdit->actions().at(7)->setEnabled(view->pageAction(QWebPage::SelectAll)->isEnabled());
}

void BrowserWindow::aboutToHideEditMenu()
{
#ifndef Q_OS_MAC
    foreach (QAction* act, m_menuEdit->actions()) {
        act->setEnabled(false);
    }
#endif

    m_menuEdit->actions().at(8)->setEnabled(true);
    m_actionPreferences->setEnabled(true);
}

void BrowserWindow::aboutToShowToolsMenu()
{
    m_actionPageInfo->setEnabled(true);
}

void BrowserWindow::aboutToHideToolsMenu()
{
#ifndef Q_OS_MAC
    m_actionPageInfo->setEnabled(false);
#endif
}

void BrowserWindow::aboutToShowEncodingMenu()
{
    m_menuEncoding->clear();
    QMenu* menuISO = new QMenu("ISO", this);
    QMenu* menuUTF = new QMenu("UTF", this);
    QMenu* menuWindows = new QMenu("Windows", this);
    QMenu* menuIscii = new QMenu("Iscii", this);
    QMenu* menuOther = new QMenu(tr("Other"), this);

    QList<QByteArray> available = QTextCodec::availableCodecs();
    qSort(available);
    const QString activeCodec = mApp->webSettings()->defaultTextEncoding();

    foreach (const QByteArray &name, available) {
        QTextCodec* codec = QTextCodec::codecForName(name);
        if (codec && codec->aliases().contains(name)) {
            continue;
        }

        const QString nameString = QString::fromUtf8(name);

        QAction* action = new QAction(nameString, 0);
        action->setData(nameString);
        action->setCheckable(true);
        connect(action, SIGNAL(triggered()), MENU_RECEIVER, SLOT(changeEncoding()));
        if (activeCodec.compare(nameString, Qt::CaseInsensitive) == 0) {
            action->setChecked(true);
        }

        if (nameString.startsWith(QLatin1String("ISO"))) {
            menuISO->addAction(action);
        }
        else if (nameString.startsWith(QLatin1String("UTF"))) {
            menuUTF->addAction(action);
        }
        else if (nameString.startsWith(QLatin1String("windows"))) {
            menuWindows->addAction(action);
        }
        else if (nameString.startsWith(QLatin1String("Iscii"))) {
            menuIscii->addAction(action);
        }
        else if (nameString == QLatin1String("System")) {
            m_menuEncoding->addAction(action);
        }
        else {
            menuOther->addAction(action);
        }
    }

    m_menuEncoding->addSeparator();
    if (!menuISO->isEmpty()) {
        m_menuEncoding->addMenu(menuISO);
    }
    if (!menuUTF->isEmpty()) {
        m_menuEncoding->addMenu(menuUTF);
    }
    if (!menuWindows->isEmpty()) {
        m_menuEncoding->addMenu(menuWindows);
    }
    if (!menuIscii->isEmpty()) {
        m_menuEncoding->addMenu(menuIscii);
    }
    if (!menuOther->isEmpty()) {
        m_menuEncoding->addMenu(menuOther);
    }
}

void BrowserWindow::changeEncoding(QObject* obj)
{
    if (!obj) {
        obj = sender();
    }

    if (QAction* action = qobject_cast<QAction*>(obj)) {
        const QString encoding = action->data().toString();
        mApp->webSettings()->setDefaultTextEncoding(encoding);

        Settings settings;
        settings.setValue("Web-Browser-Settings/DefaultEncoding", encoding);

        reload();
    }
}

void BrowserWindow::triggerCaretBrowsing()
{
#if QTWEBKIT_FROM_2_3
    bool enable = !mApp->webSettings()->testAttribute(QWebSettings::CaretBrowsingEnabled);

    Settings settings;
    settings.beginGroup("Web-Browser-Settings");
    settings.setValue("CaretBrowsing", enable);
    settings.endGroup();

    mApp->webSettings()->setAttribute(QWebSettings::CaretBrowsingEnabled, enable);
#endif
}

void BrowserWindow::bookmarkPage()
{
    TabbedWebView* view = weView();
    BookmarksTools::addBookmarkDialog(this, view->url(), view->title());
}

void BrowserWindow::bookmarkAllTabs()
{
    BookmarksTools::bookmarkAllTabsDialog(this, m_tabWidget);
}

void BrowserWindow::addBookmark(const QUrl &url, const QString &title)
{
    BookmarksTools::addBookmarkDialog(this, url, title);
}

void BrowserWindow::newWindow()
{
    mApp->makeNewWindow(Qz::BW_NewWindow);
}

void BrowserWindow::goHome()
{
    loadAddress(m_homepage);
}

void BrowserWindow::editUndo()
{
    weView()->triggerPageAction(QWebPage::Undo);
}

void BrowserWindow::editRedo()
{
    weView()->triggerPageAction(QWebPage::Redo);
}

void BrowserWindow::editCut()
{
    weView()->triggerPageAction(QWebPage::Cut);
}

void BrowserWindow::editCopy()
{
    weView()->triggerPageAction(QWebPage::Copy);
}

void BrowserWindow::editPaste()
{
    weView()->triggerPageAction(QWebPage::Paste);
}

void BrowserWindow::editSelectAll()
{
    weView()->selectAll();
}

void BrowserWindow::zoomIn()
{
    weView()->zoomIn();
}

void BrowserWindow::zoomOut()
{
    weView()->zoomOut();
}

void BrowserWindow::zoomReset()
{
    weView()->zoomReset();
}

void BrowserWindow::goHomeInNewTab()
{
    m_tabWidget->addView(m_homepage, Qz::NT_SelectedTab);
}

void BrowserWindow::stop()
{
    weView()->stop();
}

void BrowserWindow::reload()
{
    weView()->reload();
}

void BrowserWindow::reloadByPassCache()
{
    weView()->triggerPageAction(QWebPage::ReloadAndBypassCache);
}

void BrowserWindow::loadActionUrl(QObject* obj)
{
    if (!obj) {
        obj = sender();
    }

    if (QAction* action = qobject_cast<QAction*>(obj)) {
        loadAddress(action->data().toUrl());
    }
}

void BrowserWindow::loadActionUrlInNewTab(QObject* obj)
{
    if (!obj) {
        obj = sender();
    }

    if (QAction* action = qobject_cast<QAction*>(obj)) {
        m_tabWidget->addView(action->data().toUrl(), Qz::NT_SelectedTabAtTheEnd);
    }
}

void BrowserWindow::loadAddress(const QUrl &url)
{
    if (weView()->webTab()->isPinned()) {
        int index = m_tabWidget->addView(url, qzSettings->newTabPosition);
        weView(index)->setFocus();
    }
    else {
        weView()->setFocus();
        weView()->load(url);
    }
}

void BrowserWindow::showCookieManager()
{
    CookieManager* m = mApp->cookieManager();
    m->refreshTable();

    m->show();
    m->raise();
}


void BrowserWindow::showHistoryManager()
{
    mApp->browsingLibrary()->showHistory(this);
}

void BrowserWindow::showRSSManager()
{
    mApp->browsingLibrary()->showRSS(this);
}

void BrowserWindow::showBookmarksManager()
{
    mApp->browsingLibrary()->showBookmarks(this);
}

void BrowserWindow::showClearPrivateData()
{
    ClearPrivateData clear(this, this);
    clear.exec();
}

void BrowserWindow::showDownloadManager()
{
    mApp->downManager()->show();
}

void BrowserWindow::showPreferences()
{
    Preferences* prefs = new Preferences(this, this);
    prefs->show();
}

void BrowserWindow::showSource(QWebFrame* frame, const QString &selectedHtml)
{
    if (!frame) {
        frame = weView()->page()->mainFrame();
    }

    SourceViewer* source = new SourceViewer(frame, selectedHtml);
    QzTools::centerWidgetToParent(source, this);
    source->show();
}

void BrowserWindow::showPageInfo()
{
    SiteInfo* info = new SiteInfo(weView(), this);
    info->setAttribute(Qt::WA_DeleteOnClose);
    info->show();
}

void BrowserWindow::showBookmarksToolbar()
{
    bool status = m_bookmarksToolbar->isVisible();

    setUpdatesEnabled(false);
    m_bookmarksToolbar->setVisible(!status);
    setUpdatesEnabled(true);

    Settings settings;
    settings.setValue("Browser-View-Settings/showBookmarksToolbar", !status);
}

SideBar* BrowserWindow::addSideBar()
{
    if (m_sideBar) {
        return m_sideBar.data();
    }

    m_sideBar = new SideBar(m_sideBarManager, this);

    m_mainSplitter->insertWidget(0, m_sideBar.data());
    m_mainSplitter->setCollapsible(0, false);

    m_mainSplitter->setSizes(QList<int>() << m_sideBarWidth << m_webViewWidth);

#ifdef Q_OS_WIN
    if (QtWin::isCompositionEnabled()) {
        applyBlurToMainWindow();
        m_sideBar.data()->installEventFilter(this);
    }
#endif

    return m_sideBar.data();
}

void BrowserWindow::saveSideBarWidth()
{
    // That +1 is important here, without it, the sidebar width would
    // decrease by 1 pixel every close

    m_sideBarWidth = m_mainSplitter->sizes().at(0) + 1;
    m_webViewWidth = width() - m_sideBarWidth;
}

void BrowserWindow::showNavigationToolbar()
{
    if (!menuBar()->isVisible() && !m_actionShowToolbar->isChecked()) {
        showMenubar();
    }

    setUpdatesEnabled(false);

    bool status = m_navigationBar->isVisible();
    m_navigationBar->setVisible(!status);

    Settings settings;
    settings.setValue("Browser-View-Settings/showNavigationToolbar", !status);

    setUpdatesEnabled(true);
}

void BrowserWindow::showMenubar()
{
#ifndef Q_OS_MAC
    if (!m_navigationBar->isVisible() && !m_actionShowMenubar->isChecked()) {
        showNavigationToolbar();
    }

    setUpdatesEnabled(false);

    menuBar()->setVisible(!menuBar()->isVisible());
    m_navigationBar->setSuperMenuVisible(!menuBar()->isVisible());

    Settings settings;
    settings.setValue("Browser-View-Settings/showMenubar", menuBar()->isVisible());

    setUpdatesEnabled(true);
#endif
}

void BrowserWindow::showStatusbar()
{
    setUpdatesEnabled(false);

    bool status = statusBar()->isVisible();
    statusBar()->setVisible(!status);

    Settings settings;
    settings.setValue("Browser-View-Settings/showStatusbar", !status);

    setUpdatesEnabled(true);
}

void BrowserWindow::showWebInspector(bool toggle)
{
    if (m_webInspectorDock) {
        if (toggle) {
            m_webInspectorDock.data()->toggleVisibility();
        }
        else  {
            m_webInspectorDock.data()->show();
        }
        return;
    }

    m_webInspectorDock = new WebInspectorDockWidget(this);
    connect(m_tabWidget, SIGNAL(currentChanged(int)), m_webInspectorDock.data(), SLOT(tabChanged()));
    addDockWidget(Qt::BottomDockWidgetArea, m_webInspectorDock.data());

#ifdef Q_OS_WIN
    if (QtWin::isCompositionEnabled()) {
        applyBlurToMainWindow();
        m_webInspectorDock.data()->installEventFilter(this);
    }
#endif
}

void BrowserWindow::showBookmarkImport()
{
    BookmarksImportDialog* b = new BookmarksImportDialog(this);
    b->show();
}

void BrowserWindow::triggerTabsOnTop(bool enable)
{
    if (enable) {
        m_mainLayout->insertWidget(0, m_tabWidget->getTabBar());
        m_mainLayout->insertWidget(1, m_navigationContainer);
    }
    else {
        m_mainLayout->insertWidget(0, m_navigationContainer);
        m_mainLayout->insertWidget(1, m_tabWidget->getTabBar());
    }

    m_tabsOnTopState = enable ? 1 : 0;
    if (enable != qzSettings->tabsOnTop) {
        Settings settings;
        settings.setValue("Browser-Tabs-Settings/TabsOnTop", enable);
        qzSettings->tabsOnTop = enable;
    }

#ifdef Q_OS_WIN
    // workaround for changing TabsOnTop state when sidebar is visible
    // TODO: we need a solution that changing TabsOnTop state
    //       doesn't call applyBlurToMainWindow() from eventFilter()
    QTimer::singleShot(0, this, SLOT(applyBlurToMainWindow()));
#endif
}

void BrowserWindow::refreshHistory()
{
    m_navigationBar->refreshHistory();
}

void BrowserWindow::currentTabChanged()
{
    TabbedWebView* view = weView();
    if (!view) {
        return;
    }

    setWindowTitle(tr("%1 - QupZilla").arg(view->title()));
    m_ipLabel->setText(view->getIp());
    view->setFocus();

    SearchToolBar* search = searchToolBar();
    if (search) {
        search->setWebView(view);
    }

    updateLoadingActions();

    // Setting correct tab order (LocationBar -> WebSearchBar -> WebView)
    setTabOrder(locationBar(), m_navigationBar->searchLine());
    setTabOrder(m_navigationBar->searchLine(), view);
}

void BrowserWindow::updateLoadingActions()
{
    TabbedWebView* view = weView();
    if (!view) {
        return;
    }

    bool isLoading = view->isLoading();

    m_ipLabel->setVisible(!isLoading);
    m_progressBar->setVisible(isLoading);
    m_actionStop->setEnabled(isLoading);
    m_actionReload->setEnabled(!isLoading);

    if (isLoading) {
        m_progressBar->setValue(view->loadingProgress());
        m_navigationBar->showStopButton();
    }
    else {
        m_navigationBar->showReloadButton();
    }
}

void BrowserWindow::addDeleteOnCloseWidget(QWidget* widget)
{
    if (!m_deleteOnCloseWidgets.contains(widget)) {
        m_deleteOnCloseWidgets.append(widget);
    }
}

void BrowserWindow::restoreWindowState(const RestoreManager::WindowData &d)
{
    restoreState(d.windowState);
    m_tabWidget->restoreState(d.tabsState, d.currentTab);
}

void BrowserWindow::aboutQupZilla()
{
    AboutDialog about(this);
    about.exec();
}

void BrowserWindow::addTab()
{
    m_tabWidget->addView(QUrl(), Qz::NT_SelectedNewEmptyTab, true);
}

void BrowserWindow::webSearch()
{
    m_navigationBar->searchLine()->setFocus();
    m_navigationBar->searchLine()->selectAll();
}

void BrowserWindow::searchOnPage()
{
    SearchToolBar* toolBar = searchToolBar();

    if (!toolBar) {
        const int searchPos = 3;

        toolBar = new SearchToolBar(weView(), this);
        m_mainLayout->insertWidget(searchPos, toolBar);
    }

    toolBar->focusSearchLine();

#ifdef Q_OS_WIN
    if (QtWin::isCompositionEnabled()) {
        applyBlurToMainWindow();
        toolBar->installEventFilter(this);
    }
#endif
}

void BrowserWindow::openFile()
{
    const QString fileTypes = QString("%1(*.html *.htm *.shtml *.shtm *.xhtml);;"
                                      "%2(*.png *.jpg *.jpeg *.bmp *.gif *.svg *.tiff);;"
                                      "%3(*.txt);;"
                                      "%4(*.*)").arg(tr("HTML files"), tr("Image files"), tr("Text files"), tr("All files"));

    const QString filePath = QzTools::getOpenFileName("MainWindow-openFile", this, tr("Open file..."), QDir::homePath(), fileTypes);

    if (!filePath.isEmpty()) {
        loadAddress(QUrl::fromLocalFile(filePath));
    }
}

void BrowserWindow::openLocation()
{
    locationBar()->setFocus();
    locationBar()->selectAll();
}

bool BrowserWindow::fullScreenNavigationVisible() const
{
    return m_navigationContainer->isVisible();
}

void BrowserWindow::showNavigationWithFullScreen()
{
    if (m_hideNavigationTimer->isActive()) {
        m_hideNavigationTimer->stop();
    }

    m_navigationContainer->show();
    m_tabWidget->getTabBar()->updateVisibilityWithFullscreen(true);
}

void BrowserWindow::hideNavigationWithFullScreen()
{
    if (!m_hideNavigationTimer->isActive()) {
        m_hideNavigationTimer->start();
    }
}

void BrowserWindow::hideNavigationSlot()
{
    TabbedWebView* view = weView();
    bool mouseInView = view && view->underMouse();

    if (isFullScreen() && mouseInView) {
        m_navigationContainer->hide();
        m_tabWidget->getTabBar()->updateVisibilityWithFullscreen(false);
    }
}

bool BrowserWindow::event(QEvent* event)
{
    switch (event->type()) {
    case QEvent::WindowStateChange: {
        QWindowStateChangeEvent* ev = static_cast<QWindowStateChangeEvent*>(event);

        if (!(ev->oldState() & Qt::WindowFullScreen) && windowState() & Qt::WindowFullScreen) {
            // Enter fullscreen
            m_windowStates = ev->oldState();

            m_menuBarVisible = menuBar()->isVisible();
            m_statusBarVisible = statusBar()->isVisible();
            menuBar()->hide();
            statusBar()->hide();
            m_navigationContainer->hide();
            m_tabWidget->getTabBar()->hide();
#ifndef Q_OS_MAC
            m_navigationBar->setSuperMenuVisible(false);
#endif
            m_hideNavigationTimer->stop();
            m_actionShowFullScreen->setChecked(true);
            m_navigationBar->buttonExitFullscreen()->setVisible(true);
            emit setWebViewMouseTracking(true);
#ifdef Q_OS_WIN
            if (m_usingTransparentBackground) {
                m_tabWidget->getTabBar()->enableBluredBackground(false);
                QtWin::extendFrameIntoClientArea(this, 0, 0, 0 , 0);
                QtWin::enableBlurBehindWindow(this, false);
            }
#endif
        }
        else if (ev->oldState() & Qt::WindowFullScreen && !(windowState() & Qt::WindowFullScreen)) {
            // Leave fullscreen
            setWindowState(m_windowStates);

            menuBar()->setVisible(m_menuBarVisible);
            statusBar()->setVisible(m_statusBarVisible);
            m_navigationContainer->show();
            m_tabWidget->getTabBar()->updateVisibilityWithFullscreen(true);
            m_tabWidget->getTabBar()->setVisible(true);
#ifndef Q_OS_MAC
            m_navigationBar->setSuperMenuVisible(!m_menuBarVisible);
#endif
            m_hideNavigationTimer->stop();
            m_actionShowFullScreen->setChecked(false);
            m_navigationBar->buttonExitFullscreen()->setVisible(false);
            emit setWebViewMouseTracking(false);
#ifdef Q_OS_WIN
            if (m_usingTransparentBackground) {
                m_tabWidget->getTabBar()->enableBluredBackground(true);
                applyBlurToMainWindow(true);
            }
#endif
        }
    }

    default:
        break;
    }

    return QMainWindow::event(event);
}

void BrowserWindow::toggleFullScreen()
{
    if (isFullScreen()) {
        showNormal();
    }
    else {
        showFullScreen();
    }
}

void BrowserWindow::savePage()
{
    weView()->savePageAs();
}

void BrowserWindow::sendLink()
{
    weView()->sendPageByMail();
}

void BrowserWindow::printPage(QWebFrame* frame)
{
    QPrintPreviewDialog* dialog = new QPrintPreviewDialog(this);
    dialog->resize(800, 750);
    dialog->printer()->setCreator(tr("QupZilla %1 (%2)").arg(Qz::VERSION, Qz::WWWADDRESS));

    if (!frame) {
        dialog->printer()->setDocName(QzTools::getFileNameFromUrl(weView()->url()));

        connect(dialog, SIGNAL(paintRequested(QPrinter*)), weView(), SLOT(print(QPrinter*)));
    }
    else {
        dialog->printer()->setDocName(QzTools::getFileNameFromUrl(frame->url()));

        connect(dialog, SIGNAL(paintRequested(QPrinter*)), frame, SLOT(print(QPrinter*)));
    }

    dialog->exec();

    dialog->deleteLater();
}

void BrowserWindow::savePageScreen()
{
    PageScreen* p = new PageScreen(weView(), this);
    p->show();
}

void BrowserWindow::resizeEvent(QResizeEvent* event)
{
    m_bookmarksToolbar->setMaximumWidth(width());

    QMainWindow::resizeEvent(event);
}

void BrowserWindow::keyPressEvent(QKeyEvent* event)
{
    if (mApp->plugins()->processKeyPress(Qz::ON_BrowserWindow, this, event)) {
        return;
    }

    int number = -1;
    TabbedWebView* view = weView();

    switch (event->key()) {
    case Qt::Key_Back:
        if (view) {
            weView()->back();
            event->accept();
        }
        break;

    case Qt::Key_Forward:
        if (view) {
            weView()->forward();
            event->accept();
        }
        break;

    case Qt::Key_Stop:
        if (view) {
            weView()->stop();
            event->accept();
        }
        break;

    case Qt::Key_Refresh:
        if (view) {
            weView()->reload();
            event->accept();
        }
        break;

    case Qt::Key_HomePage:
        goHome();
        event->accept();
        break;

    case Qt::Key_Favorites:
        showBookmarksManager();
        event->accept();
        break;

    case Qt::Key_Search:
        searchOnPage();
        event->accept();
        break;

    case Qt::Key_F6:
    case Qt::Key_OpenUrl:
        openLocation();
        event->accept();
        break;

    case Qt::Key_History:
        showHistoryManager();
        event->accept();
        break;

    case Qt::Key_AddFavorite:
        bookmarkPage();
        event->accept();
        break;

    case Qt::Key_News:
        showRSSManager();
        event->accept();
        break;

    case Qt::Key_Tools:
        showPreferences();
        event->accept();
        break;

    case Qt::Key_Tab:
        if (event->modifiers() == Qt::ControlModifier) {
            m_tabWidget->nextTab();
            event->accept();
        }
        break;

    case Qt::Key_Backtab:
        if (event->modifiers() == (Qt::ControlModifier + Qt::ShiftModifier)) {
            m_tabWidget->previousTab();
            event->accept();
        }
        break;

    case Qt::Key_PageDown:
        if (event->modifiers() == Qt::ControlModifier) {
            m_tabWidget->nextTab();
            event->accept();
        }
        break;

    case Qt::Key_PageUp:
        if (event->modifiers() == Qt::ControlModifier) {
            m_tabWidget->previousTab();
            event->accept();
        }
        break;

    case Qt::Key_Equal:
        if (view && event->modifiers() == Qt::ControlModifier) {
            weView()->zoomIn();
            event->accept();
        }
        break;

    case Qt::Key_I:
        if (event->modifiers() == Qt::ControlModifier) {
            showPageInfo();
            event->accept();
        }
        break;

    case Qt::Key_U:
        if (event->modifiers() == Qt::ControlModifier) {
            showSource();
            event->accept();
        }
        break;

    case Qt::Key_1:
        number = 1;
        break;
    case Qt::Key_2:
        number = 2;
        break;
    case Qt::Key_3:
        number = 3;
        break;
    case Qt::Key_4:
        number = 4;
        break;
    case Qt::Key_5:
        number = 5;
        break;
    case Qt::Key_6:
        number = 6;
        break;
    case Qt::Key_7:
        number = 7;
        break;
    case Qt::Key_8:
        number = 8;
        break;
    case Qt::Key_9:
        number = 9;
        break;

    default:
        break;
    }

    if (number != -1) {
        if (event->modifiers() & Qt::AltModifier && m_useTabNumberShortcuts) {
            if (number == 9) {
                number = m_tabWidget->count();
            }
            m_tabWidget->setCurrentIndex(number - 1);
            return;
        }
        if (event->modifiers() & Qt::ControlModifier && m_useSpeedDialNumberShortcuts) {
            const QUrl url = mApp->plugins()->speedDial()->urlForShortcut(number - 1);
            if (url.isValid()) {
                loadAddress(url);
                return;
            }
        }
    }

    QMainWindow::keyPressEvent(event);
}

void BrowserWindow::keyReleaseEvent(QKeyEvent* event)
{
    if (mApp->plugins()->processKeyRelease(Qz::ON_BrowserWindow, this, event)) {
        return;
    }

    QMainWindow::keyReleaseEvent(event);
}

void BrowserWindow::closeEvent(QCloseEvent* event)
{
    if (mApp->isClosing()) {
        return;
    }

    Settings settings;
    int afterLaunch = settings.value("Web-URL-Settings/afterLaunch", 3).toInt();
    bool askOnClose = settings.value("Browser-Tabs-Settings/AskOnClosing", true).toBool();

    if (afterLaunch == 3 && mApp->windowCount() == 1) {
        askOnClose = false;
    }

    if (askOnClose && m_tabWidget->normalTabsCount() > 1) {
        CheckBoxDialog dialog(QDialogButtonBox::Yes | QDialogButtonBox::No, this);
        dialog.setText(tr("There are still %1 open tabs and your session won't be stored. \nAre you sure to close this window?").arg(m_tabWidget->count()));
        dialog.setCheckBoxText(tr("Don't ask again"));
        dialog.setWindowTitle(tr("There are still open tabs"));
        dialog.setIcon(IconProvider::standardIcon(QStyle::SP_MessageBoxWarning));

        if (dialog.exec() != QDialog::Accepted) {
            event->ignore();
            return;
        }

        if (dialog.isChecked()) {
            settings.setValue("Browser-Tabs-Settings/AskOnClosing", false);
        }
    }

    m_isClosing = true;

#ifndef Q_OS_MAC
    if (mApp->windowCount() == 1) {
        if (quitApp()) {
            disconnectObjects();
            event->accept();
        }
        else {
            event->ignore();
        }

        return;
    }
#else
    QTimer::singleShot(0, this, SLOT(refreshStateOfAllActions()));
#endif

    mApp->aboutToCloseWindow(this);

    disconnectObjects();
    event->accept();
}

SearchToolBar* BrowserWindow::searchToolBar()
{
    SearchToolBar* toolBar = 0;
    const int searchPos = 3;

    if (m_mainLayout->count() == searchPos + 1) {
        toolBar = qobject_cast<SearchToolBar*>(m_mainLayout->itemAt(searchPos)->widget());
    }

    return toolBar;
}

void BrowserWindow::disconnectObjects()
{
    // Disconnecting all important widgets before deleting this window
    // so it cannot happen that slots will be invoked after the object
    // is deleted.
    // We have to do it this way, because ~QObject is deleting all child
    // objects with plain delete - not deleteLater().
    //
    // Also using own disconnectObjects() method, not default disconnect()
    // because we need to retain connections to destroyed(QObject*) signal
    // in order to avoid crashes for example with setting stylesheets
    // (QStyleSheet backend is holding list of all widgets)

    m_tabWidget->disconnectObjects();
    m_tabWidget->getTabBar()->disconnectObjects();

    foreach (WebTab* tab, m_tabWidget->allTabs()) {
        tab->disconnectObjects();
        tab->view()->disconnectObjects();
        tab->view()->page()->disconnectObjects();
    }

    foreach (const QPointer<QWidget> &pointer, m_deleteOnCloseWidgets) {
        if (pointer) {
            pointer.data()->deleteLater();
        }
    }

    mApp->plugins()->emitMainWindowDeleted(this);
}

void BrowserWindow::closeWindow()
{
#ifdef Q_OS_MAC
    close();
#else
    if (mApp->windowCount() > 1) {
        close();
    }
#endif
}

bool BrowserWindow::quitApp()
{
    if (m_sideBar) {
        saveSideBarWidth();
    }

    if (!mApp->isPrivateSession()) {
        Settings settings;
        settings.beginGroup("Browser-View-Settings");
        settings.setValue("WindowMaximised", windowState().testFlag(Qt::WindowMaximized));
        settings.setValue("LocationBarWidth", m_navigationBar->splitter()->sizes().at(0));
        settings.setValue("WebSearchBarWidth", m_navigationBar->splitter()->sizes().at(1));
        settings.setValue("SideBarWidth", m_sideBarWidth);
        settings.setValue("WebViewWidth", m_webViewWidth);

        if (!isFullScreen()) {
            settings.setValue("WindowGeometry", saveGeometry());
        }
        settings.endGroup();
    }

    mApp->quitApplication();
    return true;
}

void BrowserWindow::closeTab()
{
    // Don't close pinned tabs with keyboard shortcuts (Ctrl+W, Ctrl+F4)
    if (weView() && !weView()->webTab()->isPinned()) {
        m_tabWidget->closeTab();
    }
}

void BrowserWindow::restoreClosedTab(QObject* obj)
{
    if (!obj) {
        obj = sender();
    }
    m_tabWidget->restoreClosedTab(obj);
}

void BrowserWindow::restoreAllClosedTabs()
{
    m_tabWidget->restoreAllClosedTabs();
}

void BrowserWindow::clearClosedTabsList()
{
    m_tabWidget->clearClosedTabsList();
}

bool BrowserWindow::bookmarksMenuChanged()
{
#ifdef Q_OS_MAC
    return mApp->macMenuReceiver()->bookmarksMenuChanged();
#else
    return m_bookmarksMenuChanged;
#endif
}

void BrowserWindow::setBookmarksMenuChanged(bool changed)
{
#ifdef Q_OS_MAC
    mApp->macMenuReceiver()->setBookmarksMenuChanged(changed);
#else
    m_bookmarksMenuChanged = changed;
#endif
}

QAction* BrowserWindow::menuBookmarksAction()
{
#ifdef Q_OS_MAC
    return mApp->macMenuReceiver()->menuBookmarksAction();
#else
    return m_menuBookmarksAction;
#endif
}

void BrowserWindow::setMenuBookmarksAction(QAction* action)
{
#ifdef Q_OS_MAC
    mApp->macMenuReceiver()->setMenuBookmarksAction(action);
#else
    m_menuBookmarksAction = action;
#endif
}

QByteArray BrowserWindow::saveState(int version) const
{
#ifdef QZ_WS_X11
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);

    stream << QMainWindow::saveState(version);
    stream << getCurrentVirtualDesktop();

    return data;
#else
    return QMainWindow::saveState(version);
#endif
}

bool BrowserWindow::restoreState(const QByteArray &state, int version)
{
#ifdef QZ_WS_X11
    QByteArray windowState;
    int desktopId = -1;

    QDataStream stream(state);
    stream >> windowState;
    stream >> desktopId;

    moveToVirtualDesktop(desktopId);

    return QMainWindow::restoreState(windowState, version);
#else
    return QMainWindow::restoreState(state, version);
#endif
}

#ifdef QZ_WS_X11
int BrowserWindow::getCurrentVirtualDesktop() const
{
    Display* display = static_cast<Display*>(QzTools::X11Display(this));
    Atom actual_type;
    int actual_format;
    unsigned long nitems;
    unsigned long bytes;
    unsigned long* data;

    Atom net_wm_desktop = XInternAtom(display, "_NET_WM_DESKTOP", False);
    if (net_wm_desktop == None) {
        return -1;
    }

    int status = XGetWindowProperty(display, winId(), net_wm_desktop, 0, 1,
                                    False, XA_CARDINAL, &actual_type, &actual_format,
                                    &nitems, &bytes, (unsigned char**) &data);

    if (status != Success || data == NULL) {
        return -1;
    }

    int desktop = *data;
    XFree(data);

    return desktop;
}

void BrowserWindow::moveToVirtualDesktop(int desktopId)
{
    // Don't move when window is already visible or it is first app window
    if (desktopId < 0 || isVisible() || m_windowType == Qz::BW_FirstAppWindow) {
        return;
    }

    Display* display = static_cast<Display*>(QzTools::X11Display(this));

    Atom net_wm_desktop = XInternAtom(display, "_NET_WM_DESKTOP", False);
    if (net_wm_desktop == None) {
        return;
    }

    // Fixes issue when the property wasn't set on some X servers
    // hmmm does it?
    //setVisible(true);

    XChangeProperty(display, winId(), net_wm_desktop, XA_CARDINAL,
                    32, PropModeReplace, (unsigned char*) &desktopId, 1L);
}
#endif

#ifdef Q_OS_WIN
#if (QT_VERSION < 0x050000)
bool BrowserWindow::winEvent(MSG* message, long* result)
{
#else
bool BrowserWindow::nativeEvent(const QByteArray &eventType, void* _message, long* result)
{
    Q_UNUSED(eventType)
    MSG* message = static_cast<MSG*>(_message);
#endif
    if (message && message->message == WM_DWMCOMPOSITIONCHANGED) {
        Settings settings;
        settings.beginGroup("Browser-View-Settings");
        m_usingTransparentBackground = settings.value("useTransparentBackground", false).toBool();
        settings.endGroup();
        if (m_usingTransparentBackground && QtWin::isCompositionEnabled()) {
            setUpdatesEnabled(false);

            QtWin::extendFrameIntoClientArea(this, 0, 0, 0, 0);
            QTimer::singleShot(0, this, SLOT(applyBlurToMainWindow()));

            //install event filter
            menuBar()->installEventFilter(this);
            m_navigationBar->installEventFilter(this);
            m_bookmarksToolbar->installEventFilter(this);
            statusBar()->installEventFilter(this);

            if (m_sideBar) {
                m_sideBar.data()->installEventFilter(this);
            }

            SearchToolBar* search = searchToolBar();
            if (search) {
                search->installEventFilter(this);
            }

            if (m_webInspectorDock) {
                m_webInspectorDock.data()->installEventFilter(this);
            }

            if (isVisible()) {
                hide();
                show();
            }
            setUpdatesEnabled(true);
        }
        else {
            m_usingTransparentBackground = false;
        }
    }
#if (QT_VERSION < 0x050000)
    return QMainWindow::winEvent(message, result);
#else
    return QMainWindow::nativeEvent(eventType, _message, result);
#endif
}

void BrowserWindow::paintEvent(QPaintEvent* event)
{
    if (isTransparentBackgroundAllowed()) {
        QPainter p(this);
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.fillRect(event->rect(), QColor(0, 0, 0, 0));
    }

    QMainWindow::paintEvent(event);
}

void BrowserWindow::applyBlurToMainWindow(bool force)
{
    if (isClosing() || m_isStarting) {
        return;
    }

    if (!force && !isTransparentBackgroundAllowed()) {
        return;
    }
    int topMargin = 0;
    int bottomMargin = 1;
    int rightMargin = 1;
    int leftMargin = 1;

    if (m_sideBar) {
        if (isRightToLeft()) {
            rightMargin += m_sideBar.data()->width() + m_mainSplitter->handleWidth();
        }
        else {
            leftMargin += m_sideBar.data()->width() + m_mainSplitter->handleWidth();
        }
    }

    topMargin += menuBar()->isVisible() ? menuBar()->height() : 0;
    topMargin += m_navigationBar->isVisible() ? m_navigationBar->height() : 0;
    topMargin += m_bookmarksToolbar->isVisible() ? m_bookmarksToolbar->height() : 0;
    topMargin += m_tabWidget->getTabBar()->height();

    SearchToolBar* search = searchToolBar();
    if (search) {
        bottomMargin += search->height();
    }

    bottomMargin += statusBar()->isVisible() ? statusBar()->height() : 0;

    if (m_webInspectorDock) {
        bottomMargin += m_webInspectorDock.data()->isVisible()
                        ? m_webInspectorDock.data()->height()
                        + m_webInspectorDock.data()->style()->pixelMetric(QStyle::PM_DockWidgetSeparatorExtent)
                        : 0;
    }

    QtWin::extendFrameIntoClientArea(this, leftMargin, topMargin, rightMargin, bottomMargin);
}

bool BrowserWindow::eventFilter(QObject* object, QEvent* event)
{
    switch (event->type()) {
    case QEvent::Hide:
        if (object == m_navigationContainer) {
            m_navigationBar->removeEventFilter(this);
            m_bookmarksToolbar->removeEventFilter(this);
            break;
        }
    case QEvent::Show:
        if (object == m_navigationContainer) {
            m_navigationBar->installEventFilter(this);
            m_bookmarksToolbar->installEventFilter(this);
            break;
        }
    case QEvent::Resize:
    case QEvent::DeferredDelete:
        if (object == m_navigationContainer) {
            break;
        }
        applyBlurToMainWindow();
        break;
    default:
        break;
    }

    return QMainWindow::eventFilter(object, event);
}
#endif

BrowserWindow::~BrowserWindow()
{
}
