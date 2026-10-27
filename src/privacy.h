#pragma once
#include <QRegularExpression>
namespace dragon {
inline bool looksLikeCredential(const QString &text){
    static const QRegularExpression credential("sk-[A-Za-z0-9_-]{8,}|AKIA[A-Z0-9]{12,}|Bearer\\s+[A-Za-z0-9._-]{8,}|(?:api[ _-]?key|password|passwd|密码|密钥)\\s*(?:[:=：]|是)\\s*[^\\s，。]{4,}|BEGIN .*PRIVATE KEY",QRegularExpression::CaseInsensitiveOption);
    return credential.match(text).hasMatch();
}
}
