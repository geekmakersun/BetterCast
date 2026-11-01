#include "Updater.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDirIterator>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QWidget>

namespace {

const QString kReleasesApi =
    "https://api.github.com/repos/geekmakersun/BetterCast/releases?per_page=30";

// 只认中文版自己产出的绿色版压缩包
const QString kAssetPattern = "BetterCast-Windows-x64-zh-Hans-([0-9]+\\.[0-9]+\\.[0-9]+)\\.zip";

QList<int> versionParts(const QString& v) {
    QList<int> parts;
    for (const auto& p : v.split('.')) parts.append(p.toInt());
    return parts;
}

bool isNewer(const QString& candidate, const QString& current) {
    const QList<int> a = versionParts(candidate);
    const QList<int> b = versionParts(current);
    for (int i = 0; i < qMax(a.size(), b.size()); ++i) {
        int x = i < a.size() ? a.at(i) : 0;
        int y = i < b.size() ? b.at(i) : 0;
        if (x != y) return x > y;
    }
    return false;
}

}  // namespace

Updater::Updater(QWidget* parent) : QObject(parent), m_parent(parent) {
    m_network = new QNetworkAccessManager(this);
}

void Updater::checkForUpdates(bool interactive) {
    QNetworkRequest req{QUrl(kReleasesApi)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "BetterCast-zh-Hans-Updater");
    req.setRawHeader("Accept", "application/vnd.github+json");

    QNetworkReply* reply = m_network->get(req);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, interactive] { onReleasesFetched(reply, interactive); });
}

void Updater::onReleasesFetched(QNetworkReply* reply, bool interactive) {
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        if (interactive) {
            QMessageBox::warning(m_parent, "检查更新",
                                 QString("无法连接 GitHub：%1\n如果开了代理，请在系统代理里放行 BetterCast。")
                                     .arg(reply->errorString()));
        }
        return;
    }

    const QString current = QApplication::applicationVersion();
    QString bestVersion;
    QString bestUrl;

    const QJsonArray releases = QJsonDocument::fromJson(reply->readAll()).array();
    QRegularExpression rx(kAssetPattern);

    for (const auto& value : releases) {
        const QJsonObject rel = value.toObject();
        if (rel.value("draft").toBool()) continue;
        for (const auto& assetValue : rel.value("assets").toArray()) {
            const QJsonObject asset = assetValue.toObject();
            const QString name = asset.value("name").toString();
            const QRegularExpressionMatch m = rx.match(name);
            if (!m.hasMatch()) continue;
            const QString v = m.captured(1);
            if (!isNewer(v, current)) continue;
            if (bestVersion.isEmpty() || isNewer(v, bestVersion)) {
                bestVersion = v;
                bestUrl = asset.value("browser_download_url").toString();
            }
        }
    }

    if (bestVersion.isEmpty() || bestUrl.isEmpty()) {
        if (interactive) {
            QMessageBox::information(m_parent, "检查更新",
                                     QString("已是最新版本（v%1）。").arg(current));
        }
        return;
    }

    promptAndInstall(bestVersion, bestUrl);
}

void Updater::promptAndInstall(const QString& version, const QString& url) {
    QMessageBox box(m_parent);
    box.setWindowTitle("发现新版本");
    box.setText(QString("发现新版本 v%1（当前 v%2），是否立即更新？")
                    .arg(version, QApplication::applicationVersion()));
    box.setInformativeText("下载完成后程序会自动重启，设置与日志会保留。");
    QPushButton* go = box.addButton("立即更新", QMessageBox::AcceptRole);
    box.addButton("稍后", QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != go) return;

    // 这些对象要活到下载结束，放堆上
    QTemporaryDir* tempDir = new QTemporaryDir(
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
        "/BetterCastUpdate-XXXXXX");
    if (!tempDir->isValid()) {
        delete tempDir;
        QMessageBox::critical(m_parent, "更新失败", "无法创建临时目录。");
        return;
    }

    QFile* zip = new QFile(tempDir->path() + "/update.zip");
    if (!zip->open(QIODevice::WriteOnly)) {
        delete zip;
        delete tempDir;
        QMessageBox::critical(m_parent, "更新失败", "无法写入临时文件。");
        return;
    }
    const QString zipPath = zip->fileName();

    QProgressDialog* progress = new QProgressDialog("正在下载更新…", "取消", 0, 100, m_parent);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAttribute(Qt::WA_DeleteOnClose);

    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "BetterCast-zh-Hans-Updater");
    QNetworkReply* reply = m_network->get(req);

    connect(reply, &QNetworkReply::downloadProgress, this,
            [progress](qint64 done, qint64 total) {
                if (total > 0) {
                    progress->setMaximum(100);
                    progress->setValue(static_cast<int>(done * 100 / total));
                }
            });
    connect(progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, progress, zip, tempDir, zipPath] {
                progress->close();
                if (reply->error() != QNetworkReply::NoError) {
                    zip->close();
                    zip->remove();
                    reply->deleteLater();
                    QMessageBox::warning(m_parent, "更新失败",
                                         QString("下载失败：%1").arg(reply->errorString()));
                    return;
                }
                zip->write(reply->readAll());
                zip->close();
                reply->deleteLater();

                // 解压
                const QString outDir = tempDir->path() + "/extracted";
                QProcess unzip;
                unzip.start("powershell",
                            {"-NoProfile", "-Command",
                             QString("Expand-Archive -LiteralPath '%1' -DestinationPath '%2' -Force")
                                 .arg(zipPath, outDir)});
                if (!unzip.waitForFinished(120000) || unzip.exitCode() != 0) {
                    QMessageBox::warning(m_parent, "更新失败",
                                         "解压失败：" + QString::fromLocal8Bit(unzip.readAllStandardError()));
                    return;
                }

                // 找到解压出来的程序目录（压缩包里带一层版本目录）
                QString sourceDir;
                QDirIterator it(outDir, {"BetterCastReceiver.exe"}, QDir::Files,
                                QDirIterator::Subdirectories);
                if (it.hasNext()) {
                    sourceDir = QFileInfo(it.next()).absolutePath();
                }
                if (sourceDir.isEmpty()) {
                    QMessageBox::warning(m_parent, "更新失败", "压缩包里找不到 BetterCastReceiver.exe。");
                    return;
                }

                const QString appDir = QCoreApplication::applicationDirPath();
                const QString batPath = tempDir->path() + "/apply_update.bat";
                QFile bat(batPath);
                if (!bat.open(QIODevice::WriteOnly)) {
                    QMessageBox::warning(m_parent, "更新失败", "无法写入更新脚本。");
                    return;
                }
                const QString script = QString(
                    "@echo off\r\n"
                    "timeout /t 2 /nobreak >nul\r\n"
                    ":wait\r\n"
                    "tasklist /fi \"imagename eq BetterCastReceiver.exe\" | find /i \"BetterCastReceiver.exe\" >nul\r\n"
                    "if not errorlevel 1 goto copy\r\n"
                    "timeout /t 1 /nobreak >nul\r\n"
                    "goto wait\r\n"
                    ":copy\r\n"
                    "robocopy \"%1\" \"%2\" /E /NFL /NDL /NJH /NJS >nul\r\n"
                    "start \"\" \"%2\\BetterCastReceiver.exe\"\r\n"
                    "rmdir /s /q \"%3\"\r\n")
                    .arg(sourceDir, appDir, tempDir->path());
                bat.write(script.toLocal8Bit());
                bat.close();

                // 临时目录要在脚本跑完前保留，所以这里放弃自动清理
                tempDir->setAutoRemove(false);

                QProcess::startDetached("cmd.exe", {"/c", batPath});
                QMessageBox::information(m_parent, "正在更新",
                                         "程序即将关闭并自动完成更新，请稍等几秒。");
                QApplication::quit();
            });
}
