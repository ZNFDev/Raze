#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

struct Expr; struct Stmt;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

struct TypeNode {
    std::string name;
    bool isArray    = false;
    bool isNullable = false;
};

struct Param {
    TypeNode    type;
    std::string name;
    ExprPtr     defaultVal;
    bool        isVariadic = false;
    // Allow copying (clone the defaultVal expr minimally)
    Param() = default;
    Param(const Param& o) : type(o.type), name(o.name), isVariadic(o.isVariadic) {}
    Param& operator=(const Param& o) { type=o.type; name=o.name; isVariadic=o.isVariadic; defaultVal=nullptr; return *this; }
    Param(Param&&) = default;
    Param& operator=(Param&&) = default;
};

struct MatchArm {
    std::vector<ExprPtr> patterns;
    ExprPtr              guard;
    StmtPtr              body;
    bool                 isDefault = false;
};

enum class ExprKind {
    IntLit, FloatLit, BoolLit, StrLit, NullLit, InterpStr,
    Ident, Binary, Unary, Ternary, NullCoal, Assign,
    Call, Member, OptMember, Index, OptIndex,
    Cast, New, PreInc, PostInc, Lambda, ArrayLit, MapLit,
    Super, Spread, Is, In,
};

struct Expr {
    ExprKind    kind; int line=0;
    int64_t     ival=0; double fval=0; bool bval=false;
    std::string sval, name, op, isType;
    ExprPtr     left, right, extra;
    std::vector<ExprPtr>   args, mapKeys;
    TypeNode    castType;
    std::vector<Param>     funcParams;
    TypeNode               funcRetType;
    std::unique_ptr<Stmt>  lambdaBody;
    bool incDir=true, incPost=false;
};

enum class StmtKind {
    Block, ExprStmt, VarDecl,
    If, While, For, ForIn, Switch, Match,
    Return, Break, Continue,
    StructDecl, FuncDecl, ClassDecl, EnumDecl, InterfaceDecl,
    NativeDecl, Import, TryCatch, Throw, Defer,
};

struct SwitchCase {
    std::vector<ExprPtr> values;
    std::vector<StmtPtr> body;
    bool isDefault = false;
    bool isFallthrough = false;
};

struct Stmt {
    StmtKind kind; int line=0;
    std::vector<StmtPtr> body;
    ExprPtr expr;
    TypeNode varType; std::string varName; ExprPtr varInit; bool isConst=false;
    ExprPtr cond; StmtPtr then, els;
    StmtPtr forInit; ExprPtr forCond, forStep; StmtPtr forBody;
    std::string forInVar; TypeNode forInType; ExprPtr forInExpr; StmtPtr forInBody;
    ExprPtr switchExpr; std::vector<SwitchCase> cases;
    ExprPtr matchVal; std::vector<MatchArm> arms;
    std::string structName; std::vector<Param> fields;
    std::string funcName; TypeNode retType; std::vector<Param> params;
    StmtPtr funcBody; bool isStatic=false;
    std::string className, parentClass;
    std::vector<std::string> interfaces;
    std::vector<Param> classFields; std::vector<StmtPtr> classMethods;
    std::string enumName; std::vector<std::string> enumValues; std::vector<ExprPtr> enumInits;
    std::string ifaceName; std::vector<std::string> ifaceMethods;
    std::string nativeName; TypeNode nativeRet; std::vector<TypeNode> nativeParams;
    uint64_t nativeAddr=0; std::string nativeSymbol;
    std::string importPath, importAlias;
};

struct Program { std::vector<StmtPtr> stmts; };
