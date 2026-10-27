#pragma once
#include <QObject>
#include <QDateTime>
#include <QJsonObject>
namespace dragon {
struct DesktopContext {
    QString kind="unknown";
    double confidence=0;
    bool lowInterruption=true;
    QDateTime since;
    QJsonObject json() const;
};
class ContextEngine final:public QObject {
    Q_OBJECT
public:
    explicit ContextEngine(QObject *parent=nullptr):QObject(parent){}
    void setEnabled(bool enabled);
    bool enabled() const{return enabled_;}
    DesktopContext current() const{return context_;}
    void observe(const QString &title,const QString &text,bool fullscreen,QDateTime now);
    void noteConversation(const QString &text,QDateTime now);
    static DesktopContext classify(const QString &title,const QString &text,bool fullscreen);
signals:
    void changed();
private:
    bool enabled_=false;
    DesktopContext context_;
    QString candidate_;
    QDateTime candidateAt_;
    QString recentConversation_;
    QDateTime conversationAt_;
};
}
