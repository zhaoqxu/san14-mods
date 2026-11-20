#include "core_services.h"
#include "common.h"
#include "json.h"
#include "manifest_identity.h"
#include "pe_check.h"
#include <array>
#include <map>
#include <set>
#include <regex>
#include <cstddef>
#include <cstring>

namespace manager_core {
static bool id(const std::string& s){auto alnum=[](unsigned char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9');};return s.size()>=2&&s.size()<=64&&alnum(s[0])&&std::all_of(s.begin(),s.end(),[&](unsigned char c){return alnum(c)||c=='.'||c=='_'||c=='-';});}
static bool url(const std::string& v){if(v.empty())return true;if(v.size()>2048||v.rfind("https://",0))return false;for(unsigned char c:v)if(c<0x21||c>0x7e||c=='\\')return false;auto end=v.find_first_of("/?#",8);auto authority=v.substr(8,end==std::string::npos?std::string::npos:end-8);if(authority.empty()||authority.find('@')!=std::string::npos||authority.front()=='.'||authority.back()=='.')return false;for(unsigned char c:authority)if(!std::isalnum(c)&&c!='.'&&c!='-'&&c!=':')return false;return true;}
static std::array<int,3> version(const std::string& s){if(!std::regex_match(s,std::regex("(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)")))throw std::runtime_error("Invalid version");std::istringstream in(s);std::array<int,3>a{};char dot;in>>a[0]>>dot>>a[1]>>dot>>a[2];if(!in)throw std::runtime_error("Version overflow");return a;}
static bool matches(const std::string& v,const std::string& c){auto a=version(v);if(c=="*")return true;std::istringstream in(c);std::string t;bool any=false;while(in>>t){any=true;auto n=t.find_first_of("0123456789");if(n==std::string::npos)throw std::runtime_error("Invalid version constraint");auto op=t.substr(0,n);auto b=version(t.substr(n));if(!(op.empty()||op=="=="?a==b:op==">="?a>=b:op=="<="?a<=b:op==">"?a>b:op=="<"?a<b:false))return false;}return any;}
static std::string quote(const std::string& s){std::string out="\"";const char* digits="0123456789abcdef";for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<32){out+="\\u00";out+=digits[c>>4];out+=digits[c&15];}else out+=char(c);}return out+'"';}
static std::string dump(const json::Value& v){switch(v.type){case json::Value::Null:return "null";case json::Value::Bool:return v.boolean?"true":"false";case json::Value::Number:return std::to_string(v.number);case json::Value::String:return quote(v.string);case json::Value::Array:{std::string s="[";for(const auto& x:v.array){if(s.size()>1)s+=',';s+=dump(x);}return s+"]";}case json::Value::Object:{std::string s="{";for(const auto& x:v.object){if(s.size()>1)s+=',';s+=quote(x.first)+":"+dump(x.second);}return s+"}";}}throw std::runtime_error("Invalid JSON value");}
static json::Value string_value(const std::string& s){json::Value v;v.type=json::Value::String;v.string=s;return v;}
static void copy_error(char* out,uint32_t cap,const char* text){if(!out||!cap)return;const auto n=std::min<size_t>(cap-1,std::strlen(text));memcpy(out,text,n);out[n]=0;}
static const std::set<std::string> permissions={"game.read.officers","game.read.forces","game.read.cities","game.read.areas","game.read.units","game.read.saves","game.commands","ui.panel","ui.officer-detail","input.hotkey","input.text","storage.global","storage.playthrough","storage.saved-edit","storage.save","network","game.rules","settings","game.memory.read","game.content"};

static San14Result SAN14_ADAPTER_CALL admit(const wchar_t* folder,const San14CoreAdmission* identity,char* output,uint32_t capacity,uint32_t* required,char* error,uint32_t error_capacity){
    try{
        if(!folder||!identity||identity->struct_size<sizeof(*identity)||!identity->host_version||!identity->game_exe_sha256||!required)return SAN14_E_INVALID_ARGUMENT;
        const auto root=fs::canonical(folder);auto bytes=read_file(root/L"mod.json",1024*1024);auto j=json::parse(std::string(bytes.begin(),bytes.end()));
        if(j.type!=json::Value::Object)throw std::runtime_error("Manifest must be an object");
        const std::set<std::string> allowed={"schema_version","id","name","version","entry","host_version","game_builds","game_exe_sha256","dependencies","conflicts","permissions","disable_mode","author","author_url","source_url","contributors","coauthors","description","summary","default_enabled","load_order"};
        for(const auto& kv:j.object)if(!allowed.count(kv.first))throw std::runtime_error("Unknown manifest field: "+kv.first);
        const auto mod_id=j.at("id").str();if(!id(mod_id))throw std::runtime_error("Invalid Mod ID");
        const auto name=j.at("name").str();if(name.empty()||wide(name).size()>80)throw std::runtime_error("Invalid name");version(j.at("version").str());
        if(j.at("schema_version").integer()!=1)throw std::runtime_error("Unsupported schema");
        if(!matches(identity->host_version,j.at("host_version").str()))throw std::runtime_error("Host version incompatible");
        manifest_identity::admit_build(j,identity->game_build);
        auto e=j.at("entry");const bool content=e.at("type").str()=="content";
        if(content){if(e.object.size()!=2)throw std::runtime_error("Content entry requires only type and JSON path");}
        else if(e.object.size()!=3||e.at("type").str()!="native"||e.at("abi").integer()!=1)throw std::runtime_error("Only native ABI 1 or declarative content admitted");
        auto rel=fs::path(wide(e.at("path").str()));if(rel.is_absolute()||rel.has_root_name())throw std::runtime_error("Absolute entry refused");for(const auto& p:rel)if(p==L"..")throw std::runtime_error("Entry traversal");
        const auto entry=fs::weakly_canonical(root/rel);if(!inside(root,entry)||lower(utf8(entry.extension().wstring()))!=(content?".json":".dll"))throw std::runtime_error("Entry outside mod root");
        const auto deps=j.at("dependencies");if(deps.type!=json::Value::Array)throw std::runtime_error("Invalid dependencies");std::set<std::string> seen;
        for(const auto& d:deps.array){if(d.type!=json::Value::Object||d.object.size()!=2)throw std::runtime_error("Invalid dependency");auto target=d.at("id").str();if(!id(target)||target==mod_id||!seen.insert(target).second)throw std::runtime_error("Invalid dependency ID");matches("1.0.0",d.at("version").str());}
        const auto conflicts=j.at("conflicts");if(conflicts.type!=json::Value::Array)throw std::runtime_error("Invalid conflicts");for(const auto& v:conflicts.array){auto target=v.str();if(!id(target)||target==mod_id)throw std::runtime_error("Invalid conflict ID");}
        const auto perms=j.at("permissions");if(perms.type!=json::Value::Array)throw std::runtime_error("Invalid permissions");std::set<std::string> granted;
        for(const auto& v:perms.array){auto p=v.str();if(!permissions.count(p)||!granted.insert(p).second)throw std::runtime_error("Unknown/duplicate permission");}
        const bool raw=manifest_identity::admit(j,granted,identity->game_exe_sha256);
        auto mode=j.at("disable_mode").str();if(mode!="hot"&&mode!="restart"&&mode!="main_menu")throw std::runtime_error("Invalid disable mode");
        const auto author=j.has("author")?j.at("author").str():"";if(wide(author).size()>80)throw std::runtime_error("Author exceeds 80 characters");
        if(j.has("author_url")){auto link=j.at("author_url").str();if(!url(link)||(!link.empty()&&author.empty()))throw std::runtime_error("Invalid author URL");}
        if(j.has("source_url")&&!url(j.at("source_url").str()))throw std::runtime_error("Invalid source URL");
        for(const char* people:{"contributors","coauthors"})if(j.has(people)){const auto& list=j.at(people);if(list.type!=json::Value::Array||list.array.size()>16)throw std::runtime_error("Invalid contributors");seen.clear();for(const auto& person:list.array){std::string n,u;if(person.type==json::Value::String&&std::string(people)=="contributors")n=person.str();else if(person.type==json::Value::Object){for(const auto& field:person.object)if(field.first!="name"&&field.first!="url")throw std::runtime_error("Invalid contributor field");n=person.at("name").str();if(person.has("url"))u=person.at("url").str();}else throw std::runtime_error("Invalid contributor");if(n.empty()||n.front()==' '||n.back()==' '||wide(n).size()>80||!seen.insert(n).second||!url(u))throw std::runtime_error("Invalid contributor");for(unsigned char c:n)if(c<32||c==127)throw std::runtime_error("Invalid contributor name");}}
        if(j.has("description"))j.at("description").str();if(j.has("summary")&&wide(j.at("summary").str()).size()>160)throw std::runtime_error("Summary exceeds 160 characters");
        if(j.has("default_enabled"))j.at("default_enabled").flag();if(j.has("load_order")){auto n=j.at("load_order").integer();if(n<-10000||n>10000)throw std::runtime_error("Invalid load order");}
        auto payload=read_file(entry,content?2*1024*1024:256*1024*1024);
        // The game backend validates its declarative content format before mounting it.
        if(content){if(!granted.count("game.content"))throw std::runtime_error("Content entry requires game.content");}
        else{PluginPe pe(payload,granted.count("network")!=0);}
        j.object.emplace("_root",string_value(utf8(root.wstring())));j.object.emplace("_entry",string_value(utf8(entry.wstring())));j.object.emplace("_sha256",string_value(digest(payload.data(),payload.size())));json::Value memory;memory.type=json::Value::Bool;memory.boolean=raw;j.object.emplace("_memory_admitted",memory);
        const auto serialized=dump(j);*required=static_cast<uint32_t>(serialized.size()+1);if(!output||capacity<*required)return SAN14_E_BUFFER_TOO_SMALL;memcpy(output,serialized.c_str(),*required);return SAN14_OK;
    }catch(const std::exception& e){copy_error(error,error_capacity,e.what());return SAN14_E_INVALID_ARGUMENT;}catch(...){copy_error(error,error_capacity,"Manifest admission failed");return SAN14_E_INVALID_ARGUMENT;}
}
static int32_t SAN14_ADAPTER_CALL valid_id(const char* s){try{return s&&id(s);}catch(...){return 0;}}
static int32_t SAN14_ADAPTER_CALL valid_url(const char* s){try{return s&&url(s);}catch(...){return 0;}}
static San14Result SAN14_ADAPTER_CALL version_matches(const char* v,const char* c,int32_t* result){try{if(!v||!c||!result)return SAN14_E_INVALID_ARGUMENT;*result=matches(v,c);return SAN14_OK;}catch(...){return SAN14_E_INVALID_ARGUMENT;}}
static San14Result invoke_init(San14Result(SAN14_MOD_CALL* fn)(const San14HostApi*,San14ModApi*),const San14HostApi* host,San14ModApi* out){__try{return fn(host,out);}__except(EXCEPTION_EXECUTE_HANDLER){return SAN14_E_PLUGIN_FAILED;}}
static San14Result SAN14_ADAPTER_CALL load(const wchar_t* path,const char* expected,const San14HostApi* host,San14ModApi* out,void** result){
    try{if(!path||!expected||!host||!out||!result)return SAN14_E_INVALID_ARGUMENT;*result=nullptr;if(hash_file(path)!=expected)return SAN14_E_INVALID_ARGUMENT;
        auto module=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!module)return SAN14_E_PLUGIN_FAILED;
        auto init=reinterpret_cast<San14Result(SAN14_MOD_CALL*)(const San14HostApi*,San14ModApi*)>(GetProcAddress(module,"SAN14Mod_Initialize"));
        out->struct_size=sizeof(*out);const bool ok=init&&invoke_init(init,host,out)==SAN14_OK&&out->struct_size>=sizeof(*out)&&out->abi_version==SAN14_HOST_ABI_V1&&out->on_load&&out->on_enable&&out->on_disable;
        if(!ok){FreeLibrary(module);return SAN14_E_PLUGIN_FAILED;}*result=module;return SAN14_OK;
    }catch(...){return SAN14_E_PLUGIN_FAILED;}
}
static void SAN14_ADAPTER_CALL release(void* m){if(m)FreeLibrary(static_cast<HMODULE>(m));}
static San14Result SAN14_ADAPTER_CALL lifecycle(San14ModContext* ctx,San14Result(SAN14_MOD_CALL* fn)(San14ModContext*)){if(!ctx||!fn)return SAN14_E_UNSUPPORTED;__try{return fn(ctx);}__except(EXCEPTION_EXECUTE_HANDLER){return SAN14_E_PLUGIN_FAILED;}}
static San14Result SAN14_ADAPTER_CALL setting(San14SettingChanged fn,void* ctx,San14StringView key,int32_t next){if(!fn)return SAN14_E_INVALID_ARGUMENT;__try{return fn(ctx,key,next);}__except(EXCEPTION_EXECUTE_HANDLER){return SAN14_E_PLUGIN_FAILED;}}
static fs::path storage_path(const wchar_t* root,const char* mod,const char* key){if(!root||!mod||!key||!id(mod)||!id(key))throw std::runtime_error("Invalid storage identity");auto base=fs::path(root)/L"plugins"/wide(mod);auto target=base/wide(key);if(!inside(base,target))throw std::runtime_error("Storage path escaped");return target;}
static San14Result SAN14_ADAPTER_CALL global_read(const wchar_t* root,const char* mod,const char* key,void* out,uint32_t cap,uint32_t* required){try{if(!required)return SAN14_E_INVALID_ARGUMENT;auto p=storage_path(root,mod,key);if(!fs::exists(p))return SAN14_E_NOT_FOUND;auto bytes=read_file(p,1024*1024);*required=static_cast<uint32_t>(bytes.size());if(!out||cap<bytes.size())return SAN14_E_BUFFER_TOO_SMALL;if(!bytes.empty())memcpy(out,bytes.data(),bytes.size());return SAN14_OK;}catch(...){return SAN14_E_INVALID_ARGUMENT;}}
static San14Result SAN14_ADAPTER_CALL global_write(const wchar_t* root,const char* mod,const char* key,const void* data,uint32_t size){try{if(size>1024*1024||(!data&&size))return SAN14_E_INVALID_ARGUMENT;write_atomic(storage_path(root,mod,key),data,size);return SAN14_OK;}catch(...){return SAN14_E_INVALID_ARGUMENT;}}
static const San14ManagerCoreApi services{
    sizeof(services),SAN14_MANAGER_CORE_ABI_V1,valid_id,valid_url,version_matches,admit,
    load,release,lifecycle,setting,global_read,global_write,worker_open,worker_request,worker_close
};
const San14ManagerCoreApi* api() noexcept{return &services;}
}
