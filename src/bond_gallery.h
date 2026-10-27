#pragma once
#include "companion.h"
#include "voice_controller.h"
#include <QWidget>
class QComboBox;class QLineEdit;class QListWidget;class QTextBrowser;class QLabel;class QProgressBar;class QPushButton;class QCheckBox;
namespace dragon {
class BondGallery final : public QWidget {
    Q_OBJECT
public:
    BondGallery(Companion &companion,VoiceController &voice,QWidget *parent=nullptr);
    void refresh();
private:
    Companion &companion_;VoiceController &voice_;
    QComboBox *category_,*state_,*stage_;QLineEdit *search_;
    QListWidget *lines_,*recent_;QTextBrowser *detail_;QLabel *overview_,*audioStatus_;
    QProgressBar *progress_;QPushButton *preview_;QCheckBox *hideUnheard_;
    QJsonArray rows_;QString selected_;
    void select(const QString &id);
    void updatePreview();
    QString visibleText(const QJsonObject &line) const;
};
}
