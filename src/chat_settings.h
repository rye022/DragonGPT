#pragma once
#include <QNetworkProxy>
#include <QUrl>

namespace dragon {
QString selectSystemProxy(const QList<QNetworkProxy> &candidates,QNetworkProxy &proxy);
QString resolveSystemProxy(QNetworkProxy &proxy,const QUrl &endpoint);
enum class ProxyMode { System = 0, Http = 1, Socks5 = 2, Tun = 3, Direct = 4 };
struct ChatSettings {
    bool onlineEnabled = false;
    ProxyMode proxyMode = ProxyMode::Direct;
    QString proxyHost = "127.0.0.1";
    int proxyPort = 7890;
    QString model="deepseek-flash";
    QString realtimeModel;
    static ChatSettings load(const QString &file);
    void save(const QString &file) const;
    QString resolveProxy(QNetworkProxy &proxy, const QUrl &endpoint) const;
};
// User-scoped Windows DPAPI. A copied portable folder cannot decrypt keys
// under a different Windows identity. No plaintext fallback is permitted.
class CredentialStore final {
public:
    explicit CredentialStore(QString file) : file_(std::move(file)) {}
    void save(const QByteArray &key) const;
    QByteArray load() const;
    void remove() const;
private:
    QString file_;
};
}
