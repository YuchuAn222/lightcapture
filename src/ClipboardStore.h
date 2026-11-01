#pragma once
#include <bcrypt.h>
#include <shlobj.h>
#pragma comment(lib,"bcrypt.lib")
namespace clip {
struct sqlite3; struct sqlite3_stmt;
struct Sql {
 HMODULE module=nullptr;
 int (*open16)(const void*,sqlite3**)=nullptr;
 int (*close)(sqlite3*)=nullptr;
 int (*exec)(sqlite3*,const char*,int(*)(void*,int,char**,char**),void*,char**)=nullptr;
 int (*prepare16_v2)(sqlite3*,const void*,int,sqlite3_stmt**,const void**)=nullptr;
 int (*step)(sqlite3_stmt*)=nullptr;int (*finalize)(sqlite3_stmt*)=nullptr;
 int (*bind_int64)(sqlite3_stmt*,int,long long)=nullptr;
 int (*bind_text16)(sqlite3_stmt*,int,const void*,int,void(*)(void*))=nullptr;
 int (*bind_blob)(sqlite3_stmt*,int,const void*,int,void(*)(void*))=nullptr;
 long long (*column_int64)(sqlite3_stmt*,int)=nullptr;
 const void* (*column_text16)(sqlite3_stmt*,int)=nullptr;
 const void* (*column_blob)(sqlite3_stmt*,int)=nullptr;
 int (*column_bytes)(sqlite3_stmt*,int)=nullptr;
 void load(){
  wchar_t dir[MAX_PATH];GetSystemDirectoryW(dir,MAX_PATH);std::wstring path=std::wstring(dir)+L"\\winsqlite3.dll";
  module=LoadLibraryW(path.c_str());if(!module)throw std::runtime_error("Windows SQLite unavailable");
#define LOAD_SQL(n) n=reinterpret_cast<decltype(n)>(GetProcAddress(module,"sqlite3_" #n));if(!n)throw std::runtime_error("SQLite API unavailable");
  LOAD_SQL(open16) LOAD_SQL(close) LOAD_SQL(exec) LOAD_SQL(prepare16_v2) LOAD_SQL(step) LOAD_SQL(finalize)
  LOAD_SQL(bind_int64) LOAD_SQL(bind_text16) LOAD_SQL(bind_blob) LOAD_SQL(column_int64) LOAD_SQL(column_text16) LOAD_SQL(column_blob) LOAD_SQL(column_bytes)
#undef LOAD_SQL
 }
 ~Sql(){if(module)FreeLibrary(module);}
};
static Sql sql;
static void dbCheck(int code){if(code!=0 && code!=100 && code!=101)throw std::runtime_error("Clipboard database operation failed");}
struct Statement {
 sqlite3_stmt* p=nullptr;
 Statement(sqlite3* db,const wchar_t* query){dbCheck(sql.prepare16_v2(db,query,-1,&p,nullptr));}
 ~Statement(){if(p)sql.finalize(p);}
 void number(int n,long long value){dbCheck(sql.bind_int64(p,n,value));}
 void string(int n,const std::wstring& value){dbCheck(sql.bind_text16(p,n,value.c_str(),(int)value.size()*2,reinterpret_cast<void(*)(void*)>(-1)));}
 void blob(int n,const std::vector<BYTE>& value){dbCheck(sql.bind_blob(p,n,value.empty()?"":(const void*)value.data(),(int)value.size(),reinterpret_cast<void(*)(void*)>(-1)));}
 int step(){int r=sql.step(p);dbCheck(r);return r;}
 long long number(int n){return sql.column_int64(p,n);}
 std::wstring string(int n){auto ptext=(const wchar_t*)sql.column_text16(p,n);return ptext?ptext:L"";}
 std::vector<BYTE> blob(int n){auto data=(const BYTE*)sql.column_blob(p,n);int size=sql.column_bytes(p,n);return size>0?std::vector<BYTE>(data,data+size):std::vector<BYTE>();}
};
struct Item {long long id=0,stamp=0,bytes=0;int kind=0;bool pinned=false;std::wstring summary;std::vector<BYTE> thumb;};
static std::wstring digest(const void* data,size_t size){
 BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;BYTE output[32]={};
 if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA256 unavailable");
 NTSTATUS r=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0);
 if(r>=0)r=BCryptHashData(hash,(PUCHAR)data,(ULONG)size,0);
 if(r>=0)r=BCryptFinishHash(hash,output,32,0);
 if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
 if(r<0)throw std::runtime_error("SHA256 failed");
 std::wstring result;for(BYTE b:output){result+=L"0123456789abcdef"[b>>4];result+=L"0123456789abcdef"[b&15];}return result;
}
static long long now(){FILETIME t;GetSystemTimeAsFileTime(&t);ULARGE_INTEGER u;u.LowPart=t.dwLowDateTime;u.HighPart=t.dwHighDateTime;return (long long)u.QuadPart;}
class Store {
 sqlite3* db=nullptr;
public:
 std::wstring directory;
 long long capacity=300LL*1024*1024;int limit=500;
 ~Store(){if(db)sql.close(db);}
 void execute(const char* query){dbCheck(sql.exec(db,query,nullptr,nullptr,nullptr));}
 void open(const std::wstring& dir){
  if(!sql.module)sql.load();directory=dir;SHCreateDirectoryExW(nullptr,dir.c_str(),nullptr);
  dbCheck(sql.open16((dir+L"\\history.db").c_str(),&db));
  execute("PRAGMA auto_vacuum=INCREMENTAL; PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL; PRAGMA cache_size=-512; PRAGMA max_page_count=78000; PRAGMA busy_timeout=1000;");
  execute("CREATE TABLE IF NOT EXISTS clips(id INTEGER PRIMARY KEY,kind INTEGER NOT NULL,stamp INTEGER NOT NULL,pinned INTEGER NOT NULL DEFAULT 0,digest TEXT UNIQUE NOT NULL,payload,summary TEXT NOT NULL,thumb BLOB); CREATE INDEX IF NOT EXISTS clips_order ON clips(pinned DESC,stamp DESC); CREATE TABLE IF NOT EXISTS settings(k TEXT PRIMARY KEY,v TEXT NOT NULL);");
 }
 std::wstring getSetting(const std::wstring& key,const std::wstring& fallback=L""){
  Statement s(db,L"SELECT v FROM settings WHERE k=?");s.string(1,key);return s.step()==100?s.string(0):fallback;
 }
 void setSetting(const std::wstring& key,const wchar_t* value){Statement s(db,L"INSERT INTO settings(k,v) VALUES(?,?) ON CONFLICT(k) DO UPDATE SET v=excluded.v");s.string(1,key);s.string(2,value);s.step();}
 bool add(int kind,const std::wstring& text,const std::vector<BYTE>& image,const std::vector<BYTE>& thumb,const std::wstring& summary){
  size_t size=kind==1?text.size()*2:image.size();if(!size || size>32*1024*1024)return false;
  auto hash=digest(kind==1?(const void*)text.data():image.data(),size);hash=(kind==1?L"text:":L"image:")+hash;
  execute("BEGIN IMMEDIATE");
  try{
   bool duplicate=false;{Statement s(db,L"SELECT 1 FROM clips WHERE digest=?");s.string(1,hash);duplicate=s.step()==100;}
   long long needed=(long long)image.size()+(long long)thumb.size();if(kind==1){Statement s(db,L"SELECT length(CAST(? AS BLOB))");s.string(1,text);s.step();needed=s.number(0);}
   if(!duplicate){
    if(needed>capacity){execute("ROLLBACK");return false;}
    for(;;){
     long long usage=bytes(),total=count();if(usage+needed<=capacity && total<limit)break;
     long long victim=0;{Statement s(db,L"SELECT id FROM clips WHERE pinned=0 ORDER BY stamp,id LIMIT 1");if(s.step()!=100){execute("ROLLBACK");return false;}victim=s.number(0);}
     {Statement s(db,L"DELETE FROM clips WHERE id=?");s.number(1,victim);s.step();}
    }
   }
   {Statement s(db,L"INSERT INTO clips(kind,stamp,digest,payload,summary,thumb) VALUES(?,max(?,COALESCE((SELECT MAX(stamp)+1 FROM clips),0)),?,?,?,?) ON CONFLICT(digest) DO UPDATE SET stamp=excluded.stamp");s.number(1,kind);s.number(2,now());s.string(3,hash);if(kind==1)s.string(4,text);else s.blob(4,image);s.string(5,summary);s.blob(6,thumb);s.step();}
   for(;;){
    long long bytes=0,count=0;{Statement s(db,L"SELECT COALESCE(SUM(length(CAST(payload AS BLOB))+length(COALESCE(thumb,''))),0),COUNT(*) FROM clips");s.step();bytes=s.number(0);count=s.number(1);}
    if(bytes<=capacity && count<=limit)break;
    long long id=0;{Statement s(db,L"SELECT id FROM clips WHERE pinned=0 ORDER BY stamp LIMIT 1");if(s.step()!=100)throw std::runtime_error("Pinned storage full");id=s.number(0);}
    {Statement s(db,L"DELETE FROM clips WHERE id=?");s.number(1,id);s.step();}
   }
   execute("COMMIT");
  }catch(...){execute("ROLLBACK");throw;}
  execute("PRAGMA incremental_vacuum(64)");Statement saved(db,L"SELECT 1 FROM clips WHERE digest=?");saved.string(1,hash);return saved.step()==100;
 }
 std::vector<Item> list(int filter,const std::wstring& query,bool onlyPinned=false){
  Statement s(db,L"SELECT id,kind,stamp,pinned,summary,length(CAST(payload AS BLOB)),thumb FROM clips WHERE (?=0 OR (?=3 AND pinned=1) OR kind=?) AND (?='' OR instr(lower(summary),lower(?))>0 OR (kind=1 AND instr(lower(payload),lower(?))>0)) AND (?=0 OR pinned=1) ORDER BY pinned DESC,stamp DESC LIMIT 2000");
  s.number(1,filter);s.number(2,filter);s.number(3,filter);s.string(4,query);s.string(5,query);s.string(6,query);s.number(7,onlyPinned?1:0);std::vector<Item> result;
  while(s.step()==100){Item i;i.id=s.number(0);i.kind=(int)s.number(1);i.stamp=s.number(2);i.pinned=s.number(3)!=0;i.summary=s.string(4);i.bytes=s.number(5);i.thumb=s.blob(6);result.push_back(std::move(i));}return result;
 }
 std::wstring text(long long id){Statement s(db,L"SELECT payload FROM clips WHERE id=? AND kind=1");s.number(1,id);return s.step()==100?s.string(0):L"";}
 std::vector<BYTE> image(long long id){Statement s(db,L"SELECT payload FROM clips WHERE id=? AND kind=2");s.number(1,id);return s.step()==100?s.blob(0):std::vector<BYTE>();}
 void pin(long long id,bool pinned){Statement s(db,L"UPDATE clips SET pinned=? WHERE id=?");s.number(1,pinned?1:0);s.number(2,id);s.step();}
 void erase(long long id){Statement s(db,L"DELETE FROM clips WHERE id=?");s.number(1,id);s.step();execute("PRAGMA incremental_vacuum(128)");}
 void clear(bool all){execute(all?"DELETE FROM clips":"DELETE FROM clips WHERE pinned=0");execute("PRAGMA incremental_vacuum");}
 long long count(){Statement s(db,L"SELECT COUNT(*) FROM clips");s.step();return s.number(0);}
 long long bytes(){Statement s(db,L"SELECT COALESCE(SUM(length(CAST(payload AS BLOB))+length(COALESCE(thumb,''))),0) FROM clips");s.step();return s.number(0);}
};
}
