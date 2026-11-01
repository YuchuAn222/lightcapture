#define wWinMain originalLightCaptureEntry
#include "../src/LightCapture.cpp"
#undef wWinMain
#include <iostream>
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2)return 2;std::wstring path=std::wstring(argv[1])+L"\\export-test.png";
 Gdiplus::GdiplusStartupInput input;ULONG_PTR token;check(Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok,"GDI+");
 {Gdiplus::Bitmap image(64,40,PixelFormat24bppRGB);{Gdiplus::Graphics g(&image);g.Clear(Gdiplus::Color(255,30,80,140));}auto png=clip::pngBytes(image);check(!png.empty(),"PNG data");
  check(clip::exportPng(png,path),"write PNG");check(clip::exportPng(png,path),"atomic overwrite PNG");
  Gdiplus::Bitmap read(path.c_str());check(read.GetLastStatus()==Gdiplus::Ok && read.GetWidth()==64 && read.GetHeight()==40,"decode export");Gdiplus::Color color;read.GetPixel(10,10,&color);check(color.GetR()==30 && color.GetG()==80 && color.GetB()==140,"export pixel");
  check(!clip::exportPng({},path),"empty export rejected");check(!clip::exportPng(png,std::wstring(argv[1])+L"\\missing-folder\\bad.png"),"invalid path rejected");
 }
 Gdiplus::GdiplusShutdown(token);std::cout<<"PASS PNG export, atomic overwrite, dimensions/pixels, empty/invalid path rejection\n";return 0;
}catch(std::exception& error){std::cerr<<error.what();return 1;}}
