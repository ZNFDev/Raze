#pragma once
#include "value.hpp"
#include "ast.hpp"
#include <deque>
#include <cmath>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <functional>
#include <unordered_map>

// ── Control flow signals ──────────────────────────────────────
struct ReturnSignal  { ValPtr val; };
struct BreakSignal   {};
struct ContinueSignal{};
struct ThrowSignal   { ValPtr val; };

// ── Native call dispatcher ────────────────────────────────────
namespace raze_native {
union Slot { int64_t i; double d; };

inline Slot pack(const ValPtr& v,const std::string& t){
    Slot s{};
    if(t=="float"||t=="double")s.d=v->toFloat();
    else s.i=v->toInt();
    return s;
}

inline ValPtr call(const NativeInfo& ni,const std::vector<ValPtr>& args){
    if(ni.callback)return ni.callback(args);
    if(!ni.addr) throw std::runtime_error("Native function has no address");
    size_t n=std::min(args.size(),ni.paramTypes.size());
    Slot slots[8]={};
    for(size_t i=0;i<n&&i<8;i++)
        slots[i]=pack(args[i],i<ni.paramTypes.size()?ni.paramTypes[i]:"int");
    bool retF=(ni.retType=="float"||ni.retType=="double");
    bool anyF=false;
    for(size_t i=0;i<n;i++)if(i<ni.paramTypes.size()&&(ni.paramTypes[i]=="float"||ni.paramTypes[i]=="double"))anyF=true;
    auto callI=[&]()->int64_t{
        typedef int64_t(*F8)(int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t);
        return((F8)ni.addr)(slots[0].i,slots[1].i,slots[2].i,slots[3].i,slots[4].i,slots[5].i,slots[6].i,slots[7].i);
    };
    auto callF=[&]()->double{
        typedef double(*F8)(double,double,double,double,double,double,double,double);
        return((F8)ni.addr)(slots[0].d,slots[1].d,slots[2].d,slots[3].d,slots[4].d,slots[5].d,slots[6].d,slots[7].d);
    };
    if(retF)return Value::fromFloat(anyF?callF():(double)callI());
    int64_t r=anyF?(int64_t)callF():callI();
    if(ni.retType=="bool")return Value::fromBool(r!=0);
    if(ni.retType=="void")return Value::null();
    return Value::fromInt(r);
}
} // namespace raze_native

// ── Interpreter ───────────────────────────────────────────────
class Interpreter {
public:
    EnvPtr  globals;
    std::unordered_map<std::string, FuncDef>   funcs;
    std::unordered_map<std::string, ClassDef>  classes;
    std::unordered_map<std::string, StructDef> structs;
    std::deque<Program> programs; // MUST be deque — vector realloc invalidates Stmt* borrows
    std::function<void(const std::string&)> importHandler; // set by main

    Interpreter(){ globals=std::make_shared<Env>(); }

    // ── Host registration API ─────────────────────────────────
    void registerNative(const std::string& name,std::function<ValPtr(std::vector<ValPtr>)> fn,uintptr_t addr=0){
        auto ni=std::make_shared<NativeInfo>(); ni->callback=fn; ni->addr=addr;
        globals->define(name,Value::fromNative(ni));
    }
    void registerAddr(const std::string& name,uintptr_t addr,std::string ret="void",std::vector<std::string> pt={}){
        auto ni=std::make_shared<NativeInfo>(); ni->addr=addr; ni->retType=ret; ni->paramTypes=pt;
        globals->define(name,Value::fromNative(ni));
    }

    // ── Run a program ─────────────────────────────────────────
    void run(Program prog){
        programs.push_back(std::move(prog));
        Program& p=programs.back();
        // Pass 1: register all top-level declarations
        for(auto& s:p.stmts)
            if(s->kind==StmtKind::StructDecl||s->kind==StmtKind::FuncDecl||
               s->kind==StmtKind::ClassDecl||s->kind==StmtKind::NativeDecl||
               s->kind==StmtKind::Import)
                execStmt(s.get(),globals);
        // Pass 2: run executable statements, then call main()
        bool hasMain=funcs.count("main")>0;
        for(auto& s:p.stmts)
            if(s->kind!=StmtKind::StructDecl&&s->kind!=StmtKind::FuncDecl&&
               s->kind!=StmtKind::ClassDecl&&s->kind!=StmtKind::NativeDecl&&
               s->kind!=StmtKind::Import)
                execStmt(s.get(),globals);
        if(hasMain) callFunc(funcs["main"],{},globals);
    }

    // ── Public call interface ─────────────────────────────────
    ValPtr callFunc(const FuncDef& fd,const std::vector<ValPtr>& args,EnvPtr outerEnv){
        auto scope=std::make_shared<Env>(globals);
        for(size_t i=0;i<fd.params.size();i++){
            if(i<args.size())scope->define(fd.params[i].name,args[i]);
            else scope->define(fd.params[i].name,defaultValue(fd.params[i].type));
        }
        try{ execStmt(fd.body,scope); }
        catch(ReturnSignal& rs){ return rs.val?rs.val:Value::null(); }
        return Value::null();
    }

    ValPtr callFuncValue(const FuncValue& fv,const std::vector<ValPtr>& args,EnvPtr callerEnv){
        if(fv.callback)return fv.callback(args);
        auto scope=std::make_shared<Env>(fv.closure?fv.closure:globals);
        for(size_t i=0;i<fv.params.size();i++){
            if(i<args.size())scope->define(fv.params[i].name,args[i]);
            else scope->define(fv.params[i].name,defaultValue(fv.params[i].type));
        }
        try{ execStmt(fv.body,scope); }
        catch(ReturnSignal& rs){ return rs.val?rs.val:Value::null(); }
        return Value::null();
    }

    ValPtr defaultValue(const TypeNode& t){
        if(t.isArray)return Value::fromArray();
        if(t.name=="int")    return Value::fromInt(0);
        if(t.name=="float")  return Value::fromFloat(0.0);
        if(t.name=="bool")   return Value::fromBool(false);
        if(t.name=="string") return Value::fromStr("");
        if(t.name=="void"||t.name=="var")return Value::null();
        if(classes.count(t.name)) return instantiateClass(t.name,{},globals);
        if(structs.count(t.name)){
            auto sv=Value::fromStruct(t.name);
            for(auto& f:structs[t.name].fields) sv->obj->fields[f.name]=defaultValue(f.type);
            return sv;
        }
        return Value::null();
    }

    // ── Class instantiation ───────────────────────────────────
    ValPtr instantiateClass(const std::string& cname,const std::vector<ValPtr>& args,EnvPtr env){
        auto it=classes.find(cname);
        if(it==classes.end()) throw std::runtime_error("Unknown class: "+cname);
        auto inst=Value::fromStruct(cname);
        // Initialize fields from class chain (parent first)
        initClassFields(inst,cname);
        // Call init method if present
        if(auto m=findMethod(cname,"init")){
            callMethod(inst,*m,args,env);
        }
        return inst;
    }

    void initClassFields(ValPtr& inst,const std::string& cname){
        auto it=classes.find(cname);if(it==classes.end())return;
        if(!it->second.parent.empty()) initClassFields(inst,it->second.parent);
        for(auto& f:it->second.fields)
            if(!inst->obj->fields.count(f.name))
                inst->obj->fields[f.name]=defaultValue(f.type);
    }

    // Find a method in class chain
    FuncDef* findMethod(const std::string& cname,const std::string& mname){
        auto it=classes.find(cname);if(it==classes.end())return nullptr;
        auto mit=it->second.methods.find(mname);
        if(mit!=it->second.methods.end())return &mit->second;
        if(!it->second.parent.empty())return findMethod(it->second.parent,mname);
        return nullptr;
    }

    ValPtr callMethod(ValPtr inst,const FuncDef& fd,const std::vector<ValPtr>& args,EnvPtr env,const std::string& parentClass=""){
        auto scope=std::make_shared<Env>(globals);
        scope->define("this",inst);
        // __superclass__ for super dispatch
        scope->define("__superclass__",Value::fromStr(parentClass.empty()?"":parentClass));
        for(size_t i=0;i<fd.params.size();i++){
            if(i<args.size())scope->define(fd.params[i].name,args[i]);
            else scope->define(fd.params[i].name,defaultValue(fd.params[i].type));
        }
        try{ execStmt(fd.body,scope); }
        catch(ReturnSignal& rs){ return rs.val?rs.val:Value::null(); }
        return Value::null();
    }

    // ── Expression evaluation ─────────────────────────────────
    ValPtr evalExpr(Expr* e,EnvPtr env){
        if(!e)return Value::null();
        switch(e->kind){
        case ExprKind::IntLit:   return Value::fromInt(e->ival);
        case ExprKind::FloatLit: return Value::fromFloat(e->fval);
        case ExprKind::BoolLit:  return Value::fromBool(e->bval);
        case ExprKind::StrLit:   return Value::fromStr(e->sval);
        case ExprKind::NullLit:  return Value::null();

        case ExprKind::Ident:    return env->get(e->name);
        case ExprKind::Super: {
            // Resolve super → current class's parent
            // We grab 'this' and look up the class chain
            auto thisVal=env->get("this");
            auto cit=classes.find(thisVal->obj->typeName);
            if(cit==classes.end()||cit->second.parent.empty())
                throw std::runtime_error("No parent class for super");
            auto sv=Value::fromStr("__super__:"+cit->second.parent);
            return sv;
        }

        case ExprKind::ArrayLit: {
            auto v=Value::fromArray();
            for(auto& a:e->args)v->arr.push_back(evalExpr(a.get(),env));
            return v;
        }
        case ExprKind::MapLit: {
            auto v=Value::fromMap();
            for(size_t i=0;i<e->mapKeys.size();i++){
                std::string k=evalExpr(e->mapKeys[i].get(),env)->toString();
                v->mapSet(k,evalExpr(e->args[i].get(),env));
            }
            return v;
        }
        case ExprKind::Lambda: {
            auto fv=std::make_shared<FuncValue>();
            fv->params=e->funcParams;fv->retType=e->funcRetType;
            fv->body=e->lambdaBody.get();fv->closure=env;
            return Value::fromFunc(fv);
        }
        case ExprKind::Cast: {
            auto v=evalExpr(e->left.get(),env);
            const std::string& t=e->castType.name;
            if(t=="int")    return Value::fromInt(v->toInt());
            if(t=="float")  return Value::fromFloat(v->toFloat());
            if(t=="bool")   return Value::fromBool(v->toBool());
            if(t=="string") return Value::fromStr(v->toString());
            return v;
        }
        case ExprKind::New: {
            auto& n=e->castType.name;
            std::vector<ValPtr> args;
            for(auto& a:e->args)args.push_back(evalExpr(a.get(),env));
            if(classes.count(n)) return instantiateClass(n,args,env);
            if(structs.count(n)){
                auto sv=Value::fromStruct(n);
                for(auto& f:structs[n].fields)sv->obj->fields[f.name]=defaultValue(f.type);
                return sv;
            }
            throw std::runtime_error("Unknown type for 'new': "+n);
        }
        case ExprKind::Unary: {
            auto v=evalExpr(e->left.get(),env);
            if(e->op=="-"){if(v->type==VType::INT)return Value::fromInt(-v->ival);return Value::fromFloat(-v->toFloat());}
            if(e->op=="!")return Value::fromBool(!v->toBool());
            if(e->op=="~")return Value::fromInt(~v->toInt());
            throw std::runtime_error("Unknown unary op: "+e->op);
        }
        case ExprKind::PreInc:
        case ExprKind::PostInc: {
            bool post=(e->kind==ExprKind::PostInc);
            auto old=evalExpr(e->left.get(),env);
            ValPtr nv;
            if(old->type==VType::FLOAT)nv=Value::fromFloat(old->fval+(e->incDir?1.0:-1.0));
            else nv=Value::fromInt(old->ival+(e->incDir?1:-1));
            doAssign(e->left.get(),nv,env);
            return post?old:nv;
        }
        case ExprKind::Ternary: {
            if(evalExpr(e->left.get(),env)->toBool())return evalExpr(e->right.get(),env);
            return evalExpr(e->extra.get(),env);
        }
        case ExprKind::NullCoal: {
            auto l=evalExpr(e->left.get(),env);
            if(!l->isNull())return l;
            return evalExpr(e->right.get(),env);
        }
        case ExprKind::Binary:   return evalBinary(e,env);
        case ExprKind::Assign:   return evalAssign(e,env);
        case ExprKind::Member:   return evalMember(e,env);
        case ExprKind::Index:    return evalIndex(e,env);
        case ExprKind::Call:     return evalCall(e,env);
        }
        return Value::null();
    }

    ValPtr evalMember(Expr* e,EnvPtr env){
        auto obj=evalExpr(e->left.get(),env);
        if(obj->type==VType::STRUCT){
            auto it=obj->obj->fields.find(e->name);
            if(it!=obj->obj->fields.end())return it->second;
            // Check if it's a method (will be called via Call)
            // Return a bound-method marker (string) — actual call handled in evalCall
            throw std::runtime_error("'"+obj->obj->typeName+"' has no field '"+e->name+"'");
        }
        if(obj->type==VType::MAP){
            return obj->mapGet(e->name);
        }
        throw std::runtime_error("Cannot access member '"+e->name+"' on "+obj->toString());
    }

    ValPtr evalIndex(Expr* e,EnvPtr env){
        auto obj=evalExpr(e->left.get(),env);
        auto idx=evalExpr(e->right.get(),env);
        if(obj->type==VType::ARRAY){
            int64_t i=idx->toInt();
            if(i<0)i=(int64_t)obj->arr.size()+i;
            if(i<0||i>=(int64_t)obj->arr.size())throw std::runtime_error("Array index out of bounds: "+std::to_string(i));
            return obj->arr[i];
        }
        if(obj->type==VType::STR){
            int64_t i=idx->toInt();
            if(i<0)i=(int64_t)obj->sval.size()+i;
            if(i<0||i>=(int64_t)obj->sval.size())throw std::runtime_error("String index out of bounds");
            // ✅ FIX: return 1-char string, not integer ASCII
            return Value::fromStr(std::string(1,(char)obj->sval[i]));
        }
        if(obj->type==VType::MAP){
            return obj->mapGet(idx->toString());
        }
        throw std::runtime_error("Cannot index type");
    }

    ValPtr evalBinary(Expr* e,EnvPtr env){
        if(e->op=="&&"){auto l=evalExpr(e->left.get(),env);if(!l->toBool())return Value::fromBool(false);return Value::fromBool(evalExpr(e->right.get(),env)->toBool());}
        if(e->op=="||"){auto l=evalExpr(e->left.get(),env);if(l->toBool())return Value::fromBool(true);return Value::fromBool(evalExpr(e->right.get(),env)->toBool());}
        auto L=evalExpr(e->left.get(),env);
        auto R=evalExpr(e->right.get(),env);
        const std::string& op=e->op;
        // String concat
        if(op=="+"&&(L->type==VType::STR||R->type==VType::STR))return Value::fromStr(L->toString()+R->toString());
        // Array concat
        if(op=="+"&&L->type==VType::ARRAY&&R->type==VType::ARRAY){
            auto v=Value::fromArray();for(auto& x:L->arr)v->arr.push_back(x);for(auto& x:R->arr)v->arr.push_back(x);return v;
        }
        bool useF=(L->isFloat()||R->isFloat());
        if(op=="+"){ return useF?Value::fromFloat(L->toFloat()+R->toFloat()):Value::fromInt(L->toInt()+R->toInt()); }
        if(op=="-"){ return useF?Value::fromFloat(L->toFloat()-R->toFloat()):Value::fromInt(L->toInt()-R->toInt()); }
        if(op=="*"){ 
            // String repeat: "abc" * 3
            if(L->type==VType::STR&&R->type==VType::INT){std::string r;for(int64_t i=0;i<R->ival;i++)r+=L->sval;return Value::fromStr(r);}
            return useF?Value::fromFloat(L->toFloat()*R->toFloat()):Value::fromInt(L->toInt()*R->toInt());
        }
        if(op=="/"){ if(useF)return Value::fromFloat(L->toFloat()/R->toFloat()); if(R->toInt()==0)throw std::runtime_error("Division by zero"); return Value::fromInt(L->toInt()/R->toInt()); }
        if(op=="%"){ if(R->toInt()==0)throw std::runtime_error("Modulo by zero"); return Value::fromInt(L->toInt()%R->toInt()); }
        if(op=="=="){
            if(L->type==VType::STR&&R->type==VType::STR)return Value::fromBool(L->sval==R->sval);
            if(L->type==VType::NULL_VAL&&R->type==VType::NULL_VAL)return Value::fromBool(true);
            if(L->type==VType::NULL_VAL||R->type==VType::NULL_VAL)return Value::fromBool(false);
            if(L->type==VType::BOOL&&R->type==VType::BOOL)return Value::fromBool(L->bval==R->bval);
            return useF?Value::fromBool(L->toFloat()==R->toFloat()):Value::fromBool(L->toInt()==R->toInt());
        }
        if(op=="!="){
            if(L->type==VType::STR&&R->type==VType::STR)return Value::fromBool(L->sval!=R->sval);
            if(L->type==VType::NULL_VAL||R->type==VType::NULL_VAL)return Value::fromBool(!(L->isNull()&&R->isNull()));
            return useF?Value::fromBool(L->toFloat()!=R->toFloat()):Value::fromBool(L->toInt()!=R->toInt());
        }
        if(useF){
            double lf=L->toFloat(),rf=R->toFloat();
            if(op=="<")return Value::fromBool(lf<rf);if(op==">")return Value::fromBool(lf>rf);
            if(op=="<=")return Value::fromBool(lf<=rf);if(op==">=")return Value::fromBool(lf>=rf);
        } else {
            int64_t li=L->toInt(),ri=R->toInt();
            if(op=="<")return Value::fromBool(li<ri);if(op==">")return Value::fromBool(li>ri);
            if(op=="<=")return Value::fromBool(li<=ri);if(op==">=")return Value::fromBool(li>=ri);
        }
        // Bitwise
        if(op=="&")return Value::fromInt(L->toInt()&R->toInt());
        if(op=="|")return Value::fromInt(L->toInt()|R->toInt());
        if(op=="^")return Value::fromInt(L->toInt()^R->toInt());
        if(op=="<<")return Value::fromInt(L->toInt()<<(int)R->toInt());
        if(op==">>")return Value::fromInt(L->toInt()>>(int)R->toInt());
        // String comparison
        if(op=="<"&&L->type==VType::STR&&R->type==VType::STR)return Value::fromBool(L->sval<R->sval);
        if(op==">"&&L->type==VType::STR&&R->type==VType::STR)return Value::fromBool(L->sval>R->sval);
        throw std::runtime_error("Unknown binary op: "+op);
    }

    void doAssign(Expr* lval,ValPtr v,EnvPtr env){
        if(lval->kind==ExprKind::Ident){ env->assign(lval->name,v); }
        else if(lval->kind==ExprKind::Member){
            auto obj=evalExpr(lval->left.get(),env);
            if(obj->type==VType::STRUCT)obj->obj->fields[lval->name]=v;
            else if(obj->type==VType::MAP)obj->mapSet(lval->name,v);
            else throw std::runtime_error("Cannot set member on non-struct/map");
        }
        else if(lval->kind==ExprKind::Index){
            auto obj=evalExpr(lval->left.get(),env);
            auto idx=evalExpr(lval->right.get(),env);
            if(obj->type==VType::ARRAY){
                int64_t i=idx->toInt();
                if(i<0)i=(int64_t)obj->arr.size()+i;
                if(i<0||i>=(int64_t)obj->arr.size())throw std::runtime_error("Array index out of bounds");
                obj->arr[i]=v;
            } else if(obj->type==VType::MAP){ obj->mapSet(idx->toString(),v); }
            else throw std::runtime_error("Cannot index assign");
        }
        else throw std::runtime_error("Invalid assignment target");
    }

    ValPtr evalAssign(Expr* e,EnvPtr env){
        auto rv=evalExpr(e->right.get(),env);
        if(e->op=="="){ doAssign(e->left.get(),rv,env); return rv; }
        auto lv=evalExpr(e->left.get(),env);
        std::string bop=e->op.substr(0,e->op.size()-1);
        bool useF=(lv->isFloat()||rv->isFloat());
        ValPtr result;
        if(bop=="+"&&(lv->type==VType::STR||rv->type==VType::STR))result=Value::fromStr(lv->toString()+rv->toString());
        else if(bop=="+")result=useF?Value::fromFloat(lv->toFloat()+rv->toFloat()):Value::fromInt(lv->toInt()+rv->toInt());
        else if(bop=="-")result=useF?Value::fromFloat(lv->toFloat()-rv->toFloat()):Value::fromInt(lv->toInt()-rv->toInt());
        else if(bop=="*")result=useF?Value::fromFloat(lv->toFloat()*rv->toFloat()):Value::fromInt(lv->toInt()*rv->toInt());
        else if(bop=="/"){if(useF)result=Value::fromFloat(lv->toFloat()/rv->toFloat());else{if(rv->toInt()==0)throw std::runtime_error("Div by zero");result=Value::fromInt(lv->toInt()/rv->toInt());}}
        else if(bop=="%"){if(rv->toInt()==0)throw std::runtime_error("Mod by zero");result=Value::fromInt(lv->toInt()%rv->toInt());}
        else result=rv;
        doAssign(e->left.get(),result,env);
        return result;
    }

    // ── String methods ────────────────────────────────────────
    ValPtr callStringMethod(ValPtr obj,const std::string& method,const std::vector<ValPtr>& args){
        std::string& s=obj->sval;
        if(method=="len"||method=="length")return Value::fromInt((int64_t)s.size());
        if(method=="upper"){std::string r=s;for(auto& c:r)c=toupper(c);return Value::fromStr(r);}
        if(method=="lower"){std::string r=s;for(auto& c:r)c=tolower(c);return Value::fromStr(r);}
        if(method=="trim"){
            size_t a=s.find_first_not_of(" \t\r\n"),b=s.find_last_not_of(" \t\r\n");
            return Value::fromStr(a==std::string::npos?"":s.substr(a,b-a+1));
        }
        if(method=="ltrim"){size_t a=s.find_first_not_of(" \t\r\n");return Value::fromStr(a==std::string::npos?"":s.substr(a));}
        if(method=="rtrim"){size_t b=s.find_last_not_of(" \t\r\n");return Value::fromStr(b==std::string::npos?"":s.substr(0,b+1));}
        if(method=="split"){
            std::string delim=args.empty()?" ":args[0]->toString();
            auto v=Value::fromArray();
            if(delim.empty()){for(char c:s)v->arr.push_back(Value::fromStr(std::string(1,c)));return v;}
            size_t start=0,found;
            while((found=s.find(delim,start))!=std::string::npos){
                v->arr.push_back(Value::fromStr(s.substr(start,found-start)));
                start=found+delim.size();
            }
            v->arr.push_back(Value::fromStr(s.substr(start)));
            return v;
        }
        if(method=="replace"){
            if(args.size()<2)throw std::runtime_error("replace() needs 2 args");
            std::string from=args[0]->toString(),to=args[1]->toString(),r=s;
            size_t p=0;while((p=r.find(from,p))!=std::string::npos){r.replace(p,from.size(),to);p+=to.size();}
            return Value::fromStr(r);
        }
        if(method=="replaceFirst"){
            if(args.size()<2)throw std::runtime_error("replaceFirst() needs 2 args");
            std::string from=args[0]->toString(),to=args[1]->toString(),r=s;
            size_t p=r.find(from);if(p!=std::string::npos)r.replace(p,from.size(),to);
            return Value::fromStr(r);
        }
        if(method=="startsWith"||method=="hasPrefix"){
            if(args.empty())return Value::fromBool(false);
            std::string p=args[0]->toString();
            return Value::fromBool(s.size()>=p.size()&&s.substr(0,p.size())==p);
        }
        if(method=="endsWith"||method=="hasSuffix"){
            if(args.empty())return Value::fromBool(false);
            std::string p=args[0]->toString();
            return Value::fromBool(s.size()>=p.size()&&s.substr(s.size()-p.size())==p);
        }
        if(method=="indexOf"||method=="find"){
            std::string sub=args.empty()?"":args[0]->toString();
            size_t p=s.find(sub);
            return Value::fromInt(p==std::string::npos?-1:(int64_t)p);
        }
        if(method=="lastIndexOf"){
            std::string sub=args.empty()?"":args[0]->toString();
            size_t p=s.rfind(sub);
            return Value::fromInt(p==std::string::npos?-1:(int64_t)p);
        }
        if(method=="contains"){std::string sub=args.empty()?"":args[0]->toString();return Value::fromBool(s.find(sub)!=std::string::npos);}
        if(method=="count"){
            std::string sub=args.empty()?"":args[0]->toString();
            int64_t cnt=0;size_t p=0;
            while((p=s.find(sub,p))!=std::string::npos){cnt++;p+=sub.size();}
            return Value::fromInt(cnt);
        }
        if(method=="substr"||method=="slice"){
            int64_t start=args.empty()?0:args[0]->toInt();
            if(start<0)start=(int64_t)s.size()+start;
            start=std::max((int64_t)0,std::min(start,(int64_t)s.size()));
            if(args.size()<2){return Value::fromStr(s.substr(start));}
            int64_t len=args[1]->toInt();
            if(len<0)len=(int64_t)s.size()+len-start;
            return Value::fromStr(s.substr(start,std::max((int64_t)0,len)));
        }
        if(method=="repeat"){int64_t n=args.empty()?1:args[0]->toInt();std::string r;for(int64_t i=0;i<n;i++)r+=s;return Value::fromStr(r);}
        if(method=="reverse"){std::string r=s;std::reverse(r.begin(),r.end());return Value::fromStr(r);}
        if(method=="padLeft"){
            int64_t w=args.empty()?0:args[0]->toInt();
            char ch=args.size()<2?' ':args[1]->toString()[0];
            std::string r=s;while((int64_t)r.size()<w)r=ch+r;return Value::fromStr(r);
        }
        if(method=="padRight"){
            int64_t w=args.empty()?0:args[0]->toInt();
            char ch=args.size()<2?' ':args[1]->toString()[0];
            std::string r=s;while((int64_t)r.size()<w)r+=ch;return Value::fromStr(r);
        }
        if(method=="toInt"){try{return Value::fromInt(std::stoll(s));}catch(...){return Value::fromInt(0);}}
        if(method=="toFloat"){try{return Value::fromFloat(std::stod(s));}catch(...){return Value::fromFloat(0.0);}}
        if(method=="isEmpty")return Value::fromBool(s.empty());
        if(method=="chars"){auto v=Value::fromArray();for(char c:s)v->arr.push_back(Value::fromStr(std::string(1,c)));return v;}
        if(method=="bytes"){auto v=Value::fromArray();for(unsigned char c:s)v->arr.push_back(Value::fromInt(c));return v;}
        if(method=="format"){
            // Simple format: replace {} with args in order
            std::string r=s;size_t ai=0;size_t p;
            while((p=r.find("{}"))!=std::string::npos&&ai<args.size()){
                r.replace(p,2,args[ai++]->toString());
            }
            return Value::fromStr(r);
        }
        throw std::runtime_error("Unknown string method: '"+method+"'");
    }

    // ── Array methods ─────────────────────────────────────────
    ValPtr callArrayMethod(ValPtr obj,const std::string& method,const std::vector<ValPtr>& args,EnvPtr env){
        auto& arr=obj->arr;
        if(method=="len"||method=="length")return Value::fromInt((int64_t)arr.size());
        if(method=="push"||method=="append"){if(!args.empty())arr.push_back(args[0]);return Value::null();}
        if(method=="pop"){if(arr.empty())throw std::runtime_error("pop() on empty array");auto v=arr.back();arr.pop_back();return v;}
        if(method=="shift"){if(arr.empty())throw std::runtime_error("shift() on empty array");auto v=arr.front();arr.erase(arr.begin());return v;}
        if(method=="unshift"){if(!args.empty())arr.insert(arr.begin(),args[0]);return Value::null();}
        if(method=="isEmpty")return Value::fromBool(arr.empty());
        if(method=="first")return arr.empty()?Value::null():arr.front();
        if(method=="last")return arr.empty()?Value::null():arr.back();
        if(method=="clear"){arr.clear();return Value::null();}
        if(method=="contains"){
            if(args.empty())return Value::fromBool(false);
            for(auto& v:arr){
                if(v->type==VType::STR&&args[0]->type==VType::STR&&v->sval==args[0]->sval)return Value::fromBool(true);
                if(v->type==VType::INT&&args[0]->type==VType::INT&&v->ival==args[0]->ival)return Value::fromBool(true);
                if(v->type==VType::FLOAT&&args[0]->isNum()&&v->toFloat()==args[0]->toFloat())return Value::fromBool(true);
            }
            return Value::fromBool(false);
        }
        if(method=="indexOf"){
            if(args.empty())return Value::fromInt(-1);
            for(size_t i=0;i<arr.size();i++){
                if(arr[i]->toString()==args[0]->toString())return Value::fromInt((int64_t)i);
            }
            return Value::fromInt(-1);
        }
        if(method=="slice"){
            int64_t start=args.empty()?0:args[0]->toInt();
            int64_t end=(int64_t)arr.size();if(args.size()>1)end=args[1]->toInt();
            if(start<0)start=(int64_t)arr.size()+start;
            if(end<0)end=(int64_t)arr.size()+end;
            start=std::max((int64_t)0,std::min(start,(int64_t)arr.size()));
            end=std::max(start,std::min(end,(int64_t)arr.size()));
            auto v=Value::fromArray();for(int64_t i=start;i<end;i++)v->arr.push_back(arr[i]);return v;
        }
        if(method=="reverse"){auto v=Value::fromArray();v->arr=arr;std::reverse(v->arr.begin(),v->arr.end());return v;}
        if(method=="reverseInPlace"){std::reverse(arr.begin(),arr.end());return Value::null();}
        if(method=="sort"){
            auto v=Value::fromArray();v->arr=arr;
            std::sort(v->arr.begin(),v->arr.end(),[](const ValPtr& a,const ValPtr& b){
                if(a->isNum()&&b->isNum())return a->toFloat()<b->toFloat();
                return a->toString()<b->toString();
            });
            return v;
        }
        if(method=="sortInPlace"){
            std::sort(arr.begin(),arr.end(),[](const ValPtr& a,const ValPtr& b){
                if(a->isNum()&&b->isNum())return a->toFloat()<b->toFloat();
                return a->toString()<b->toString();
            });
            return Value::null();
        }
        if(method=="join"){std::string delim=args.empty()?"":args[0]->toString();std::string r;for(size_t i=0;i<arr.size();i++){if(i)r+=delim;r+=arr[i]->toString();}return Value::fromStr(r);}
        if(method=="copy"){auto v=Value::fromArray();v->arr=arr;return v;}
        if(method=="map"){
            if(args.empty())throw std::runtime_error("map() needs a function arg");
            auto fn=args[0];auto v=Value::fromArray();
            for(size_t i=0;i<arr.size();i++){
                std::vector<ValPtr> fa={arr[i],Value::fromInt((int64_t)i)};
                v->arr.push_back(callValue(fn,fa,env));
            }
            return v;
        }
        if(method=="filter"){
            if(args.empty())throw std::runtime_error("filter() needs a function arg");
            auto fn=args[0];auto v=Value::fromArray();
            for(size_t i=0;i<arr.size();i++){
                std::vector<ValPtr> fa={arr[i],Value::fromInt((int64_t)i)};
                if(callValue(fn,fa,env)->toBool())v->arr.push_back(arr[i]);
            }
            return v;
        }
        if(method=="reduce"){
            if(args.empty())throw std::runtime_error("reduce() needs a function arg");
            auto fn=args[0];
            ValPtr acc=args.size()>1?args[1]:(!arr.empty()?arr[0]:Value::null());
            size_t start=args.size()>1?0:1;
            for(size_t i=start;i<arr.size();i++){
                std::vector<ValPtr> fa={acc,arr[i],Value::fromInt((int64_t)i)};
                acc=callValue(fn,fa,env);
            }
            return acc;
        }
        if(method=="forEach"){
            if(args.empty())throw std::runtime_error("forEach() needs a function arg");
            auto fn=args[0];
            for(size_t i=0;i<arr.size();i++){
                std::vector<ValPtr> fa={arr[i],Value::fromInt((int64_t)i)};
                callValue(fn,fa,env);
            }
            return Value::null();
        }
        if(method=="find"){
            if(args.empty())throw std::runtime_error("find() needs a function arg");
            auto fn=args[0];
            for(size_t i=0;i<arr.size();i++){
                std::vector<ValPtr> fa={arr[i],Value::fromInt((int64_t)i)};
                if(callValue(fn,fa,env)->toBool())return arr[i];
            }
            return Value::null();
        }
        if(method=="any"){
            if(args.empty())throw std::runtime_error("any() needs a function arg");
            auto fn=args[0];
            for(auto& v:arr){if(callValue(fn,{v},env)->toBool())return Value::fromBool(true);}
            return Value::fromBool(false);
        }
        if(method=="all"){
            if(args.empty())throw std::runtime_error("all() needs a function arg");
            auto fn=args[0];
            for(auto& v:arr){if(!callValue(fn,{v},env)->toBool())return Value::fromBool(false);}
            return Value::fromBool(true);
        }
        if(method=="flat"){
            auto v=Value::fromArray();
            for(auto& x:arr){
                if(x->type==VType::ARRAY)for(auto& y:x->arr)v->arr.push_back(y);
                else v->arr.push_back(x);
            }
            return v;
        }
        if(method=="zip"){
            if(args.empty()||args[0]->type!=VType::ARRAY)throw std::runtime_error("zip() needs array");
            auto& other=args[0]->arr;
            size_t n=std::min(arr.size(),other.size());
            auto v=Value::fromArray();
            for(size_t i=0;i<n;i++){auto pair=Value::fromArray();pair->arr.push_back(arr[i]);pair->arr.push_back(other[i]);v->arr.push_back(pair);}
            return v;
        }
        if(method=="sum"){double s=0;for(auto& v:arr)s+=v->toFloat();return arr.empty()||(!arr[0]->isFloat())?Value::fromInt((int64_t)s):Value::fromFloat(s);}
        if(method=="min"){if(arr.empty())return Value::null();auto m=arr[0];for(size_t i=1;i<arr.size();i++)if(arr[i]->toFloat()<m->toFloat())m=arr[i];return m;}
        if(method=="max"){if(arr.empty())return Value::null();auto m=arr[0];for(size_t i=1;i<arr.size();i++)if(arr[i]->toFloat()>m->toFloat())m=arr[i];return m;}
        if(method=="count"){
            if(args.empty()){return Value::fromInt((int64_t)arr.size());}
            auto fn=args[0];int64_t cnt=0;
            for(auto& v:arr){if(callValue(fn,{v},env)->toBool())cnt++;}
            return Value::fromInt(cnt);
        }
        if(method=="unique"){
            auto v=Value::fromArray();std::unordered_map<std::string,bool> seen;
            for(auto& x:arr){std::string k=x->toString();if(!seen[k]){seen[k]=true;v->arr.push_back(x);}}
            return v;
        }
        if(method=="extend"){
            if(!args.empty()&&args[0]->type==VType::ARRAY)for(auto& x:args[0]->arr)arr.push_back(x);
            return Value::null();
        }
        if(method=="insert"){
            if(args.size()<2)throw std::runtime_error("insert() needs index and value");
            int64_t idx=args[0]->toInt();
            if(idx<0)idx=(int64_t)arr.size()+idx;
            idx=std::max((int64_t)0,std::min(idx,(int64_t)arr.size()));
            arr.insert(arr.begin()+idx,args[1]);
            return Value::null();
        }
        if(method=="remove"){
            if(args.empty())throw std::runtime_error("remove() needs index");
            int64_t idx=args[0]->toInt();
            if(idx<0)idx=(int64_t)arr.size()+idx;
            if(idx<0||idx>=(int64_t)arr.size())throw std::runtime_error("remove() index out of bounds");
            auto v=arr[idx];arr.erase(arr.begin()+idx);return v;
        }
        if(method=="toString"||method=="str")return Value::fromStr(obj->toString());
        throw std::runtime_error("Unknown array method: '"+method+"'");
    }

    // ── Map methods ───────────────────────────────────────────
    ValPtr callMapMethod(ValPtr obj,const std::string& method,const std::vector<ValPtr>& args,EnvPtr env){
        if(method=="keys"){auto v=Value::fromArray();for(auto& k:*obj->mapOrder)v->arr.push_back(Value::fromStr(k));return v;}
        if(method=="values"){auto v=Value::fromArray();for(auto& k:*obj->mapOrder)v->arr.push_back((*obj->map)[k]);return v;}
        if(method=="has"){return Value::fromBool(!args.empty()&&obj->mapHas(args[0]->toString()));}
        if(method=="remove"||method=="delete"){if(!args.empty())obj->mapRemove(args[0]->toString());return Value::null();}
        if(method=="len"||method=="length")return Value::fromInt((int64_t)obj->map->size());
        if(method=="isEmpty")return Value::fromBool(obj->map->empty());
        if(method=="clear"){obj->map->clear();obj->mapOrder->clear();return Value::null();}
        if(method=="entries"){
            auto v=Value::fromArray();
            for(auto& k:*obj->mapOrder){
                auto pair=Value::fromArray();pair->arr.push_back(Value::fromStr(k));pair->arr.push_back((*obj->map)[k]);
                v->arr.push_back(pair);
            }
            return v;
        }
        if(method=="get"){
            if(args.empty())return Value::null();
            auto v=obj->mapGet(args[0]->toString());
            if(v->isNull()&&args.size()>1)return args[1]; // default value
            return v;
        }
        if(method=="set"){if(args.size()>=2)obj->mapSet(args[0]->toString(),args[1]);return Value::null();}
        if(method=="merge"){
            if(!args.empty()&&args[0]->type==VType::MAP)
                for(auto& k:*args[0]->mapOrder)obj->mapSet(k,(*args[0]->map)[k]);
            return Value::null();
        }
        if(method=="forEach"){
            if(args.empty())throw std::runtime_error("forEach() needs a function");
            auto fn=args[0];
            for(auto& k:*obj->mapOrder){
                std::vector<ValPtr> fa={Value::fromStr(k),(*obj->map)[k]};
                callValue(fn,fa,env);
            }
            return Value::null();
        }
        throw std::runtime_error("Unknown map method: '"+method+"'");
    }

    // ── Call a value (func or native) ─────────────────────────
    ValPtr callValue(ValPtr fn,const std::vector<ValPtr>& args,EnvPtr env){
        if(fn->type==VType::FUNC) return callFuncValue(*fn->func,args,env);
        if(fn->type==VType::NATIVE) return raze_native::call(*fn->native,args);
        throw std::runtime_error("Value is not callable: "+fn->toString());
    }

    // ── Call expression evaluation ────────────────────────────
    ValPtr evalCall(Expr* e,EnvPtr env){
        // ── super.method(args) must come FIRST before generic Member handler ──
        if(e->left->kind==ExprKind::Member&&e->left->left&&e->left->left->kind==ExprKind::Super){
            std::string method=e->left->name;
            auto thisVal=env->get("this");
            std::string cname=thisVal->obj->typeName;
            auto it=classes.find(cname);
            if(it==classes.end()||it->second.parent.empty())
                throw std::runtime_error("No parent class for super");
            std::string parentName=it->second.parent;
            std::vector<ValPtr> args;
            for(auto& a:e->args)args.push_back(evalExpr(a.get(),env));
            if(auto m=findMethod(parentName,method))
                return callMethod(thisVal,*m,args,env,classes.count(parentName)?classes[parentName].parent:"");
            throw std::runtime_error("Parent '"+parentName+"' has no method '"+method+"'");
        }

        // Method call on an object
        if(e->left->kind==ExprKind::Member){
            auto obj=evalExpr(e->left->left.get(),env);
            std::string method=e->left->name;
            std::vector<ValPtr> args;
            for(auto& a:e->args)args.push_back(evalExpr(a.get(),env));

            if(obj->type==VType::STR)   return callStringMethod(obj,method,args);
            if(obj->type==VType::ARRAY) return callArrayMethod(obj,method,args,env);
            if(obj->type==VType::MAP)   return callMapMethod(obj,method,args,env);
            if(obj->type==VType::STRUCT){
                // Look up method in class
                if(auto m=findMethod(obj->obj->typeName,method)){
                    auto parentClass=classes.count(obj->obj->typeName)?classes[obj->obj->typeName].parent:"";
                    return callMethod(obj,*m,args,env,parentClass);
                }
                throw std::runtime_error("'"+obj->obj->typeName+"' has no method '"+method+"'");
            }
            throw std::runtime_error("Cannot call method '"+method+"' on "+obj->toString());
        }

        // super.method() call
        if(e->left->kind==ExprKind::Super||
           (e->left->kind==ExprKind::Ident&&e->left->name=="super")){
            // Get this
            auto thisVal=env->get("this");
            std::string cname=thisVal->obj->typeName;
            auto it=classes.find(cname);
            if(it==classes.end()||it->second.parent.empty())
                throw std::runtime_error("No parent class for super");
            std::string parentName=it->second.parent;
            std::vector<ValPtr> args;
            for(auto& a:e->args)args.push_back(evalExpr(a.get(),env));
            // Call parent init / specific method
            if(e->left->kind==ExprKind::Super){
                // super(args) — call parent init
                if(auto m=findMethod(parentName,"init"))
                    return callMethod(thisVal,*m,args,env);
                return Value::null();
            }
            return Value::null();
        }

        // Check if callee is a super member access (from Member on Super node)
        // e.left is Call, with left being Member on Super
        // handled above in Member branch — but Member on Super needs special handling
        if(e->left->kind==ExprKind::Member&&e->left->left->kind==ExprKind::Super){
            std::string method=e->left->name;
            auto thisVal=env->get("this");
            std::string cname=thisVal->obj->typeName;
            auto it=classes.find(cname);
            if(it==classes.end()||it->second.parent.empty())
                throw std::runtime_error("No parent class for super");
            std::string parentName=it->second.parent;
            std::vector<ValPtr> args;
            for(auto& a:e->args)args.push_back(evalExpr(a.get(),env));
            if(auto m=findMethod(parentName,method))
                return callMethod(thisVal,*m,args,env,classes.count(parentName)?classes[parentName].parent:"");
            throw std::runtime_error("Parent class '"+parentName+"' has no method '"+method+"'");
        }

        // Regular call
        std::string fname;
        if(e->left->kind==ExprKind::Ident) fname=e->left->name;

        std::vector<ValPtr> args;
        for(auto& a:e->args)args.push_back(evalExpr(a.get(),env));

        // ── Built-ins ─────────────────────────────────────────
        if(fname=="print")   {for(auto& v:args)std::cout<<v->toString();std::cout.flush();return Value::null();}
        if(fname=="println") {for(auto& v:args)std::cout<<v->toString();std::cout<<"\n";return Value::null();}
        if(fname=="eprint")  {for(auto& v:args)std::cerr<<v->toString();std::cerr.flush();return Value::null();}
        if(fname=="eprintln"){for(auto& v:args)std::cerr<<v->toString();std::cerr<<"\n";return Value::null();}
        if(fname=="input")   {if(!args.empty())std::cout<<args[0]->toString();std::string ln;std::getline(std::cin,ln);return Value::fromStr(ln);}
        if(fname=="len")     {if(args.empty())throw std::runtime_error("len() needs arg");auto& v=args[0];if(v->type==VType::STR)return Value::fromInt((int64_t)v->sval.size());if(v->type==VType::ARRAY)return Value::fromInt((int64_t)v->arr.size());if(v->type==VType::MAP)return Value::fromInt((int64_t)v->map->size());throw std::runtime_error("len() requires string/array/map");}
        if(fname=="push")    {if(args.size()<2)throw std::runtime_error("push() needs array+val");args[0]->arr.push_back(args[1]);return Value::null();}
        if(fname=="pop")     {if(args.empty())throw std::runtime_error("pop() needs array");auto& a=args[0]->arr;if(a.empty())throw std::runtime_error("pop() on empty array");auto v=a.back();a.pop_back();return v;}
        if(fname=="array"||fname=="Array")return Value::fromArray();
        if(fname=="map"||fname=="Map")return Value::fromMap();
        if(fname=="range"){
            int64_t start=0,end=0,step=1;
            if(args.size()==1){end=args[0]->toInt();}
            else if(args.size()>=2){start=args[0]->toInt();end=args[1]->toInt();}
            if(args.size()>=3)step=args[2]->toInt();
            if(step==0)throw std::runtime_error("range() step cannot be 0");
            auto v=Value::fromArray();
            if(step>0)for(int64_t i=start;i<end;i+=step)v->arr.push_back(Value::fromInt(i));
            else for(int64_t i=start;i>end;i+=step)v->arr.push_back(Value::fromInt(i));
            return v;
        }
        if(fname=="typeof"){
            if(args.empty())return Value::fromStr("null");
            switch(args[0]->type){
                case VType::INT:return Value::fromStr("int");
                case VType::FLOAT:return Value::fromStr("float");
                case VType::BOOL:return Value::fromStr("bool");
                case VType::STR:return Value::fromStr("string");
                case VType::STRUCT:return Value::fromStr(args[0]->obj->typeName);
                case VType::ARRAY:return Value::fromStr("array");
                case VType::MAP:return Value::fromStr("map");
                case VType::FUNC:return Value::fromStr("func");
                case VType::NATIVE:return Value::fromStr("native");
                case VType::NULL_VAL:return Value::fromStr("null");
            }
        }
        if(fname=="isNull")  {return Value::fromBool(args.empty()||args[0]->isNull());}
        if(fname=="isInt")   {return Value::fromBool(!args.empty()&&args[0]->type==VType::INT);}
        if(fname=="isFloat") {return Value::fromBool(!args.empty()&&args[0]->type==VType::FLOAT);}
        if(fname=="isBool")  {return Value::fromBool(!args.empty()&&args[0]->type==VType::BOOL);}
        if(fname=="isString"){return Value::fromBool(!args.empty()&&args[0]->type==VType::STR);}
        if(fname=="isArray") {return Value::fromBool(!args.empty()&&args[0]->type==VType::ARRAY);}
        if(fname=="isMap")   {return Value::fromBool(!args.empty()&&args[0]->type==VType::MAP);}
        if(fname=="isFunc")  {return Value::fromBool(!args.empty()&&args[0]->type==VType::FUNC);}
        // Type conversions
        if(fname=="int"||fname=="Int")   {if(args.empty())return Value::fromInt(0);return Value::fromInt(args[0]->toInt());}
        if(fname=="float"||fname=="Float"){if(args.empty())return Value::fromFloat(0);return Value::fromFloat(args[0]->toFloat());}
        if(fname=="str"||fname=="Str")   {if(args.empty())return Value::fromStr("");return Value::fromStr(args[0]->toString());}
        if(fname=="bool"||fname=="Bool") {if(args.empty())return Value::fromBool(false);return Value::fromBool(args[0]->toBool());}
        if(fname=="chr"||fname=="char")  {if(args.empty())return Value::fromStr("");int64_t n=args[0]->toInt();return Value::fromStr(std::string(1,(char)n));}
        if(fname=="ord")                 {if(args.empty())return Value::fromInt(0);auto s=args[0]->toString();return Value::fromInt(s.empty()?0:(unsigned char)s[0]);}
        // Math
        if(fname=="sqrt")  {if(args.empty())throw std::runtime_error("sqrt() needs arg");return Value::fromFloat(std::sqrt(args[0]->toFloat()));}
        if(fname=="abs")   {if(args.empty())throw std::runtime_error("abs() needs arg");auto& v=args[0];return v->isFloat()?Value::fromFloat(std::fabs(v->fval)):Value::fromInt(std::abs(v->ival));}
        if(fname=="pow")   {if(args.size()<2)throw std::runtime_error("pow() needs 2 args");return Value::fromFloat(std::pow(args[0]->toFloat(),args[1]->toFloat()));}
        if(fname=="log")   {if(args.empty())throw std::runtime_error("log() needs arg");return Value::fromFloat(std::log(args[0]->toFloat()));}
        if(fname=="log2")  {if(args.empty())throw std::runtime_error("log2() needs arg");return Value::fromFloat(std::log2(args[0]->toFloat()));}
        if(fname=="log10") {if(args.empty())throw std::runtime_error("log10() needs arg");return Value::fromFloat(std::log10(args[0]->toFloat()));}
        if(fname=="exp")   {if(args.empty())throw std::runtime_error("exp() needs arg");return Value::fromFloat(std::exp(args[0]->toFloat()));}
        if(fname=="sin")   {return Value::fromFloat(std::sin(args[0]->toFloat()));}
        if(fname=="cos")   {return Value::fromFloat(std::cos(args[0]->toFloat()));}
        if(fname=="tan")   {return Value::fromFloat(std::tan(args[0]->toFloat()));}
        if(fname=="asin")  {return Value::fromFloat(std::asin(args[0]->toFloat()));}
        if(fname=="acos")  {return Value::fromFloat(std::acos(args[0]->toFloat()));}
        if(fname=="atan")  {return Value::fromFloat(std::atan(args[0]->toFloat()));}
        if(fname=="atan2") {if(args.size()<2)throw std::runtime_error("atan2() needs 2 args");return Value::fromFloat(std::atan2(args[0]->toFloat(),args[1]->toFloat()));}
        if(fname=="floor") {if(args.empty())return Value::fromFloat(0);return Value::fromFloat(std::floor(args[0]->toFloat()));}
        if(fname=="ceil")  {if(args.empty())return Value::fromFloat(0);return Value::fromFloat(std::ceil(args[0]->toFloat()));}
        if(fname=="round") {if(args.empty())return Value::fromFloat(0);return Value::fromFloat(std::round(args[0]->toFloat()));}
        if(fname=="trunc") {if(args.empty())return Value::fromFloat(0);return Value::fromFloat(std::trunc(args[0]->toFloat()));}
        if(fname=="hypot") {if(args.size()<2)throw std::runtime_error("hypot() needs 2 args");return Value::fromFloat(std::hypot(args[0]->toFloat(),args[1]->toFloat()));}
        if(fname=="min")   {if(args.size()<2)throw std::runtime_error("min() needs 2 args");bool f=args[0]->isFloat()||args[1]->isFloat();return f?Value::fromFloat(std::min(args[0]->toFloat(),args[1]->toFloat())):Value::fromInt(std::min(args[0]->toInt(),args[1]->toInt()));}
        if(fname=="max")   {if(args.size()<2)throw std::runtime_error("max() needs 2 args");bool f=args[0]->isFloat()||args[1]->isFloat();return f?Value::fromFloat(std::max(args[0]->toFloat(),args[1]->toFloat())):Value::fromInt(std::max(args[0]->toInt(),args[1]->toInt()));}
        if(fname=="clamp") {if(args.size()<3)throw std::runtime_error("clamp() needs 3 args");double v=args[0]->toFloat(),lo=args[1]->toFloat(),hi=args[2]->toFloat();bool f=args[0]->isFloat()||args[1]->isFloat()||args[2]->isFloat();return f?Value::fromFloat(std::max(lo,std::min(hi,v))):Value::fromInt((int64_t)std::max(lo,std::min(hi,v)));}
        if(fname=="lerp")  {if(args.size()<3)throw std::runtime_error("lerp() needs 3 args");double a=args[0]->toFloat(),b=args[1]->toFloat(),t=args[2]->toFloat();return Value::fromFloat(a+(b-a)*t);}
        if(fname=="exit")  {std::exit(args.empty()?0:(int)args[0]->toInt());}
        if(fname=="assert"){
            bool cond=!args.empty()&&args[0]->toBool();
            if(!cond){
                std::string msg=args.size()>1?args[1]->toString():"Assertion failed";
                throw ThrowSignal{Value::fromStr(msg)};
            }
            return Value::null();
        }
        if(fname=="error"||fname=="panic"){
            std::string msg=args.empty()?"Error":args[0]->toString();
            throw ThrowSignal{Value::fromStr(msg)};
        }
        if(fname=="join"){
            if(args.size()<2)throw std::runtime_error("join() needs array+delim");
            auto& arr=args[0]->arr;std::string delim=args[1]->toString();
            std::string r;for(size_t i=0;i<arr.size();i++){if(i)r+=delim;r+=arr[i]->toString();}
            return Value::fromStr(r);
        }

        // Variable lookup → might be func value or native
        if(env->has(fname)){
            auto val=env->get(fname);
            if(val->type==VType::FUNC)  return callFuncValue(*val->func,args,env);
            if(val->type==VType::NATIVE)return raze_native::call(*val->native,args);
        }

        // User-defined function
        auto fit=funcs.find(fname);
        if(fit!=funcs.end())return callFunc(fit->second,args,env);

        // Class constructor shorthand: ClassName(args) same as new ClassName(args)
        if(classes.count(fname))return instantiateClass(fname,args,env);

        // Eval left as expression (callable value)
        auto callable=evalExpr(e->left.get(),env);
        if(callable->type==VType::FUNC)  return callFuncValue(*callable->func,args,env);
        if(callable->type==VType::NATIVE)return raze_native::call(*callable->native,args);

        throw std::runtime_error("Unknown function: '"+fname+"'");
    }

    // ── Statement execution ───────────────────────────────────
    void execStmt(Stmt* s,EnvPtr env){
        if(!s)return;
        switch(s->kind){
        case StmtKind::Block:
            for(auto& sub:s->body)execStmt(sub.get(),env);
            break;

        case StmtKind::ExprStmt:
            evalExpr(s->expr.get(),env);
            break;

        case StmtKind::VarDecl: {
            ValPtr v=s->varInit?evalExpr(s->varInit.get(),env):defaultValue(s->varType);
            env->define(s->varName,v);
            break;
        }

        case StmtKind::If:
            if(evalExpr(s->cond.get(),env)->toBool())execStmt(s->then.get(),env);
            else if(s->els)execStmt(s->els.get(),env);
            break;

        case StmtKind::While: {
            auto loopEnv=std::make_shared<Env>(env);
            while(evalExpr(s->cond.get(),loopEnv)->toBool()){
                auto iterEnv=std::make_shared<Env>(loopEnv);
                try{execStmt(s->body[0].get(),iterEnv);}
                catch(BreakSignal&){goto wDone;}
                catch(ContinueSignal&){continue;}
            }
            wDone:;
            break;
        }

        case StmtKind::For: {
            auto forEnv=std::make_shared<Env>(env);
            if(s->forInit)execStmt(s->forInit.get(),forEnv);
            while(true){
                if(s->forCond&&!evalExpr(s->forCond.get(),forEnv)->toBool())break;
                auto iterEnv=std::make_shared<Env>(forEnv);
                try{execStmt(s->forBody.get(),iterEnv);}
                catch(BreakSignal&){goto fDone;}
                catch(ContinueSignal&){}
                if(s->forStep)evalExpr(s->forStep.get(),forEnv);
            }
            fDone:;
            break;
        }

        case StmtKind::ForIn: {
            auto collection=evalExpr(s->forInExpr.get(),env);
            auto forEnv=std::make_shared<Env>(env);
            auto iterate=[&](ValPtr item){
                forEnv->define(s->forInVar,item);
                auto iterEnv=std::make_shared<Env>(forEnv);
                execStmt(s->forInBody.get(),iterEnv);
            };
            try{
                if(collection->type==VType::ARRAY){
                    for(auto& v:collection->arr){iterate(v);}
                } else if(collection->type==VType::MAP){
                    for(auto& k:*collection->mapOrder)iterate(Value::fromStr(k));
                } else if(collection->type==VType::STR){
                    for(char c:collection->sval)iterate(Value::fromStr(std::string(1,c)));
                } else {
                    throw std::runtime_error("for-in: cannot iterate over "+collection->toString());
                }
            }catch(BreakSignal&){}
            break;
        }

        case StmtKind::Return: {
            ValPtr v=s->expr?evalExpr(s->expr.get(),env):Value::null();
            throw ReturnSignal{v};
        }
        case StmtKind::Break:    throw BreakSignal{};
        case StmtKind::Continue: throw ContinueSignal{};

        case StmtKind::Throw: {
            auto v=evalExpr(s->expr.get(),env);
            throw ThrowSignal{v};
        }

        case StmtKind::TryCatch: {
            auto tryEnv=std::make_shared<Env>(env);
            try{execStmt(s->tryBody.get(),tryEnv);}
            catch(ThrowSignal& ts){
                auto catchEnv=std::make_shared<Env>(env);
                catchEnv->define(s->catchVar,ts.val);
                execStmt(s->catchBody.get(),catchEnv);
            }
            catch(std::runtime_error& ex){
                auto catchEnv=std::make_shared<Env>(env);
                catchEnv->define(s->catchVar,Value::fromStr(ex.what()));
                execStmt(s->catchBody.get(),catchEnv);
            }
            break;
        }

        case StmtKind::StructDecl: {
            StructDef sd;sd.name=s->structName;sd.fields=s->fields;
            structs[sd.name]=std::move(sd);
            break;
        }

        case StmtKind::FuncDecl: {
            FuncDef fd;fd.name=s->funcName;fd.params=s->params;fd.retType=s->retType;
            fd.body=s->funcBody.get();fd.closure=env;
            funcs[fd.name]=std::move(fd);
            break;
        }

        case StmtKind::ClassDecl: {
            ClassDef cd;cd.name=s->className;cd.parent=s->parentClass;cd.fields=s->classFields;
            // If has parent, inherit its fields too
            if(!cd.parent.empty()&&classes.count(cd.parent)){
                auto& parentFields=classes[cd.parent].fields;
                for(auto& f:parentFields)cd.fields.insert(cd.fields.begin(),f);
            }
            for(auto& m:s->classMethods){
                FuncDef fd;fd.name=m->funcName;fd.params=m->params;fd.retType=m->retType;
                fd.body=m->funcBody.get();fd.closure=env;
                cd.methods[fd.name]=std::move(fd);
            }
            classes[cd.name]=std::move(cd);
            break;
        }

        case StmtKind::NativeDecl: {
            auto ni=std::make_shared<NativeInfo>();
            ni->addr=s->nativeAddr;ni->retType=s->nativeRet.name;
            for(auto& pt:s->nativeParams)ni->paramTypes.push_back(pt.name);
            env->define(s->nativeName,Value::fromNative(ni));
            break;
        }

        case StmtKind::Import: {
            if(importHandler)importHandler(s->importPath);
            break;
        }
        }
    }
};
