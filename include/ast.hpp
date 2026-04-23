#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

struct Expr; struct Stmt;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

// ── Type node ────────────────────────────────────────────────
struct TypeNode {
    std::string name; // int, float, bool, string, void, var, struct/class name
    bool isArray = false;
};

// ── Param ────────────────────────────────────────────────────
struct Param { TypeNode type; std::string name; };

// ── Expressions ──────────────────────────────────────────────
enum class ExprKind {
    IntLit, FloatLit, BoolLit, StrLit, NullLit,
    Ident,
    Binary,    // a OP b
    Unary,     // OP a
    Ternary,   // cond ? then : else
    NullCoal,  // a ?? b
    Assign,    // a = b  (or a op= b)
    Call,      // f(args)
    Member,    // a.field
    Index,     // a[i]
    Cast,      // (type)expr
    New,       // new Type(args)
    PreInc,    // ++a / --a
    PostInc,   // a++ / a--
    Lambda,    // func(params)->ret { body }
    ArrayLit,  // [1, 2, 3]
    MapLit,    // {"k": v, ...}
    Super,     // super
};

struct Expr {
    ExprKind    kind;
    int         line = 0;

    // Literals
    int64_t     ival = 0;
    double      fval = 0.0;
    bool        bval = false;
    std::string sval;

    // Ident / Member / Call
    std::string name;

    // Binary / Unary / Assign
    std::string op;
    ExprPtr     left, right, extra; // extra = ternary else / null-coal right

    // Call args / ArrayLit elements / MapLit values
    std::vector<ExprPtr> args;

    // MapLit keys (same length as args for MapLit)
    std::vector<ExprPtr> mapKeys;

    // Cast / New
    TypeNode castType;

    // Lambda
    std::vector<Param>    funcParams;
    TypeNode              funcRetType;
    std::unique_ptr<Stmt> lambdaBody;

    // Inc/dec
    bool incDir  = true;  // true=++ false=--
    bool incPost = false; // true=postfix
};

// ── Statements ───────────────────────────────────────────────
enum class StmtKind {
    Block, ExprStmt, VarDecl,
    If, While, For, ForIn,
    Return, Break, Continue,
    StructDecl, FuncDecl, ClassDecl,
    NativeDecl, Import,
    TryCatch, Throw,
};

struct Stmt {
    StmtKind    kind;
    int         line = 0;

    // Block / ClassDecl body
    std::vector<StmtPtr> body;

    // ExprStmt / Return / Throw / VarDecl init
    ExprPtr expr;

    // VarDecl
    TypeNode    varType;
    std::string varName;
    ExprPtr     varInit;

    // If
    ExprPtr  cond;
    StmtPtr  then, els;

    // While
    // cond + body[0]

    // For
    StmtPtr forInit;
    ExprPtr forCond, forStep;
    StmtPtr forBody;

    // ForIn
    std::string forInVar;
    TypeNode    forInType; // optional type
    ExprPtr     forInExpr;
    StmtPtr     forInBody;

    // StructDecl
    std::string        structName;
    std::vector<Param> fields;

    // FuncDecl
    std::string        funcName;
    TypeNode           retType;
    std::vector<Param> params;
    StmtPtr            funcBody;

    // ClassDecl
    std::string        className;
    std::string        parentClass;
    std::vector<Param> classFields;
    std::vector<StmtPtr> classMethods; // FuncDecl stmts

    // NativeDecl
    std::string            nativeName;
    TypeNode               nativeRet;
    std::vector<TypeNode>  nativeParams;
    uint64_t               nativeAddr   = 0;
    std::string            nativeSymbol;

    // Import
    std::string importPath;

    // TryCatch
    StmtPtr     tryBody;
    std::string catchVar;
    StmtPtr     catchBody;
};

struct Program { std::vector<StmtPtr> stmts; };
