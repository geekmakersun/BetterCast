// 中文版自带更新检查：查 GitHub 上本仓库的 Release，发现新版就弹窗下载并替换。
#pragma once

#include <QObject>
#include <QString>

class QWidget;
class QNetworkAccessManager;
class QNetworkReply;

class Updater : public QObject {
    Q_OBJECT
public:
    explicit Updater(QWidget* parent = nullptr);

    /// 查到新版本后弹窗询问是否更新。interactive=false 时（启动自动检查）
    /// 只在真的发现新版才打扰用户，网络失败静默。
    void checkForUpdates(bool interactive);

private:
    void onReleasesFetched(QNetworkReply* reply, bool interactive);
    void promptAndInstall(const QString& version, const QString& url);

    QWidget* m_parent = nullptr;
    QNetworkAccessManager* m_network = nullptr;
};
