#include <QCoreApplication>
#include <QImage>
#include <QBuffer>
#include <QFile>
#include <io.h>
#include <fcntl.h>
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// Runs separately because PrintWindow may hang inside a third-party renderer.
// Parent owns a deadline and can kill this helper without blocking its UI.
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);if(app.arguments().size()!=2)return 2;
    _setmode(_fileno(stdout),_O_BINARY);
    SetProcessDPIAware();const auto hwnd=reinterpret_cast<HWND>(app.arguments()[1].toULongLong());RECT r{};
    if(!IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd) || !GetWindowRect(hwnd,&r))return 3;
    const int width=r.right-r.left,height=r.bottom-r.top;if(width<1 || height<1 || width>16384 || height>16384)return 4;
    const auto desktop=GetDC(nullptr);const auto dc=CreateCompatibleDC(desktop);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void *pixels=nullptr;const auto bitmap=CreateDIBSection(desktop,&info,DIB_RGB_COLORS,&pixels,nullptr,0);QImage image;
    if(bitmap && dc){const auto old=SelectObject(dc,bitmap);PatBlt(dc,0,0,width,height,BLACKNESS);if(PrintWindow(hwnd,dc,2))image=QImage(static_cast<uchar*>(pixels),width,height,QImage::Format_RGB32).copy();SelectObject(dc,old);}
    if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);ReleaseDC(nullptr,desktop);if(image.isNull())return 5;
    bool content=false;for(int y=0;y<height && !content;y+=qMax(1,height/30))for(int x=0;x<width;x+=qMax(1,width/30)){const auto c=image.pixelColor(x,y);if(c.red()>8 || c.green()>8 || c.blue()>8){content=true;break;}}
    if(!content)return 6;
    QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);if(!image.scaled(2600,2600,Qt::KeepAspectRatio,Qt::SmoothTransformation).save(&buffer,"PNG"))return 7;
    QFile output;if(!output.open(stdout,QIODevice::WriteOnly))return 8;if(output.write(png)!=png.size() || !output.flush())return 9;return 0;
}
