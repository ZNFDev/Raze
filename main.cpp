#include "include/lexer.hpp"
#include "include/ast.hpp"
#include "include/parser.hpp"
#include "include/value.hpp"
#include "include/interp.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <thread>
#include <random>
#include <regex>
#include <set>

namespace fs = std::filesystem;

// ── Helpers ───────────────────────────────────────────────────
static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open: " + path);
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

static void runSource(Interpreter& interp, const std::string& src) {
    Lexer lex(src); auto toks = lex.tokenize();
    Parser par(std::move(toks)); auto prog = par.parse();
    interp.run(std::move(prog));
}

// ── Demo C functions (raw address proof) ─────────────────────
extern "C" {
    int64_t _raze_demo_add(int64_t a, int64_t b) {
        printf("  [native_add] %lld + %lld = %lld\n",(long long)a,(long long)b,(long long)(a+b));
        return a + b;
    }
    int64_t _raze_demo_clamp(int64_t v, int64_t lo, int64_t hi) {
        int64_t r = v<lo?lo:(v>hi?hi:v);
        printf("  [native_clamp] clamp(%lld,%lld,%lld)=%lld\n",(long long)v,(long long)lo,(long long)hi,(long long)r);
        return r;
    }
}

// ── Register all built-in natives ─────────────────────────────
static void registerBuiltins(Interpreter& interp) {
    static std::mt19937_64 rng(std::random_device{}());

    // ── Constants ────────────────────────────────────────────
    interp.globals->define("PI",      Value::fromFloat(M_PI));
    interp.globals->define("TAU",     Value::fromFloat(2.0*M_PI));
    interp.globals->define("E",       Value::fromFloat(M_E));
    interp.globals->define("INF",     Value::fromFloat(std::numeric_limits<double>::infinity()));
    interp.globals->define("NAN_VAL", Value::fromFloat(std::numeric_limits<double>::quiet_NaN()));
    interp.globals->define("INT_MAX", Value::fromInt(std::numeric_limits<int64_t>::max()));
    interp.globals->define("INT_MIN", Value::fromInt(std::numeric_limits<int64_t>::min()));
    interp.globals->define("RAZE_VERSION", Value::fromStr("3.0.0"));
    interp.globals->define("RAZE_PLATFORM",
#if defined(_WIN32)
        Value::fromStr("windows")
#elif defined(__APPLE__)
        Value::fromStr("macos")
#else
        Value::fromStr("linux")
#endif
    );

    // ── Random ───────────────────────────────────────────────
    interp.registerNative("rand", [](std::vector<ValPtr> args) -> ValPtr {
        static std::mt19937_64 r(std::random_device{}());
        if (args.empty()) { std::uniform_real_distribution<double> d(0,1); return Value::fromFloat(d(r)); }
        if (args.size()==1) { std::uniform_int_distribution<int64_t> d(0,args[0]->toInt()-1); return Value::fromInt(d(r)); }
        std::uniform_int_distribution<int64_t> d(args[0]->toInt(),args[1]->toInt()); return Value::fromInt(d(r));
    });
    interp.registerNative("randFloat", [](std::vector<ValPtr> args) -> ValPtr {
        static std::mt19937_64 r(std::random_device{}());
        double lo=0,hi=1;
        if(args.size()==1)hi=args[0]->toFloat();
        else if(args.size()>=2){lo=args[0]->toFloat();hi=args[1]->toFloat();}
        std::uniform_real_distribution<double> d(lo,hi); return Value::fromFloat(d(r));
    });
    interp.registerNative("shuffle", [](std::vector<ValPtr> args) -> ValPtr {
        static std::mt19937_64 r(std::random_device{}());
        if(args.empty()||args[0]->type!=VType::ARRAY) throw std::runtime_error("shuffle() needs array");
        auto v = Value::fromArray(); v->arr = args[0]->arr;
        std::shuffle(v->arr.begin(), v->arr.end(), r);
        return v;
    });
    interp.registerNative("randomChoice", [](std::vector<ValPtr> args) -> ValPtr {
        static std::mt19937_64 r(std::random_device{}());
        if(args.empty()||args[0]->type!=VType::ARRAY||args[0]->arr.empty()) return Value::null();
        std::uniform_int_distribution<size_t> d(0,args[0]->arr.size()-1);
        return args[0]->arr[d(r)];
    });

    // ── Time ─────────────────────────────────────────────────
    interp.registerNative("time", [](std::vector<ValPtr>) -> ValPtr {
        return Value::fromInt((int64_t)std::time(nullptr));
    });
    interp.registerNative("clock", [](std::vector<ValPtr>) -> ValPtr {
        using namespace std::chrono;
        return Value::fromInt(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
    });
    interp.registerNative("clockNs", [](std::vector<ValPtr>) -> ValPtr {
        using namespace std::chrono;
        return Value::fromInt(duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
    });
    interp.registerNative("sleep", [](std::vector<ValPtr> args) -> ValPtr {
        std::this_thread::sleep_for(std::chrono::milliseconds(args.empty()?0:args[0]->toInt()));
        return Value::null();
    });

    // ── I/O ──────────────────────────────────────────────────
    interp.registerNative("readFile", [](std::vector<ValPtr> args) -> ValPtr {
        if(args.empty()) throw std::runtime_error("readFile() needs path");
        std::ifstream f(args[0]->toString());
        if(!f) throw std::runtime_error("readFile: cannot open '"+args[0]->toString()+"'");
        std::ostringstream ss; ss<<f.rdbuf(); return Value::fromStr(ss.str());
    });
    interp.registerNative("writeFile", [](std::vector<ValPtr> args) -> ValPtr {
        if(args.size()<2) throw std::runtime_error("writeFile(path, content)");
        std::ofstream f(args[0]->toString());
        if(!f) throw std::runtime_error("writeFile: cannot open '"+args[0]->toString()+"'");
        f<<args[1]->toString(); return Value::null();
    });
    interp.registerNative("appendFile", [](std::vector<ValPtr> args) -> ValPtr {
        if(args.size()<2) throw std::runtime_error("appendFile(path, content)");
        std::ofstream f(args[0]->toString(), std::ios::app);
        f<<args[1]->toString(); return Value::null();
    });
    interp.registerNative("readLines", [](std::vector<ValPtr> args) -> ValPtr {
        if(args.empty()) throw std::runtime_error("readLines() needs path");
        std::ifstream f(args[0]->toString());
        if(!f) throw std::runtime_error("readLines: cannot open '"+args[0]->toString()+"'");
        auto v=Value::fromArray(); std::string ln;
        while(std::getline(f,ln)) v->arr.push_back(Value::fromStr(ln));
        return v;
    });
    interp.registerNative("fileExists", [](std::vector<ValPtr> args) -> ValPtr {
        return Value::fromBool(!args.empty()&&fs::exists(args[0]->toString()));
    });
    interp.registerNative("deleteFile", [](std::vector<ValPtr> args) -> ValPtr {
        return Value::fromBool(!args.empty()&&fs::remove(args[0]->toString()));
    });
    interp.registerNative("listDir", [](std::vector<ValPtr> args) -> ValPtr {
        std::string path=args.empty()?".":args[0]->toString();
        auto v=Value::fromArray();
        for(auto&e:fs::directory_iterator(path))
            v->arr.push_back(Value::fromStr(e.path().string()));
        return v;
    });
    interp.registerNative("mkDir", [](std::vector<ValPtr> args) -> ValPtr {
        if(args.empty()) return Value::fromBool(false);
        return Value::fromBool(fs::create_directories(args[0]->toString()));
    });
    interp.registerNative("fileSize", [](std::vector<ValPtr> args) -> ValPtr {
        if(args.empty()) return Value::fromInt(0);
        return Value::fromInt((int64_t)fs::file_size(args[0]->toString()));
    });
    interp.registerNative("pathJoin", [](std::vector<ValPtr> args) -> ValPtr {
        if(args.empty()) return Value::fromStr(".");
        fs::path p = args[0]->toString();
        for(size_t i=1;i<args.size();i++) p /= args[i]->toString();
        return Value::fromStr(p.string());
    });
    interp.registerNative("pathBasename", [](std::vector<ValPtr> args) -> ValPtr {
        if(args.empty()) return Value::fromStr("");
        return Value::fromStr(fs::path(args[0]->toString()).filename().string());
    });
    interp.registerNative("pathDirname", [](std::vector<ValPtr> args) -> ValPtr {
        if(args.empty()) return Value::fromStr(".");
        return Value::fromStr(fs::path(args[0]->toString()).parent_path().string());
    });
    interp.registerNative("pathExtension", [](std::vector<ValPtr> args) -> ValPtr {
        if(args.empty()) return Value::fromStr("");
        return Value::fromStr(fs::path(args[0]->toString()).extension().string());
    });

    // ── String utils ─────────────────────────────────────────
    interp.registerNative("format", [](std::vector<ValPtr> args) -> ValPtr {
        if(args.empty()) return Value::fromStr("");
        std::string s=args[0]->toString(); size_t ai=1,p=0;
        while((p=s.find("{}",p))!=std::string::npos&&ai<args.size()){
            std::string rep=args[ai++]->toString(); s.replace(p,2,rep); p+=rep.size();}
        return Value::fromStr(s);
    });
    interp.registerNative("sprintf", [](std::vector<ValPtr> args) -> ValPtr {
        if(args.empty()) return Value::fromStr("");
        std::string fmt=args[0]->toString(),out; size_t ai=1;
        for(size_t i=0;i<fmt.size();i++){
            if(fmt[i]=='%'&&i+1<fmt.size()){
                std::string spec="%"; i++;
                while(i<fmt.size()&&(fmt[i]=='-'||fmt[i]=='+'||fmt[i]==' '||fmt[i]=='0'||fmt[i]=='#'))spec+=fmt[i++];
                while(i<fmt.size()&&isdigit(fmt[i]))spec+=fmt[i++];
                if(i<fmt.size()&&fmt[i]=='.')spec+=fmt[i++];
                while(i<fmt.size()&&isdigit(fmt[i]))spec+=fmt[i++];
                if(i>=fmt.size()){out+=spec;break;}
                char sp=fmt[i]; spec+=sp;
                if(sp=='%'){out+='%';continue;}
                if(ai>=args.size()){out+=spec;continue;}
                auto&v=args[ai++]; char buf[256];
                switch(sp){
                    case 'd':case 'i':{std::string sf=spec;sf.back()='l';sf+="ld";snprintf(buf,256,sf.c_str(),(long long)v->toInt());out+=buf;break;}
                    case 'f':case 'g':case 'e':snprintf(buf,256,spec.c_str(),v->toFloat());out+=buf;break;
                    case 's':{std::string sf=spec;snprintf(buf,256,sf.c_str(),v->toString().c_str());out+=buf;break;}
                    case 'x':{std::string sf=spec;sf.back()='l';sf+="lx";snprintf(buf,256,sf.c_str(),(unsigned long long)v->toInt());out+=buf;break;}
                    case 'X':{std::string sf=spec;sf.back()='l';sf+="lX";snprintf(buf,256,sf.c_str(),(unsigned long long)v->toInt());out+=buf;break;}
                    case 'o':{std::string sf=spec;sf.back()='l';sf+="lo";snprintf(buf,256,sf.c_str(),(unsigned long long)v->toInt());out+=buf;break;}
                    case 'b':{uint64_t n=(uint64_t)v->toInt();std::string b;if(!n)b="0";else while(n){b=(char)('0'+n%2)+b;n>>=1;}out+=b;break;}
                    default:out+=spec;break;
                }
            }else out+=fmt[i];
        }
        return Value::fromStr(out);
    });
    interp.registerNative("parseJson", [&interp](std::vector<ValPtr> args) -> ValPtr {
        // Minimal JSON parser (numbers, strings, bools, null, arrays, objects)
        if(args.empty()) return Value::null();
        std::string src=args[0]->toString();
        size_t pos=0;
        std::function<ValPtr()> parseVal=[&]()->ValPtr{
            while(pos<src.size()&&isspace(src[pos]))pos++;
            if(pos>=src.size())return Value::null();
            char c=src[pos];
            if(c=='"'){pos++;std::string s;while(pos<src.size()&&src[pos]!='"'){if(src[pos]=='\\'&&pos+1<src.size()){pos++;switch(src[pos]){case 'n':s+='\n';break;case 't':s+='\t';break;case '"':s+='"';break;case '\\':s+='\\';break;default:s+=src[pos];}}else s+=src[pos];pos++;}pos++;return Value::fromStr(s);}
            if(c=='['){pos++;auto v=Value::fromArray();while(pos<src.size()&&isspace(src[pos]))pos++;if(src[pos]==']'){pos++;return v;}do{v->arr.push_back(parseVal());while(pos<src.size()&&isspace(src[pos]))pos++;}while(pos<src.size()&&src[pos++]==',');return v;}
            if(c=='{'){pos++;auto v=Value::fromMap();while(pos<src.size()&&isspace(src[pos]))pos++;if(src[pos]=='}'){pos++;return v;}do{while(pos<src.size()&&isspace(src[pos]))pos++;auto k=parseVal();while(pos<src.size()&&isspace(src[pos]))pos++;if(src[pos]==':')pos++;auto val=parseVal();v->mapSet(k->toString(),val);while(pos<src.size()&&isspace(src[pos]))pos++;}while(pos<src.size()&&src[pos++]==',');return v;}
            if(src.substr(pos,4)=="null"){pos+=4;return Value::null();}
            if(src.substr(pos,4)=="true"){pos+=4;return Value::fromBool(true);}
            if(src.substr(pos,5)=="false"){pos+=5;return Value::fromBool(false);}
            // number
            std::string num;bool isF=false;
            if(c=='-')num+=src[pos++];
            while(pos<src.size()&&isdigit(src[pos]))num+=src[pos++];
            if(pos<src.size()&&src[pos]=='.'){isF=true;num+=src[pos++];while(pos<src.size()&&isdigit(src[pos]))num+=src[pos++];}
            if(pos<src.size()&&(src[pos]=='e'||src[pos]=='E')){isF=true;num+=src[pos++];if(pos<src.size()&&(src[pos]=='+'||src[pos]=='-'))num+=src[pos++];while(pos<src.size()&&isdigit(src[pos]))num+=src[pos++];}
            if(!num.empty())return isF?Value::fromFloat(std::stod(num)):Value::fromInt(std::stoll(num));
            return Value::null();
        };
        try{return parseVal();}catch(...){return Value::null();}
    });
    interp.registerNative("toJson", [](std::vector<ValPtr> args) -> ValPtr {
        if(args.empty()) return Value::fromStr("null");
        std::function<std::string(ValPtr,int)> dump=[&](ValPtr v,int ind)->std::string{
            std::string tab(ind*2,' ');
            switch(v->type){
                case VType::NULL_VAL:return "null";
                case VType::BOOL:return v->bval?"true":"false";
                case VType::INT:return std::to_string(v->ival);
                case VType::FLOAT:{char b[64];snprintf(b,64,"%.10g",v->fval);return b;}
                case VType::STR:{std::string s="\"";for(char c:v->sval){switch(c){case '"':s+="\\\"";break;case '\\':s+="\\\\";break;case '\n':s+="\\n";break;case '\t':s+="\\t";break;default:s+=c;}}s+="\"";return s;}
                case VType::ARRAY:{if(v->arr.empty())return "[]";std::string r="[\n";for(size_t i=0;i<v->arr.size();i++){r+=std::string((ind+1)*2,' ')+dump(v->arr[i],ind+1);if(i+1<v->arr.size())r+=",";r+="\n";}r+=tab+"]";return r;}
                case VType::MAP:{if(v->map->empty())return "{}";std::string r="{\n";bool f=true;for(auto&k:*v->mapOrder){if(!f)r+=",\n";f=false;r+=std::string((ind+1)*2,' ')+"\""+k+"\": "+dump((*v->map)[k],ind+1);}r+="\n"+tab+"}";return r;}
                default:return "\""+v->toString()+"\"";
            }
        };
        return Value::fromStr(dump(args[0],0));
    });

    // ── System ───────────────────────────────────────────────
    interp.registerNative("getenv", [](std::vector<ValPtr> args) -> ValPtr {
        if(args.empty()) return Value::null();
        const char* v=std::getenv(args[0]->toString().c_str());
        return v?Value::fromStr(v):Value::null();
    });
    interp.registerNative("setenv", [](std::vector<ValPtr> args) -> ValPtr {
        if(args.size()<2) return Value::fromBool(false);
        return Value::fromBool(0==
#ifdef _WIN32
            _putenv_s(args[0]->toString().c_str(), args[1]->toString().c_str())
#else
            setenv(args[0]->toString().c_str(), args[1]->toString().c_str(), 1)
#endif
        );
    });
    interp.registerNative("system", [](std::vector<ValPtr> args) -> ValPtr {
        if(args.empty()) return Value::fromInt(0);
        return Value::fromInt((int64_t)std::system(args[0]->toString().c_str()));
    });
    interp.registerNative("exec", [](std::vector<ValPtr> args) -> ValPtr {
        if(args.empty()) return Value::fromStr("");
        std::string cmd=args[0]->toString();
        FILE* pipe=popen(cmd.c_str(),"r");
        if(!pipe) return Value::fromStr("");
        std::string result; char buf[256];
        while(fgets(buf,256,pipe)) result+=buf;
        pclose(pipe);
        return Value::fromStr(result);
    });
    interp.registerNative("getCwd", [](std::vector<ValPtr>) -> ValPtr {
        return Value::fromStr(fs::current_path().string());
    });

    // ── Bit ops ──────────────────────────────────────────────
    interp.registerNative("popcnt",[](std::vector<ValPtr> a)->ValPtr{uint64_t v=(uint64_t)a[0]->toInt();int c=0;while(v){c+=v&1;v>>=1;}return Value::fromInt(c);});
    interp.registerNative("clz",   [](std::vector<ValPtr> a)->ValPtr{uint64_t v=(uint64_t)a[0]->toInt();if(!v)return Value::fromInt(64);int c=0;while(!(v&(1ULL<<63))){c++;v<<=1;}return Value::fromInt(c);});
    interp.registerNative("ctz",   [](std::vector<ValPtr> a)->ValPtr{uint64_t v=(uint64_t)a[0]->toInt();if(!v)return Value::fromInt(64);int c=0;while(!(v&1)){c++;v>>=1;}return Value::fromInt(c);});

    // ── Regex ─────────────────────────────────────────────────
    interp.registerNative("regexMatch", [](std::vector<ValPtr> args) -> ValPtr {
        if(args.size()<2) return Value::fromBool(false);
        try{std::regex re(args[1]->toString());return Value::fromBool(std::regex_search(args[0]->toString(),re));}catch(...){return Value::fromBool(false);}
    });
    interp.registerNative("regexFind", [](std::vector<ValPtr> args) -> ValPtr {
        if(args.size()<2) return Value::fromArray();
        try{
            std::regex re(args[1]->toString());
            std::string s=args[0]->toString();
            auto v=Value::fromArray();
            std::sregex_iterator it(s.begin(),s.end(),re),end;
            for(;it!=end;++it) v->arr.push_back(Value::fromStr((*it)[0].str()));
            return v;
        }catch(...){return Value::fromArray();}
    });
    interp.registerNative("regexReplace", [](std::vector<ValPtr> args) -> ValPtr {
        if(args.size()<3) return args.empty()?Value::fromStr(""):args[0];
        try{std::regex re(args[1]->toString());return Value::fromStr(std::regex_replace(args[0]->toString(),re,args[2]->toString()));}
        catch(...){return args[0];}
    });

    // ── Conversion ────────────────────────────────────────────
    interp.registerNative("parseHex",[](std::vector<ValPtr> a)->ValPtr{std::string s=a.empty()?"0":a[0]->toString();if(s.size()>2&&s[0]=='0'&&(s[1]=='x'||s[1]=='X'))s=s.substr(2);return Value::fromInt((int64_t)strtoull(s.c_str(),nullptr,16));});
    interp.registerNative("parseBin",[](std::vector<ValPtr> a)->ValPtr{std::string s=a.empty()?"0":a[0]->toString();if(s.size()>2&&s[0]=='0'&&(s[1]=='b'||s[1]=='B'))s=s.substr(2);return Value::fromInt((int64_t)strtoull(s.c_str(),nullptr,2));});
    interp.registerNative("isNaN",  [](std::vector<ValPtr> a)->ValPtr{return Value::fromBool(!a.empty()&&std::isnan(a[0]->toFloat()));});
    interp.registerNative("isInf",  [](std::vector<ValPtr> a)->ValPtr{return Value::fromBool(!a.empty()&&std::isinf(a[0]->toFloat()));});
    interp.registerNative("hex",    [](std::vector<ValPtr> a)->ValPtr{if(a.empty())return Value::fromStr("0x0");char b[32];snprintf(b,32,"0x%llX",(unsigned long long)a[0]->toInt());return Value::fromStr(b);});
    interp.registerNative("bin",    [](std::vector<ValPtr> a)->ValPtr{if(a.empty())return Value::fromStr("0b0");int64_t n=a[0]->toInt();if(!n)return Value::fromStr("0b0");std::string r;uint64_t u=(uint64_t)n;while(u){r=(char)('0'+u%2)+r;u>>=1;}return Value::fromStr("0b"+r);});
    interp.registerNative("oct",    [](std::vector<ValPtr> a)->ValPtr{if(a.empty())return Value::fromStr("0o0");char b[32];snprintf(b,32,"0o%llo",(unsigned long long)a[0]->toInt());return Value::fromStr(b);});

    // ── Raw native address demos ───────────────────────────────
    interp.registerAddr("native_add",
        reinterpret_cast<uintptr_t>(_raze_demo_add), "int", {"int","int"});
    interp.registerAddr("native_clamp",
        reinterpret_cast<uintptr_t>(_raze_demo_clamp), "int", {"int","int","int"});
}

// ── Module cache (avoid re-importing) ─────────────────────────
static std::set<std::string> importedModules;

static void setupImport(Interpreter& interp, const std::string& baseDir) {
    interp.importHandler = [&interp, baseDir](const std::string& path, const std::string& alias) {
        std::vector<std::string> candidates = {
            baseDir + "/" + path,
            baseDir + "/" + path + ".rz",
            path,
            path + ".rz",
        };
        for (auto& c : candidates) {
            if (!fs::exists(c)) continue;
            auto canon = fs::canonical(c).string();
            if (importedModules.count(canon)) return; // already imported
            importedModules.insert(canon);
            std::string src = readFile(c);
            runSource(interp, src);
            return;
        }
        throw std::runtime_error("import: cannot find '" + path + "'");
    };
}

// ── REPL ──────────────────────────────────────────────────────
static void runREPL(Interpreter& interp) {
    std::cout << "\033[36m"
        "╔══════════════════════════════════════╗\n"
        "║  Raze Language v3.0   REPL           ║\n"
        "║  type 'exit;' to quit                ║\n"
        "╚══════════════════════════════════════╝"
        "\033[0m\n\n";
    std::string buf;
    while (true) {
        std::cout << (buf.empty() ? "\033[32m>>>\033[0m " : "\033[33m...\033[0m ");
        std::string line;
        if (!std::getline(std::cin, line)) break;
        if (line == "exit;" || line == "quit;" || line == "exit" || line == "quit") break;
        buf += line + "\n";
        try {
            Lexer lex(buf); auto toks=lex.tokenize();
            Parser par(std::move(toks)); auto prog=par.parse();
            interp.run(std::move(prog));
            buf.clear();
        } catch (std::exception& ex) {
            std::string m = ex.what();
            if (m.find("Expected") != std::string::npos ||
                m.find("Unexpected") != std::string::npos) continue;
            std::cerr << "\033[31m[Error]\033[0m " << m << "\n";
            buf.clear();
        }
    }
}

// ── Main ──────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    Interpreter interp;
    importedModules.clear();
    registerBuiltins(interp);

    // Store CLI args
    auto cliArgs = Value::fromArray();
    for (int i = 1; i < argc; i++)
        cliArgs->arr.push_back(Value::fromStr(argv[i]));
    interp.globals->define("ARGS", cliArgs, true);

    if (argc == 1) { setupImport(interp, "."); runREPL(interp); return 0; }

    std::string arg1 = argv[1];

    if ((arg1=="-e"||arg1=="--eval") && argc>=3) {
        setupImport(interp, ".");
        try { runSource(interp, argv[2]); }
        catch (ThrowSignal& ts) { std::cerr<<"\033[31m[Uncaught]\033[0m "<<ts.val->toString()<<"\n"; return 1; }
        catch (std::exception& e) { std::cerr<<"\033[31m[Error]\033[0m "<<e.what()<<"\n"; return 1; }
        return 0;
    }

    std::string filePath = arg1;
    std::string baseDir  = fs::path(filePath).parent_path().string();
    if (baseDir.empty()) baseDir = ".";
    setupImport(interp, baseDir);

    try {
        runSource(interp, readFile(filePath));
    } catch (ThrowSignal& ts) {
        std::cerr<<"\033[31m[Uncaught throw]\033[0m "<<ts.val->toString()<<"\n"; return 1;
    } catch (std::exception& e) {
        std::cerr<<"\033[31m[Raze Error]\033[0m "<<e.what()<<"\n"; return 1;
    }
    return 0;
}
