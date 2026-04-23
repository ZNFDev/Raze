#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <stdexcept>
#include <cstdint>
#include <cstdio>
#include "ast.hpp"

// Forward declarations
struct Value;
struct Env;
using ValPtr = std::shared_ptr<Value>;
using EnvPtr = std::shared_ptr<Env>;

// ── Struct / class instance ──────────────────────────────────
struct StructInstance {
    std::string typeName;
    std::unordered_map<std::string, ValPtr> fields;
};

// ── Native (raw address / C++ callback) ─────────────────────
struct NativeInfo {
    uintptr_t              addr = 0;
    std::string            retType;
    std::vector<std::string> paramTypes;
    std::function<ValPtr(std::vector<ValPtr>)> callback;
};

// ── Func (first-class function / closure) ───────────────────
struct FuncValue {
    std::string        name;        // empty for anonymous
    std::vector<Param> params;
    TypeNode           retType;
    Stmt*              body = nullptr; // raw ptr — valid while Program is alive
    EnvPtr             closure;
    // For built-in callbacks (e.g. stdlib)
    std::function<ValPtr(std::vector<ValPtr>)> callback;
};

// ── Value types ──────────────────────────────────────────────
enum class VType { INT, FLOAT, BOOL, STR, STRUCT, ARRAY, MAP, FUNC, NATIVE, NULL_VAL };

struct Value {
    VType type = VType::NULL_VAL;

    int64_t     ival = 0;
    double      fval = 0.0;
    bool        bval = false;
    std::string sval;

    std::shared_ptr<StructInstance>                       obj;    // STRUCT
    std::vector<ValPtr>                                   arr;    // ARRAY
    std::shared_ptr<std::unordered_map<std::string,ValPtr>> map;  // MAP (ordered by insertion)
    std::shared_ptr<FuncValue>                            func;   // FUNC
    std::shared_ptr<NativeInfo>                           native; // NATIVE

    // Key ordering for maps
    std::shared_ptr<std::vector<std::string>> mapOrder;

    // ── Factory methods ──────────────────────────────────────
    static ValPtr fromInt(int64_t v)    { auto p=make(); p->type=VType::INT;   p->ival=v; return p; }
    static ValPtr fromFloat(double v)   { auto p=make(); p->type=VType::FLOAT; p->fval=v; return p; }
    static ValPtr fromBool(bool v)      { auto p=make(); p->type=VType::BOOL;  p->bval=v; return p; }
    static ValPtr fromStr(std::string v){ auto p=make(); p->type=VType::STR;   p->sval=std::move(v); return p; }
    static ValPtr null()                { return make(); }

    static ValPtr fromStruct(const std::string& name){
        auto p=make(); p->type=VType::STRUCT;
        p->obj=std::make_shared<StructInstance>(); p->obj->typeName=name; return p;
    }
    static ValPtr fromArray(){
        auto p=make(); p->type=VType::ARRAY; return p;
    }
    static ValPtr fromMap(){
        auto p=make(); p->type=VType::MAP;
        p->map=std::make_shared<std::unordered_map<std::string,ValPtr>>();
        p->mapOrder=std::make_shared<std::vector<std::string>>();
        return p;
    }
    static ValPtr fromFunc(std::shared_ptr<FuncValue> fv){
        auto p=make(); p->type=VType::FUNC; p->func=std::move(fv); return p;
    }
    static ValPtr fromNative(std::shared_ptr<NativeInfo> ni){
        auto p=make(); p->type=VType::NATIVE; p->native=std::move(ni); return p;
    }

    // ── Map helpers ──────────────────────────────────────────
    ValPtr mapGet(const std::string& k) const {
        if(!map) return null();
        auto it=map->find(k); if(it!=map->end())return it->second; return null();
    }
    void mapSet(const std::string& k, ValPtr v){
        if(!map)return;
        if(!map->count(k)) mapOrder->push_back(k);
        (*map)[k]=std::move(v);
    }
    bool mapHas(const std::string& k) const { return map && map->count(k)>0; }
    void mapRemove(const std::string& k){
        if(!map)return;
        map->erase(k);
        auto& o=*mapOrder;
        o.erase(std::remove(o.begin(),o.end(),k),o.end());
    }

    // ── Type coercions ───────────────────────────────────────
    int64_t toInt() const {
        switch(type){
            case VType::INT:   return ival;
            case VType::FLOAT: return (int64_t)fval;
            case VType::BOOL:  return bval?1:0;
            case VType::STR:   try{return std::stoll(sval);}catch(...){return 0;}
            default: throw std::runtime_error("Cannot convert to int");
        }
    }
    double toFloat() const {
        switch(type){
            case VType::FLOAT: return fval;
            case VType::INT:   return (double)ival;
            case VType::BOOL:  return bval?1.0:0.0;
            case VType::STR:   try{return std::stod(sval);}catch(...){return 0.0;}
            default: throw std::runtime_error("Cannot convert to float");
        }
    }
    bool toBool() const {
        switch(type){
            case VType::BOOL:     return bval;
            case VType::INT:      return ival!=0;
            case VType::FLOAT:    return fval!=0.0;
            case VType::STR:      return !sval.empty();
            case VType::NULL_VAL: return false;
            case VType::ARRAY:    return true;
            case VType::MAP:      return true;
            default:              return true;
        }
    }
    std::string toString() const {
        switch(type){
            case VType::INT:   return std::to_string(ival);
            case VType::FLOAT: {
                // Pretty float: remove trailing zeros but keep at least one decimal
                char buf[64]; snprintf(buf,sizeof(buf),"%.10g",fval);
                std::string s=buf;
                if(s.find('.')==std::string::npos&&s.find('e')==std::string::npos)s+=".0";
                return s;
            }
            case VType::BOOL:     return bval?"true":"false";
            case VType::STR:      return sval;
            case VType::NULL_VAL: return "null";
            case VType::STRUCT:   return "<"+obj->typeName+" instance>";
            case VType::ARRAY:{
                std::string r="[";
                for(size_t i=0;i<arr.size();i++){if(i)r+=", ";r+=arr[i]->toString();}
                return r+"]";
            }
            case VType::MAP:{
                std::string r="{";bool first=true;
                for(auto& k:*mapOrder){
                    if(!first)r+=", ";first=false;
                    r+="\""+k+"\": "+(*map)[k]->toString();
                }
                return r+"}";
            }
            case VType::FUNC:
                return "<func "+(func->name.empty()?"<lambda>":func->name)+">";
            case VType::NATIVE:{
                char buf[32];snprintf(buf,sizeof(buf),"0x%llx",(unsigned long long)native->addr);
                return "<native "+std::string(buf)+">";
            }
        }
        return "null";
    }
    bool isNull()  const { return type==VType::NULL_VAL; }
    bool isNum()   const { return type==VType::INT||type==VType::FLOAT; }
    bool isFloat() const { return type==VType::FLOAT; }
    bool isStr()   const { return type==VType::STR; }

private:
    static ValPtr make(){ return std::make_shared<Value>(); }
};

// ── Environment ──────────────────────────────────────────────
struct Env : std::enable_shared_from_this<Env> {
    std::unordered_map<std::string, ValPtr> vars;
    EnvPtr parent;

    Env()=default;
    Env(EnvPtr p):parent(std::move(p)){}

    ValPtr get(const std::string& n){
        auto it=vars.find(n);if(it!=vars.end())return it->second;
        if(parent)return parent->get(n);
        throw std::runtime_error("Undefined variable: '"+n+"'");
    }
    void define(const std::string& n, ValPtr v){ vars[n]=std::move(v); }
    void assign(const std::string& n, ValPtr v){
        auto it=vars.find(n);if(it!=vars.end()){it->second=std::move(v);return;}
        if(parent){parent->assign(n,std::move(v));return;}
        throw std::runtime_error("Undefined variable: '"+n+"'");
    }
    bool has(const std::string& n) const {
        if(vars.count(n))return true;
        if(parent)return parent->has(n);
        return false;
    }
};

// ── FuncDef (user functions registered at top level) ────────
struct FuncDef {
    std::string        name;
    std::vector<Param> params;
    TypeNode           retType;
    Stmt*              body = nullptr;
    EnvPtr             closure;
};

// ── ClassDef ─────────────────────────────────────────────────
struct ClassDef {
    std::string   name;
    std::string   parent;
    std::vector<Param> fields;
    std::unordered_map<std::string, FuncDef> methods;
};

// ── StructDef (plain struct, no methods) ────────────────────
struct StructDef {
    std::string        name;
    std::vector<Param> fields;
};
