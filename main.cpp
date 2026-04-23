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

namespace fs = std::filesystem;

// ── Helpers ───────────────────────────────────────────────────
static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open file: " + path);
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}

static void runSource(Interpreter& interp, const std::string& src, const std::string& origin="<eval>") {
    Lexer  lex(src);
    auto   toks = lex.tokenize();
    Parser par(std::move(toks));
    auto   prog = par.parse();
    interp.run(std::move(prog));
}

// ── Demo native C functions (callable by raw address) ─────────
extern "C" {
    int64_t demo_add(int64_t a, int64_t b) {
        printf("  [native_add] %lld + %lld = %lld\n",(long long)a,(long long)b,(long long)(a+b));
        return a+b;
    }
    int64_t demo_clamp(int64_t v, int64_t lo, int64_t hi) {
        int64_t r = v<lo?lo:(v>hi?hi:v);
        printf("  [native_clamp] clamp(%lld,%lld,%lld)=%lld\n",(long long)v,(long long)lo,(long long)hi,(long long)r);
        return r;
    }
}

// ── Standard library built-ins registered in C++ ──────────────
static void registerBuiltins(Interpreter& interp) {
    // ── Math constants ────────────────────────────────────────
    interp.globals->define("PI",   Value::fromFloat(M_PI));
    interp.globals->define("TAU",  Value::fromFloat(2.0*M_PI));
    interp.globals->define("E",    Value::fromFloat(M_E));
    interp.globals->define("INF",  Value::fromFloat(std::numeric_limits<double>::infinity()));
    interp.globals->define("NAN",  Value::fromFloat(std::numeric_limits<double>::quiet_NaN()));
    interp.globals->define("INT_MAX", Value::fromInt(std::numeric_limits<int64_t>::max()));
    interp.globals->define("INT_MIN", Value::fromInt(std::numeric_limits<int64_t>::min()));

    // ── Random ───────────────────────────────────────────────
    interp.registerNative("rand", [](std::vector<ValPtr> args) -> ValPtr {
        static std::mt19937_64 rng(std::random_device{}());
        if (args.empty()) {
            std::uniform_real_distribution<double> d(0.0, 1.0);
            return Value::fromFloat(d(rng));
        } else if (args.size() == 1) {
            int64_t hi = args[0]->toInt();
            std::uniform_int_distribution<int64_t> d(0, hi-1);
            return Value::fromInt(d(rng));
        } else {
            int64_t lo = args[0]->toInt(), hi = args[1]->toInt();
            std::uniform_int_distribution<int64_t> d(lo, hi);
            return Value::fromInt(d(rng));
        }
    });
    interp.registerNative("randFloat", [](std::vector<ValPtr> args) -> ValPtr {
        static std::mt19937_64 rng(std::random_device{}());
        double lo=0.0, hi=1.0;
        if (args.size()>=1) hi=args[0]->toFloat();
        if (args.size()>=2) { lo=args[0]->toFloat(); hi=args[1]->toFloat(); }
        std::uniform_real_distribution<double> d(lo,hi);
        return Value::fromFloat(d(rng));
    });
    interp.registerNative("srand", [](std::vector<ValPtr> args) -> ValPtr {
        // Can't easily reseed static rng, but honour the call
        return Value::null();
    });

    // ── Time ─────────────────────────────────────────────────
    interp.registerNative("time", [](std::vector<ValPtr>) -> ValPtr {
        return Value::fromInt((int64_t)std::time(nullptr));
    });
    interp.registerNative("clock", [](std::vector<ValPtr>) -> ValPtr {
        using namespace std::chrono;
        auto now = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
        return Value::fromInt((int64_t)now);
    });
    interp.registerNative("sleep", [](std::vector<ValPtr> args) -> ValPtr {
        int64_t ms = args.empty() ? 0 : args[0]->toInt();
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        return Value::null();
    });

    // ── I/O ──────────────────────────────────────────────────
    interp.registerNative("readFile", [](std::vector<ValPtr> args) -> ValPtr {
        if (args.empty()) throw std::runtime_error("readFile() needs path");
        std::ifstream f(args[0]->toString());
        if (!f) throw std::runtime_error("readFile: cannot open '"+args[0]->toString()+"'");
        std::ostringstream ss; ss << f.rdbuf();
        return Value::fromStr(ss.str());
    });
    interp.registerNative("writeFile", [](std::vector<ValPtr> args) -> ValPtr {
        if (args.size() < 2) throw std::runtime_error("writeFile() needs path+content");
        std::ofstream f(args[0]->toString());
        if (!f) throw std::runtime_error("writeFile: cannot open '"+args[0]->toString()+"'");
        f << args[1]->toString();
        return Value::null();
    });
    interp.registerNative("appendFile", [](std::vector<ValPtr> args) -> ValPtr {
        if (args.size() < 2) throw std::runtime_error("appendFile() needs path+content");
        std::ofstream f(args[0]->toString(), std::ios::app);
        if (!f) throw std::runtime_error("appendFile: cannot open '"+args[0]->toString()+"'");
        f << args[1]->toString();
        return Value::null();
    });
    interp.registerNative("fileExists", [](std::vector<ValPtr> args) -> ValPtr {
        if (args.empty()) return Value::fromBool(false);
        return Value::fromBool(fs::exists(args[0]->toString()));
    });
    interp.registerNative("deleteFile", [](std::vector<ValPtr> args) -> ValPtr {
        if (args.empty()) return Value::fromBool(false);
        return Value::fromBool(fs::remove(args[0]->toString()));
    });
    interp.registerNative("listDir", [](std::vector<ValPtr> args) -> ValPtr {
        std::string path = args.empty() ? "." : args[0]->toString();
        auto v = Value::fromArray();
        for (auto& e : fs::directory_iterator(path))
            v->arr.push_back(Value::fromStr(e.path().string()));
        return v;
    });
    interp.registerNative("readLines", [](std::vector<ValPtr> args) -> ValPtr {
        if (args.empty()) throw std::runtime_error("readLines() needs path");
        std::ifstream f(args[0]->toString());
        if (!f) throw std::runtime_error("readLines: cannot open '"+args[0]->toString()+"'");
        auto v = Value::fromArray();
        std::string line;
        while (std::getline(f, line)) v->arr.push_back(Value::fromStr(line));
        return v;
    });

    // ── String utils ─────────────────────────────────────────
    interp.registerNative("format", [](std::vector<ValPtr> args) -> ValPtr {
        if (args.empty()) return Value::fromStr("");
        std::string s = args[0]->toString();
        size_t ai = 1;
        size_t p = 0;
        while ((p = s.find("{}", p)) != std::string::npos && ai < args.size()) {
            std::string rep = args[ai++]->toString();
            s.replace(p, 2, rep); p += rep.size();
        }
        return Value::fromStr(s);
    });
    interp.registerNative("sprintf", [](std::vector<ValPtr> args) -> ValPtr {
        if (args.empty()) return Value::fromStr("");
        std::string fmt = args[0]->toString();
        std::string out; size_t ai = 1;
        for (size_t i = 0; i < fmt.size(); i++) {
            if (fmt[i] == '%' && i+1 < fmt.size()) {
                // Grab full format spec: %[flags][width][.precision]spec
                std::string spec_buf = "%";
                i++;
                while (i < fmt.size() && (fmt[i]=='-'||fmt[i]=='+'||fmt[i]==' '||fmt[i]=='0'||fmt[i]=='#')) spec_buf += fmt[i++];
                while (i < fmt.size() && isdigit(fmt[i])) spec_buf += fmt[i++];
                if (i < fmt.size() && fmt[i]=='.') { spec_buf += fmt[i++]; while(i<fmt.size()&&isdigit(fmt[i]))spec_buf+=fmt[i++]; }
                if (i >= fmt.size()) { out += spec_buf; break; }
                char spec = fmt[i];
                spec_buf += spec;
                if (spec == '%') { out += '%'; continue; }
                if (ai >= args.size()) { out += spec_buf; continue; }
                auto& v = args[ai++];
                char buf[128];
                switch (spec) {
                    case 'd': case 'i': { std::string sf=spec_buf; sf.back()='l'; sf+="ld"; snprintf(buf,sizeof(buf),sf.c_str(),(long long)v->toInt()); out+=buf; break; }
                    case 'f': case 'g': case 'e': snprintf(buf,sizeof(buf),spec_buf.c_str(),v->toFloat()); out+=buf; break;
                    case 's': { std::string sf=spec_buf; sf.back()='s'; snprintf(buf,sizeof(buf),sf.c_str(),v->toString().c_str()); out+=buf; break; }
                    case 'x': { std::string sf=spec_buf; sf.back()='l'; sf+="lx"; snprintf(buf,sizeof(buf),sf.c_str(),(unsigned long long)v->toInt()); out+=buf; break; }
                    case 'X': { std::string sf=spec_buf; sf.back()='l'; sf+="lX"; snprintf(buf,sizeof(buf),sf.c_str(),(unsigned long long)v->toInt()); out+=buf; break; }
                    case 'o': { std::string sf=spec_buf; sf.back()='l'; sf+="lo"; snprintf(buf,sizeof(buf),sf.c_str(),(unsigned long long)v->toInt()); out+=buf; break; }
                    case 'b': { int64_t n=v->toInt(); std::string b; if(n==0)b="0"; else{uint64_t u=(uint64_t)n;while(u){b=(char)('0'+u%2)+b;u>>=1;}} out+=b; break; }
                    default: out += spec_buf; break;
                }
            } else { out += fmt[i]; }
        }
        return Value::fromStr(out);
    });

    // ── System ───────────────────────────────────────────────
    interp.registerNative("getenv", [](std::vector<ValPtr> args) -> ValPtr {
        if (args.empty()) return Value::null();
        const char* v = std::getenv(args[0]->toString().c_str());
        return v ? Value::fromStr(v) : Value::null();
    });
    interp.registerNative("system", [](std::vector<ValPtr> args) -> ValPtr {
        if (args.empty()) return Value::fromInt(0);
        return Value::fromInt((int64_t)std::system(args[0]->toString().c_str()));
    });
    interp.registerNative("args", [](std::vector<ValPtr>) -> ValPtr {
        return Value::fromArray(); // populated in main if needed
    });

    // ── Bit ops ──────────────────────────────────────────────
    interp.registerNative("band",   [](std::vector<ValPtr> a)->ValPtr{ return Value::fromInt(a[0]->toInt()&a[1]->toInt()); });
    interp.registerNative("bor",    [](std::vector<ValPtr> a)->ValPtr{ return Value::fromInt(a[0]->toInt()|a[1]->toInt()); });
    interp.registerNative("bxor",   [](std::vector<ValPtr> a)->ValPtr{ return Value::fromInt(a[0]->toInt()^a[1]->toInt()); });
    interp.registerNative("bnot",   [](std::vector<ValPtr> a)->ValPtr{ return Value::fromInt(~a[0]->toInt()); });
    interp.registerNative("shl",    [](std::vector<ValPtr> a)->ValPtr{ return Value::fromInt(a[0]->toInt()<<(int)a[1]->toInt()); });
    interp.registerNative("shr",    [](std::vector<ValPtr> a)->ValPtr{ return Value::fromInt(a[0]->toInt()>>(int)a[1]->toInt()); });
    interp.registerNative("popcnt", [](std::vector<ValPtr> a)->ValPtr{
        uint64_t v=(uint64_t)a[0]->toInt(); int c=0; while(v){c+=v&1;v>>=1;} return Value::fromInt(c);
    });

    // ── Conversions ───────────────────────────────────────────
    interp.registerNative("hex", [](std::vector<ValPtr> args)->ValPtr{
        if (args.empty()) return Value::fromStr("0x0");
        char buf[32]; snprintf(buf,sizeof(buf),"0x%llX",(unsigned long long)args[0]->toInt());
        return Value::fromStr(buf);
    });
    interp.registerNative("bin", [](std::vector<ValPtr> args)->ValPtr{
        if (args.empty()) return Value::fromStr("0b0");
        int64_t n=args[0]->toInt(); if(n==0)return Value::fromStr("0b0");
        std::string r; uint64_t v=(uint64_t)n;
        while(v){r=(char)('0'+v%2)+r;v>>=1;} return Value::fromStr("0b"+r);
    });
    interp.registerNative("oct", [](std::vector<ValPtr> args)->ValPtr{
        if (args.empty()) return Value::fromStr("0o0");
        char buf[32]; snprintf(buf,sizeof(buf),"0o%llo",(unsigned long long)args[0]->toInt());
        return Value::fromStr(buf);
    });
    interp.registerNative("parseHex", [](std::vector<ValPtr> args)->ValPtr{
        if (args.empty()) return Value::fromInt(0);
        std::string s=args[0]->toString();
        if(s.size()>2&&s[0]=='0'&&(s[1]=='x'||s[1]=='X'))s=s.substr(2);
        return Value::fromInt((int64_t)strtoull(s.c_str(),nullptr,16));
    });
    interp.registerNative("parseBin", [](std::vector<ValPtr> args)->ValPtr{
        if (args.empty()) return Value::fromInt(0);
        std::string s=args[0]->toString();
        if(s.size()>2&&s[0]=='0'&&(s[1]=='b'||s[1]=='B'))s=s.substr(2);
        return Value::fromInt((int64_t)strtoull(s.c_str(),nullptr,2));
    });
    interp.registerNative("isNaN", [](std::vector<ValPtr> args)->ValPtr{
        if (args.empty()) return Value::fromBool(false);
        return Value::fromBool(std::isnan(args[0]->toFloat()));
    });
    interp.registerNative("isInf", [](std::vector<ValPtr> args)->ValPtr{
        if (args.empty()) return Value::fromBool(false);
        return Value::fromBool(std::isinf(args[0]->toFloat()));
    });

    // ── Raw native address demos ───────────────────────────────
    interp.registerAddr("native_add",
        reinterpret_cast<uintptr_t>(demo_add), "int", {"int","int"});
    interp.registerAddr("native_clamp",
        reinterpret_cast<uintptr_t>(demo_clamp), "int", {"int","int","int"});
}

// ── Import handler ─────────────────────────────────────────────
static void setupImport(Interpreter& interp, const std::string& baseDir) {
    interp.importHandler = [&interp, baseDir](const std::string& path) {
        // Try relative to base dir first, then stdlib/
        std::vector<std::string> candidates = {
            baseDir + "/" + path,
            baseDir + "/" + path + ".rz",
            path,
            path + ".rz",
        };
        for (auto& c : candidates) {
            if (fs::exists(c)) {
                std::string src = readFile(c);
                runSource(interp, src, c);
                return;
            }
        }
        throw std::runtime_error("import: cannot find '" + path + "'");
    };
}

// ── REPL ──────────────────────────────────────────────────────
static void runREPL(Interpreter& interp) {
    std::cout << "\033[36m╔══════════════════════════════════╗\033[0m\n";
    std::cout << "\033[36m║   Raze Language v2.0  REPL       ║\033[0m\n";
    std::cout << "\033[36m╚══════════════════════════════════╝\033[0m\n";
    std::cout << "Type 'exit;' to quit. Multi-line OK.\n\n";
    std::string buffer;
    while (true) {
        std::cout << (buffer.empty() ? "\033[32m>>>\033[0m " : "\033[33m...\033[0m ");
        std::string line;
        if (!std::getline(std::cin, line)) break;
        if (line == "exit;" || line == "quit;") break;
        buffer += line + "\n";
        try {
            Lexer lex(buffer); auto toks=lex.tokenize();
            Parser par(std::move(toks)); auto prog=par.parse();
            interp.run(std::move(prog));
            buffer.clear();
        } catch (std::exception& ex) {
            std::string msg = ex.what();
            if (msg.find("Expected") != std::string::npos ||
                msg.find("Unexpected") != std::string::npos) continue;
            std::cerr << "\033[31m[Error]\033[0m " << msg << "\n";
            buffer.clear();
        }
    }
}

// ── Main ──────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    Interpreter interp;
    registerBuiltins(interp);

    // Store CLI args as global array
    auto cliArgs = Value::fromArray();
    for (int i = 1; i < argc; i++)
        cliArgs->arr.push_back(Value::fromStr(argv[i]));
    interp.globals->define("ARGS", cliArgs);

    // REPL
    if (argc == 1) { setupImport(interp, "."); runREPL(interp); return 0; }

    std::string arg1 = argv[1];

    // Inline eval: raze -e "code"
    if ((arg1 == "-e" || arg1 == "--eval") && argc >= 3) {
        setupImport(interp, ".");
        try { runSource(interp, argv[2]); }
        catch (ThrowSignal& ts) { std::cerr << "\033[31m[Uncaught throw]\033[0m " << ts.val->toString() << "\n"; return 1; }
        catch (std::exception& ex) { std::cerr << "\033[31m[Error]\033[0m " << ex.what() << "\n"; return 1; }
        return 0;
    }

    // Run file
    std::string filePath = arg1;
    std::string baseDir  = fs::path(filePath).parent_path().string();
    if (baseDir.empty()) baseDir = ".";
    setupImport(interp, baseDir);

    try {
        std::string src = readFile(filePath);
        runSource(interp, src, filePath);
    } catch (ThrowSignal& ts) {
        std::cerr << "\033[31m[Uncaught throw]\033[0m " << ts.val->toString() << "\n";
        return 1;
    } catch (std::exception& ex) {
        std::cerr << "\033[31m[Raze Error]\033[0m " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
