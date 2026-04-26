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

struct Value; struct Env;
using ValPtr = std::shared_ptr<Value>;
using EnvPtr = std::shared_ptr<Env>;

struct StructInstance {
    std::string typeName;
    std::unordered_map<std::string,ValPtr> fields;
};

struct NativeInfo {
    uintptr_t   addr = 0;
    std::string retType;
    std::vector<std::string> paramTypes;
    std::function<ValPtr(std::vector<ValPtr>)> callback;
};

struct FuncValue {
    std::string        name;
    std::vector<Param> params;
    TypeNode           retType;
    Stmt*              body = nullptr;
    EnvPtr             closure;
    std::function<ValPtr(std::vector<ValPtr>)> callback;
};

enum class VType { INT, FLOAT, BOOL, STR, STRUCT, ARRAY, MAP, FUNC, NATIVE, NULL_VAL };

struct Value {
    VType type = VType::NULL_VAL;
    int64_t     ival = 0;
    double      fval = 0.0;
    bool        bval = false;
    std::string sval;
    bool        isConst = false;

    std::shared_ptr<StructInstance>                       obj;
    std::vector<ValPtr>                                   arr;
    std::shared_ptr<std::unordered_map<std::string,ValPtr>> map;
    std::shared_ptr<std::vector<std::string>>             mapOrder;
    std::shared_ptr<FuncValue>                            func;
    std::shared_ptr<NativeInfo>                           native;

    static ValPtr fromInt(int64_t v)    { auto p=mk();p->type=VType::INT;  p->ival=v;return p;}
    static ValPtr fromFloat(double v)   { auto p=mk();p->type=VType::FLOAT;p->fval=v;return p;}
    static ValPtr fromBool(bool v)      { auto p=mk();p->type=VType::BOOL; p->bval=v;return p;}
    static ValPtr fromStr(std::string v){ auto p=mk();p->type=VType::STR;  p->sval=std::move(v);return p;}
    static ValPtr null()                { return mk(); }
    static ValPtr fromStruct(const std::string& n){
        auto p=mk();p->type=VType::STRUCT;p->obj=std::make_shared<StructInstance>();p->obj->typeName=n;return p;}
    static ValPtr fromArray(){auto p=mk();p->type=VType::ARRAY;return p;}
    static ValPtr fromMap(){
        auto p=mk();p->type=VType::MAP;
        p->map=std::make_shared<std::unordered_map<std::string,ValPtr>>();
        p->mapOrder=std::make_shared<std::vector<std::string>>();return p;}
    static ValPtr fromFunc(std::shared_ptr<FuncValue> fv){auto p=mk();p->type=VType::FUNC;p->func=std::move(fv);return p;}
    static ValPtr fromNative(std::shared_ptr<NativeInfo> ni){auto p=mk();p->type=VType::NATIVE;p->native=std::move(ni);return p;}

    ValPtr mapGet(const std::string& k)const{if(!map)return null();auto it=map->find(k);if(it!=map->end())return it->second;return null();}
    void mapSet(const std::string& k,ValPtr v){if(!map)return;if(!map->count(k))mapOrder->push_back(k);(*map)[k]=std::move(v);}
    bool mapHas(const std::string& k)const{return map&&map->count(k)>0;}
    void mapDel(const std::string& k){if(!map)return;map->erase(k);auto&o=*mapOrder;o.erase(std::remove(o.begin(),o.end(),k),o.end());}

    int64_t toInt()const{
        switch(type){case VType::INT:return ival;case VType::FLOAT:return(int64_t)fval;
            case VType::BOOL:return bval?1:0;
            case VType::STR:try{return std::stoll(sval);}catch(...){return 0;}
            default:throw std::runtime_error("Cannot convert to int");}}
    double toFloat()const{
        switch(type){case VType::FLOAT:return fval;case VType::INT:return(double)ival;
            case VType::BOOL:return bval?1.0:0.0;
            case VType::STR:try{return std::stod(sval);}catch(...){return 0.0;}
            default:throw std::runtime_error("Cannot convert to float");}}
    bool toBool()const{
        switch(type){case VType::BOOL:return bval;case VType::INT:return ival!=0;
            case VType::FLOAT:return fval!=0.0;case VType::STR:return!sval.empty();
            case VType::NULL_VAL:return false;default:return true;}}
    std::string toString()const{
        switch(type){
            case VType::INT:return std::to_string(ival);
            case VType::FLOAT:{char b[64];snprintf(b,64,"%.10g",fval);
                std::string s=b;if(s.find('.')==std::string::npos&&s.find('e')==std::string::npos)s+=".0";return s;}
            case VType::BOOL:return bval?"true":"false";
            case VType::STR:return sval;
            case VType::NULL_VAL:return "null";
            case VType::STRUCT:return "<"+obj->typeName+" instance>";
            case VType::ARRAY:{std::string r="[";for(size_t i=0;i<arr.size();i++){if(i)r+=", ";r+=arr[i]->toString();}return r+"]";}
            case VType::MAP:{std::string r="{";bool f=true;for(auto&k:*mapOrder){if(!f)r+=", ";f=false;r+="\""+k+"\": "+(*map)[k]->toString();}return r+"}";}
            case VType::FUNC:return "<func "+(func->name.empty()?"<lambda>":func->name)+">";
            case VType::NATIVE:{char b[32];snprintf(b,32,"0x%llx",(unsigned long long)native->addr);return "<native "+std::string(b)+">";}
        }return "null";}
    bool isNull()const{return type==VType::NULL_VAL;}
    bool isNum()const{return type==VType::INT||type==VType::FLOAT;}
    bool isFloat()const{return type==VType::FLOAT;}
    bool isStr()const{return type==VType::STR;}
private:
    static ValPtr mk(){return std::make_shared<Value>();}
};

struct Env:std::enable_shared_from_this<Env>{
    std::unordered_map<std::string,ValPtr> vars;
    std::unordered_map<std::string,bool>   consts;
    EnvPtr parent;
    Env()=default;Env(EnvPtr p):parent(std::move(p)){}
    ValPtr get(const std::string&n){
        auto it=vars.find(n);if(it!=vars.end())return it->second;
        if(parent)return parent->get(n);
        throw std::runtime_error("Undefined variable: '"+n+"'");}
    void define(const std::string&n,ValPtr v,bool isConst=false){vars[n]=std::move(v);consts[n]=isConst;}
    void assign(const std::string&n,ValPtr v){
        auto it=vars.find(n);if(it!=vars.end()){
            if(consts.count(n)&&consts[n])throw std::runtime_error("Cannot reassign const '"+n+"'");
            it->second=std::move(v);return;}
        if(parent){parent->assign(n,std::move(v));return;}
        throw std::runtime_error("Undefined variable: '"+n+"'");}
    bool has(const std::string&n)const{if(vars.count(n))return true;if(parent)return parent->has(n);return false;}
};

struct FuncDef{std::string name;std::vector<Param>params;TypeNode retType;Stmt*body=nullptr;EnvPtr closure;std::string ownerClass;/* class that defines this method */};
struct ClassDef{
    std::string name,parent;
    std::vector<std::string> interfaces;
    std::vector<Param>fields;
    std::unordered_map<std::string,FuncDef>methods;
    std::unordered_map<std::string,FuncDef>staticMethods;
    std::unordered_map<std::string,ValPtr>  staticFields;
};
struct StructDef{std::string name;std::vector<Param>fields;};
struct EnumDef{std::string name;std::vector<std::string>values;std::unordered_map<std::string,int64_t>valueMap;};
struct InterfaceDef{std::string name;std::vector<std::string>methods;};
