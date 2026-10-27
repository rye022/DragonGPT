#include "api_errors.h"
#include <QJsonDocument>
#include <QRegularExpression>
namespace dragon {
QString apiError(const QString &service,int status,const QJsonObject &body,const QByteArray &requestId){
    const auto nested=body.value("error").toObject();
    auto code=body.value("code").toString();if(code.isEmpty())code=nested.value("code").toString();
    QString hint;
    if(status==401)hint="密钥无效或与服务地域不匹配";
    else if(status==403)hint="未获授权；请核对地域、模型权限、账户及额度";
    else if(status==407)hint="代理需要认证，请检查代理设置";
    else if(status==429)hint="请求受限或额度不足；未自动重发";
    else if(status==400)hint="请求参数或模型配置无效";
    else if(status==404)hint="接口或模型不存在";
    else if(status>=300 && status<400)hint="重定向已停止，未转发凭据";
    else if(status>=500)hint="服务暂时不可用；未自动重发";
    else if(status==0)hint="网络、TLS 或代理连接失败";
    else hint="服务拒绝请求";
    if(code=="AllocationQuota.FreeTierOnly")hint="该模型免费额度已耗尽，且开启了用完即停；未自动改为收费";
    else if(code=="Model.AccessDenied")hint="当前业务空间/密钥没有调用该模型的权限";
    else if(code=="Workspace.AccessDenied")hint="当前密钥没有访问目标业务空间的权限";
    else if(code=="Endpoint.AccessDenied")hint="业务空间入口拒绝访问；核对 URL 中的 Workspace ID、地域及密钥所属空间，不据此判定额度不足";
    else if(code=="AccessDenied.Unpurchased")hint="该地域百炼服务尚未开通或账号不具备使用资格";
    else if(code=="AccessDenied" && body.value("message").toString(nested.value("message").toString()).contains("Access denied by API-Key restrictions",Qt::CaseInsensitive))hint="API Key 的访问限制拒绝本次调用；核对该 Key 的可访问模型范围和 IP 白名单（含代理出口），不据此判定额度不足";
    QString result=QString("%1：%2（HTTP %3）").arg(service,hint).arg(status);
    static const QRegularExpression codePattern("^[A-Za-z][A-Za-z0-9_.:-]{0,95}$"),idPattern("^[A-Fa-f0-9-]{8,100}$");
    if(codePattern.match(code).hasMatch() && !code.contains("sk-",Qt::CaseInsensitive))result+=" · code="+code;
    auto id=body.value("request_id").toString();if(id.isEmpty())id=QString::fromLatin1(requestId);
    if(idPattern.match(id).hasMatch())result+=" · request_id="+id;
    return result;
}
QString apiError(const QString &service,int status,const QByteArray &body,const QByteArray &requestId){
    const auto bounded=body.left(65536);auto object=QJsonDocument::fromJson(bounded).object();
    // Some streaming services return SSE-shaped errors even with HTTP 4xx.
    // Only parse JSON data fields; never include free-form messages in logs.
    if(object.isEmpty()){
        QByteArray data;auto normalized=bounded;normalized.replace("\r\n","\n");
        for(const auto &line:normalized.split('\n')){
            if(line.startsWith("data:")){if(!data.isEmpty())data+='\n';data+=line.mid(5).trimmed();}
            if(line.isEmpty() && !data.isEmpty()){object=QJsonDocument::fromJson(data).object();if(!object.isEmpty())break;data.clear();}
        }
        if(object.isEmpty() && !data.isEmpty())object=QJsonDocument::fromJson(data).object();
    }
    return apiError(service,status,object,requestId);
}
}
