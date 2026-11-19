#pragma once
#include "common.h"
#include <set>
// Admission is static, before LoadLibrary (DllMain and TLS have not executed).
class PluginPe {
    const std::vector<unsigned char>& b;IMAGE_NT_HEADERS64 nt{};std::vector<IMAGE_SECTION_HEADER> sections;
    template<class T>T get(size_t o)const{if(o>b.size()||sizeof(T)>b.size()-o)throw std::runtime_error("Truncated PE");T x;memcpy(&x,b.data()+o,sizeof(x));return x;}
    size_t offset(DWORD rva)const{for(const auto& s:sections)if(rva>=s.VirtualAddress && uint64_t(rva)-s.VirtualAddress<s.SizeOfRawData){uint64_t o=uint64_t(s.PointerToRawData)+rva-s.VirtualAddress;if(o<b.size())return(size_t)o;}if(rva<nt.OptionalHeader.SizeOfHeaders&&rva<b.size())return rva;throw std::runtime_error("Invalid PE RVA");}
    std::string str(DWORD rva)const{size_t o=offset(rva);std::string s;for(unsigned i=0;i<512&&o+i<b.size();i++){char c=(char)b[o+i];if(!c)return s;if((unsigned char)c>127)break;s+=c;}throw std::runtime_error("Invalid PE string");}
    static bool forbidden(const std::string& s){static const std::set<std::string> names={"createremotethread","createremotethreadex","flushinstructioncache","openprocess","readprocessmemory","virtualallocex","virtualprotect","virtualprotectex","virtualquery","virtualqueryex","writeprocessmemory","ntprotectvirtualmemory","ntreadvirtualmemory","ntwritevirtualmemory"};return names.count(lower(s))!=0;}
    void names(const std::string& dll,DWORD thunk,bool network)const{
        std::string d=lower(dll);if(d=="ntdll.dll"||d.rfind("frida",0)==0||d.rfind("minhook",0)==0||d.rfind("detours",0)==0)throw std::runtime_error("Forbidden library: "+dll);
        if(!network&&(d=="winhttp.dll"||d=="wininet.dll"||d=="ws2_32.dll"||d=="libcurl.dll"))throw std::runtime_error("Network permission missing");
        for(unsigned n=0;n<65536;n++){uint64_t v=get<uint64_t>(offset(thunk)+n*8);if(!v)return;if(v&IMAGE_ORDINAL_FLAG64)continue;if(v>0xffffffffu||forbidden(str((DWORD)v+2)))throw std::runtime_error("Forbidden import");}throw std::runtime_error("Unterminated import table");
    }
public:
    bool has_export(const std::string& name)const{
        const auto ex=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if(!ex.VirtualAddress)return false;
        const auto e=get<IMAGE_EXPORT_DIRECTORY>(offset(ex.VirtualAddress));
        if(e.NumberOfNames>65536)throw std::runtime_error("Invalid export table");
        for(DWORD i=0;i<e.NumberOfNames;++i)if(str(get<DWORD>(offset(e.AddressOfNames)+i*4))==name){
            const auto ord=get<WORD>(offset(e.AddressOfNameOrdinals)+i*2);
            if(ord>=e.NumberOfFunctions)throw std::runtime_error("Invalid export ordinal");
            const auto target=get<DWORD>(offset(e.AddressOfFunctions)+ord*4);
            if(target>=ex.VirtualAddress&&uint64_t(target)<uint64_t(ex.VirtualAddress)+ex.Size)return false;
            offset(target);return true;
        }return false;
    }
    PluginPe(const std::vector<unsigned char>& bytes,bool network):b(bytes){
        auto dos=get<IMAGE_DOS_HEADER>(0);if(dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0)throw std::runtime_error("Not a PE DLL");nt=get<IMAGE_NT_HEADERS64>((size_t)dos.e_lfanew);
        if(nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||!(nt.FileHeader.Characteristics&IMAGE_FILE_DLL)||nt.FileHeader.NumberOfSections>96||nt.FileHeader.SizeOfOptionalHeader<sizeof(IMAGE_OPTIONAL_HEADER64))throw std::runtime_error("Expected AMD64 PE32+ DLL");
        size_t at=(size_t)dos.e_lfanew+24+nt.FileHeader.SizeOfOptionalHeader;for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++)sections.push_back(get<IMAGE_SECTION_HEADER>(at+i*40));
        auto ex=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];if(!ex.VirtualAddress)throw std::runtime_error("Missing plugin export");auto e=get<IMAGE_EXPORT_DIRECTORY>(offset(ex.VirtualAddress));bool found=false;if(e.NumberOfNames>65536)throw std::runtime_error("Invalid export table");
        for(DWORD i=0;i<e.NumberOfNames;i++)if(str(get<DWORD>(offset(e.AddressOfNames)+i*4))=="SAN14Mod_Initialize"){
            auto ord=get<WORD>(offset(e.AddressOfNameOrdinals)+i*2);if(ord>=e.NumberOfFunctions)throw std::runtime_error("Invalid export ordinal");auto target=get<DWORD>(offset(e.AddressOfFunctions)+ord*4);
            if(target>=ex.VirtualAddress&&uint64_t(target)<uint64_t(ex.VirtualAddress)+ex.Size)throw std::runtime_error("Forwarded plugin entry refused");offset(target);found=true;
        }if(!found)throw std::runtime_error("Missing SAN14Mod_Initialize");
        auto im=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];if(im.VirtualAddress){for(unsigned i=0;;i++){if(i>=1024||i*sizeof(IMAGE_IMPORT_DESCRIPTOR)>=im.Size)throw std::runtime_error("Invalid imports");auto d=get<IMAGE_IMPORT_DESCRIPTOR>(offset(im.VirtualAddress)+i*sizeof(IMAGE_IMPORT_DESCRIPTOR));if(!d.Name)break;names(str(d.Name),d.OriginalFirstThunk?d.OriginalFirstThunk:d.FirstThunk,network);}}
        auto delay=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];if(delay.VirtualAddress){struct D{DWORD attr,name,module,iat,intab,bound,unload,time;};for(unsigned i=0;;i++){if(i>=1024||i*sizeof(D)>=delay.Size)throw std::runtime_error("Invalid delay imports");auto d=get<D>(offset(delay.VirtualAddress)+i*sizeof(D));if(!d.name)break;if(!(d.attr&1))throw std::runtime_error("VA delay import refused");names(str(d.name),d.intab,network);}}
    }
};
