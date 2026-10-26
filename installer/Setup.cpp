#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "Payload.h"

namespace fs=std::filesystem;
static HINSTANCE instance;
static std::wstring logPath;
static bool silent=false;
static void log(const std::string& message) {
    if (!logPath.empty()) { std::ofstream file(fs::path(logPath),std::ios::app); file<<message<<"\n"; }
}
static void fail(const char* message) { throw std::runtime_error(std::string(message)+" (Windows error "+std::to_string(GetLastError())+")"); }
static std::string sha256(const unsigned char* data,ULONG length) {
    BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) fail("Cannot initialize SHA256");
    DWORD size=0,bytes=0;
    BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof(size),&bytes,0);
    std::vector<unsigned char> object(size); unsigned char digest[32]{};
    auto status=BCryptCreateHash(algorithm,&hash,object.data(),size,nullptr,0,0);
    if (status>=0) status=BCryptHashData(hash,const_cast<PUCHAR>(data),length,0);
    if (status>=0) status=BCryptFinishHash(hash,digest,32,0);
    if (hash) BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm,0);
    if (status<0) fail("SHA256 failed");
    const char* hex="0123456789abcdef"; std::string result;
    for(auto byte:digest) { result+=hex[byte>>4]; result+=hex[byte&15]; }
    return result;
}
struct Resource { const unsigned char* data; DWORD size; };
static Resource resource(const Payload& file) {
    auto found=FindResourceW(instance,MAKEINTRESOURCEW(file.id),RT_RCDATA);
    if (!found) fail("Installer payload is missing");
    auto loaded=LoadResource(instance,found);
    Resource result{static_cast<const unsigned char*>(LockResource(loaded)),SizeofResource(instance,found)};
    if (!result.data || sha256(result.data,result.size)!=file.hash) fail("Installer payload checksum mismatch");
    return result;
}
static void verify(const fs::path& root) {
    for(const auto& file:payload) {
        std::ifstream input(root/file.path,std::ios::binary);
        if (!input) fail("Cannot read an installed file");
        std::vector<unsigned char> data((std::istreambuf_iterator<char>(input)),{});
        if (sha256(data.data(),ULONG(data.size()))!=file.hash) fail("Installed file checksum mismatch");
    }
}
static void install(const fs::path& root) {
    // A new staging directory holds the complete verified bundle before replacement.
    fs::create_directories(root.parent_path());
    fs::path staging=root.parent_path()/(L".NabdGate-stage-"+std::to_wstring(GetCurrentProcessId()));
    fs::path backup=root.parent_path()/(L".NabdGate-backup-"+std::to_wstring(GetCurrentProcessId()));
    if (fs::exists(staging)||fs::exists(backup)) fail("Staging directory already exists");
    if (!fs::create_directory(staging)) fail("Cannot create staging directory");
    bool movedOld=false;
    try {
        for(const auto& file:payload) {
            const auto contents=resource(file); const auto destination=staging/file.path;
            fs::create_directories(destination.parent_path());
            HANDLE handle=CreateFileW(destination.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if (handle==INVALID_HANDLE_VALUE) fail("Cannot create plugin file");
            DWORD written=0;
            const bool ok=WriteFile(handle,contents.data,contents.size,&written,nullptr) && written==contents.size && FlushFileBuffers(handle);
            CloseHandle(handle); if (!ok) fail("Cannot write plugin file");
        }
        verify(staging);
        if (fs::exists(root)) {
            // Only replace an existing Nabd Gate bundle, never an unrelated directory.
            if (!fs::is_regular_file(root/L"Contents/x86_64-win/Nabd Gate.vst3"))
                fail("The destination contains an unrelated directory");
            fs::rename(root,backup); movedOld=true;
        }
        try { fs::rename(staging,root); }
        catch(...) { if(movedOld)fs::rename(backup,root); throw; }
        verify(root);
        if (movedOld) { std::error_code ignored; fs::remove_all(backup,ignored); }
        log("INSTALLED_AND_VERIFIED: "+root.u8string());
    } catch(...) {
        // Only the unique staging directory created above is cleaned on failure.
        std::error_code ignored; fs::remove_all(staging,ignored); throw;
    }
}
static bool administrator() {
    SID_IDENTIFIER_AUTHORITY authority=SECURITY_NT_AUTHORITY; PSID group=nullptr; BOOL member=FALSE;
    if (AllocateAndInitializeSid(&authority,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&group)) {
        CheckTokenMembership(nullptr,group,&member); FreeSid(group);
    }
    return member!=FALSE;
}
int WINAPI wWinMain(HINSTANCE module,HINSTANCE,LPWSTR commandLine,int) {
    instance=module;
    int count=0; auto args=CommandLineToArgvW(GetCommandLineW(),&count);
    std::wstring extract; bool verifyOnly=false;
    for(int i=1;i<count;++i) {
        const std::wstring arg=args[i];
        if(arg==L"--silent")silent=true;
        else if(arg==L"--verify")verifyOnly=true;
        else if(arg==L"--extract" && i+1<count)extract=args[++i];
        else if(arg==L"--log" && i+1<count)logPath=args[++i];
    }
    LocalFree(args);
    try {
        for(const auto& file:payload)resource(file);
        if(verifyOnly){log("PAYLOAD_VERIFIED");return 0;}
        if(!extract.empty()) { install(fs::absolute(extract)/L"Nabd Gate.vst3"); return 0; }
        if(!administrator()) {
            wchar_t executable[32768];GetModuleFileNameW(nullptr,executable,32768);
            SHELLEXECUTEINFOW request{};request.cbSize=sizeof(request);request.fMask=SEE_MASK_NOCLOSEPROCESS;
            request.lpVerb=L"runas";request.lpFile=executable;request.lpParameters=commandLine;
            request.nShow=silent?SW_HIDE:SW_SHOWNORMAL;
            if(!ShellExecuteExW(&request))fail("Windows administrator permission was not granted");
            WaitForSingleObject(request.hProcess,INFINITE);DWORD result=1;GetExitCodeProcess(request.hProcess,&result);CloseHandle(request.hProcess);
            return int(result);
        }
        PWSTR common=nullptr;
        if(FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFilesCommon,0,nullptr,&common)))fail("Cannot locate the VST3 installation folder");
        fs::path root=fs::path(common)/L"VST3/Nabd Gate.vst3";CoTaskMemFree(common);
        install(root);
        if(!silent)MessageBoxW(nullptr,L"تم تثبيت Nabd Gate بنجاح.\n\nافتح Manage plugins في FL Studio واضغط Find installed plugins.",L"Nabd Gate",MB_OK|MB_ICONINFORMATION|MB_RIGHT|MB_RTLREADING);
        return 0;
    } catch(const std::exception& error) {
        log(std::string("ERROR: ")+error.what());
        if(!silent) {std::string msg=std::string("Installation failed. Close FL Studio and try again.\n\n")+error.what();MessageBoxA(nullptr,msg.c_str(),"Nabd Gate Setup",MB_OK|MB_ICONERROR);}
        return 1;
    }
}
