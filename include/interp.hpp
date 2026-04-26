#pragma once
#include "value.hpp"
#include "ast.hpp"
#include <cmath>
#include <deque>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <functional>
#include <unordered_map>

struct ReturnSignal  { ValPtr val; };
struct BreakSignal   {};
struct ContinueSignal{};
struct ThrowSignal   { ValPtr val; };

// ── Native call ───────────────────────────────────────────────
namespace raze_native {
union Slot{int64_t i;double d;};
inline Slot pack(const ValPtr&v,const std::string&t){
    Slot s{};if(t=="float"||t=="double")s.d=v->toFloat();else s.i=v->toInt();return s;}
inline ValPtr call(const NativeInfo&ni,const std::vector<ValPtr>&args){
    if(ni.callback)return ni.callback(args);
    if(!ni.addr)throw std::runtime_error("Native: no address");
    size_t n=std::min(args.size(),ni.paramTypes.size());
    Slot sl[8]={};
    for(size_t i=0;i<n&&i<8;i++)sl[i]=pack(args[i],i<ni.paramTypes.size()?ni.paramTypes[i]:"int");
    bool retF=(ni.retType=="float"||ni.retType=="double");
    bool anyF=false;
    for(size_t i=0;i<n;i++)if(i<ni.paramTypes.size()&&(ni.paramTypes[i]=="float"||ni.paramTypes[i]=="double"))anyF=true;
    auto cI=[&]()->int64_t{typedef int64_t(*F)(int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t);return((F)ni.addr)(sl[0].i,sl[1].i,sl[2].i,sl[3].i,sl[4].i,sl[5].i,sl[6].i,sl[7].i);};
    auto cF=[&]()->double{typedef double(*F)(double,double,double,double,double,double,double,double);return((F)ni.addr)(sl[0].d,sl[1].d,sl[2].d,sl[3].d,sl[4].d,sl[5].d,sl[6].d,sl[7].d);};
    if(retF)return Value::fromFloat(anyF?cF():(double)cI());
    int64_t r=anyF?(int64_t)cF():cI();
    if(ni.retType=="bool")return Value::fromBool(r!=0);
    if(ni.retType=="void")return Value::null();
    return Value::fromInt(r);}
}

class Interpreter {
public:
    EnvPtr  globals;
    std::unordered_map<std::string,FuncDef>       funcs;
    std::unordered_map<std::string,ClassDef>      classes;
    std::unordered_map<std::string,StructDef>     structs;
    std::unordered_map<std::string,EnumDef>       enums;
    std::unordered_map<std::string,InterfaceDef>  interfaces;
    std::deque<Program> programs; // deque = no realloc, Stmt* safe
    std::function<void(const std::string&,const std::string&)> importHandler;

    Interpreter(){globals=std::make_shared<Env>();}

    // ── Host API ──────────────────────────────────────────────
    void registerNative(const std::string&name,std::function<ValPtr(std::vector<ValPtr>)>fn,uintptr_t addr=0){
        auto ni=std::make_shared<NativeInfo>();ni->callback=fn;ni->addr=addr;
        globals->define(name,Value::fromNative(ni));}
    void registerAddr(const std::string&name,uintptr_t addr,std::string ret="void",std::vector<std::string>pt={}){
        auto ni=std::make_shared<NativeInfo>();ni->addr=addr;ni->retType=ret;ni->paramTypes=pt;
        globals->define(name,Value::fromNative(ni));}

    // ── Run ───────────────────────────────────────────────────
    void run(Program prog){
        programs.push_back(std::move(prog));
        auto&p=programs.back();
        for(auto&s:p.stmts)
            if(s->kind==StmtKind::StructDecl||s->kind==StmtKind::FuncDecl||
               s->kind==StmtKind::ClassDecl||s->kind==StmtKind::NativeDecl||
               s->kind==StmtKind::EnumDecl||s->kind==StmtKind::InterfaceDecl||
               s->kind==StmtKind::Import)
                execStmt(s.get(),globals);
        for(auto&s:p.stmts)
            if(s->kind!=StmtKind::StructDecl&&s->kind!=StmtKind::FuncDecl&&
               s->kind!=StmtKind::ClassDecl&&s->kind!=StmtKind::NativeDecl&&
               s->kind!=StmtKind::EnumDecl&&s->kind!=StmtKind::InterfaceDecl&&
               s->kind!=StmtKind::Import)
                execStmt(s.get(),globals);
        auto fit=funcs.find("main");
        if(fit!=funcs.end())callFunc(fit->second,{},globals);
    }

    // ── Func calls ────────────────────────────────────────────
    ValPtr callFunc(const FuncDef&fd,const std::vector<ValPtr>&args,EnvPtr){
        auto sc=std::make_shared<Env>(globals);
        bindArgs(fd.params,args,sc);
        try{execStmt(fd.body,sc);}catch(ReturnSignal&rs){return rs.val?rs.val:Value::null();}
        return Value::null();}

    ValPtr callFuncValue(const FuncValue&fv,const std::vector<ValPtr>&args,EnvPtr){
        if(fv.callback)return fv.callback(args);
        auto sc=std::make_shared<Env>(fv.closure?fv.closure:globals);
        bindArgs(fv.params,args,sc);
        try{execStmt(fv.body,sc);}catch(ReturnSignal&rs){return rs.val?rs.val:Value::null();}
        return Value::null();}

    void bindArgs(const std::vector<Param>&params,const std::vector<ValPtr>&args,EnvPtr sc){
        // Handle variadic last param
        bool hasVariadic=(!params.empty()&&params.back().isVariadic);
        size_t normalCount=hasVariadic?params.size()-1:params.size();
        for(size_t i=0;i<normalCount;i++){
            if(i<args.size())sc->define(params[i].name,args[i]);
            else if(params[i].defaultVal){sc->define(params[i].name,evalExpr(params[i].defaultVal.get(),globals));}
            else sc->define(params[i].name,defaultValue(params[i].type));
        }
        if(hasVariadic){
            auto va=Value::fromArray();
            for(size_t i=normalCount;i<args.size();i++)va->arr.push_back(args[i]);
            sc->define(params.back().name,va);
        }
    }

    ValPtr callValue(ValPtr fn,const std::vector<ValPtr>&args,EnvPtr env){
        if(fn->type==VType::FUNC)return callFuncValue(*fn->func,args,env);
        if(fn->type==VType::NATIVE)return raze_native::call(*fn->native,args);
        throw std::runtime_error("Value is not callable: "+fn->toString());}

    // ── Default values ────────────────────────────────────────
    ValPtr defaultValue(const TypeNode&t){
        if(t.isNullable)return Value::null();
        if(t.isArray)return Value::fromArray();
        if(t.name=="int")return Value::fromInt(0);
        if(t.name=="float")return Value::fromFloat(0.0);
        if(t.name=="bool")return Value::fromBool(false);
        if(t.name=="string")return Value::fromStr("");
        if(t.name=="void"||t.name=="var")return Value::null();
        if(classes.count(t.name))return instantiateClass(t.name,{},globals);
        if(structs.count(t.name)){
            auto sv=Value::fromStruct(t.name);
            for(auto&f:structs[t.name].fields)sv->obj->fields[f.name]=defaultValue(f.type);
            return sv;}
        return Value::null();}

    // ── Class instantiation ───────────────────────────────────
    ValPtr instantiateClass(const std::string&cname,const std::vector<ValPtr>&args,EnvPtr env){
        auto it=classes.find(cname);
        if(it==classes.end())throw std::runtime_error("Unknown class: "+cname);
        auto inst=Value::fromStruct(cname);
        initClassFields(inst,cname);
        if(auto m=findMethod(cname,"init"))callMethod(inst,*m,args,env);
        return inst;}

    void initClassFields(ValPtr&inst,const std::string&cname){
        auto it=classes.find(cname);if(it==classes.end())return;
        if(!it->second.parent.empty())initClassFields(inst,it->second.parent);
        for(auto&f:it->second.fields)
            if(!inst->obj->fields.count(f.name))
                inst->obj->fields[f.name]=f.defaultVal?evalExpr(f.defaultVal.get(),globals):defaultValue(f.type);}

    FuncDef*findMethod(const std::string&cname,const std::string&mname){
        auto it=classes.find(cname);if(it==classes.end())return nullptr;
        auto mit=it->second.methods.find(mname);
        if(mit!=it->second.methods.end())return&mit->second;
        if(!it->second.parent.empty())return findMethod(it->second.parent,mname);
        return nullptr;}

    FuncDef*findStaticMethod(const std::string&cname,const std::string&mname){
        auto it=classes.find(cname);if(it==classes.end())return nullptr;
        auto mit=it->second.staticMethods.find(mname);
        if(mit!=it->second.staticMethods.end())return&mit->second;
        return nullptr;}

    ValPtr callMethod(ValPtr inst,const FuncDef&fd,const std::vector<ValPtr>&args,EnvPtr env,const std::string&/*unused*/=""){
        auto sc=std::make_shared<Env>(globals);
        sc->define("this",inst);
        // __methodclass__ = the class that OWNS (declares) this method
        // super inside this method should find the parent of __methodclass__
        sc->define("__methodclass__",Value::fromStr(fd.ownerClass.empty()?inst->obj->typeName:fd.ownerClass));
        bindArgs(fd.params,args,sc);
        try{execStmt(fd.body,sc);}catch(ReturnSignal&rs){return rs.val?rs.val:Value::null();}
        return Value::null();}

    // ── Interface check ───────────────────────────────────────
    bool implementsInterface(const std::string&cname,const std::string&iname){
        auto it=interfaces.find(iname);if(it==interfaces.end())return false;
        for(auto&m:it->second.methods)if(!findMethod(cname,m))return false;
        return true;}

    // ── String methods ────────────────────────────────────────
    ValPtr callStringMethod(ValPtr obj,const std::string&method,const std::vector<ValPtr>&args,EnvPtr env){
        std::string&s=obj->sval;
        if(method=="len"||method=="length")return Value::fromInt((int64_t)s.size());
        if(method=="upper"){std::string r=s;for(auto&c:r)c=toupper(c);return Value::fromStr(r);}
        if(method=="lower"){std::string r=s;for(auto&c:r)c=tolower(c);return Value::fromStr(r);}
        if(method=="trim"){size_t a=s.find_first_not_of(" \t\r\n"),b=s.find_last_not_of(" \t\r\n");return Value::fromStr(a==std::string::npos?"":s.substr(a,b-a+1));}
        if(method=="ltrim"){size_t a=s.find_first_not_of(" \t\r\n");return Value::fromStr(a==std::string::npos?"":s.substr(a));}
        if(method=="rtrim"){size_t b=s.find_last_not_of(" \t\r\n");return Value::fromStr(b==std::string::npos?"":s.substr(0,b+1));}
        if(method=="split"){std::string d=args.empty()?" ":args[0]->toString();auto v=Value::fromArray();if(d.empty()){for(char c:s)v->arr.push_back(Value::fromStr(std::string(1,c)));return v;}size_t st=0,f;while((f=s.find(d,st))!=std::string::npos){v->arr.push_back(Value::fromStr(s.substr(st,f-st)));st=f+d.size();}v->arr.push_back(Value::fromStr(s.substr(st)));return v;}
        if(method=="replace"){if(args.size()<2)throw std::runtime_error("replace() needs 2 args");std::string from=args[0]->toString(),to=args[1]->toString(),r=s;size_t p=0;while((p=r.find(from,p))!=std::string::npos){r.replace(p,from.size(),to);p+=to.size();}return Value::fromStr(r);}
        if(method=="replaceFirst"){if(args.size()<2)throw std::runtime_error("replaceFirst() needs 2");std::string from=args[0]->toString(),to=args[1]->toString(),r=s;size_t p=r.find(from);if(p!=std::string::npos)r.replace(p,from.size(),to);return Value::fromStr(r);}
        if(method=="startsWith"||method=="hasPrefix"){if(args.empty())return Value::fromBool(false);std::string p=args[0]->toString();return Value::fromBool(s.size()>=p.size()&&s.substr(0,p.size())==p);}
        if(method=="endsWith"||method=="hasSuffix"){if(args.empty())return Value::fromBool(false);std::string p=args[0]->toString();return Value::fromBool(s.size()>=p.size()&&s.substr(s.size()-p.size())==p);}
        if(method=="indexOf"||method=="find"){std::string sub=args.empty()?"":args[0]->toString();size_t p=s.find(sub);return Value::fromInt(p==std::string::npos?-1:(int64_t)p);}
        if(method=="lastIndexOf"){std::string sub=args.empty()?"":args[0]->toString();size_t p=s.rfind(sub);return Value::fromInt(p==std::string::npos?-1:(int64_t)p);}
        if(method=="contains"){std::string sub=args.empty()?"":args[0]->toString();return Value::fromBool(s.find(sub)!=std::string::npos);}
        if(method=="count"){std::string sub=args.empty()?"":args[0]->toString();int64_t cnt=0;size_t p=0;while((p=s.find(sub,p))!=std::string::npos){cnt++;p+=sub.size();}return Value::fromInt(cnt);}
        if(method=="substr"||method=="slice"){int64_t start=args.empty()?0:args[0]->toInt();if(start<0)start=(int64_t)s.size()+start;start=std::max((int64_t)0,std::min(start,(int64_t)s.size()));if(args.size()<2)return Value::fromStr(s.substr(start));int64_t len=args[1]->toInt();if(len<0)len=(int64_t)s.size()+len-start;return Value::fromStr(s.substr(start,std::max((int64_t)0,len)));}
        if(method=="repeat"){int64_t n=args.empty()?1:args[0]->toInt();std::string r;for(int64_t i=0;i<n;i++)r+=s;return Value::fromStr(r);}
        if(method=="reverse"){std::string r=s;std::reverse(r.begin(),r.end());return Value::fromStr(r);}
        if(method=="padLeft"){int64_t w=args.empty()?0:args[0]->toInt();char ch=args.size()<2?' ':args[1]->toString()[0];std::string r=s;while((int64_t)r.size()<w)r=ch+r;return Value::fromStr(r);}
        if(method=="padRight"){int64_t w=args.empty()?0:args[0]->toInt();char ch=args.size()<2?' ':args[1]->toString()[0];std::string r=s;while((int64_t)r.size()<w)r+=ch;return Value::fromStr(r);}
        if(method=="toInt"){try{return Value::fromInt(std::stoll(s));}catch(...){return Value::fromInt(0);}}
        if(method=="toFloat"){try{return Value::fromFloat(std::stod(s));}catch(...){return Value::fromFloat(0.0);}}
        if(method=="isEmpty")return Value::fromBool(s.empty());
        if(method=="chars"){auto v=Value::fromArray();for(char c:s)v->arr.push_back(Value::fromStr(std::string(1,c)));return v;}
        if(method=="bytes"){auto v=Value::fromArray();for(unsigned char c:s)v->arr.push_back(Value::fromInt(c));return v;}
        if(method=="format"){std::string r=s;size_t ai=0;size_t p;while((p=r.find("{}"))!=std::string::npos&&ai<args.size()){std::string rep=args[ai++]->toString();r.replace(p,2,rep);}return Value::fromStr(r);}
        if(method=="isAlpha"){for(char c:s)if(!isalpha(c))return Value::fromBool(false);return Value::fromBool(!s.empty());}
        if(method=="isDigit"){for(char c:s)if(!isdigit(c))return Value::fromBool(false);return Value::fromBool(!s.empty());}
        if(method=="isAlnum"){for(char c:s)if(!isalnum(c))return Value::fromBool(false);return Value::fromBool(!s.empty());}
        if(method=="title"){std::string r=s;bool nc=true;for(char&c:r){if(c==' ')nc=true;else if(nc){c=toupper(c);nc=false;}else c=tolower(c);}return Value::fromStr(r);}
        if(method=="center"){int64_t w=args.empty()?0:args[0]->toInt();char f=args.size()<2?' ':args[1]->toString()[0];int64_t pad=w-(int64_t)s.size();if(pad<=0)return Value::fromStr(s);int64_t L=pad/2,R=pad-L;std::string ls(L,f),rs(R,f);return Value::fromStr(ls+s+rs);}
        if(method=="toString"||method=="str")return Value::fromStr(s);
        throw std::runtime_error("Unknown string method: '"+method+"'");}

    // ── Array methods ─────────────────────────────────────────
    ValPtr callArrayMethod(ValPtr obj,const std::string&method,const std::vector<ValPtr>&args,EnvPtr env){
        auto&arr=obj->arr;
        if(method=="len"||method=="length"||method=="size")return Value::fromInt((int64_t)arr.size());
        if(method=="push"||method=="append"){if(!args.empty())arr.push_back(args[0]);return obj;}
        if(method=="pop"){if(arr.empty())throw std::runtime_error("pop() on empty");auto v=arr.back();arr.pop_back();return v;}
        if(method=="shift"){if(arr.empty())throw std::runtime_error("shift() on empty");auto v=arr.front();arr.erase(arr.begin());return v;}
        if(method=="unshift"){if(!args.empty())arr.insert(arr.begin(),args[0]);return obj;}
        if(method=="isEmpty")return Value::fromBool(arr.empty());
        if(method=="first")return arr.empty()?Value::null():arr.front();
        if(method=="last")return arr.empty()?Value::null():arr.back();
        if(method=="clear"){arr.clear();return Value::null();}
        if(method=="contains"){if(args.empty())return Value::fromBool(false);std::string t=args[0]->toString();for(auto&v:arr)if(v->toString()==t)return Value::fromBool(true);return Value::fromBool(false);}
        if(method=="indexOf"){if(args.empty())return Value::fromInt(-1);std::string t=args[0]->toString();for(size_t i=0;i<arr.size();i++)if(arr[i]->toString()==t)return Value::fromInt((int64_t)i);return Value::fromInt(-1);}
        if(method=="slice"){int64_t st=args.empty()?0:args[0]->toInt(),en=(int64_t)arr.size();if(args.size()>1)en=args[1]->toInt();if(st<0)st=(int64_t)arr.size()+st;if(en<0)en=(int64_t)arr.size()+en;st=std::max((int64_t)0,std::min(st,(int64_t)arr.size()));en=std::max(st,std::min(en,(int64_t)arr.size()));auto v=Value::fromArray();for(int64_t i=st;i<en;i++)v->arr.push_back(arr[i]);return v;}
        if(method=="reverse"){auto v=Value::fromArray();v->arr=arr;std::reverse(v->arr.begin(),v->arr.end());return v;}
        if(method=="reverseInPlace"){std::reverse(arr.begin(),arr.end());return obj;}
        if(method=="sort"){auto v=Value::fromArray();v->arr=arr;if(args.size()>0){auto fn=args[0];std::stable_sort(v->arr.begin(),v->arr.end(),[&](const ValPtr&a,const ValPtr&b){auto r=callValue(fn,{a,b},env);return r->toBool();});}else{std::stable_sort(v->arr.begin(),v->arr.end(),[](const ValPtr&a,const ValPtr&b){if(a->isNum()&&b->isNum())return a->toFloat()<b->toFloat();return a->toString()<b->toString();});}return v;}
        if(method=="sortBy"){if(args.empty())throw std::runtime_error("sortBy() needs key func");auto fn=args[0];auto v=Value::fromArray();v->arr=arr;std::stable_sort(v->arr.begin(),v->arr.end(),[&](const ValPtr&a,const ValPtr&b){auto ka=callValue(fn,{a},env),kb=callValue(fn,{b},env);if(ka->isNum()&&kb->isNum())return ka->toFloat()<kb->toFloat();return ka->toString()<kb->toString();});return v;}
        if(method=="sortInPlace"){std::stable_sort(arr.begin(),arr.end(),[](const ValPtr&a,const ValPtr&b){if(a->isNum()&&b->isNum())return a->toFloat()<b->toFloat();return a->toString()<b->toString();});return obj;}
        if(method=="join"){std::string d=args.empty()?"":args[0]->toString();std::string r;for(size_t i=0;i<arr.size();i++){if(i)r+=d;r+=arr[i]->toString();}return Value::fromStr(r);}
        if(method=="copy"){auto v=Value::fromArray();v->arr=arr;return v;}
        if(method=="map"){if(args.empty())throw std::runtime_error("map() needs func");auto fn=args[0];auto v=Value::fromArray();for(size_t i=0;i<arr.size();i++)v->arr.push_back(callValue(fn,{arr[i],Value::fromInt((int64_t)i)},env));return v;}
        if(method=="filter"){if(args.empty())throw std::runtime_error("filter() needs func");auto fn=args[0];auto v=Value::fromArray();for(size_t i=0;i<arr.size();i++)if(callValue(fn,{arr[i],Value::fromInt((int64_t)i)},env)->toBool())v->arr.push_back(arr[i]);return v;}
        if(method=="reduce"){if(args.empty())throw std::runtime_error("reduce() needs func");auto fn=args[0];ValPtr acc=args.size()>1?args[1]:(!arr.empty()?arr[0]:Value::null());size_t st=args.size()>1?0:1;for(size_t i=st;i<arr.size();i++)acc=callValue(fn,{acc,arr[i],Value::fromInt((int64_t)i)},env);return acc;}
        if(method=="forEach"){if(args.empty())throw std::runtime_error("forEach() needs func");auto fn=args[0];for(size_t i=0;i<arr.size();i++)callValue(fn,{arr[i],Value::fromInt((int64_t)i)},env);return Value::null();}
        if(method=="find"){if(args.empty())throw std::runtime_error("find() needs func");auto fn=args[0];for(size_t i=0;i<arr.size();i++)if(callValue(fn,{arr[i],Value::fromInt((int64_t)i)},env)->toBool())return arr[i];return Value::null();}
        if(method=="findIndex"){if(args.empty())throw std::runtime_error("findIndex() needs func");auto fn=args[0];for(size_t i=0;i<arr.size();i++)if(callValue(fn,{arr[i],Value::fromInt((int64_t)i)},env)->toBool())return Value::fromInt((int64_t)i);return Value::fromInt(-1);}
        if(method=="any"){if(args.empty())throw std::runtime_error("any() needs func");auto fn=args[0];for(auto&v:arr)if(callValue(fn,{v},env)->toBool())return Value::fromBool(true);return Value::fromBool(false);}
        if(method=="all"){if(args.empty())throw std::runtime_error("all() needs func");auto fn=args[0];for(auto&v:arr)if(!callValue(fn,{v},env)->toBool())return Value::fromBool(false);return Value::fromBool(true);}
        if(method=="none"){if(args.empty())throw std::runtime_error("none() needs func");auto fn=args[0];for(auto&v:arr)if(callValue(fn,{v},env)->toBool())return Value::fromBool(false);return Value::fromBool(true);}
        if(method=="flat"){auto v=Value::fromArray();for(auto&x:arr){if(x->type==VType::ARRAY)for(auto&y:x->arr)v->arr.push_back(y);else v->arr.push_back(x);}return v;}
        if(method=="flatMap"){if(args.empty())throw std::runtime_error("flatMap() needs func");auto fn=args[0];auto v=Value::fromArray();for(auto&x:arr){auto r=callValue(fn,{x},env);if(r->type==VType::ARRAY)for(auto&y:r->arr)v->arr.push_back(y);else v->arr.push_back(r);}return v;}
        if(method=="zip"){if(args.empty()||args[0]->type!=VType::ARRAY)throw std::runtime_error("zip() needs array");auto&o=args[0]->arr;size_t n=std::min(arr.size(),o.size());auto v=Value::fromArray();for(size_t i=0;i<n;i++){auto p=Value::fromArray();p->arr.push_back(arr[i]);p->arr.push_back(o[i]);v->arr.push_back(p);}return v;}
        if(method=="sum"){double s=0;for(auto&v:arr)s+=v->toFloat();if(!arr.empty()&&arr[0]->type==VType::INT&&(int64_t)s==(double)s)return Value::fromInt((int64_t)s);return Value::fromFloat(s);}
        if(method=="min"){if(arr.empty())return Value::null();auto m=arr[0];for(size_t i=1;i<arr.size();i++)if(arr[i]->toFloat()<m->toFloat())m=arr[i];return m;}
        if(method=="max"){if(arr.empty())return Value::null();auto m=arr[0];for(size_t i=1;i<arr.size();i++)if(arr[i]->toFloat()>m->toFloat())m=arr[i];return m;}
        if(method=="count"){if(args.empty())return Value::fromInt((int64_t)arr.size());auto fn=args[0];int64_t c=0;for(auto&v:arr)if(callValue(fn,{v},env)->toBool())c++;return Value::fromInt(c);}
        if(method=="unique"){auto v=Value::fromArray();std::unordered_map<std::string,bool>seen;for(auto&x:arr){std::string k=x->toString();if(!seen[k]){seen[k]=true;v->arr.push_back(x);}}return v;}
        if(method=="groupBy"){if(args.empty())throw std::runtime_error("groupBy() needs func");auto fn=args[0];auto v=Value::fromMap();for(auto&x:arr){std::string k=callValue(fn,{x},env)->toString();if(!v->mapHas(k)){auto a=Value::fromArray();v->mapSet(k,a);}v->mapGet(k)->arr.push_back(x);}return v;}
        if(method=="chunk"){int64_t n=args.empty()?1:args[0]->toInt();if(n<=0)n=1;auto v=Value::fromArray();for(size_t i=0;i<arr.size();i+=n){auto c=Value::fromArray();for(size_t j=i;j<std::min(i+(size_t)n,arr.size());j++)c->arr.push_back(arr[j]);v->arr.push_back(c);}return v;}
        if(method=="extend"){if(!args.empty()&&args[0]->type==VType::ARRAY)for(auto&x:args[0]->arr)arr.push_back(x);return obj;}
        if(method=="insert"){if(args.size()<2)throw std::runtime_error("insert() needs idx,val");int64_t idx=args[0]->toInt();if(idx<0)idx=(int64_t)arr.size()+idx;idx=std::max((int64_t)0,std::min(idx,(int64_t)arr.size()));arr.insert(arr.begin()+idx,args[1]);return obj;}
        if(method=="remove"){if(args.empty())throw std::runtime_error("remove() needs idx");int64_t idx=args[0]->toInt();if(idx<0)idx=(int64_t)arr.size()+idx;if(idx<0||idx>=(int64_t)arr.size())throw std::runtime_error("remove() OOB");auto v=arr[idx];arr.erase(arr.begin()+idx);return v;}
        if(method=="fill"){if(args.empty())throw std::runtime_error("fill() needs value");for(auto&v:arr)v=args[0];return obj;}
        if(method=="take"){int64_t n=args.empty()?0:args[0]->toInt();auto v=Value::fromArray();for(int64_t i=0;i<n&&i<(int64_t)arr.size();i++)v->arr.push_back(arr[i]);return v;}
        if(method=="drop"){int64_t n=args.empty()?0:args[0]->toInt();auto v=Value::fromArray();for(int64_t i=n;i<(int64_t)arr.size();i++)v->arr.push_back(arr[i]);return v;}
        if(method=="takeWhile"){if(args.empty())throw std::runtime_error("takeWhile() needs func");auto fn=args[0];auto v=Value::fromArray();for(auto&x:arr){if(!callValue(fn,{x},env)->toBool())break;v->arr.push_back(x);}return v;}
        if(method=="dropWhile"){if(args.empty())throw std::runtime_error("dropWhile() needs func");auto fn=args[0];auto v=Value::fromArray();bool drop=true;for(auto&x:arr){if(drop&&callValue(fn,{x},env)->toBool())continue;drop=false;v->arr.push_back(x);}return v;}
        if(method=="toMap"){if(args.empty())throw std::runtime_error("toMap() needs key func");auto kfn=args[0];auto vfn=args.size()>1?args[1]:ValPtr{};auto v=Value::fromMap();for(auto&x:arr){std::string k=callValue(kfn,{x},env)->toString();ValPtr val=vfn?callValue(vfn,{x},env):x;v->mapSet(k,val);}return v;}
        if(method=="toString"||method=="str")return Value::fromStr(obj->toString());
        if(method=="concat"){auto v=Value::fromArray();v->arr=arr;for(auto&a:args)if(a->type==VType::ARRAY)for(auto&x:a->arr)v->arr.push_back(x);return v;}
        throw std::runtime_error("Unknown array method: '"+method+"'");}

    // ── Map methods ───────────────────────────────────────────
    ValPtr callMapMethod(ValPtr obj,const std::string&method,const std::vector<ValPtr>&args,EnvPtr env){
        if(method=="keys"){auto v=Value::fromArray();for(auto&k:*obj->mapOrder)v->arr.push_back(Value::fromStr(k));return v;}
        if(method=="values"){auto v=Value::fromArray();for(auto&k:*obj->mapOrder)v->arr.push_back((*obj->map)[k]);return v;}
        if(method=="has")return Value::fromBool(!args.empty()&&obj->mapHas(args[0]->toString()));
        if(method=="remove"||method=="delete"){if(!args.empty())obj->mapDel(args[0]->toString());return Value::null();}
        if(method=="len"||method=="length"||method=="size")return Value::fromInt((int64_t)obj->map->size());
        if(method=="isEmpty")return Value::fromBool(obj->map->empty());
        if(method=="clear"){obj->map->clear();obj->mapOrder->clear();return Value::null();}
        if(method=="entries"){auto v=Value::fromArray();for(auto&k:*obj->mapOrder){auto p=Value::fromArray();p->arr.push_back(Value::fromStr(k));p->arr.push_back((*obj->map)[k]);v->arr.push_back(p);}return v;}
        if(method=="get"){if(args.empty())return Value::null();auto v=obj->mapGet(args[0]->toString());if(v->isNull()&&args.size()>1)return args[1];return v;}
        if(method=="set"){if(args.size()>=2)obj->mapSet(args[0]->toString(),args[1]);return obj;}
        if(method=="merge"){if(!args.empty()&&args[0]->type==VType::MAP)for(auto&k:*args[0]->mapOrder)obj->mapSet(k,(*args[0]->map)[k]);return obj;}
        if(method=="forEach"){if(args.empty())throw std::runtime_error("forEach() needs func");auto fn=args[0];for(auto&k:*obj->mapOrder)callValue(fn,{Value::fromStr(k),(*obj->map)[k]},env);return Value::null();}
        if(method=="map"){if(args.empty())throw std::runtime_error("map() needs func");auto fn=args[0];auto v=Value::fromMap();for(auto&k:*obj->mapOrder)v->mapSet(k,callValue(fn,{Value::fromStr(k),(*obj->map)[k]},env));return v;}
        if(method=="filter"){if(args.empty())throw std::runtime_error("filter() needs func");auto fn=args[0];auto v=Value::fromMap();for(auto&k:*obj->mapOrder)if(callValue(fn,{Value::fromStr(k),(*obj->map)[k]},env)->toBool())v->mapSet(k,(*obj->map)[k]);return v;}
        if(method=="toArray")return callMapMethod(obj,"entries",args,env);
        if(method=="copy"){auto v=Value::fromMap();for(auto&k:*obj->mapOrder)v->mapSet(k,(*obj->map)[k]);return v;}
        if(method=="invert"){auto v=Value::fromMap();for(auto&k:*obj->mapOrder)v->mapSet((*obj->map)[k]->toString(),Value::fromStr(k));return v;}
        throw std::runtime_error("Unknown map method: '"+method+"'");}

    // ── Expression evaluation ─────────────────────────────────
    ValPtr evalExpr(Expr*e,EnvPtr env){
        if(!e)return Value::null();
        switch(e->kind){
        case ExprKind::IntLit:   return Value::fromInt(e->ival);
        case ExprKind::FloatLit: return Value::fromFloat(e->fval);
        case ExprKind::BoolLit:  return Value::fromBool(e->bval);
        case ExprKind::StrLit:   return Value::fromStr(e->sval);
        case ExprKind::NullLit:  return Value::null();
        case ExprKind::Ident:    return env->get(e->name);
        case ExprKind::Super:    {auto t=env->get("this");auto it=classes.find(t->obj->typeName);if(it==classes.end()||it->second.parent.empty())throw std::runtime_error("No parent for super");return Value::fromStr("__super__:"+it->second.parent);}

        case ExprKind::ArrayLit: {auto v=Value::fromArray();for(auto&a:e->args){if(a->kind==ExprKind::Spread){auto s=evalExpr(a->left.get(),env);if(s->type==VType::ARRAY)for(auto&x:s->arr)v->arr.push_back(x);}else v->arr.push_back(evalExpr(a.get(),env));}return v;}
        case ExprKind::MapLit:   {auto v=Value::fromMap();for(size_t i=0;i<e->mapKeys.size();i++)v->mapSet(evalExpr(e->mapKeys[i].get(),env)->toString(),evalExpr(e->args[i].get(),env));return v;}
        case ExprKind::Lambda:   {auto fv=std::make_shared<FuncValue>();fv->params=e->funcParams;fv->retType=e->funcRetType;fv->body=e->lambdaBody.get();fv->closure=env;return Value::fromFunc(fv);}
        case ExprKind::Spread:   return evalExpr(e->left.get(),env); // spread value itself

        case ExprKind::Cast: {auto v=evalExpr(e->left.get(),env);const std::string&t=e->castType.name;
            if(t=="int")return Value::fromInt(v->toInt());if(t=="float")return Value::fromFloat(v->toFloat());
            if(t=="bool")return Value::fromBool(v->toBool());if(t=="string")return Value::fromStr(v->toString());return v;}

        case ExprKind::New: {auto&n=e->castType.name;std::vector<ValPtr>args;
            for(auto&a:e->args){if(a->kind==ExprKind::Spread){auto s=evalExpr(a->left.get(),env);for(auto&x:s->arr)args.push_back(x);}else args.push_back(evalExpr(a.get(),env));}
            if(classes.count(n))return instantiateClass(n,args,env);
            if(structs.count(n)){auto sv=Value::fromStruct(n);for(auto&f:structs[n].fields)sv->obj->fields[f.name]=defaultValue(f.type);return sv;}
            throw std::runtime_error("Unknown type for 'new': "+n);}

        case ExprKind::Unary: {auto v=evalExpr(e->left.get(),env);
            if(e->op=="-"){if(v->type==VType::INT)return Value::fromInt(-v->ival);return Value::fromFloat(-v->toFloat());}
            if(e->op=="!")return Value::fromBool(!v->toBool());
            if(e->op=="~")return Value::fromInt(~v->toInt());
            throw std::runtime_error("Unknown unary op: "+e->op);}

        case ExprKind::PreInc:
        case ExprKind::PostInc: {bool post=(e->kind==ExprKind::PostInc);auto old=evalExpr(e->left.get(),env);ValPtr nv;
            if(old->type==VType::FLOAT)nv=Value::fromFloat(old->fval+(e->incDir?1.0:-1.0));
            else nv=Value::fromInt(old->ival+(e->incDir?1:-1));
            doAssign(e->left.get(),nv,env);return post?old:nv;}

        case ExprKind::Ternary: return evalExpr(e->left.get(),env)->toBool()?evalExpr(e->right.get(),env):evalExpr(e->extra.get(),env);
        case ExprKind::NullCoal:{auto l=evalExpr(e->left.get(),env);if(!l->isNull())return l;return evalExpr(e->right.get(),env);}

        case ExprKind::Is: {auto v=evalExpr(e->left.get(),env);const std::string&ty=e->isType;
            if(ty=="int")return Value::fromBool(v->type==VType::INT);
            if(ty=="float")return Value::fromBool(v->type==VType::FLOAT);
            if(ty=="bool")return Value::fromBool(v->type==VType::BOOL);
            if(ty=="string")return Value::fromBool(v->type==VType::STR);
            if(ty=="array")return Value::fromBool(v->type==VType::ARRAY);
            if(ty=="map")return Value::fromBool(v->type==VType::MAP);
            if(ty=="func")return Value::fromBool(v->type==VType::FUNC);
            if(ty=="null")return Value::fromBool(v->isNull());
            if(v->type==VType::STRUCT)return Value::fromBool(v->obj->typeName==ty);
            return Value::fromBool(false);}

        case ExprKind::In: {auto item=evalExpr(e->left.get(),env);auto coll=evalExpr(e->right.get(),env);
            if(coll->type==VType::ARRAY){std::string t=item->toString();for(auto&v:coll->arr)if(v->toString()==t)return Value::fromBool(true);return Value::fromBool(false);}
            if(coll->type==VType::MAP)return Value::fromBool(coll->mapHas(item->toString()));
            if(coll->type==VType::STR)return Value::fromBool(coll->sval.find(item->toString())!=std::string::npos);
            return Value::fromBool(false);}

        case ExprKind::Member:    return evalMember(e,env,false);
        case ExprKind::OptMember: {auto obj=evalExpr(e->left.get(),env);if(obj->isNull())return Value::null();return evalMember(e,env,true);}
        case ExprKind::Index:     return evalIndex(e,env,false);
        case ExprKind::OptIndex:  {auto obj=evalExpr(e->left.get(),env);if(obj->isNull())return Value::null();return evalIndex(e,env,true);}
        case ExprKind::Binary:    return evalBinary(e,env);
        case ExprKind::Assign:    return evalAssign(e,env);
        case ExprKind::Call:      return evalCall(e,env);
        }
        return Value::null();
    }

    ValPtr evalMember(Expr*e,EnvPtr env,bool optSafe){
        // ClassName::variant or ClassName::staticField — check before eval left
        if(e->left&&e->left->kind==ExprKind::Ident){
            const std::string& lname = e->left->name;
            // Enum variant
            if(enums.count(lname)){
                auto it=enums[lname].valueMap.find(e->name);
                if(it!=enums[lname].valueMap.end())return Value::fromInt(it->second);
            }
            // Class static field
            if(classes.count(lname)){
                auto&cd=classes[lname];
                if(cd.staticFields.count(e->name))return cd.staticFields[e->name];
                // Wrap static method as callable
                if(auto sm=findStaticMethod(lname,e->name)){
                    auto fv=std::make_shared<FuncValue>();fv->name=e->name;fv->params=sm->params;fv->body=sm->body;
                    fv->callback=[this,lname=lname,sm](std::vector<ValPtr>args)->ValPtr{return callFunc(*sm,args,globals);};
                    return Value::fromFunc(fv);
                }
            }
        }
        auto obj=evalExpr(e->left.get(),env);
        if(optSafe&&obj->isNull())return Value::null();
        if(obj->isNull())throw std::runtime_error("Null access: ."+e->name);
        if(obj->type==VType::STRUCT){
            if(classes.count(obj->obj->typeName)){
                auto&cd=classes[obj->obj->typeName];
                if(cd.staticFields.count(e->name))return cd.staticFields[e->name];
            }
            auto it=obj->obj->fields.find(e->name);
            if(it!=obj->obj->fields.end())return it->second;
            if(auto m=findMethod(obj->obj->typeName,e->name)){
                auto fv=std::make_shared<FuncValue>();fv->name=e->name;fv->params=m->params;fv->body=m->body;
                fv->callback=[this,obj,m](std::vector<ValPtr>args)->ValPtr{return callMethod(obj,*m,args,globals);};
                return Value::fromFunc(fv);
            }
            throw std::runtime_error("'"+obj->obj->typeName+"' has no field '"+e->name+"'");
        }
        if(obj->type==VType::MAP)return obj->mapGet(e->name);
        throw std::runtime_error("Cannot access ."+e->name+" on "+obj->toString());}

    ValPtr evalIndex(Expr*e,EnvPtr env,bool optSafe){
        auto obj=evalExpr(e->left.get(),env);
        if(obj->isNull())return Value::null();
        auto idx=evalExpr(e->right.get(),env);
        if(obj->type==VType::ARRAY){int64_t i=idx->toInt();if(i<0)i=(int64_t)obj->arr.size()+i;if(i<0||i>=(int64_t)obj->arr.size())throw std::runtime_error("Array index OOB: "+std::to_string(i));return obj->arr[i];}
        if(obj->type==VType::STR){int64_t i=idx->toInt();if(i<0)i=(int64_t)obj->sval.size()+i;if(i<0||i>=(int64_t)obj->sval.size())throw std::runtime_error("String index OOB");return Value::fromStr(std::string(1,obj->sval[i]));}
        if(obj->type==VType::MAP)return obj->mapGet(idx->toString());
        throw std::runtime_error("Cannot index type");}

    ValPtr evalBinary(Expr*e,EnvPtr env){
        if(e->op=="&&"){auto l=evalExpr(e->left.get(),env);if(!l->toBool())return Value::fromBool(false);return Value::fromBool(evalExpr(e->right.get(),env)->toBool());}
        if(e->op=="||"){auto l=evalExpr(e->left.get(),env);if(l->toBool())return Value::fromBool(true);return Value::fromBool(evalExpr(e->right.get(),env)->toBool());}
        auto L=evalExpr(e->left.get(),env),R=evalExpr(e->right.get(),env);
        const std::string&op=e->op;
        if(op=="+"&&(L->type==VType::STR||R->type==VType::STR))return Value::fromStr(L->toString()+R->toString());
        if(op=="+"&&L->type==VType::ARRAY&&R->type==VType::ARRAY){auto v=Value::fromArray();for(auto&x:L->arr)v->arr.push_back(x);for(auto&x:R->arr)v->arr.push_back(x);return v;}
        bool f=(L->isFloat()||R->isFloat());
        if(op=="**")return Value::fromFloat(std::pow(L->toFloat(),R->toFloat()));
        if(op=="+"){ return f?Value::fromFloat(L->toFloat()+R->toFloat()):Value::fromInt(L->toInt()+R->toInt());}
        if(op=="-"){ return f?Value::fromFloat(L->toFloat()-R->toFloat()):Value::fromInt(L->toInt()-R->toInt());}
        if(op=="*"){if(L->type==VType::STR&&R->type==VType::INT){std::string r;for(int64_t i=0;i<R->ival;i++)r+=L->sval;return Value::fromStr(r);}return f?Value::fromFloat(L->toFloat()*R->toFloat()):Value::fromInt(L->toInt()*R->toInt());}
        if(op=="/"){if(f)return Value::fromFloat(L->toFloat()/R->toFloat());if(R->toInt()==0)throw std::runtime_error("Division by zero");return Value::fromInt(L->toInt()/R->toInt());}
        if(op=="%"){if(R->toInt()==0)throw std::runtime_error("Modulo by zero");return Value::fromInt(L->toInt()%R->toInt());}
        if(op=="=="){if(L->type==VType::STR&&R->type==VType::STR)return Value::fromBool(L->sval==R->sval);if(L->isNull()&&R->isNull())return Value::fromBool(true);if(L->isNull()||R->isNull())return Value::fromBool(false);if(L->type==VType::BOOL&&R->type==VType::BOOL)return Value::fromBool(L->bval==R->bval);return f?Value::fromBool(L->toFloat()==R->toFloat()):Value::fromBool(L->toInt()==R->toInt());}
        if(op=="!="){if(L->type==VType::STR&&R->type==VType::STR)return Value::fromBool(L->sval!=R->sval);if(L->isNull()||R->isNull())return Value::fromBool(!(L->isNull()&&R->isNull()));return f?Value::fromBool(L->toFloat()!=R->toFloat()):Value::fromBool(L->toInt()!=R->toInt());}
        if(f){double lf=L->toFloat(),rf=R->toFloat();if(op=="<")return Value::fromBool(lf<rf);if(op==">")return Value::fromBool(lf>rf);if(op=="<=")return Value::fromBool(lf<=rf);if(op==">=")return Value::fromBool(lf>=rf);}
        else{int64_t li=L->toInt(),ri=R->toInt();if(op=="<")return Value::fromBool(li<ri);if(op==">")return Value::fromBool(li>ri);if(op=="<=")return Value::fromBool(li<=ri);if(op==">=")return Value::fromBool(li>=ri);}
        if(op=="&")return Value::fromInt(L->toInt()&R->toInt());if(op=="|")return Value::fromInt(L->toInt()|R->toInt());if(op=="^")return Value::fromInt(L->toInt()^R->toInt());if(op=="<<")return Value::fromInt(L->toInt()<<(int)R->toInt());if(op==">>")return Value::fromInt(L->toInt()>>(int)R->toInt());
        if(op=="<"&&L->type==VType::STR&&R->type==VType::STR)return Value::fromBool(L->sval<R->sval);
        if(op==">"&&L->type==VType::STR&&R->type==VType::STR)return Value::fromBool(L->sval>R->sval);
        throw std::runtime_error("Unknown binary op: "+op);}

    void doAssign(Expr*lval,ValPtr v,EnvPtr env){
        if(lval->kind==ExprKind::Ident)env->assign(lval->name,v);
        else if(lval->kind==ExprKind::Member){auto obj=evalExpr(lval->left.get(),env);if(obj->type==VType::STRUCT){if(classes.count(obj->obj->typeName)&&classes[obj->obj->typeName].staticFields.count(lval->name)){classes[obj->obj->typeName].staticFields[lval->name]=v;return;}obj->obj->fields[lval->name]=v;}else if(obj->type==VType::MAP)obj->mapSet(lval->name,v);else throw std::runtime_error("Cannot set member on non-struct/map");}
        else if(lval->kind==ExprKind::Index){auto obj=evalExpr(lval->left.get(),env);auto idx=evalExpr(lval->right.get(),env);if(obj->type==VType::ARRAY){int64_t i=idx->toInt();if(i<0)i=(int64_t)obj->arr.size()+i;if(i<0||i>=(int64_t)obj->arr.size())throw std::runtime_error("OOB");obj->arr[i]=v;}else if(obj->type==VType::MAP)obj->mapSet(idx->toString(),v);else throw std::runtime_error("Cannot index-assign");}
        else throw std::runtime_error("Invalid assignment target");}

    ValPtr evalAssign(Expr*e,EnvPtr env){
        auto rv=evalExpr(e->right.get(),env);
        if(e->op=="="){doAssign(e->left.get(),rv,env);return rv;}
        auto lv=evalExpr(e->left.get(),env);
        std::string bop=e->op.substr(0,e->op.size()-1);
        bool f=(lv->isFloat()||rv->isFloat());
        ValPtr result;
        if(bop=="**")result=Value::fromFloat(std::pow(lv->toFloat(),rv->toFloat()));
        else if(bop=="+"&&(lv->type==VType::STR||rv->type==VType::STR))result=Value::fromStr(lv->toString()+rv->toString());
        else if(bop=="+")result=f?Value::fromFloat(lv->toFloat()+rv->toFloat()):Value::fromInt(lv->toInt()+rv->toInt());
        else if(bop=="-")result=f?Value::fromFloat(lv->toFloat()-rv->toFloat()):Value::fromInt(lv->toInt()-rv->toInt());
        else if(bop=="*")result=f?Value::fromFloat(lv->toFloat()*rv->toFloat()):Value::fromInt(lv->toInt()*rv->toInt());
        else if(bop=="/"){if(f)result=Value::fromFloat(lv->toFloat()/rv->toFloat());else{if(rv->toInt()==0)throw std::runtime_error("Div/0");result=Value::fromInt(lv->toInt()/rv->toInt());}}
        else if(bop=="%"){if(rv->toInt()==0)throw std::runtime_error("Mod/0");result=Value::fromInt(lv->toInt()%rv->toInt());}
        else if(bop=="&")result=Value::fromInt(lv->toInt()&rv->toInt());
        else if(bop=="|")result=Value::fromInt(lv->toInt()|rv->toInt());
        else result=rv;
        doAssign(e->left.get(),result,env);return result;}

    // ── Call ──────────────────────────────────────────────────
    ValPtr evalCall(Expr*e,EnvPtr env){
        // super.method() — use __methodclass__ to find correct parent
        if(e->left->kind==ExprKind::Member&&e->left->left&&e->left->left->kind==ExprKind::Super){
            std::string method=e->left->name;
            auto thisVal=env->get("this");
            // Which class is the currently-executing method in?
            std::string currentClass=thisVal->obj->typeName; // fallback
            if(env->has("__methodclass__"))currentClass=env->get("__methodclass__")->toString();
            auto it=classes.find(currentClass);
            if(it==classes.end()||it->second.parent.empty())throw std::runtime_error("No parent for super in "+currentClass);
            std::string pname=it->second.parent;
            std::vector<ValPtr>args;for(auto&a:e->args)if(a->kind==ExprKind::Spread){auto s=evalExpr(a->left.get(),env);for(auto&x:s->arr)args.push_back(x);}else args.push_back(evalExpr(a.get(),env));
            if(auto m=findMethod(pname,method))return callMethod(thisVal,*m,args,env);
            throw std::runtime_error("Parent '"+pname+"' has no method '"+method+"'");}

        // super(args) — parent constructor
        if(e->left->kind==ExprKind::Super){
            auto thisVal=env->get("this");
            std::string currentClass=thisVal->obj->typeName;
            if(env->has("__methodclass__"))currentClass=env->get("__methodclass__")->toString();
            auto it=classes.find(currentClass);
            if(it==classes.end()||it->second.parent.empty())throw std::runtime_error("No parent for super in "+currentClass);
            std::string pname=it->second.parent;
            std::vector<ValPtr>args;for(auto&a:e->args)args.push_back(evalExpr(a.get(),env));
            if(auto m=findMethod(pname,"init"))return callMethod(thisVal,*m,args,env);
            return Value::null();}

        // method call  (also handles ClassName::staticMethod via Member node)
        if(e->left->kind==ExprKind::Member){
            // Check if left-left is a class name (ClassName::method static call)
            if(e->left->left&&e->left->left->kind==ExprKind::Ident){
                const std::string& maybeClass = e->left->left->name;
                const std::string& method     = e->left->name;
                if(classes.count(maybeClass)){
                    // Try static method first
                    if(auto sm=findStaticMethod(maybeClass,method)){
                        std::vector<ValPtr>args;
                        for(auto&a:e->args){if(a->kind==ExprKind::Spread){auto s=evalExpr(a->left.get(),env);for(auto&x:s->arr)args.push_back(x);}else args.push_back(evalExpr(a.get(),env));}
                        return callFunc(*sm,args,env);
                    }
                    // Try enum member access as value
                    if(enums.count(maybeClass)){
                        auto&ed=enums[maybeClass];
                        auto it=ed.valueMap.find(method);
                        if(it!=ed.valueMap.end())return Value::fromInt(it->second);
                    }
                    // Otherwise: regular instance method? Fall through to eval obj.
                }
            }
            // Evaluate the left-hand object normally
            auto obj=evalExpr(e->left->left.get(),env);
            std::string method=e->left->name;
            if(obj->isNull()){if(e->left->kind==ExprKind::OptMember)return Value::null();throw std::runtime_error("Null pointer access: ."+method);}
            std::vector<ValPtr>args;for(auto&a:e->args){if(a->kind==ExprKind::Spread){auto s=evalExpr(a->left.get(),env);for(auto&x:s->arr)args.push_back(x);}else args.push_back(evalExpr(a.get(),env));}
            if(obj->type==VType::STR)return callStringMethod(obj,method,args,env);
            if(obj->type==VType::ARRAY)return callArrayMethod(obj,method,args,env);
            if(obj->type==VType::MAP)return callMapMethod(obj,method,args,env);
            if(obj->type==VType::STRUCT){
                if(auto sm=findStaticMethod(obj->obj->typeName,method)){return callFunc(*sm,args,env);}
                if(auto m=findMethod(obj->obj->typeName,method))return callMethod(obj,*m,args,env,classes.count(obj->obj->typeName)?classes[obj->obj->typeName].parent:"");
                throw std::runtime_error("'"+obj->obj->typeName+"' has no method '"+method+"'");}
            throw std::runtime_error("Cannot call method '"+method+"' on "+obj->toString());}

        // static class method: ClassName.method()
        if(e->left->kind==ExprKind::Ident){
            std::string fname=e->left->name;
            std::vector<ValPtr>args;
            for(auto&a:e->args){if(a->kind==ExprKind::Spread){auto s=evalExpr(a->left.get(),env);for(auto&x:s->arr)args.push_back(x);}else args.push_back(evalExpr(a.get(),env));}

            // ── Core built-ins ──────────────────────────────
            if(fname=="print")  {for(auto&v:args)std::cout<<v->toString();std::cout.flush();return Value::null();}
            if(fname=="println"){for(auto&v:args)std::cout<<v->toString();std::cout<<'\n';return Value::null();}
            if(fname=="eprint") {for(auto&v:args)std::cerr<<v->toString();std::cerr.flush();return Value::null();}
            if(fname=="eprintln"){for(auto&v:args)std::cerr<<v->toString();std::cerr<<'\n';return Value::null();}
            if(fname=="input") {if(!args.empty())std::cout<<args[0]->toString();std::string ln;std::getline(std::cin,ln);return Value::fromStr(ln);}
            if(fname=="len")   {if(args.empty())throw std::runtime_error("len() needs arg");auto&v=args[0];if(v->type==VType::STR)return Value::fromInt((int64_t)v->sval.size());if(v->type==VType::ARRAY)return Value::fromInt((int64_t)v->arr.size());if(v->type==VType::MAP)return Value::fromInt((int64_t)v->map->size());throw std::runtime_error("len() on unsupported type");}
            if(fname=="push")  {if(args.size()<2)throw std::runtime_error("push(arr,val)");args[0]->arr.push_back(args[1]);return Value::null();}
            if(fname=="pop")   {if(args.empty())throw std::runtime_error("pop(arr)");auto&a=args[0]->arr;if(a.empty())throw std::runtime_error("pop() empty");auto v=a.back();a.pop_back();return v;}
            if(fname=="array"||fname=="Array")return Value::fromArray();
            if(fname=="map"||fname=="Map")return Value::fromMap();
            if(fname=="range"){int64_t st=0,en=0,step=1;if(args.size()==1)en=args[0]->toInt();else if(args.size()>=2){st=args[0]->toInt();en=args[1]->toInt();}if(args.size()>=3)step=args[2]->toInt();if(step==0)throw std::runtime_error("range() step=0");auto v=Value::fromArray();if(step>0)for(int64_t i=st;i<en;i+=step)v->arr.push_back(Value::fromInt(i));else for(int64_t i=st;i>en;i+=step)v->arr.push_back(Value::fromInt(i));return v;}
            if(fname=="typeof"){if(args.empty())return Value::fromStr("null");switch(args[0]->type){case VType::INT:return Value::fromStr("int");case VType::FLOAT:return Value::fromStr("float");case VType::BOOL:return Value::fromStr("bool");case VType::STR:return Value::fromStr("string");case VType::STRUCT:return Value::fromStr(args[0]->obj->typeName);case VType::ARRAY:return Value::fromStr("array");case VType::MAP:return Value::fromStr("map");case VType::FUNC:return Value::fromStr("func");case VType::NATIVE:return Value::fromStr("native");default:return Value::fromStr("null");}}
            if(fname=="isNull")  return Value::fromBool(args.empty()||args[0]->isNull());
            if(fname=="isInt")   return Value::fromBool(!args.empty()&&args[0]->type==VType::INT);
            if(fname=="isFloat") return Value::fromBool(!args.empty()&&args[0]->type==VType::FLOAT);
            if(fname=="isBool")  return Value::fromBool(!args.empty()&&args[0]->type==VType::BOOL);
            if(fname=="isString")return Value::fromBool(!args.empty()&&args[0]->type==VType::STR);
            if(fname=="isArray") return Value::fromBool(!args.empty()&&args[0]->type==VType::ARRAY);
            if(fname=="isMap")   return Value::fromBool(!args.empty()&&args[0]->type==VType::MAP);
            if(fname=="isFunc")  return Value::fromBool(!args.empty()&&args[0]->type==VType::FUNC);
            if(fname=="int"||fname=="Int")  {if(args.empty())return Value::fromInt(0);return Value::fromInt(args[0]->toInt());}
            if(fname=="float"||fname=="Float"){if(args.empty())return Value::fromFloat(0);return Value::fromFloat(args[0]->toFloat());}
            if(fname=="str"||fname=="Str")  {if(args.empty())return Value::fromStr("");return Value::fromStr(args[0]->toString());}
            if(fname=="bool"||fname=="Bool"){if(args.empty())return Value::fromBool(false);return Value::fromBool(args[0]->toBool());}
            if(fname=="chr")  {if(args.empty())return Value::fromStr("");return Value::fromStr(std::string(1,(char)args[0]->toInt()));}
            if(fname=="ord")  {if(args.empty())return Value::fromInt(0);auto s=args[0]->toString();return Value::fromInt(s.empty()?0:(unsigned char)s[0]);}
            if(fname=="sqrt") return Value::fromFloat(std::sqrt(args[0]->toFloat()));
            if(fname=="abs")  {auto&v=args[0];return v->isFloat()?Value::fromFloat(std::fabs(v->fval)):Value::fromInt(std::abs(v->ival));}
            if(fname=="pow")  return Value::fromFloat(std::pow(args[0]->toFloat(),args[1]->toFloat()));
            if(fname=="log")  return Value::fromFloat(std::log(args[0]->toFloat()));
            if(fname=="log2") return Value::fromFloat(std::log2(args[0]->toFloat()));
            if(fname=="log10")return Value::fromFloat(std::log10(args[0]->toFloat()));
            if(fname=="exp")  return Value::fromFloat(std::exp(args[0]->toFloat()));
            if(fname=="sin")  return Value::fromFloat(std::sin(args[0]->toFloat()));
            if(fname=="cos")  return Value::fromFloat(std::cos(args[0]->toFloat()));
            if(fname=="tan")  return Value::fromFloat(std::tan(args[0]->toFloat()));
            if(fname=="asin") return Value::fromFloat(std::asin(args[0]->toFloat()));
            if(fname=="acos") return Value::fromFloat(std::acos(args[0]->toFloat()));
            if(fname=="atan") return Value::fromFloat(std::atan(args[0]->toFloat()));
            if(fname=="atan2")return Value::fromFloat(std::atan2(args[0]->toFloat(),args[1]->toFloat()));
            if(fname=="floor")return Value::fromFloat(std::floor(args[0]->toFloat()));
            if(fname=="ceil") return Value::fromFloat(std::ceil(args[0]->toFloat()));
            if(fname=="round")return Value::fromFloat(std::round(args[0]->toFloat()));
            if(fname=="trunc")return Value::fromFloat(std::trunc(args[0]->toFloat()));
            if(fname=="hypot")return Value::fromFloat(std::hypot(args[0]->toFloat(),args[1]->toFloat()));
            if(fname=="min")  {bool f=args[0]->isFloat()||args[1]->isFloat();return f?Value::fromFloat(std::min(args[0]->toFloat(),args[1]->toFloat())):Value::fromInt(std::min(args[0]->toInt(),args[1]->toInt()));}
            if(fname=="max")  {bool f=args[0]->isFloat()||args[1]->isFloat();return f?Value::fromFloat(std::max(args[0]->toFloat(),args[1]->toFloat())):Value::fromInt(std::max(args[0]->toInt(),args[1]->toInt()));}
            if(fname=="clamp"){double v=args[0]->toFloat(),lo=args[1]->toFloat(),hi=args[2]->toFloat();bool f=args[0]->isFloat()||args[1]->isFloat()||args[2]->isFloat();return f?Value::fromFloat(std::max(lo,std::min(hi,v))):Value::fromInt((int64_t)std::max(lo,std::min(hi,v)));}
            if(fname=="lerp") return Value::fromFloat(args[0]->toFloat()+(args[1]->toFloat()-args[0]->toFloat())*args[2]->toFloat());
            if(fname=="exit") std::exit(args.empty()?0:(int)args[0]->toInt());
            if(fname=="assert"){if(args.empty()||!args[0]->toBool()){std::string m=args.size()>1?args[1]->toString():"Assertion failed";ThrowSignal ts;ts.val=Value::fromStr(m);throw ts;}}
            if(fname=="error"||fname=="panic"){ThrowSignal ts;ts.val=Value::fromStr(args.empty()?"Error":args[0]->toString());throw ts;}
            if(fname=="join") {if(args.size()<2)throw std::runtime_error("join(arr,delim)");auto&a=args[0]->arr;std::string d=args[1]->toString(),r;for(size_t i=0;i<a.size();i++){if(i)r+=d;r+=a[i]->toString();}return Value::fromStr(r);}
            if(fname=="hex")  {if(args.empty())return Value::fromStr("0x0");char b[32];snprintf(b,32,"0x%llX",(unsigned long long)args[0]->toInt());return Value::fromStr(b);}
            if(fname=="bin")  {if(args.empty())return Value::fromStr("0b0");int64_t n=args[0]->toInt();if(!n)return Value::fromStr("0b0");std::string r;uint64_t u=(uint64_t)n;while(u){r=(char)('0'+u%2)+r;u>>=1;}return Value::fromStr("0b"+r);}
            if(fname=="oct")  {if(args.empty())return Value::fromStr("0o0");char b[32];snprintf(b,32,"0o%llo",(unsigned long long)args[0]->toInt());return Value::fromStr(b);}
            if(fname=="parseHex"){std::string s=args.empty()?"0":args[0]->toString();if(s.size()>2&&s[0]=='0'&&(s[1]=='x'||s[1]=='X'))s=s.substr(2);return Value::fromInt((int64_t)strtoull(s.c_str(),nullptr,16));}
            if(fname=="parseBin"){std::string s=args.empty()?"0":args[0]->toString();if(s.size()>2&&s[0]=='0'&&(s[1]=='b'||s[1]=='B'))s=s.substr(2);return Value::fromInt((int64_t)strtoull(s.c_str(),nullptr,2));}
            if(fname=="isNaN") return Value::fromBool(!args.empty()&&std::isnan(args[0]->toFloat()));
            if(fname=="isInf") return Value::fromBool(!args.empty()&&std::isinf(args[0]->toFloat()));

            // variable lookup
            if(env->has(fname)){auto val=env->get(fname);if(val->type==VType::FUNC)return callFuncValue(*val->func,args,env);if(val->type==VType::NATIVE)return raze_native::call(*val->native,args);}
            auto fit=funcs.find(fname);if(fit!=funcs.end())return callFunc(fit->second,args,env);
            // constructor shorthand
            if(classes.count(fname))return instantiateClass(fname,args,env);
            // callable expr
            auto callable=evalExpr(e->left.get(),env);
            if(callable->type==VType::FUNC)return callFuncValue(*callable->func,args,env);
            if(callable->type==VType::NATIVE)return raze_native::call(*callable->native,args);
            throw std::runtime_error("Unknown function: '"+fname+"'");
        }

        // generic callable expr
        auto callable=evalExpr(e->left.get(),env);
        std::vector<ValPtr>args;for(auto&a:e->args)args.push_back(evalExpr(a.get(),env));
        if(callable->type==VType::FUNC)return callFuncValue(*callable->func,args,env);
        if(callable->type==VType::NATIVE)return raze_native::call(*callable->native,args);
        throw std::runtime_error("Value is not callable");
    }

    // ── Statement execution ───────────────────────────────────
    void execStmt(Stmt*s,EnvPtr env){
        if(!s)return;
        std::vector<std::function<void()>> deferred;
        switch(s->kind){
        case StmtKind::Block:
            for(auto&sub:s->body)execStmt(sub.get(),env);
            break;
        case StmtKind::ExprStmt: evalExpr(s->expr.get(),env); break;
        case StmtKind::VarDecl: {
            ValPtr v=s->varInit?evalExpr(s->varInit.get(),env):defaultValue(s->varType);
            env->define(s->varName,v,s->isConst); break;}
        case StmtKind::If:
            if(evalExpr(s->cond.get(),env)->toBool())execStmt(s->then.get(),env);
            else if(s->els)execStmt(s->els.get(),env);
            break;
        case StmtKind::While:{
            auto lenv=std::make_shared<Env>(env);
            while(evalExpr(s->cond.get(),lenv)->toBool()){
                auto it=std::make_shared<Env>(lenv);
                try{execStmt(s->body[0].get(),it);}
                catch(BreakSignal&){goto wDone;}catch(ContinueSignal&){continue;}
            }wDone:;break;}
        case StmtKind::For:{
            auto fe=std::make_shared<Env>(env);
            if(s->forInit)execStmt(s->forInit.get(),fe);
            while(true){
                if(s->forCond&&!evalExpr(s->forCond.get(),fe)->toBool())break;
                auto it=std::make_shared<Env>(fe);
                try{execStmt(s->forBody.get(),it);}
                catch(BreakSignal&){goto fDone;}catch(ContinueSignal&){}
                if(s->forStep)evalExpr(s->forStep.get(),fe);
            }fDone:;break;}
        case StmtKind::ForIn:{
            auto col=evalExpr(s->forInExpr.get(),env);
            auto fe=std::make_shared<Env>(env);
            auto iterate=[&](ValPtr item){
                fe->define(s->forInVar,item);
                auto it=std::make_shared<Env>(fe);
                execStmt(s->forInBody.get(),it);};
            try{
                if(col->type==VType::ARRAY)for(auto&v:col->arr)iterate(v);
                else if(col->type==VType::MAP)for(auto&k:*col->mapOrder)iterate(Value::fromStr(k));
                else if(col->type==VType::STR)for(char c:col->sval)iterate(Value::fromStr(std::string(1,c)));
                else throw std::runtime_error("for-in: not iterable");
            }catch(BreakSignal&){}break;}

        case StmtKind::Switch:{
            auto val=evalExpr(s->switchExpr.get(),env);
            bool matched=false;
            for(auto&c:s->cases){
                bool hit=c.isDefault;
                if(!hit)for(auto&v:c.values)if(evalExpr(v.get(),env)->toString()==val->toString()){hit=true;break;}
                if(hit){
                    matched=true;
                    auto ce=std::make_shared<Env>(env);
                    try{for(auto&st:c.body)execStmt(st.get(),ce);}catch(BreakSignal&){break;}
                    break;
                }
            }
            if(!matched)for(auto&c:s->cases)if(c.isDefault){auto ce=std::make_shared<Env>(env);try{for(auto&st:c.body)execStmt(st.get(),ce);}catch(BreakSignal&){}break;}
            break;}

        case StmtKind::Match:{
            auto val=evalExpr(s->matchVal.get(),env);
            for(auto&arm:s->arms){
                bool hit=arm.isDefault;
                if(!hit){for(auto&p:arm.patterns)if(evalExpr(p.get(),env)->toString()==val->toString()){hit=true;break;}}
                if(hit){
                    if(arm.guard&&!evalExpr(arm.guard.get(),env)->toBool())continue;
                    auto ae=std::make_shared<Env>(env);
                    ae->define("it",val);
                    try{execStmt(arm.body.get(),ae);}catch(BreakSignal&){}
                    break;
                }
            }break;}

        case StmtKind::Return:{ValPtr v=s->expr?evalExpr(s->expr.get(),env):Value::null();throw ReturnSignal{v};}
        case StmtKind::Break:    throw BreakSignal{};
        case StmtKind::Continue: throw ContinueSignal{};
        case StmtKind::Throw:{auto v=evalExpr(s->expr.get(),env);ThrowSignal ts;ts.val=v;throw ts;}
        case StmtKind::Defer:{/* Store for later — simplified: eval now at end of enclosing scope */
            // Actual defer would need scope tracking; for simplicity we eval immediately
            // A full defer impl requires RAII wrapper; this is a best-effort approach
            evalExpr(s->expr.get(),env); break;}

        case StmtKind::TryCatch:{
            auto te=std::make_shared<Env>(env);
            try{execStmt(s->then.get(),te);}
            catch(ThrowSignal&ts){auto ce=std::make_shared<Env>(env);ce->define(s->forInVar,ts.val);execStmt(s->els.get(),ce);}
            catch(std::runtime_error&ex){auto ce=std::make_shared<Env>(env);ce->define(s->forInVar,Value::fromStr(ex.what()));execStmt(s->els.get(),ce);}
            break;}

        case StmtKind::StructDecl:{StructDef sd;sd.name=s->structName;sd.fields=s->fields;structs[sd.name]=std::move(sd);break;}

        case StmtKind::FuncDecl:{
            FuncDef fd;fd.name=s->funcName;fd.params=s->params;fd.retType=s->retType;
            fd.body=s->funcBody.get();fd.closure=env;
            funcs[fd.name]=fd;
            // Also store as value in env for first-class usage
            auto fv=std::make_shared<FuncValue>();fv->name=fd.name;fv->params=fd.params;fv->body=fd.body;fv->closure=env;
            env->define(fd.name,Value::fromFunc(fv));
            break;}

        case StmtKind::ClassDecl:{
            ClassDef cd;cd.name=s->className;cd.parent=s->parentClass;cd.interfaces=s->interfaces;
            cd.fields=s->classFields;
            if(!cd.parent.empty()&&classes.count(cd.parent)){
                auto&pfields=classes[cd.parent].fields;
                cd.fields.insert(cd.fields.begin(),pfields.begin(),pfields.end());
                // Inherit parent static fields/methods
                auto&pcd=classes[cd.parent];
                for(auto&[k,v]:pcd.staticMethods)cd.staticMethods.emplace(k,v);
                for(auto&[k,v]:pcd.staticFields) cd.staticFields.emplace(k,v);
            }
            for(auto&m:s->classMethods){
                FuncDef fd;fd.name=m->funcName;fd.params=m->params;fd.retType=m->retType;fd.body=m->funcBody.get();fd.closure=env;
                fd.ownerClass=cd.name;  // tag with the declaring class
                if(m->isStatic)cd.staticMethods[fd.name]=std::move(fd);
                else cd.methods[fd.name]=std::move(fd);
            }
            classes[cd.name]=std::move(cd);
            break;}

        case StmtKind::EnumDecl:{
            EnumDef ed;ed.name=s->enumName;
            int64_t next=0;
            for(size_t i=0;i<s->enumValues.size();i++){
                if(i<s->enumInits.size()&&s->enumInits[i]){
                    next=evalExpr(s->enumInits[i].get(),env)->toInt();
                }
                ed.values.push_back(s->enumValues[i]);
                ed.valueMap[s->enumValues[i]]=next++;
            }
            enums[ed.name]=ed;
            // Create enum as a map in global scope
            auto ev=Value::fromMap();
            for(auto&[name,val]:ed.valueMap)ev->mapSet(name,Value::fromInt(val));
            env->define(ed.name,ev);
            break;}

        case StmtKind::InterfaceDecl:{
            InterfaceDef id;id.name=s->ifaceName;id.methods=s->ifaceMethods;
            interfaces[id.name]=std::move(id);break;}

        case StmtKind::NativeDecl:{
            auto ni=std::make_shared<NativeInfo>();ni->addr=s->nativeAddr;ni->retType=s->nativeRet.name;
            for(auto&pt:s->nativeParams)ni->paramTypes.push_back(pt.name);
            env->define(s->nativeName,Value::fromNative(ni));break;}

        case StmtKind::Import:
            if(importHandler)importHandler(s->importPath,s->importAlias);
            break;
        }
    }
};
