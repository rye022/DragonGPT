#include "pet_widget.h"
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTest>
#include <stdexcept>
#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace dragon;
int main(int argc,char **argv){
    QApplication app(argc,argv);QTemporaryDir dir;int checks=0;
    auto expect=[&](bool ok,const char *why){++checks;if(!ok)throw std::runtime_error(why);};
    try{
        Companion companion(dir.filePath("memory.sqlite"));PetWidget pet(companion);
        pet.showOnDesktop();QCoreApplication::processEvents();expect(pet.isVisible(),"desktop show");
#ifdef Q_OS_WIN
        auto hwnd=reinterpret_cast<HWND>(pet.winId());
        ShowWindow(hwnd,SW_HIDE);expect(pet.isVisible() && !IsWindowVisible(hwnd),"native-hidden fixture");
        pet.showOnDesktop();QCoreApplication::processEvents();expect(IsWindowVisible(hwnd),"native-hidden recovery");
#endif
        pet.hide();pet.showOnDesktop();expect(pet.isVisible(),"explicitly hidden pet restored");
        pet.move(-30000,-30000);pet.showOnDesktop();bool visible=false;
        for(auto *display:QGuiApplication::screens())visible=visible || display->availableGeometry().intersects(pet.frameGeometry());
        expect(visible,"offscreen pet moved back");
        for(int i=0;i<3;++i)pet.showOnDesktop();expect(pet.isVisible(),"repeat show is idempotent");
        expect(companion.memory().exchanges()==0 && companion.memory().life().experience==0,"startup does not fabricate interaction or growth");
        QTextStream(stdout)<<"PASS: "<<checks<<" native desktop startup checks; no microphone or network\n";return 0;
    }catch(const std::exception &e){QTextStream(stderr)<<"FAIL: "<<e.what()<<'\n';return 1;}
}
