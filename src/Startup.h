#pragma once
#include <taskschd.h>
#include <comdef.h>
#include <sddl.h>
#pragma comment(lib,"taskschd.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"oleaut32.lib")
#pragma comment(lib,"comsuppw.lib")

template<class T> struct ComRef {
    T* value=nullptr;
    ~ComRef(){if(value)value->Release();}
    T** address(){return &value;}
    T* operator->() const{return value;}
    ComRef()=default;
    ComRef(const ComRef&)=delete;
    ComRef& operator=(const ComRef&)=delete;
};
static void requireHr(HRESULT result){if(FAILED(result))_com_issue_error(result);}
struct ComScope {
    HRESULT result;
    ComScope():result(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)){
        if(result!=RPC_E_CHANGED_MODE)requireHr(result);
        HRESULT security=CoInitializeSecurity(nullptr,-1,nullptr,nullptr,RPC_C_AUTHN_LEVEL_DEFAULT,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE,nullptr);
        if(security!=RPC_E_TOO_LATE)requireHr(security);
    }
    ~ComScope(){if(SUCCEEDED(result))CoUninitialize();}
};
static std::wstring currentUserSid(){
    HANDLE token=nullptr;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))_com_issue_error(HRESULT_FROM_WIN32(GetLastError()));
    DWORD bytes=0;GetTokenInformation(token,TokenUser,nullptr,0,&bytes);
    std::vector<BYTE> data(bytes);
    BOOL got=GetTokenInformation(token,TokenUser,data.data(),bytes,&bytes);DWORD error=GetLastError();CloseHandle(token);
    if(!got)_com_issue_error(HRESULT_FROM_WIN32(error));
    LPWSTR stringSid=nullptr;
    if(!ConvertSidToStringSidW(((TOKEN_USER*)data.data())->User.Sid,&stringSid))_com_issue_error(HRESULT_FROM_WIN32(GetLastError()));
    std::wstring result=stringSid;LocalFree(stringSid);return result;
}
static std::wstring taskName(){return L"LightCapture_Autostart_"+currentUserSid();}
static void connectTasks(ComRef<ITaskService>& service,ComRef<ITaskFolder>& folder){
    requireHr(CoCreateInstance(CLSID_TaskScheduler,nullptr,CLSCTX_INPROC_SERVER,IID_ITaskService,(void**)service.address()));
    requireHr(service->Connect(_variant_t(),_variant_t(),_variant_t(),_variant_t()));
    requireHr(service->GetFolder(_bstr_t(L"\\"),folder.address()));
}
static std::wstring xmlText(const std::wstring& text){
    std::wstring output;
    for(wchar_t c:text){switch(c){case L'&':output+=L"&amp;";break;case L'<':output+=L"&lt;";break;case L'>':output+=L"&gt;";break;default:output+=c;}}
    return output;
}
static HRESULT setScheduledAutostart(bool enabled){
    try{
        ComScope com;ComRef<ITaskService> service;ComRef<ITaskFolder> folder;connectTasks(service,folder);
        std::wstring name=taskName();
        if(!enabled){
            HRESULT result=folder->DeleteTask(_bstr_t(name.c_str()),0);
            return result==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || result==HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)?S_OK:result;
        }
        std::wstring sid=xmlText(currentUserSid()),path=exePath();
        std::wstring directory=path.substr(0,path.find_last_of(L"\\/"));
        std::wstring xml=L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
            L"<RegistrationInfo><Description>轻截图：当前用户登录后启动</Description></RegistrationInfo>"
            L"<Triggers><LogonTrigger><Enabled>true</Enabled><UserId>"+sid+L"</UserId><Delay>PT10S</Delay></LogonTrigger></Triggers>"
            L"<Principals><Principal id=\"CurrentUser\"><UserId>"+sid+L"</UserId><LogonType>InteractiveToken</LogonType><RunLevel>LeastPrivilege</RunLevel></Principal></Principals>"
            L"<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>"
            L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries><StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>"
            L"<AllowHardTerminate>false</AllowHardTerminate><StartWhenAvailable>true</StartWhenAvailable>"
            L"<RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>"
            L"<IdleSettings><StopOnIdleEnd>false</StopOnIdleEnd><RestartOnIdle>false</RestartOnIdle></IdleSettings>"
            L"<AllowStartOnDemand>true</AllowStartOnDemand><Enabled>true</Enabled><Hidden>false</Hidden>"
            L"<RunOnlyIfIdle>false</RunOnlyIfIdle><WakeToRun>false</WakeToRun><ExecutionTimeLimit>PT0S</ExecutionTimeLimit><Priority>7</Priority>"
            L"<RestartOnFailure><Interval>PT1M</Interval><Count>3</Count></RestartOnFailure></Settings>"
            L"<Actions Context=\"CurrentUser\"><Exec><Command>"+xmlText(path)+L"</Command><Arguments>--background</Arguments><WorkingDirectory>"+
            xmlText(directory)+L"</WorkingDirectory></Exec></Actions></Task>";
        ComRef<IRegisteredTask> registered;
        return folder->RegisterTask(_bstr_t(name.c_str()),_bstr_t(xml.c_str()),TASK_CREATE_OR_UPDATE,
            _variant_t(currentUserSid().c_str()),_variant_t(),TASK_LOGON_INTERACTIVE_TOKEN,_variant_t(),registered.address());
    }catch(const _com_error& error){return error.Error();}
}
static bool scheduledAutostartEnabled(){
    try{
        ComScope com;ComRef<ITaskService> service;ComRef<ITaskFolder> folder;connectTasks(service,folder);
        ComRef<IRegisteredTask> task;requireHr(folder->GetTask(_bstr_t(taskName().c_str()),task.address()));
        VARIANT_BOOL enabled=VARIANT_FALSE;requireHr(task->get_Enabled(&enabled));if(enabled!=VARIANT_TRUE)return false;
        ComRef<ITaskDefinition> definition;requireHr(task->get_Definition(definition.address()));
        ComRef<IActionCollection> actions;requireHr(definition->get_Actions(actions.address()));
        ComRef<IAction> action;requireHr(actions->get_Item(1,action.address()));
        ComRef<IExecAction> execute;requireHr(action->QueryInterface(IID_IExecAction,(void**)execute.address()));
        BSTR path=nullptr,arguments=nullptr;HRESULT result=execute->get_Path(&path);
        if(FAILED(result)){SysFreeString(path);return false;}
        result=execute->get_Arguments(&arguments);
        bool matches=SUCCEEDED(result) && path && arguments && _wcsicmp(path,exePath().c_str())==0 && wcscmp(arguments,L"--background")==0;
        SysFreeString(path);SysFreeString(arguments);return matches;
    }catch(const _com_error&){return false;}
}
