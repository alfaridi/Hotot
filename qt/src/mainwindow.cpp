/***************************************************************************
 *   Copyright (C) 2011~2011 by CSSlayer                                   *
 *   wengxt@gmail.com                                                      *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation, version 2 of the License.               *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "common.h"

// system
#include <unistd.h>

// Qt
#include <QWebView>
#include <QWebDatabase>
#include <QWebSettings>
#include <QDir>
#include <QWebSecurityOrigin>
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QWebFrame>
#include <QNetworkProxy>
#include <QSettings>
#include <QLocale>
#include <QSystemTrayIcon>
#include <QMenu>

// Hotot
#include "ui_mainwindow.h"
#include "mainwindow.h"
#include "hototwebpage.h"
#include "trayiconbackend.h"
#include "qttraybackend.h"
#ifdef HAVE_KDE
#include "kdetraybackend.h"
#endif

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    m_page(0),
    m_webView(0)
{
#ifdef Q_OS_UNIX
    chdir(PREFIX);
#endif
    QSettings settings("hotot-qt", "hotot");
    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());

    this->setWindowTitle(i18n("Hotot"));
    this->setWindowIcon(QIcon::fromTheme("hotot_qt", QIcon("share/hotot-qt/html/image/ic64_hotot_classics.png")));
    ui->setupUi(this);

    m_menu = new QMenu(this);
    QAction* action;
    action = new QAction(QIcon::fromTheme("application-exit"), i18n("&Exit"), this);
    action->setShortcut(QKeySequence::Quit);
    connect(action, SIGNAL(triggered()), this, SLOT(close()));
    m_menu->addAction(action);

#ifdef HAVE_KDE
    m_tray = new KDETrayBackend(this);
#else
    m_tray = new QtTrayBackend(this);
#endif

    m_tray->setContextMenu(m_menu);
    this->addAction(action);

    this->m_page = new HototWebPage(this);

    QWebSettings::setOfflineStoragePath(QDir::homePath().append("/.config/hotot-qt"));
    QWebSettings::setOfflineStorageDefaultQuota(15 * 1024 * 1024);

    m_webView = ui->webView;
    ui->webView->setPage(m_page);
    ui->webView->settings()->globalSettings()->setAttribute(QWebSettings::LocalContentCanAccessFileUrls, true);
    ui->webView->settings()->globalSettings()->setAttribute(QWebSettings::LocalContentCanAccessRemoteUrls, true);
    ui->webView->settings()->globalSettings()->setAttribute(QWebSettings::LocalStorageEnabled, true);
    ui->webView->settings()->globalSettings()->setAttribute(QWebSettings::OfflineStorageDatabaseEnabled, true);
    ui->webView->settings()->globalSettings()->setAttribute(QWebSettings::JavascriptCanOpenWindows, true);
    ui->webView->settings()->globalSettings()->setAttribute(QWebSettings::JavascriptCanAccessClipboard, true);
    ui->webView->settings()->globalSettings()->setAttribute(QWebSettings::JavascriptEnabled, true);

    
#ifdef Q_OS_UNIX
    m_webView->load(QUrl("file://" PREFIX "/share/hotot-qt/html/index.html"));
#else
    m_webView->load(QUrl("share/hotot-qt/html/index.html"));
#endif
    connect(m_webView, SIGNAL(loadFinished(bool)), this, SLOT(loadFinished(bool)));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings settings("hotot-qt", "hotot");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
    QMainWindow::closeEvent(event);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::loadFinished(bool ok)
{
    disconnect(m_webView, SIGNAL(loadFinished(bool)), this, SLOT(loadFinished(bool)));
    if (ok) {
        initDatabases();
        m_webView->page()->currentFrame()->evaluateJavaScript(QString("db.MAX_TWEET_CACHE_SIZE = 2048;"));
        m_webView->page()->currentFrame()->evaluateJavaScript(QString("db.MAX_USER_CACHE_SIZE = 128;"));
        m_webView->page()->currentFrame()->evaluateJavaScript(QString("i18n.locale = \"%1\";").arg(QLocale::system().name()));
        m_webView->page()->currentFrame()->evaluateJavaScript("globals.load_flags = 1;");
    }
}

void MainWindow::initDatabases()
{
    const QWebSecurityOrigin& origin = m_webView->page()->currentFrame()->securityOrigin();
    const QList<QWebDatabase>& databases = origin.databases();
    Q_FOREACH(QWebDatabase webDatabase, databases) {
        {
            QSqlDatabase sqldb = QSqlDatabase::addDatabase("QSQLITE", "myconnection");
            sqldb.setDatabaseName(webDatabase.fileName());
            if (sqldb.open()) {
                sqldb.exec("vacuum");

                if (webDatabase.name() == "hotot.cache") {
                    sqldb.exec("vacuum");
                    QSqlQuery result = sqldb.exec("select value from Info where key=\"settings\"");
                    while (result.next()) {
                        QString settings = result.value(0).toString();
                        m_webView->page()->currentFrame()->evaluateJavaScript("hotot_qt = " + settings + ";");
                        bool useHttpProxy = m_webView->page()->currentFrame()->evaluateJavaScript("hotot_qt.use_http_proxy").toBool();
                        int httpProxyPort = m_webView->page()->currentFrame()->evaluateJavaScript("hotot_qt.http_proxy_port").toInt();
                        QString httpProxyHost = m_webView->page()->currentFrame()->evaluateJavaScript("hotot_qt.http_proxy_host").toString();

                        if (useHttpProxy) {
                            QNetworkProxy proxy(QNetworkProxy::HttpProxy,
                                                httpProxyHost,
                                                httpProxyPort);

                            m_webView->page()->networkAccessManager()->setProxy(proxy);
                        }
                    }
                }
                sqldb.close();
            }
        }
        QSqlDatabase::removeDatabase("myconnection");
    }
}

void MainWindow::triggerVisible()
{
    if (this->isActiveWindow()) {
        if (this->isVisible())
            this->hide();
    } else {
        if (!this->isVisible())
            this->show();
        this->activateWindow();
        this->raise();
    }
}

void MainWindow::notification(QString type, QString title, QString message, QString image)
{
    m_tray->showMessage(type, title, message, image);
}

void MainWindow::activate()
{
    if (!this->isActiveWindow()) {
        if (!this->isVisible())
            this->show();
        this->activateWindow();
        this->raise();
    }
}

void MainWindow::unreadAlert(QString number)
{
    m_tray->unreadAlert(number);
}

#include "mainwindow.moc"