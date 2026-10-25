#include "chat_settings.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkProxyFactory>
#include <QSaveFile>
#include <QSettings>
#include <stdexcept>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace dragon {
QString selectSystemProxy(const QList<QNetworkProxy> &candidates,QNetworkProxy &proxy){
    for(const auto &candidate:candidates){
        if(candidate.type()==QNetworkProxy::NoProxy ||
           ((candidate.type()==QNetworkProxy::HttpProxy || candidate.type()==QNetworkProxy::Socks5Proxy) && !candidate.hostName().isEmpty() && candidate.port()>0)){
            proxy=candidate;return {};
        }
    }
    return "无法解析系统代理；请检查系统代理或选择直接连接";
}
QString resolveSystemProxy(QNetworkProxy &proxy,const QUrl &endpoint){return selectSystemProxy(QNetworkProxyFactory::systemProxyForQuery(QNetworkProxyQuery(endpoint)),proxy);}
ChatSettings ChatSettings::load(const QString &file) {
    QSettings ini(file,QSettings::IniFormat);
    ChatSettings s;
    // Old OpenAI credentials/configuration must never be sent to DeepSeek.
    if(ini.value("online/provider").toString()!="deepseek")return s;
    s.onlineEnabled=ini.value("online/enabled",false).toBool();
    const auto mode=ini.value("online/proxyMode",0).toInt();
    s.proxyMode=static_cast<ProxyMode>(qBound(0,mode,4));
    s.proxyHost=ini.value("online/proxyHost","127.0.0.1").toString();
    s.proxyPort=ini.value("online/proxyPort",7890).toInt();
    s.model=ini.value("online/model",s.model).toString();
    s.realtimeModel=ini.value("online/realtimeModel").toString();
    s.complexModel=ini.value("online/complexModel").toString();
    s.visionModel=ini.value("online/visionModel").toString();
    s.visionVerifiedModel=ini.value("online/visionVerifiedModel").toString();
    return s;
}
void ChatSettings::save(const QString &file) const {
    QSettings ini(file,QSettings::IniFormat);
    ini.setValue("online/provider","deepseek");
    ini.setValue("online/enabled",onlineEnabled); ini.setValue("online/proxyMode",int(proxyMode));
    ini.setValue("online/proxyHost",proxyHost.trimmed()); ini.setValue("online/proxyPort",proxyPort);
    ini.setValue("online/model",model.trimmed()); ini.sync();
    ini.setValue("online/realtimeModel",realtimeModel.trimmed());
    ini.setValue("online/complexModel",complexModel.trimmed());ini.setValue("online/visionModel",visionModel.trimmed());
    ini.setValue("online/visionVerifiedModel",visionVerifiedModel);ini.sync();
    if(ini.status()!=QSettings::NoError) throw std::runtime_error("Cannot save online settings");
}
QString ChatSettings::resolveProxy(QNetworkProxy &proxy,const QUrl &endpoint) const {
    if(!onlineEnabled) return "未启用在线模式";
    if(model.trimmed().isEmpty()) return "尚未填写 DeepSeek 模型 ID";
    if(proxyMode==ProxyMode::System) {
        return resolveSystemProxy(proxy,endpoint);
    }
    if(proxyMode==ProxyMode::Tun || proxyMode==ProxyMode::Direct) {proxy=QNetworkProxy(QNetworkProxy::NoProxy);return {};}
    if(proxyHost.trimmed().isEmpty() || proxyPort<1 || proxyPort>65535) return "代理地址或端口无效";
    proxy=QNetworkProxy(proxyMode==ProxyMode::Http?QNetworkProxy::HttpProxy:QNetworkProxy::Socks5Proxy,
                        proxyHost.trimmed(),quint16(proxyPort));
    return {};
}
void CredentialStore::save(const QByteArray &key) const {
    if(key.trimmed().isEmpty() || key.size()>4096 || key.contains('\n') || key.contains('\r'))
        throw std::runtime_error("Invalid API key format");
#ifdef Q_OS_WIN
    DATA_BLOB input{DWORD(key.size()),reinterpret_cast<BYTE*>(const_cast<char*>(key.constData()))},output{};
    if(!CryptProtectData(&input,L"DragonGPT API key",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output))
        throw std::runtime_error("Windows could not encrypt the API key");
    QByteArray encrypted(reinterpret_cast<char*>(output.pbData),int(output.cbData));
    SecureZeroMemory(output.pbData,output.cbData); LocalFree(output.pbData);
    if(!QDir().mkpath(QFileInfo(file_).absolutePath())) throw std::runtime_error("Cannot create credential directory");
    QSaveFile file(file_);
    if(!file.open(QIODevice::WriteOnly) || file.write(encrypted)!=encrypted.size() || !file.commit())
        throw std::runtime_error("Cannot save encrypted API key");
#else
    throw std::runtime_error("This prototype requires Windows credential encryption");
#endif
}
QByteArray CredentialStore::load() const {
    QFile file(file_);
    if(!file.exists()) return {};
    if(!file.open(QIODevice::ReadOnly) || file.size()>65536) throw std::runtime_error("Cannot read encrypted API key");
    const auto encrypted=file.readAll();
#ifdef Q_OS_WIN
    DATA_BLOB input{DWORD(encrypted.size()),reinterpret_cast<BYTE*>(const_cast<char*>(encrypted.constData()))},output{};
    if(!CryptUnprotectData(&input,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output))
        throw std::runtime_error("Saved API key cannot be decrypted by this Windows account");
    QByteArray result(reinterpret_cast<char*>(output.pbData),int(output.cbData));
    SecureZeroMemory(output.pbData,output.cbData); LocalFree(output.pbData);
    return result;
#else
    return {};
#endif
}
void CredentialStore::remove() const {
    if(QFile::exists(file_) && !QFile::remove(file_)) throw std::runtime_error("Cannot remove saved API key");
}
}
