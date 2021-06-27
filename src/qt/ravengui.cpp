// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2020 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/raven-config.h"
#endif

#include "ravengui.h"

#include "ravenunits.h"
#include "clientmodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "modaloverlay.h"
#include "networkstyle.h"
#include "notificator.h"
#include "openuridialog.h"
#include "optionsdialog.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "rpcconsole.h"
#include "utilitydialog.h"

#ifdef ENABLE_WALLET
#include "walletframe.h"
#include "walletmodel.h"
#include "mnemonicdialog.h"
#endif // ENABLE_WALLET

#ifdef Q_OS_MAC
#include "macdockiconhandler.h"
#endif

#include "chainparams.h"
#include "init.h"
#include "ui_interface.h"
#include "util.h"
#include "core_io.h"
#include "darkstyle.h"

#include <iostream>

#include <QDebug>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QGraphicsDropShadowEffect>
#include <QToolButton>
#include <QPushButton>
#include <QPainter>
#include <QWidgetAction>
#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDragEnterEvent>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QComboBox>

// Fixing Boost 1.73 compile errors
#include <boost/bind/bind.hpp>
using namespace boost::placeholders;

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
#include <QTextDocument>
#include <QUrl>
#else
#include <QUrlQuery>
#include <validation.h>
#include <tinyformat.h>
#include <QFontDatabase>
#include <univalue/include/univalue.h>
#include <QDesktopServices>

#endif

#if QT_VERSION < QT_VERSION_CHECK(5, 11, 0)
#define QTversionPreFiveEleven
#endif

const std::string RavenGUI::DEFAULT_UIPLATFORM =
#if defined(Q_OS_MAC)
        "macosx"
#elif defined(Q_OS_WIN)
        "windows"
#else
        "other"
#endif
        ;

/** Display name for default wallet name. Uses tilde to avoid name
 * collisions in the future with additional wallets */
const QString RavenGUI::DEFAULT_WALLET = "~Default";

/* Bit of a bodge, c++ really doesn't want you to predefine values
 * in only header files, so we do one-time value assignment here. */
std::array<CurrencyUnitDetails, 5> CurrencyUnits::CurrencyOptions = { {
    { "BTC",    "RVNBTC"  , 1,          8},
    { "mBTC",   "RVNBTC"  , 1000,       5},
    { "µBTC",   "RVNBTC"  , 1000000,    2},
    { "Satoshi","RVNBTC"  , 100000000,  0},
    { "USDT",   "RVNUSDT" , 1,          5}
} };

static bool ThreadSafeMessageBox(RavenGUI *gui, const std::string& message, const std::string& caption, unsigned int style);

RavenGUI::RavenGUI(const PlatformStyle *_platformStyle, const NetworkStyle *networkStyle, QWidget *parent) :
    QMainWindow(parent),
    enableWallet(false),
    platformStyle(_platformStyle)

{
    QSettings settings;
    if (!restoreGeometry(settings.value("MainWindowGeometry").toByteArray())) {
        // Restore failed (perhaps missing setting), center the window
        move(QGuiApplication::primaryScreen()->availableGeometry().center() - frameGeometry().center());
    }

    QString windowTitle = tr(PACKAGE_NAME) + " - ";
#ifdef ENABLE_WALLET
    enableWallet = WalletModel::isWalletEnabled();
#endif // ENABLE_WALLET
    if(enableWallet)
    {
        windowTitle += tr("Wallet");
    } else {
        windowTitle += tr("Node");
    }
    windowTitle += " " + networkStyle->getTitleAddText();
#ifndef Q_OS_MAC
    QApplication::setWindowIcon(networkStyle->getTrayAndWindowIcon());
    setWindowIcon(networkStyle->getTrayAndWindowIcon());
#else
    MacDockIconHandler::instance()->setIcon(networkStyle->getAppIcon());
#endif
    setWindowTitle(windowTitle);

#if defined(Q_OS_MAC) && QT_VERSION < 0x050000
    // This property is not implemented in Qt 5. Setting it has no effect.
    // A replacement API (QtMacUnifiedToolBar) is available in QtMacExtras.
    setUnifiedTitleAndToolBarOnMac(true);
#endif

    rpcConsole = new RPCConsole(_platformStyle, 0);
    helpMessageDialog = new HelpMessageDialog(this, false);
#ifdef ENABLE_WALLET
    if(enableWallet)
    {
        /** Create wallet frame and make it the central widget */
        walletFrame = new WalletFrame(_platformStyle, this);
        setCentralWidget(walletFrame);
    } else
#endif // ENABLE_WALLET
    {
        /* When compiled without wallet or -disablewallet is provided,
         * the central widget is the rpc console.
         */
        setCentralWidget(rpcConsole);
    }

    /** RVN START */
    labelCurrentMarket = new QLabel();
    labelCurrentPrice = new QLabel();
    headerWidget = new QWidget();
    pricingTimer = new QTimer();
    networkManager = new QNetworkAccessManager();
    request = new QNetworkRequest();
    labelVersionUpdate = new QLabel();
    networkVersionManager = new QNetworkAccessManager();
    versionRequest = new QNetworkRequest();
    /** RVN END */

    // Accept D&D of URIs
    setAcceptDrops(true);

    loadFonts();

#if !defined(Q_OS_MAC)
    this->setFont(QFont("Open Sans"));
#endif

    // Create actions for the toolbar, menu bar and tray/dock icon
    // Needs walletFrame to be initialized
    createActions();

    // Create application menu bar
    createMenuBar();

    // Create the toolbars
    createToolBars();

    // Create system tray icon and notification
    createTrayIcon(networkStyle);

    // Create status bar
    statusBar();

    // Disable size grip because it looks ugly and nobody needs it
    statusBar()->setSizeGripEnabled(false);

    // Status bar notification icons
    QFrame *frameBlocks = new QFrame();
    frameBlocks->setContentsMargins(0,0,0,0);
    frameBlocks->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    QHBoxLayout *frameBlocksLayout = new QHBoxLayout(frameBlocks);
    frameBlocksLayout->setContentsMargins(3,0,3,0);
    frameBlocksLayout->setSpacing(3);
    unitDisplayControl = new UnitDisplayStatusBarControl(platformStyle);
    labelWalletEncryptionIcon = new QLabel();
    labelWalletHDStatusIcon = new QLabel();
    connectionsControl = new GUIUtil::ClickableLabel();
    labelBlocksIcon = new GUIUtil::ClickableLabel();
    if(enableWallet)
    {
        frameBlocksLayout->addStretch();
        frameBlocksLayout->addWidget(unitDisplayControl);
        frameBlocksLayout->addStretch();
        frameBlocksLayout->addWidget(labelWalletEncryptionIcon);
        frameBlocksLayout->addWidget(labelWalletHDStatusIcon);
    }
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(connectionsControl);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelBlocksIcon);
    frameBlocksLayout->addStretch();

    // Progress bar and label for blocks download
    progressBarLabel = new QLabel();
    progressBarLabel->setVisible(false);
    progressBarLabel->setStyleSheet(QString(".QLabel { color : %1; }").arg(platformStyle->TextColor().name()));
    progressBar = new GUIUtil::ProgressBar();
    progressBar->setAlignment(Qt::AlignCenter);
    progressBar->setVisible(false);

    // Override style sheet for progress bar for styles that have a segmented progress bar,
    // as they make the text unreadable (workaround for issue #1071)
    // See https://qt-project.org/doc/qt-4.8/gallery.html
    QString curStyle = QApplication::style()->metaObject()->className();
    if(curStyle == "QWindowsStyle" || curStyle == "QWindowsXPStyle")
    {
        progressBar->setStyleSheet("QProgressBar { background-color: #e8e8e8; border: 1px solid grey; border-radius: 7px; padding: 1px; text-align: center; } QProgressBar::chunk { background: QLinearGradient(x1: 0, y1: 0, x2: 1, y2: 0, stop: 0 #FF8000, stop: 1 orange); border-radius: 7px; margin: 0px; }");
    }

    statusBar()->addWidget(progressBarLabel);
    statusBar()->addWidget(progressBar);
    statusBar()->addPermanentWidget(frameBlocks);

    if(darkModeEnabled)
        statusBar()->setStyleSheet(QString(".QStatusBar{background-color: %1; border-top: 1px solid %2;}").arg(platformStyle->TopWidgetBackGroundColor().name(), platformStyle->TextColor().name()));

    // Install event filter to be able to catch status tip events (QEvent::StatusTip)
    this->installEventFilter(this);

    // Initially wallet actions should be disabled
    setWalletActionsEnabled(false);

    // Subscribe to notifications from core
    subscribeToCoreSignals();

    connect(connectionsControl, SIGNAL(clicked(QPoint)), this, SLOT(toggleNetworkActive()));

    modalOverlay = new ModalOverlay(this->centralWidget());
#ifdef ENABLE_WALLET
    if(enableWallet) {
        connect(walletFrame, SIGNAL(requestedSyncWarningInfo()), this, SLOT(showModalOverlay()));
        connect(labelBlocksIcon, SIGNAL(clicked(QPoint)), this, SLOT(showModalOverlay()));
        connect(progressBar, SIGNAL(clicked(QPoint)), this, SLOT(showModalOverlay()));
    }
#endif
}

RavenGUI::~RavenGUI()
{
    // Unsubscribe from notifications from core
    unsubscribeFromCoreSignals();

    QSettings settings;
    settings.setValue("MainWindowGeometry", saveGeometry());
    if(trayIcon) // Hide tray icon, as deleting will let it linger until quit (on Ubuntu)
        trayIcon->hide();
#ifdef Q_OS_MAC
    delete appMenuBar;
    MacDockIconHandler::cleanup();
#endif

    delete rpcConsole;
}

void RavenGUI::loadFonts()
{
    QFontDatabase::addApplicationFont(":/fonts/opensans-bold");
    QFontDatabase::addApplicationFont(":/fonts/opensans-bolditalic");
    QFontDatabase::addApplicationFont(":/fonts/opensans-extrabold");
    QFontDatabase::addApplicationFont(":/fonts/opensans-extrabolditalic");
    QFontDatabase::addApplicationFont(":/fonts/opensans-italic");
    QFontDatabase::addApplicationFont(":/fonts/opensans-light");
    QFontDatabase::addApplicationFont(":/fonts/opensans-lightitalic");
    QFontDatabase::addApplicationFont(":/fonts/opensans-regular");
    QFontDatabase::addApplicationFont(":/fonts/opensans-semibold");
    QFontDatabase::addApplicationFont(":/fonts/opensans-semibolditalic");
}


void RavenGUI::createActions()
{
    QFont font = QFont();
    font.setPixelSize(22);
    font.setLetterSpacing(QFont::SpacingType::AbsoluteSpacing, -0.43);
#if !defined(Q_OS_MAC)
    font.setFamily("Open Sans");
#endif
    font.setWeight(QFont::Weight::ExtraLight);

    QActionGroup *tabGroup = new QActionGroup(this);

    overviewAction = new QAction(platformStyle->SingleColorIconOnOff(":/icons/overview_selected", ":/icons/overview"), tr("&Overview"), this);
    overviewAction->setStatusTip(tr("Show general overview of wallet"));
    overviewAction->setToolTip(overviewAction->statusTip());
    overviewAction->setCheckable(true);
    overviewAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_1));
    overviewAction->setFont(font);
    tabGroup->addAction(overviewAction);

    sendCoinsAction = new QAction(platformStyle->SingleColorIconOnOff(":/icons/send_selected", ":/icons/send"), tr("&Send"), this);
    sendCoinsAction->setStatusTip(tr("Send coins to a Raven address"));
    sendCoinsAction->setToolTip(sendCoinsAction->statusTip());
    sendCoinsAction->setCheckable(true);
    sendCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_2));
    sendCoinsAction->setFont(font);
    tabGroup->addAction(sendCoinsAction);

    sendCoinsMenuAction = new QAction(platformStyle->TextColorIcon(":/icons/send"), sendCoinsAction->text(), this);
    sendCoinsMenuAction->setStatusTip(sendCoinsAction->statusTip());
    sendCoinsMenuAction->setToolTip(sendCoinsMenuAction->statusTip());

    receiveCoinsAction = new QAction(platformStyle->SingleColorIconOnOff(":/icons/receiving_addresses_selected", ":/icons/receiving_addresses"), tr("&Receive"), this);
    receiveCoinsAction->setStatusTip(tr("Request payments (generates QR codes and raven: URIs)"));
    receiveCoinsAction->setToolTip(receiveCoinsAction->statusTip());
    receiveCoinsAction->setCheckable(true);
    receiveCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_3));
    receiveCoinsAction->setFont(font);
    tabGroup->addAction(receiveCoinsAction);

    receiveCoinsMenuAction = new QAction(platformStyle->TextColorIcon(":/icons/receiving_addresses"), receiveCoinsAction->text(), this);
    receiveCoinsMenuAction->setStatusTip(receiveCoinsAction->statusTip());
    receiveCoinsMenuAction->setToolTip(receiveCoinsMenuAction->statusTip());

    historyAction = new QAction(platformStyle->SingleColorIconOnOff(":/icons/history_selected", ":/icons/history"), tr("&Transactions"), this);
    historyAction->setStatusTip(tr("Browse transaction history"));
    historyAction->setToolTip(historyAction->statusTip());
    historyAction->setCheckable(true);
    historyAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_4));
    historyAction->setFont(font);
    tabGroup->addAction(historyAction);

    /** RVN START */
    createAssetAction = new QAction(platformStyle->SingleColorIconOnOff(":/icons/asset_create_selected", ":/icons/asset_create"), tr("&Create Assets"), this);
    createAssetAction->setStatusTip(tr("Create new main/sub/unique assets"));
    createAssetAction->setToolTip(createAssetAction->statusTip());
    createAssetAction->setCheckable(true);
    createAssetAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_5));
    createAssetAction->setFont(font);
    tabGroup->addAction(createAssetAction);

    transferAssetAction = new QAction(platformStyle->SingleColorIconOnOff(":/icons/asset_transfer_selected", ":/icons/asset_transfer"), tr("&Transfer Assets"), this);
    transferAssetAction->setStatusTip(tr("Transfer assets to RVN addresses"));
    transferAssetAction->setToolTip(transferAssetAction->statusTip());
    transferAssetAction->setCheckable(true);
    transferAssetAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_6));
    transferAssetAction->setFont(font);
    tabGroup->addAction(transferAssetAction);

    manageAssetAction = new QAction(platformStyle->SingleColorIconOnOff(":/icons/asset_manage_selected", ":/icons/asset_manage"), tr("&Manage Assets"), this);
    manageAssetAction->setStatusTip(tr("Manage assets you are the administrator of"));
    manageAssetAction->setToolTip(manageAssetAction->statusTip());
    manageAssetAction->setCheckable(true);
    manageAssetAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_7));
    manageAssetAction->setFont(font);
    tabGroup->addAction(manageAssetAction);

    messagingAction = new QAction(platformStyle->SingleColorIcon(":/icons/editcopy"), tr("&Messaging"), this);
    messagingAction->setStatusTip(tr("Coming Soon"));
    messagingAction->setToolTip(messagingAction->statusTip());
    messagingAction->setCheckable(true);
//    messagingAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_9));
    messagingAction->setFont(font);
    tabGroup->addAction(messagingAction);

    votingAction = new QAction(platformStyle->SingleColorIcon(":/icons/edit"), tr("&Voting"), this);
    votingAction->setStatusTip(tr("Coming Soon"));
    votingAction->setToolTip(votingAction->statusTip());
    votingAction->setCheckable(true);
    // votingAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_V));
    votingAction->setFont(font);
    tabGroup->addAction(votingAction);

    restrictedAssetAction = new QAction(platformStyle->SingleColorIconOnOff(":/icons/restricted_asset_selected", ":/icons/restricted_asset"), tr("&Restricted Assets"), this);
    restrictedAssetAction->setStatusTip(tr("Manage restricted assets"));
    restrictedAssetAction->setToolTip(restrictedAssetAction->statusTip());
    restrictedAssetAction->setCheckable(true);
    restrictedAssetAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_8));
    restrictedAssetAction->setFont(font);
    tabGroup->addAction(restrictedAssetAction);

    atomicSwapsAction = new QAction(platformStyle->SingleColorIconOnOff(":/icons/atomic_swap_selected", ":/icons/atomic_swap"), tr("&Atomic Swaps"), this);
    atomicSwapsAction->setStatusTip(tr("Manage atomic swaps (purchases, sales, trades)"));
    atomicSwapsAction->setToolTip(atomicSwapsAction->statusTip());
    atomicSwapsAction->setCheckable(true);
    //atomicSwapsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_8));
    atomicSwapsAction->setFont(font);
    tabGroup->addAction(atomicSwapsAction);

    /** RVN END */

#ifdef ENABLE_WALLET
    // These showNormalIfMinimized are needed because Send Coins and Receive Coins
    // can be triggered from the tray menu, and need to show the GUI to be useful.
    connect(overviewAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(overviewAction, SIGNAL(triggered()), this, SLOT(gotoOverviewPage()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(gotoSendCoinsPage()));
    connect(sendCoinsMenuAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(sendCoinsMenuAction, SIGNAL(triggered()), this, SLOT(gotoSendCoinsPage()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(gotoReceiveCoinsPage()));
    connect(receiveCoinsMenuAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(receiveCoinsMenuAction, SIGNAL(triggered()), this, SLOT(gotoReceiveCoinsPage()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(gotoHistoryPage()));
    connect(transferAssetAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(transferAssetAction, SIGNAL(triggered()), this, SLOT(gotoAssetsPage()));
    connect(createAssetAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(createAssetAction, SIGNAL(triggered()), this, SLOT(gotoCreateAssetsPage()));
    connect(manageAssetAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(manageAssetAction, SIGNAL(triggered()), this, SLOT(gotoManageAssetsPage()));
    connect(restrictedAssetAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(restrictedAssetAction, SIGNAL(triggered()), this, SLOT(gotoRestrictedAssetsPage()));
    connect(atomicSwapsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(atomicSwapsAction, SIGNAL(triggered()), this, SLOT(gotoAtomicSwapsPage()));
    // TODO add messaging actions to go to messaging page when clicked
    // TODO add voting actions to go to voting page when clicked
#endif // ENABLE_WALLET

    quitAction = new QAction(platformStyle->TextColorIcon(":/icons/quit"), tr("E&xit"), this);
    quitAction->setStatusTip(tr("Quit application"));
    quitAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q));
    quitAction->setMenuRole(QAction::QuitRole);
    aboutAction = new QAction(platformStyle->TextColorIcon(":/icons/about"), tr("&About %1").arg(tr(PACKAGE_NAME)), this);
    aboutAction->setStatusTip(tr("Show information about %1").arg(tr(PACKAGE_NAME)));
    aboutAction->setMenuRole(QAction::AboutRole);
    aboutAction->setEnabled(false);
    aboutQtAction = new QAction(platformStyle->TextColorIcon(":/icons/about_qt"), tr("About &Qt"), this);
    aboutQtAction->setStatusTip(tr("Show information about Qt"));
    aboutQtAction->setMenuRole(QAction::AboutQtRole);
    optionsAction = new QAction(platformStyle->TextColorIcon(":/icons/options"), tr("&Options..."), this);
    optionsAction->setStatusTip(tr("Modify configuration options for %1").arg(tr(PACKAGE_NAME)));
    optionsAction->setMenuRole(QAction::PreferencesRole);
    optionsAction->setEnabled(false);
    toggleHideAction = new QAction(platformStyle->TextColorIcon(":/icons/about"), tr("&Show / Hide"), this);
    toggleHideAction->setStatusTip(tr("Show or hide the main Window"));

    encryptWalletAction = new QAction(platformStyle->TextColorIcon(":/icons/lock_closed"), tr("&Encrypt Wallet..."), this);
    encryptWalletAction->setStatusTip(tr("Encrypt the private keys that belong to your wallet"));
    encryptWalletAction->setCheckable(true);
    backupWalletAction = new QAction(platformStyle->TextColorIcon(":/icons/filesave"), tr("&Backup Wallet..."), this);
    backupWalletAction->setStatusTip(tr("Backup wallet to another location"));
    changePassphraseAction = new QAction(platformStyle->TextColorIcon(":/icons/key"), tr("&Change Passphrase..."), this);
    changePassphraseAction->setStatusTip(tr("Change the passphrase used for wallet encryption"));

    getMyWordsAction = new QAction(platformStyle->TextColorIcon(":/icons/key"), tr("&Get my words..."), this);
    getMyWordsAction->setStatusTip(tr("Show the recoverywords for this wallet"));

    signMessageAction = new QAction(platformStyle->TextColorIcon(":/icons/edit"), tr("Sign &message..."), this);
    signMessageAction->setStatusTip(tr("Sign messages with your Raven addresses to prove you own them"));
    verifyMessageAction = new QAction(platformStyle->TextColorIcon(":/icons/verify"), tr("&Verify message..."), this);
    verifyMessageAction->setStatusTip(tr("Verify messages to ensure they were signed with specified Raven addresses"));

    openRPCConsoleAction = new QAction(platformStyle->TextColorIcon(":/icons/debugwindow"), tr("&Debug Window"), this);
    openRPCConsoleAction->setStatusTip(tr("Open debugging and diagnostic console"));
    // initially disable the debug window menu item
    openRPCConsoleAction->setEnabled(false);

    openWalletRepairAction = new QAction(platformStyle->TextColorIcon(":/icons/debugwindow"), tr("&Wallet Repair"), this);
    openWalletRepairAction->setStatusTip(tr("Open wallet repair options"));

    usedSendingAddressesAction = new QAction(platformStyle->TextColorIcon(":/icons/address-book"), tr("&Sending addresses..."), this);
    usedSendingAddressesAction->setStatusTip(tr("Show the list of used sending addresses and labels"));
    usedReceivingAddressesAction = new QAction(platformStyle->TextColorIcon(":/icons/address-book"), tr("&Receiving addresses..."), this);
    usedReceivingAddressesAction->setStatusTip(tr("Show the list of used receiving addresses and labels"));

    openAction = new QAction(platformStyle->TextColorIcon(":/icons/open"), tr("Open &URI..."), this);
    openAction->setStatusTip(tr("Open a raven: URI or payment request"));

    showHelpMessageAction = new QAction(platformStyle->TextColorIcon(":/icons/info"), tr("&Command-line options"), this);
    showHelpMessageAction->setMenuRole(QAction::NoRole);
    showHelpMessageAction->setStatusTip(tr("Show the %1 help message to get a list with possible Raven command-line options").arg(tr(PACKAGE_NAME)));

    connect(quitAction, SIGNAL(triggered()), qApp, SLOT(quit()));
    connect(aboutAction, SIGNAL(triggered()), this, SLOT(aboutClicked()));
    connect(aboutQtAction, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
    connect(optionsAction, SIGNAL(triggered()), this, SLOT(optionsClicked()));
    connect(toggleHideAction, SIGNAL(triggered()), this, SLOT(toggleHidden()));
    connect(showHelpMessageAction, SIGNAL(triggered()), this, SLOT(showHelpMessageClicked()));
    connect(openRPCConsoleAction, SIGNAL(triggered()), this, SLOT(showDebugWindow()));
    connect(openWalletRepairAction, SIGNAL(triggered()), this, SLOT(showWalletRepair()));
    // Get restart command-line parameters and handle restart
    connect(rpcConsole, SIGNAL(handleRestart(QStringList)), this, SLOT(handleRestart(QStringList)));
    // prevents an open debug window from becoming stuck/unusable on client shutdown
    connect(quitAction, SIGNAL(triggered()), rpcConsole, SLOT(hide()));

#ifdef ENABLE_WALLET
    if(walletFrame)
    {
        connect(encryptWalletAction, SIGNAL(triggered(bool)), walletFrame, SLOT(encryptWallet(bool)));
        connect(backupWalletAction, SIGNAL(triggered()), walletFrame, SLOT(backupWallet()));
        connect(changePassphraseAction, SIGNAL(triggered()), walletFrame, SLOT(changePassphrase()));
        connect(getMyWordsAction, SIGNAL(triggered()), walletFrame, SLOT(getMyWords()));
        connect(signMessageAction, SIGNAL(triggered()), this, SLOT(gotoSignMessageTab()));
        connect(verifyMessageAction, SIGNAL(triggered()), this, SLOT(gotoVerifyMessageTab()));
        connect(usedSendingAddressesAction, SIGNAL(triggered()), walletFrame, SLOT(usedSendingAddresses()));
        connect(usedReceivingAddressesAction, SIGNAL(triggered()), walletFrame, SLOT(usedReceivingAddresses()));
        connect(openAction, SIGNAL(triggered()), this, SLOT(openClicked()));
    }
#endif // ENABLE_WALLET

    new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_C), this, SLOT(showDebugWindowActivateConsole()));
    new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_D), this, SLOT(showDebugWindow()));
}

void RavenGUI::createMenuBar()
{
#ifdef Q_OS_MAC
    // Create a decoupled menu bar on Mac which stays even if the window is closed
    appMenuBar = new QMenuBar();
#else
    // Get the main window's menu bar on other platforms
    appMenuBar = menuBar();
#endif

    // Configure the menus
    QMenu *file = appMenuBar->addMenu(tr("&File"));
    if(walletFrame)
    {
        file->addAction(openAction);
        file->addAction(signMessageAction);
        file->addAction(verifyMessageAction);
        file->addSeparator();
        file->addAction(usedSendingAddressesAction);
        file->addAction(usedReceivingAddressesAction);
        file->addSeparator();
    }
    file->addAction(quitAction);

    QMenu *settings = appMenuBar->addMenu(tr("&Wallet"));
    if(walletFrame)
    {
        settings->addAction(encryptWalletAction);
        settings->addAction(backupWalletAction);
        settings->addAction(changePassphraseAction);
        settings->addAction(getMyWordsAction);
        settings->addSeparator();
    }
    settings->addAction(optionsAction);

    QMenu *help = appMenuBar->addMenu(tr("&Help"));
    if(walletFrame)
    {
        help->addAction(openRPCConsoleAction);
        help->addAction(openWalletRepairAction);
    }
    help->addAction(showHelpMessageAction);
    help->addSeparator();
    help->addAction(aboutAction);
    help->addAction(aboutQtAction);
}

void RavenGUI::createToolBars()
{
    if(walletFrame)
    {
        QSettings settings;
        bool IconsOnly = settings.value("fToolbarIconsOnly", false).toBool();

        /** RVN START */
        // Create the background and the vertical tool bar
        QWidget* toolbarWidget = new QWidget();

        QString widgetStyleSheet = ".QWidget {background-color: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 %1, stop: 1 %2);}";

        toolbarWidget->setStyleSheet(widgetStyleSheet.arg(platformStyle->LightBlueColor().name(), platformStyle->DarkBlueColor().name()));

        labelToolbar = new QLabel();
        labelToolbar->setContentsMargins(0,0,0,50);
        labelToolbar->setAlignment(Qt::AlignLeft);

        if(IconsOnly) {
            labelToolbar->setPixmap(QPixmap::fromImage(QImage(":/icons/rvntext")));
        }
        else {
            labelToolbar->setPixmap(QPixmap::fromImage(QImage(":/icons/ravencointext")));
        }
        labelToolbar->setStyleSheet(".QLabel{background-color: transparent;}");

        /** RVN END */

        m_toolbar = new QToolBar();
        m_toolbar->setStyle(style());
        m_toolbar->setContextMenuPolicy(Qt::PreventContextMenu);
        m_toolbar->setMovable(false);

        if(IconsOnly) {
            m_toolbar->setMaximumWidth(65);
            m_toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
        }
        else {
            m_toolbar->setMinimumWidth(labelToolbar->width());
            m_toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        }
        m_toolbar->addAction(overviewAction);
        m_toolbar->addAction(sendCoinsAction);
        m_toolbar->addAction(receiveCoinsAction);
        m_toolbar->addAction(historyAction);
        m_toolbar->addAction(createAssetAction);
        m_toolbar->addAction(transferAssetAction);
        m_toolbar->addAction(manageAssetAction);
//        m_toolbar->addAction(messagingAction);
//        m_toolbar->addAction(votingAction);
        m_toolbar->addAction(restrictedAssetAction);
        m_toolbar->addAction(atomicSwapsAction);

        QString openSansFontString = "font: normal 22pt \"Open Sans\";";
        QString normalString = "font: normal 22pt \"Arial\";";
        QString stringToUse = "";

#if !defined(Q_OS_MAC)
        stringToUse = openSansFontString;
#else
        stringToUse = normalString;
#endif

        /** RVN START */
        QString tbStyleSheet = ".QToolBar {background-color : transparent; border-color: transparent; }  "
                               ".QToolButton {background-color: transparent; border-color: transparent; width: 249px; color: %1; border: none;} "
                               ".QToolButton:checked {background: none; background-color: none; selection-background-color: none; color: %2; border: none; font: %4} "
                               ".QToolButton:hover {background: none; background-color: none; border: none; color: %3;} "
                               ".QToolButton:disabled {color: gray;}";

        m_toolbar->setStyleSheet(tbStyleSheet.arg(platformStyle->ToolBarNotSelectedTextColor().name(),
                                                platformStyle->ToolBarSelectedTextColor().name(),
                                                platformStyle->DarkOrangeColor().name(), stringToUse));

        m_toolbar->setOrientation(Qt::Vertical);
        m_toolbar->setIconSize(QSize(40, 40));

        QLayout* lay = m_toolbar->layout();
        for(int i = 0; i < lay->count(); ++i)
            lay->itemAt(i)->setAlignment(Qt::AlignLeft);

        overviewAction->setChecked(true);

        QVBoxLayout* ravenLabelLayout = new QVBoxLayout(toolbarWidget);
        ravenLabelLayout->addWidget(labelToolbar);
        ravenLabelLayout->addWidget(m_toolbar);
        ravenLabelLayout->setDirection(QBoxLayout::TopToBottom);
        ravenLabelLayout->addStretch(1);

        QString mainWalletWidgetStyle = QString(".QWidget{background-color: %1}").arg(platformStyle->MainBackGroundColor().name());
        QWidget* mainWalletWidget = new QWidget();
        mainWalletWidget->setStyleSheet(mainWalletWidgetStyle);

        /** Create the shadow effects for the main wallet frame. Make it so it puts a shadow on the tool bar */
#if !defined(Q_OS_MAC)
        QGraphicsDropShadowEffect *walletFrameShadow = new QGraphicsDropShadowEffect;
        walletFrameShadow->setBlurRadius(50);
        walletFrameShadow->setColor(COLOR_WALLETFRAME_SHADOW);
        walletFrameShadow->setXOffset(-8.0);
        walletFrameShadow->setYOffset(0);
        mainWalletWidget->setGraphicsEffect(walletFrameShadow);
#endif

        QString widgetBackgroundSytleSheet = QString(".QWidget{background-color: %1}").arg(platformStyle->TopWidgetBackGroundColor().name());

        // Set the headers widget options
        headerWidget->setContentsMargins(0,25,0,0);
        headerWidget->setStyleSheet(widgetBackgroundSytleSheet);
        headerWidget->setGraphicsEffect(GUIUtil::getShadowEffect());
        headerWidget->setFixedHeight(75);

        QFont currentMarketFont;
        currentMarketFont.setFamily("Open Sans");
        currentMarketFont.setWeight(QFont::Weight::Normal);
        currentMarketFont.setLetterSpacing(QFont::SpacingType::AbsoluteSpacing, -0.6);
        currentMarketFont.setPixelSize(18);

        // Set the pricing information
        QHBoxLayout* priceLayout = new QHBoxLayout(headerWidget);
        priceLayout->setContentsMargins(0,0,0,25);
        priceLayout->setDirection(QBoxLayout::LeftToRight);
        priceLayout->setAlignment(Qt::AlignVCenter);
        labelCurrentMarket->setContentsMargins(50,0,0,0);
        labelCurrentMarket->setAlignment(Qt::AlignVCenter);
        labelCurrentMarket->setStyleSheet(STRING_LABEL_COLOR);
        labelCurrentMarket->setFont(currentMarketFont);
        labelCurrentMarket->setText(tr("Ravencoin Market Price"));

        QString currentPriceStyleSheet = ".QLabel{color: %1;}";
        labelCurrentPrice->setContentsMargins(25,0,0,0);
        labelCurrentPrice->setAlignment(Qt::AlignVCenter);
        labelCurrentPrice->setStyleSheet(currentPriceStyleSheet.arg(COLOR_LABELS.name()));
        labelCurrentPrice->setFont(currentMarketFont);

        comboRvnUnit = new QComboBox(headerWidget);
        QStringList list;
        for(int unitNum = 0; unitNum < CurrencyUnits::count(); unitNum++) {
            list.append(QString(CurrencyUnits::CurrencyOptions[unitNum].Header));
        }
        comboRvnUnit->addItems(list);
        comboRvnUnit->setFixedHeight(26);
        comboRvnUnit->setContentsMargins(5,0,0,0);
        comboRvnUnit->setStyleSheet(STRING_LABEL_COLOR);
        comboRvnUnit->setFont(currentMarketFont);

        labelVersionUpdate->setText("<a href=\"https://github.com/RavenProject/Ravencoin/releases\">New Wallet Version Available</a>");
        labelVersionUpdate->setTextFormat(Qt::RichText);
        labelVersionUpdate->setTextInteractionFlags(Qt::TextBrowserInteraction);
        labelVersionUpdate->setOpenExternalLinks(true);
        labelVersionUpdate->setContentsMargins(0,0,15,0);
        labelVersionUpdate->setAlignment(Qt::AlignVCenter);
        labelVersionUpdate->setStyleSheet(STRING_LABEL_COLOR);
        labelVersionUpdate->setFont(currentMarketFont);
        labelVersionUpdate->hide();

        priceLayout->setGeometry(headerWidget->rect());
        priceLayout->addWidget(labelCurrentMarket, 0, Qt::AlignVCenter | Qt::AlignLeft);
        priceLayout->addWidget(labelCurrentPrice, 0,  Qt::AlignVCenter | Qt::AlignLeft);
        priceLayout->addWidget(comboRvnUnit, 0 , Qt::AlignBottom| Qt::AlignLeft);
        priceLayout->addStretch();
        priceLayout->addWidget(labelVersionUpdate, 0 , Qt::AlignVCenter | Qt::AlignRight);

        // Create the layout for widget to the right of the tool bar
        QVBoxLayout* mainFrameLayout = new QVBoxLayout(mainWalletWidget);
        mainFrameLayout->addWidget(headerWidget);
#ifdef ENABLE_WALLET
        mainFrameLayout->addWidget(walletFrame);
#endif
        mainFrameLayout->setDirection(QBoxLayout::TopToBottom);
        mainFrameLayout->setContentsMargins(QMargins());

        QVBoxLayout* layout = new QVBoxLayout();
        layout->addWidget(toolbarWidget);
        layout->addWidget(mainWalletWidget);
        layout->setSpacing(0);
        layout->setContentsMargins(QMargins());
        layout->setDirection(QBoxLayout::LeftToRight);
        QWidget* containerWidget = new QWidget();
        containerWidget->setLayout(layout);
        setCentralWidget(containerWidget);

        // Network request code for the header widget
        QObject::connect(networkManager, &QNetworkAccessManager::finished,
                         this, [=](QNetworkReply *reply) {
                    if (reply->error()) {
                        labelCurrentPrice->setText("");
                        qDebug() << reply->errorString();
                        return;
                    }
                    // Get the data from the network request
                    QString answer = reply->readAll();

                    // Create regex expression to find the value with 8 decimals
                    QRegExp rx("\\d*.\\d\\d\\d\\d\\d\\d\\d\\d");
                    rx.indexIn(answer);

                    // List the found values
                    QStringList list = rx.capturedTexts();

                    QString currentPriceStyleSheet = ".QLabel{color: %1;}";
                    // Evaluate the current and next numbers and assign a color (green for positive, red for negative)
                    bool ok;
                    if (!list.isEmpty()) {
                        double next = list.first().toDouble(&ok) * this->currentPriceDisplay->Scalar;
                        if (!ok) {
                            labelCurrentPrice->setStyleSheet(currentPriceStyleSheet.arg(COLOR_LABELS.name()));
                            labelCurrentPrice->setText("");
                        } else {
                            double current = labelCurrentPrice->text().toDouble(&ok);
                            if (!ok) {
                                current = 0.00000000;
                            } else {
                                if (next < current && !this->unitChanged)
                                    labelCurrentPrice->setStyleSheet(currentPriceStyleSheet.arg("red"));
                                else if (next > current && !this->unitChanged)
                                    labelCurrentPrice->setStyleSheet(currentPriceStyleSheet.arg("green"));
                                else
                                    labelCurrentPrice->setStyleSheet(currentPriceStyleSheet.arg(COLOR_LABELS.name()));
                            }
                            this->unitChanged = false;
                            labelCurrentPrice->setText(QString("%1").arg(QString().setNum(next, 'f', this->currentPriceDisplay->Decimals)));
                            labelCurrentPrice->setToolTip(tr("Brought to you by binance.com"));
                        }
                    }
                }
        );

        connect(quitAction, SIGNAL(triggered()), qApp, SLOT(quit()));


        // Signal change of displayed price units, must get new conversion ratio
        connect(comboRvnUnit, SIGNAL(activated(int)), this, SLOT(currencySelectionChanged(int)));
        // Create the timer
        connect(pricingTimer, SIGNAL(timeout()), this, SLOT(getPriceInfo()));
        pricingTimer->start(10000);
        getPriceInfo();
        /** RVN END */

        // Get the latest Ravencoin release and let the user know if they are using the latest version
        // Network request code for the header widget
        QObject::connect(networkVersionManager, &QNetworkAccessManager::finished,
                         this, [=](QNetworkReply *reply) {
                    if (reply->error()) {
                        qDebug() << reply->errorString();
                        return;
                    }

                    // Get the data from the network request
                    QString answer = reply->readAll();

                    UniValue releases(UniValue::VARR);
                    releases.read(answer.toStdString());

                    if (!releases.isArray()) {
                        return;
                    }

                    if (!releases.size()) {
                        return;
                    }

                    // Latest release lives in the first index of the array return from github v3 api
                    auto latestRelease = releases[0];

                    auto keys = latestRelease.getKeys();
                    for (auto key : keys) {
                       if (key == "tag_name") {
                           auto latestVersion = latestRelease["tag_name"].get_str();

                           QRegExp rx("v(\\d+).(\\d+).(\\d+)");
                           rx.indexIn(QString::fromStdString(latestVersion));

                           // List the found values
                           QStringList list = rx.capturedTexts();
                           static const int CLIENT_VERSION_MAJOR_INDEX = 1;
                           static const int CLIENT_VERSION_MINOR_INDEX = 2;
                           static const int CLIENT_VERSION_REVISION_INDEX = 3;
                           bool fNewSoftwareFound = false;
                           bool fStopSearch = false;
                           if (list.size() >= 4) {
                               if (CLIENT_VERSION_MAJOR < list[CLIENT_VERSION_MAJOR_INDEX].toInt()) {
                                   fNewSoftwareFound = true;
                               } else {
                                   if (CLIENT_VERSION_MAJOR > list[CLIENT_VERSION_MAJOR_INDEX].toInt()) {
                                       fStopSearch = true;
                                   }
                               }

                               if (!fStopSearch) {
                                   if (CLIENT_VERSION_MINOR < list[CLIENT_VERSION_MINOR_INDEX].toInt()) {
                                       fNewSoftwareFound = true;
                                   } else {
                                       if (CLIENT_VERSION_MINOR > list[CLIENT_VERSION_MINOR_INDEX].toInt()) {
                                           fStopSearch = true;
                                       }
                                   }
                               }

                               if (!fStopSearch) {
                                   if (CLIENT_VERSION_REVISION < list[CLIENT_VERSION_REVISION_INDEX].toInt()) {
                                       fNewSoftwareFound = true;
                                   }
                               }
                           }

                           if (fNewSoftwareFound) {
                               labelVersionUpdate->setToolTip(QString::fromStdString(strprintf("Currently running: %s\nLatest version: %s", FormatFullVersion(),
                                                                                               latestVersion)));
                               labelVersionUpdate->show();

                               // Only display the message on startup to the user around 1/2 of the time
                               if (GetRandInt(2) == 1) {
                                   bool fRet = uiInterface.ThreadSafeQuestion(
                                           strprintf("\nCurrently running: %s\nLatest version: %s", FormatFullVersion(),
                                                     latestVersion) + "\n\nWould you like to visit the releases page?",
                                           "",
                                           "New Wallet Version Found",
                                           CClientUIInterface::MSG_VERSION | CClientUIInterface::BTN_NO);
                                   if (fRet) {
                                       QString link = "https://github.com/RavenProject/Ravencoin/releases";
                                       QDesktopServices::openUrl(QUrl(link));
                                   }
                               }
                           } else {
                               labelVersionUpdate->hide();
                           }
                       }
                    }
                }
        );

        getLatestVersion();
    }
}

void RavenGUI::updateIconsOnlyToolbar(bool IconsOnly)
{
    if(IconsOnly) {
        labelToolbar->setPixmap(QPixmap::fromImage(QImage(":/icons/rvntext")));
        m_toolbar->setMaximumWidth(65);
        m_toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    }
    else {
        labelToolbar->setPixmap(QPixmap::fromImage(QImage(":/icons/ravencointext")));
        m_toolbar->setMinimumWidth(labelToolbar->width());
        m_toolbar->setMaximumWidth(255);
        m_toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);        
    }
}
void RavenGUI::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;
    if(_clientModel)
    {
        // Create system tray menu (or setup the dock menu) that late to prevent users from calling actions,
        // while the client has not yet fully loaded
        createTrayIconMenu();

        // Keep up to date with client
        updateNetworkState();
        connect(_clientModel, SIGNAL(numConnectionsChanged(int)), this, SLOT(setNumConnections(int)));
        connect(_clientModel, SIGNAL(networkActiveChanged(bool)), this, SLOT(setNetworkActive(bool)));

        modalOverlay->setKnownBestHeight(_clientModel->getHeaderTipHeight(), QDateTime::fromTime_t(_clientModel->getHeaderTipTime()));
        setNumBlocks(_clientModel->getNumBlocks(), _clientModel->getLastBlockDate(), _clientModel->getVerificationProgress(nullptr), false);
        connect(_clientModel, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)), this, SLOT(setNumBlocks(int,QDateTime,double,bool)));

        // Receive and report messages from client model
        connect(_clientModel, SIGNAL(message(QString,QString,unsigned int)), this, SLOT(message(QString,QString,unsigned int)));

        // Show progress dialog
        connect(_clientModel, SIGNAL(showProgress(QString,int)), this, SLOT(showProgress(QString,int)));

        rpcConsole->setClientModel(_clientModel);
#ifdef ENABLE_WALLET
        if(walletFrame)
        {
            walletFrame->setClientModel(_clientModel);
        }
#endif // ENABLE_WALLET
        unitDisplayControl->setOptionsModel(_clientModel->getOptionsModel());

        OptionsModel* optionsModel = _clientModel->getOptionsModel();
        if(optionsModel)
        {
            // be aware of the tray icon disable state change reported by the OptionsModel object.
            connect(optionsModel,SIGNAL(hideTrayIconChanged(bool)),this,SLOT(setTrayIconVisible(bool)));

            // initialize the disable state of the tray icon with the current value in the model.
            setTrayIconVisible(optionsModel->getHideTrayIcon());

            // Signal to notify the settings have updated the display currency
            connect(optionsModel,SIGNAL(displayCurrencyIndexChanged(int)), this, SLOT(onCurrencyChange(int)));

            // Init the currency display from settings
            this->onCurrencyChange(optionsModel->getDisplayCurrencyIndex());

            // Signal to update toolbar on iconsonly checkbox clicked.
            connect(optionsModel, SIGNAL(updateIconsOnlyToolbar(bool)), this, SLOT(updateIconsOnlyToolbar(bool)));

        }
    } else {
        // Disable possibility to show main window via action
        toggleHideAction->setEnabled(false);
        if(trayIconMenu)
        {
            // Disable context menu on tray icon
            trayIconMenu->clear();
        }
        // Propagate cleared model to child objects
        rpcConsole->setClientModel(nullptr);
#ifdef ENABLE_WALLET
        if (walletFrame)
        {
            walletFrame->setClientModel(nullptr);
        }
#endif // ENABLE_WALLET
        unitDisplayControl->setOptionsModel(nullptr);
    }
}

#ifdef ENABLE_WALLET
bool RavenGUI::addWallet(const QString& name, WalletModel *walletModel)
{
    if(!walletFrame)
        return false;
    setWalletActionsEnabled(true);
    return walletFrame->addWallet(name, walletModel);
}

bool RavenGUI::setCurrentWallet(const QString& name)
{
    if(!walletFrame)
        return false;
    return walletFrame->setCurrentWallet(name);
}

void RavenGUI::removeAllWallets()
{
    if(!walletFrame)
        return;
    setWalletActionsEnabled(false);
    walletFrame->removeAllWallets();
}
#endif // ENABLE_WALLET

void RavenGUI::setWalletActionsEnabled(bool enabled)
{
    overviewAction->setEnabled(enabled);
    sendCoinsAction->setEnabled(enabled);
    sendCoinsMenuAction->setEnabled(enabled);
    receiveCoinsAction->setEnabled(enabled);
    receiveCoinsMenuAction->setEnabled(enabled);
    historyAction->setEnabled(enabled);
    encryptWalletAction->setEnabled(enabled);
    backupWalletAction->setEnabled(enabled);
    changePassphraseAction->setEnabled(enabled);
    getMyWordsAction->setEnabled(enabled);
    signMessageAction->setEnabled(enabled);
    verifyMessageAction->setEnabled(enabled);
    usedSendingAddressesAction->setEnabled(enabled);
    usedReceivingAddressesAction->setEnabled(enabled);
    openAction->setEnabled(enabled);

    /** RVN START */
    transferAssetAction->setEnabled(false);
    createAssetAction->setEnabled(false);
    manageAssetAction->setEnabled(false);
    messagingAction->setEnabled(false);
    votingAction->setEnabled(false);
    restrictedAssetAction->setEnabled(false);
    /** RVN END */
}

void RavenGUI::createTrayIcon(const NetworkStyle *networkStyle)
{
#ifndef Q_OS_MAC
    trayIcon = new QSystemTrayIcon(this);
    QString toolTip = tr("%1 client").arg(tr(PACKAGE_NAME)) + " " + networkStyle->getTitleAddText();
    trayIcon->setToolTip(toolTip);
    trayIcon->setIcon(networkStyle->getTrayAndWindowIcon());
    trayIcon->hide();
#endif

    notificator = new Notificator(QApplication::applicationName(), trayIcon, this);
}

void RavenGUI::createTrayIconMenu()
{
#ifndef Q_OS_MAC
    // return if trayIcon is unset (only on non-Mac OSes)
    if (!trayIcon)
        return;

    trayIconMenu = new QMenu(this);
    trayIcon->setContextMenu(trayIconMenu);

    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
            this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
#else
    // Note: On Mac, the dock icon is used to provide the tray's functionality.
    MacDockIconHandler *dockIconHandler = MacDockIconHandler::instance();
    dockIconHandler->setMainWindow((QMainWindow *)this);
    trayIconMenu = dockIconHandler->dockMenu();
#endif

    // Configuration of the tray icon (or dock icon) icon menu
    trayIconMenu->addAction(toggleHideAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(sendCoinsMenuAction);
    trayIconMenu->addAction(receiveCoinsMenuAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(signMessageAction);
    trayIconMenu->addAction(verifyMessageAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(optionsAction);
    trayIconMenu->addAction(openRPCConsoleAction);
#ifndef Q_OS_MAC // This is built-in on Mac
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);
#endif
}

#ifndef Q_OS_MAC
void RavenGUI::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if(reason == QSystemTrayIcon::Trigger)
    {
        // Click on system tray icon triggers show/hide of the main window
        toggleHidden();
    }
}
#endif

void RavenGUI::optionsClicked()
{
    if(!clientModel || !clientModel->getOptionsModel())
        return;

    OptionsDialog dlg(this, enableWallet);
    dlg.setModel(clientModel->getOptionsModel());
    dlg.exec();
}

void RavenGUI::aboutClicked()
{
    if(!clientModel)
        return;

    HelpMessageDialog dlg(this, true);
    dlg.exec();
}

void RavenGUI::showDebugWindow()
{
    rpcConsole->showNormal();
    rpcConsole->show();
    rpcConsole->raise();
    rpcConsole->activateWindow();
}

void RavenGUI::showDebugWindowActivateConsole()
{
    rpcConsole->setTabFocus(RPCConsole::TAB_CONSOLE);
    showDebugWindow();
}

void RavenGUI::showWalletRepair()
{
    rpcConsole->setTabFocus(RPCConsole::TAB_REPAIR);
    showDebugWindow();
}

void RavenGUI::showHelpMessageClicked()
{
    helpMessageDialog->show();
}

#ifdef ENABLE_WALLET
void RavenGUI::openClicked()
{
    OpenURIDialog dlg(this);
    if(dlg.exec())
    {
        Q_EMIT receivedURI(dlg.getURI());
    }
}

void RavenGUI::gotoOverviewPage()
{
    overviewAction->setChecked(true);
    if (walletFrame) walletFrame->gotoOverviewPage();
}

void RavenGUI::gotoHistoryPage()
{
    historyAction->setChecked(true);
    if (walletFrame) walletFrame->gotoHistoryPage();
}

void RavenGUI::gotoReceiveCoinsPage()
{
    receiveCoinsAction->setChecked(true);
    if (walletFrame) walletFrame->gotoReceiveCoinsPage();
}

void RavenGUI::gotoSendCoinsPage(QString addr)
{
    sendCoinsAction->setChecked(true);
    if (walletFrame) walletFrame->gotoSendCoinsPage(addr);
}

void RavenGUI::gotoSignMessageTab(QString addr)
{
    if (walletFrame) walletFrame->gotoSignMessageTab(addr);
}

void RavenGUI::gotoVerifyMessageTab(QString addr)
{
    if (walletFrame) walletFrame->gotoVerifyMessageTab(addr);
}

/** RVN START */
void RavenGUI::gotoAssetsPage()
{
    transferAssetAction->setChecked(true);
    if (walletFrame) walletFrame->gotoAssetsPage();
};

void RavenGUI::gotoAtomicSwapsPage()
{
    atomicSwapsAction->setChecked(true);
    if (walletFrame) walletFrame->gotoAtomicSwapsPage();
};

void RavenGUI::gotoCreateAssetsPage()
{
    createAssetAction->setChecked(true);
    if (walletFrame) walletFrame->gotoCreateAssetsPage();
};

void RavenGUI::gotoManageAssetsPage()
{
    manageAssetAction->setChecked(true);
    if (walletFrame) walletFrame->gotoManageAssetsPage();
};

void RavenGUI::gotoRestrictedAssetsPage()
{
    restrictedAssetAction->setChecked(true);
    if (walletFrame) walletFrame->gotoRestrictedAssetsPage();
};
/** RVN END */
#endif // ENABLE_WALLET

void RavenGUI::updateNetworkState()
{
    int count = clientModel->getNumConnections();
    QString icon;
    switch(count)
    {
    case 0: icon = ":/icons/connect_0"; break;
    case 1: case 2: case 3: icon = ":/icons/connect_1"; break;
    case 4: case 5: case 6: icon = ":/icons/connect_2"; break;
    case 7: case 8: case 9: icon = ":/icons/connect_3"; break;
    default: icon = ":/icons/connect_4"; break;
    }

    QString tooltip;

    if (clientModel->getNetworkActive()) {
        tooltip = tr("%n active connection(s) to Raven network", "", count) + QString(".<br>") + tr("Click to disable network activity.");
    } else {
        tooltip = tr("Network activity disabled.") + QString("<br>") + tr("Click to enable network activity again.");
        icon = ":/icons/network_disabled";
    }

    // Don't word-wrap this (fixed-width) tooltip
    tooltip = QString("<nobr>") + tooltip + QString("</nobr>");
    connectionsControl->setToolTip(tooltip);

    connectionsControl->setPixmap(platformStyle->SingleColorIcon(icon).pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
}

void RavenGUI::setNumConnections(int count)
{
    updateNetworkState();
}

void RavenGUI::setNetworkActive(bool networkActive)
{
    updateNetworkState();
}

void RavenGUI::updateHeadersSyncProgressLabel()
{
    int64_t headersTipTime = clientModel->getHeaderTipTime();
    int headersTipHeight = clientModel->getHeaderTipHeight();
    int estHeadersLeft = (GetTime() - headersTipTime) / GetParams().GetConsensus().nPowTargetSpacing;
    if (estHeadersLeft > HEADER_HEIGHT_DELTA_SYNC)
        progressBarLabel->setText(tr("Syncing Headers (%1%)...").arg(QString::number(100.0 / (headersTipHeight+estHeadersLeft)*headersTipHeight, 'f', 1)));
}

void RavenGUI::setNumBlocks(int count, const QDateTime& blockDate, double nVerificationProgress, bool header)
{
    if (modalOverlay)
    {
        if (header)
            modalOverlay->setKnownBestHeight(count, blockDate);
        else
            modalOverlay->tipUpdate(count, blockDate, nVerificationProgress);
    }

    if (!clientModel)
        return;

    // Prevent orphan statusbar messages (e.g. hover Quit in main menu, wait until chain-sync starts -> garbled text)
    statusBar()->clearMessage();

    // Acquire current block source
    enum BlockSource blockSource = clientModel->getBlockSource();
    switch (blockSource) {
        case BLOCK_SOURCE_NETWORK:
            if (header) {
                updateHeadersSyncProgressLabel();
                return;
            }
            progressBarLabel->setText(tr("Synchronizing with network..."));
            updateHeadersSyncProgressLabel();
            break;
        case BLOCK_SOURCE_DISK:
            if (header) {
                progressBarLabel->setText(tr("Indexing blocks on disk..."));
            } else {
                progressBarLabel->setText(tr("Processing blocks on disk..."));
            }
            break;
        case BLOCK_SOURCE_REINDEX:
            progressBarLabel->setText(tr("Reindexing blocks on disk..."));
            break;
        case BLOCK_SOURCE_NONE:
            if (header) {
                return;
            }
            progressBarLabel->setText(tr("Connecting to peers..."));
            break;
    }

    QString tooltip;

    QDateTime currentDate = QDateTime::currentDateTime();
    qint64 secs = blockDate.secsTo(currentDate);

    tooltip = tr("Processed %n block(s) of transaction history.", "", count);

    // Set icon state: spinning if catching up, tick otherwise
    if(secs < 90*60)
    {
        tooltip = tr("Up to date") + QString(".<br>") + tooltip;
        labelBlocksIcon->setPixmap(platformStyle->SingleColorIcon(":/icons/synced").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));

#ifdef ENABLE_WALLET
        if(walletFrame)
        {
            walletFrame->showOutOfSyncWarning(false);
            modalOverlay->showHide(true, true);
        }
#endif // ENABLE_WALLET

        progressBarLabel->setVisible(false);
        progressBar->setVisible(false);
    }
    else
    {
        QString timeBehindText = GUIUtil::formatNiceTimeOffset(secs);

        progressBarLabel->setVisible(true);
        progressBar->setFormat(tr("%1 behind").arg(timeBehindText));
        progressBar->setMaximum(1000000000);
        progressBar->setValue(nVerificationProgress * 1000000000.0 + 0.5);
        progressBar->setVisible(true);

        tooltip = tr("Catching up...") + QString("<br>") + tooltip;
        if(count != prevBlocks)
        {
            labelBlocksIcon->setPixmap(platformStyle->SingleColorIcon(QString(
                ":/movies/spinner-%1").arg(spinnerFrame, 3, 10, QChar('0')))
                .pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
            spinnerFrame = (spinnerFrame + 1) % SPINNER_FRAMES;
        }
        prevBlocks = count;

#ifdef ENABLE_WALLET
        if(walletFrame)
        {
            walletFrame->showOutOfSyncWarning(true);
            modalOverlay->showHide();
        }
#endif // ENABLE_WALLET

        tooltip += QString("<br>");
        tooltip += tr("Last received block was generated %1 ago.").arg(timeBehindText);
        tooltip += QString("<br>");
        tooltip += tr("Transactions after this will not yet be visible.");
    }

    // Don't word-wrap this (fixed-width) tooltip
    tooltip = QString("<nobr>") + tooltip + QString("</nobr>");

    labelBlocksIcon->setToolTip(tooltip);
    progressBarLabel->setToolTip(tooltip);
    progressBar->setToolTip(tooltip);
}

void RavenGUI::message(const QString &title, const QString &message, unsigned int style, bool *ret)
{
    QString strTitle = tr("Raven"); // default title
    // Default to information icon
    int nMBoxIcon = QMessageBox::Information;
    int nNotifyIcon = Notificator::Information;

    QString msgType;

    // Prefer supplied title over style based title
    if (!title.isEmpty()) {
        msgType = title;
    }
    else {
        switch (style) {
        case CClientUIInterface::MSG_ERROR:
            msgType = tr("Error");
            break;
        case CClientUIInterface::MSG_WARNING:
            msgType = tr("Warning");
            break;
        case CClientUIInterface::MSG_INFORMATION:
            msgType = tr("Information");
            break;
        default:
            break;
        }
    }
    // Append title to "Raven - "
    if (!msgType.isEmpty())
        strTitle += " - " + msgType;

    // Check for error/warning icon
    if (style & CClientUIInterface::ICON_ERROR) {
        nMBoxIcon = QMessageBox::Critical;
        nNotifyIcon = Notificator::Critical;
    }
    else if (style & CClientUIInterface::ICON_WARNING) {
        nMBoxIcon = QMessageBox::Warning;
        nNotifyIcon = Notificator::Warning;
    }

    // Display message
    if (style & CClientUIInterface::MODAL) {
        // Check for buttons, use OK as default, if none was supplied
        QMessageBox::StandardButton buttons;
        if (!(buttons = (QMessageBox::StandardButton)(style & CClientUIInterface::BTN_MASK)))
            buttons = QMessageBox::Ok;

        showNormalIfMinimized();
        QMessageBox mBox((QMessageBox::Icon)nMBoxIcon, strTitle, message, buttons, this);
        int r = mBox.exec();
        if (ret != nullptr)
            *ret = r == QMessageBox::Ok;
    }
    else
        notificator->notify((Notificator::Class)nNotifyIcon, strTitle, message);
}

void RavenGUI::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
#ifndef Q_OS_MAC // Ignored on Mac
    if(e->type() == QEvent::WindowStateChange)
    {
        if(clientModel && clientModel->getOptionsModel() && clientModel->getOptionsModel()->getMinimizeToTray())
        {
            QWindowStateChangeEvent *wsevt = static_cast<QWindowStateChangeEvent*>(e);
            if(!(wsevt->oldState() & Qt::WindowMinimized) && isMinimized())
            {
                QTimer::singleShot(0, this, SLOT(hide()));
                e->ignore();
            }
        }
    }
#endif
}

void RavenGUI::closeEvent(QCloseEvent *event)
{
#ifndef Q_OS_MAC // Ignored on Mac
    if(clientModel && clientModel->getOptionsModel())
    {
        if(!clientModel->getOptionsModel()->getMinimizeOnClose())
        {
            // close rpcConsole in case it was open to make some space for the shutdown window
            rpcConsole->close();

            QApplication::quit();
        }
        else
        {
            QMainWindow::showMinimized();
            event->ignore();
        }
    }
#else
    QMainWindow::closeEvent(event);
#endif
}

void RavenGUI::showEvent(QShowEvent *event)
{
    // enable the debug window when the main window shows up
    openRPCConsoleAction->setEnabled(true);
    aboutAction->setEnabled(true);
    optionsAction->setEnabled(true);
}

#ifdef ENABLE_WALLET
void RavenGUI::incomingTransaction(const QString& date, int unit, const CAmount& amount, const QString& type, const QString& address, const QString& label, const QString& assetName)
{
    // On new transaction, make an info balloon
    QString msg = tr("Date: %1\n").arg(date);
    if (assetName == "RVN")
        msg += tr("Amount: %1\n").arg(RavenUnits::formatWithUnit(unit, amount, true));
    else
        msg += tr("Amount: %1\n").arg(RavenUnits::formatWithCustomName(assetName, amount, MAX_ASSET_UNITS, true));

    msg += tr("Type: %1\n").arg(type);

    if (!label.isEmpty())
        msg += tr("Label: %1\n").arg(label);
    else if (!address.isEmpty())
        msg += tr("Address: %1\n").arg(address);
    message((amount)<0 ? tr("Sent transaction") : tr("Incoming transaction"),
             msg, CClientUIInterface::MSG_INFORMATION);
}

void RavenGUI::checkAssets()
{
    // Check that status of RIP2 and activate the assets icon if it is active
    if(AreAssetsDeployed()) {
        transferAssetAction->setDisabled(false);
        transferAssetAction->setToolTip(tr("Transfer assets to RVN addresses"));
        createAssetAction->setDisabled(false);
        createAssetAction->setToolTip(tr("Create new main/sub/unique assets"));
        manageAssetAction->setDisabled(false);
        }
    else {
        transferAssetAction->setDisabled(true);
        transferAssetAction->setToolTip(tr("Assets not yet active"));
        createAssetAction->setDisabled(true);
        createAssetAction->setToolTip(tr("Assets not yet active"));
        manageAssetAction->setDisabled(true);
        }

    if (AreRestrictedAssetsDeployed()) {
        restrictedAssetAction->setDisabled(false);
        restrictedAssetAction->setToolTip(tr("Manage restricted assets"));

    } else {
        restrictedAssetAction->setDisabled(true);
        restrictedAssetAction->setToolTip(tr("Restricted Assets not yet active"));
    }
}
#endif // ENABLE_WALLET

void RavenGUI::dragEnterEvent(QDragEnterEvent *event)
{
    // Accept only URIs
    if(event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void RavenGUI::dropEvent(QDropEvent *event)
{
    if(event->mimeData()->hasUrls())
    {
        for (const QUrl &uri : event->mimeData()->urls())
        {
            Q_EMIT receivedURI(uri.toString());
        }
    }
    event->acceptProposedAction();
}

bool RavenGUI::eventFilter(QObject *object, QEvent *event)
{
    // Catch status tip events
    if (event->type() == QEvent::StatusTip)
    {
        // Prevent adding text from setStatusTip(), if we currently use the status bar for displaying other stuff
        if (progressBarLabel->isVisible() || progressBar->isVisible())
            return true;
    }
    return QMainWindow::eventFilter(object, event);
}

#ifdef ENABLE_WALLET
bool RavenGUI::handlePaymentRequest(const SendCoinsRecipient& recipient)
{
    // URI has to be valid
    if (walletFrame && walletFrame->handlePaymentRequest(recipient))
    {
        showNormalIfMinimized();
        gotoSendCoinsPage();
        return true;
    }
    return false;
}

void RavenGUI::setHDStatus(int hdEnabled)
{
    QString icon = "";
    if (hdEnabled == HD_DISABLED) {
        icon = ":/icons/hd_disabled";
    } else if (hdEnabled == HD_ENABLED) {
        icon = ":/icons/hd_enabled";
    } else if (hdEnabled == HD44_ENABLED) {
        icon = ":/icons/hd_enabled_44";
    }

    labelWalletHDStatusIcon->setPixmap(platformStyle->SingleColorIcon(icon).pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
    labelWalletHDStatusIcon->setToolTip(hdEnabled ? tr("HD key generation is <b>enabled</b>") : tr("HD key generation is <b>disabled</b>"));

    // eventually disable the QLabel to set its opacity to 50%
    labelWalletHDStatusIcon->setEnabled(hdEnabled);
}

void RavenGUI::setEncryptionStatus(int status)
{
    switch(status)
    {
    case WalletModel::Unencrypted:
        labelWalletEncryptionIcon->hide();
        encryptWalletAction->setChecked(false);
        changePassphraseAction->setEnabled(false);
        encryptWalletAction->setEnabled(true);
        break;
    case WalletModel::Unlocked:
        labelWalletEncryptionIcon->show();
        labelWalletEncryptionIcon->setPixmap(platformStyle->SingleColorIcon(":/icons/lock_open").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelWalletEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>unlocked</b>"));
        encryptWalletAction->setChecked(true);
        changePassphraseAction->setEnabled(true);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
        break;
    case WalletModel::Locked:
        labelWalletEncryptionIcon->show();
        labelWalletEncryptionIcon->setPixmap(platformStyle->SingleColorIcon(":/icons/lock_closed").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelWalletEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>locked</b>"));
        encryptWalletAction->setChecked(true);
        changePassphraseAction->setEnabled(true);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
        break;
    }
}
#endif // ENABLE_WALLET

void RavenGUI::showNormalIfMinimized(bool fToggleHidden)
{
    if(!clientModel)
        return;

    // activateWindow() (sometimes) helps with keyboard focus on Windows
    if (isHidden())
    {
        show();
        activateWindow();
    }
    else if (isMinimized())
    {
        showNormal();
        activateWindow();
    }
    else if (GUIUtil::isObscured(this))
    {
        raise();
        activateWindow();
    }
    else if(fToggleHidden)
        hide();
}

void RavenGUI::toggleHidden()
{
    showNormalIfMinimized(true);
}

void RavenGUI::detectShutdown()
{
    if (ShutdownRequested())
    {
        if(rpcConsole)
            rpcConsole->hide();
        qApp->quit();
    }
}

void RavenGUI::showProgress(const QString &title, int nProgress)
{
    if (nProgress == 0)
    {
        progressDialog = new QProgressDialog(title, "", 0, 100);
        progressDialog->setWindowModality(Qt::ApplicationModal);
        progressDialog->setMinimumDuration(0);
        progressDialog->setCancelButton(0);
        progressDialog->setAutoClose(false);
        progressDialog->setValue(0);
    }
    else if (nProgress == 100)
    {
        if (progressDialog)
        {
            progressDialog->close();
            progressDialog->deleteLater();
        }
    }
    else if (progressDialog)
        progressDialog->setValue(nProgress);
}

void RavenGUI::setTrayIconVisible(bool fHideTrayIcon)
{
    if (trayIcon)
    {
        trayIcon->setVisible(!fHideTrayIcon);
    }
}

void RavenGUI::showModalOverlay()
{
    if (modalOverlay && (progressBar->isVisible() || modalOverlay->isLayerVisible()))
        modalOverlay->toggleVisibility();
}

static bool ThreadSafeMessageBox(RavenGUI *gui, const std::string& message, const std::string& caption, unsigned int style)
{
    bool modal = (style & CClientUIInterface::MODAL);
    // The SECURE flag has no effect in the Qt GUI.
    // bool secure = (style & CClientUIInterface::SECURE);
    style &= ~CClientUIInterface::SECURE;
    bool ret = false;
    // In case of modal message, use blocking connection to wait for user to click a button
    QMetaObject::invokeMethod(gui, "message",
                               modal ? GUIUtil::blockingGUIThreadConnection() : Qt::QueuedConnection,
                               Q_ARG(QString, QString::fromStdString(caption)),
                               Q_ARG(QString, QString::fromStdString(message)),
                               Q_ARG(unsigned int, style),
                               Q_ARG(bool*, &ret));
    return ret;
}

static bool ThreadSafeMnemonic(RavenGUI *gui, unsigned int style)
{
    bool modal = (style & CClientUIInterface::MODAL);
    // The SECURE flag has no effect in the Qt GUI.
    // bool secure = (style & CClientUIInterface::SECURE);
    style &= ~CClientUIInterface::SECURE;
    bool ret = false;
    // In case of modal message, use blocking connection to wait for user to click a button
    QMetaObject::invokeMethod(gui, "mnemonic",
                              modal ? GUIUtil::blockingGUIThreadConnection() : Qt::QueuedConnection);
    return ret;
}

void RavenGUI::subscribeToCoreSignals()
{
    // Connect signals to client
    uiInterface.ThreadSafeMessageBox.connect(boost::bind(ThreadSafeMessageBox, this, _1, _2, _3));
    uiInterface.ThreadSafeQuestion.connect(boost::bind(ThreadSafeMessageBox, this, _1, _3, _4));
    uiInterface.ShowMnemonic.connect(boost::bind(ThreadSafeMnemonic, this, _1));
}

void RavenGUI::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    uiInterface.ThreadSafeMessageBox.disconnect(boost::bind(ThreadSafeMessageBox, this, _1, _2, _3));
    uiInterface.ThreadSafeQuestion.disconnect(boost::bind(ThreadSafeMessageBox, this, _1, _3, _4));
    uiInterface.ShowMnemonic.disconnect(boost::bind(ThreadSafeMnemonic, this, _1));
}

void RavenGUI::toggleNetworkActive()
{
    if (clientModel) {
        clientModel->setNetworkActive(!clientModel->getNetworkActive());
    }
}

/** Get restart command-line parameters and request restart */
void RavenGUI::handleRestart(QStringList args)
{
    if (!ShutdownRequested())
        Q_EMIT requestedRestart(args);
}

UnitDisplayStatusBarControl::UnitDisplayStatusBarControl(const PlatformStyle *platformStyle) :
    optionsModel(0),
    menu(0)
{
    createContextMenu(platformStyle);
    setToolTip(tr("Unit to show amounts in. Click to select another unit."));
    QList<RavenUnits::Unit> units = RavenUnits::availableUnits();
    int max_width = 0;
    const QFontMetrics fm(font());
    for (const RavenUnits::Unit unit : units)
    {
    #ifndef QTversionPreFiveEleven
        max_width = qMax(max_width, fm.horizontalAdvance(RavenUnits::name(unit)));
    #else
        max_width = qMax(max_width, fm.width(RavenUnits::name(unit)));
    #endif
    }
    setMinimumSize(max_width, 0);
    setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setStyleSheet(QString("QLabel { color : %1; }").arg(platformStyle->DarkOrangeColor().name()));
}

/** So that it responds to button clicks */
void UnitDisplayStatusBarControl::mousePressEvent(QMouseEvent *event)
{
    onDisplayUnitsClicked(event->pos());
}

/** Creates context menu, its actions, and wires up all the relevant signals for mouse events. */
void UnitDisplayStatusBarControl::createContextMenu(const PlatformStyle *platformStyle)
{
    menu = new QMenu(this);
    for (RavenUnits::Unit u : RavenUnits::availableUnits())
    {
        QAction *menuAction = new QAction(QString(RavenUnits::name(u)), this);
        menuAction->setData(QVariant(u));
        menu->addAction(menuAction);
    }
    menu->setStyleSheet(QString("QMenu::item{ color: %1; } QMenu::item:selected{ color: %2;}").arg(platformStyle->DarkOrangeColor().name(), platformStyle->TextColor().name()));
    connect(menu,SIGNAL(triggered(QAction*)),this,SLOT(onMenuSelection(QAction*)));
}

/** Lets the control know about the Options Model (and its signals) */
void UnitDisplayStatusBarControl::setOptionsModel(OptionsModel *_optionsModel)
{
    if (_optionsModel)
    {
        this->optionsModel = _optionsModel;

        // be aware of a display unit change reported by the OptionsModel object.
        connect(_optionsModel,SIGNAL(displayUnitChanged(int)),this,SLOT(updateDisplayUnit(int)));

        // initialize the display units label with the current value in the model.
        updateDisplayUnit(_optionsModel->getDisplayUnit());
    }
}

/** When Display Units are changed on OptionsModel it will refresh the display text of the control on the status bar */
void UnitDisplayStatusBarControl::updateDisplayUnit(int newUnits)
{
    setText(RavenUnits::name(newUnits));
}

/** Shows context menu with Display Unit options by the mouse coordinates */
void UnitDisplayStatusBarControl::onDisplayUnitsClicked(const QPoint& point)
{
    QPoint globalPos = mapToGlobal(point);
    menu->exec(globalPos);
}

/** Tells underlying optionsModel to update its current display unit. */
void UnitDisplayStatusBarControl::onMenuSelection(QAction* action)
{
    if (action)
    {
        optionsModel->setDisplayUnit(action->data());
    }
}

/** Triggered only when the user changes the combobox on the main GUI */
void RavenGUI::currencySelectionChanged(int unitIndex)
{
    if(clientModel && clientModel->getOptionsModel())
    {
        clientModel->getOptionsModel()->setDisplayCurrencyIndex(unitIndex);
    }
}

/** Triggered when the options model's display currency is updated */
void RavenGUI::onCurrencyChange(int newIndex)
{
    qDebug() << "RavenGUI::onPriceUnitChange: " + QString::number(newIndex);

    if(newIndex < 0 || newIndex >= CurrencyUnits::count()){
        return;
    }

    this->unitChanged = true;
    this->currentPriceDisplay = &CurrencyUnits::CurrencyOptions[newIndex];
    //Update the main GUI box in case this was changed from the settings screen
    //This will fire the event again, but the options model prevents the infinite loop
    this->comboRvnUnit->setCurrentIndex(newIndex);
    this->getPriceInfo();
}

void RavenGUI::getPriceInfo()
{
    request->setUrl(QUrl(QString("https://api.binance.com/api/v1/ticker/price?symbol=%1").arg(this->currentPriceDisplay->Ticker)));
    networkManager->get(*request);
}

#ifdef ENABLE_WALLET
void RavenGUI::mnemonic()
{
        MnemonicDialog dlg(this);
        dlg.exec();
}
#endif

void RavenGUI::getLatestVersion()
{
    versionRequest->setUrl(QUrl("https://api.github.com/repos/RavenProject/Ravencoin/releases"));
    networkVersionManager->get(*versionRequest);
}