#define UNICODE
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include "../src/ClipboardStore.h"
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"ole32.lib")
static void require(bool value){if(!value)throw std::runtime_error("assertion failed");}
int wmain(int argc,wchar_t** argv){try{
 require(argc==2);
 {
 clip::Store s;s.open(argv[1]);s.clear(true);
 std::wstring full=L"中文与 emoji \U0001F680\r\n"+std::wstring(250,L'x')+L"全文搜索尾部";
 require(s.add(1,full,{},{},L"中文预览"));require(s.count()==1);auto first=s.list(0,L"")[0];require(s.text(first.id)==full);
 require(s.list(1,L"全文搜索尾部").size()==1);require(s.list(2,L"").empty());
 require(s.add(1,full,{},{},L"中文预览"));require(s.count()==1);
 s.pin(first.id,true);require(s.list(3,L"").size()==1);s.limit=2;
 s.add(1,L"second",{},{},L"second");s.add(1,L"third",{},{},L"third");require(s.count()==2);require(s.list(0,L"second").empty());require(s.list(3,L"")[0].id==first.id);
 s.setSetting(L"paused",L"1");s.setSetting(L"excluded",L"KeePass.exe");
 }
 {
 clip::Store s;s.open(argv[1]);require(s.count()==2);require(s.getSetting(L"paused")==L"1");require(s.getSetting(L"excluded")==L"KeePass.exe");require(s.list(3,L"").size()==1);
 s.clear(false);require(s.count()==1);s.capacity=s.bytes()+1;require(!s.add(1,L"new clipboard content",{},{},L"new"));require(s.count()==1);require(s.list(3,L"").size()==1);
 s.clear(true);require(s.count()==0);s.capacity=1000;require(s.add(2,L"",std::vector<BYTE>(100,42),std::vector<BYTE>(5,1),L"image"));auto image=s.list(2,L"")[0];require(s.image(image.id)==std::vector<BYTE>(100,42));s.erase(image.id);require(s.count()==0);
 s.capacity=300LL*1024*1024;s.limit=500;for(int i=0;i<510;i++)require(s.add(1,L"history "+std::to_wstring(i),{},{},L"item"));require(s.count()==500);require(s.list(0,L"history 0").empty());require(s.list(0,L"history 509").size()==1);
 }
 std::cout<<"PASS Unicode/emoji, full-text search, dedup, filtering, pin, capacity eviction, protected favorites, settings, reopen persistence, image blob, deletion/clear\n";return 0;
 }catch(std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
