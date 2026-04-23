#pragma once
#include "lexer.hpp"
#include "ast.hpp"
#include <stdexcept>
#include <cstdlib>

class Parser {
    std::vector<Token> toks;
    size_t pos = 0;

    Token& cur()  { return toks[pos]; }
    Token  consume(){ return toks[pos++]; }
    bool   check(TT t){ return cur().type==t; }
    bool   match(TT t){ if(check(t)){consume();return true;}return false; }
    Token  expect(TT t,const char* msg){
        if(cur().type!=t)
            throw std::runtime_error(std::string(msg)+" (got '"+cur().val+"' at line "+std::to_string(cur().line)+")");
        return consume();
    }

    // ── Type ─────────────────────────────────────────────────
    TypeNode parseType(){
        TypeNode tn;
        if(check(TT::KW_INT))    {consume();tn.name="int";}
        else if(check(TT::KW_FLOAT)) {consume();tn.name="float";}
        else if(check(TT::KW_BOOL))  {consume();tn.name="bool";}
        else if(check(TT::KW_STRING)){consume();tn.name="string";}
        else if(check(TT::KW_VOID))  {consume();tn.name="void";}
        else if(check(TT::KW_VAR))   {consume();tn.name="var";}
        else if(check(TT::IDENT))    {tn.name=consume().val;}
        else throw std::runtime_error("Expected type at line "+std::to_string(cur().line));
        if(match(TT::LBRACKET)){expect(TT::RBRACKET,"Expected ']'");tn.isArray=true;}
        return tn;
    }

    // ── Expressions ───────────────────────────────────────────
    ExprPtr parsePrimary(){
        int l=cur().line;

        // Integer literal
        if(check(TT::INT_LIT)){
            auto t=consume();
            auto e=std::make_unique<Expr>();e->kind=ExprKind::IntLit;e->line=l;
            if(t.val.size()>2&&t.val[0]=='0'&&(t.val[1]=='x'||t.val[1]=='X'))
                e->ival=(int64_t)strtoull(t.val.c_str(),nullptr,16);
            else e->ival=std::stoll(t.val);
            return e;
        }
        // Float literal
        if(check(TT::FLOAT_LIT)){
            auto t=consume();auto e=std::make_unique<Expr>();
            e->kind=ExprKind::FloatLit;e->fval=std::stod(t.val);e->line=l;return e;
        }
        // String literal
        if(check(TT::STRING_LIT)){
            auto e=std::make_unique<Expr>();e->kind=ExprKind::StrLit;
            e->sval=consume().val;e->line=l;return e;
        }
        // Bool / null literals
        if(check(TT::KW_TRUE)) {consume();auto e=std::make_unique<Expr>();e->kind=ExprKind::BoolLit;e->bval=true;e->line=l;return e;}
        if(check(TT::KW_FALSE)){consume();auto e=std::make_unique<Expr>();e->kind=ExprKind::BoolLit;e->bval=false;e->line=l;return e;}
        if(check(TT::KW_NULL)) {consume();auto e=std::make_unique<Expr>();e->kind=ExprKind::NullLit;e->line=l;return e;}

        // `this`
        if(check(TT::KW_THIS)){consume();auto e=std::make_unique<Expr>();e->kind=ExprKind::Ident;e->name="this";e->line=l;return e;}

        // `super`
        if(check(TT::KW_SUPER)){consume();auto e=std::make_unique<Expr>();e->kind=ExprKind::Super;e->line=l;return e;}

        // Array literal [...]
        if(check(TT::LBRACKET)){
            consume();
            auto e=std::make_unique<Expr>();e->kind=ExprKind::ArrayLit;e->line=l;
            if(!check(TT::RBRACKET)){
                e->args.push_back(parseExpr());
                while(match(TT::COMMA)&&!check(TT::RBRACKET))e->args.push_back(parseExpr());
            }
            expect(TT::RBRACKET,"Expected ']'");
            return e;
        }

        // Map literal {...}
        if(check(TT::LBRACE)){
            consume();
            auto e=std::make_unique<Expr>();e->kind=ExprKind::MapLit;e->line=l;
            if(!check(TT::RBRACE)){
                do {
                    e->mapKeys.push_back(parseExpr());
                    expect(TT::COLON,"Expected ':' in map literal");
                    e->args.push_back(parseExpr());
                } while(match(TT::COMMA)&&!check(TT::RBRACE));
            }
            expect(TT::RBRACE,"Expected '}'");
            return e;
        }

        // Lambda: func(params) -> ret { body }
        if(check(TT::KW_FUNC)){
            consume();
            auto e=std::make_unique<Expr>();e->kind=ExprKind::Lambda;e->line=l;
            expect(TT::LPAREN,"Expected '(' in lambda");
            if(!check(TT::RPAREN)){
                do{
                    Param p;p.type=parseType();
                    p.name=expect(TT::IDENT,"Expected param name").val;
                    e->funcParams.push_back(std::move(p));
                }while(match(TT::COMMA));
            }
            expect(TT::RPAREN,"Expected ')'");
            if(match(TT::ARROW))e->funcRetType=parseType();
            else e->funcRetType={"void",false};
            e->lambdaBody=parseBlock();
            return e;
        }

        // Cast: (type)expr  or  (expr)
        if(check(TT::LPAREN)){
            size_t saved=pos;
            try{
                consume();
                TypeNode tn=parseType();
                if(check(TT::RPAREN)){
                    consume();
                    auto inner=parseUnary();
                    auto e=std::make_unique<Expr>();e->kind=ExprKind::Cast;
                    e->castType=tn;e->left=std::move(inner);e->line=l;return e;
                }
                pos=saved;
            }catch(...){pos=saved;}
            consume();
            auto e=parseExpr();
            expect(TT::RPAREN,"Expected ')'");
            return e;
        }

        // new Type(args)  or  new Type
        if(check(TT::KW_NEW)){
            consume();
            auto e=std::make_unique<Expr>();e->kind=ExprKind::New;e->line=l;
            e->castType.name=expect(TT::IDENT,"Expected type name after 'new'").val;
            if(match(TT::LPAREN)){
                if(!check(TT::RPAREN)){
                    e->args.push_back(parseExpr());
                    while(match(TT::COMMA))e->args.push_back(parseExpr());
                }
                expect(TT::RPAREN,"Expected ')'");
            }
            return e;
        }

        // Pre-inc / dec
        if(check(TT::INC)||check(TT::DEC)){
            bool isInc=(cur().type==TT::INC);consume();
            auto e=std::make_unique<Expr>();e->kind=ExprKind::PreInc;
            e->incDir=isInc;e->incPost=false;e->line=l;
            e->left=parsePrimary();return e;
        }

        // Identifier
        if(check(TT::IDENT)){
            auto e=std::make_unique<Expr>();e->kind=ExprKind::Ident;
            e->name=consume().val;e->line=l;return e;
        }

        throw std::runtime_error("Unexpected '"+cur().val+"' at line "+std::to_string(l));
    }

    ExprPtr parsePostfix(ExprPtr base){
        while(true){
            int l=cur().line;
            if(check(TT::DOT)){
                consume();
                std::string field=expect(TT::IDENT,"Expected field name").val;
                auto e=std::make_unique<Expr>();e->kind=ExprKind::Member;
                e->left=std::move(base);e->name=field;e->line=l;
                base=std::move(e);
            } else if(check(TT::LBRACKET)){
                consume();
                auto idx=parseExpr();
                expect(TT::RBRACKET,"Expected ']'");
                auto e=std::make_unique<Expr>();e->kind=ExprKind::Index;
                e->left=std::move(base);e->right=std::move(idx);e->line=l;
                base=std::move(e);
            } else if(check(TT::LPAREN)){
                consume();
                auto e=std::make_unique<Expr>();e->kind=ExprKind::Call;e->line=l;
                e->left=std::move(base);
                if(!check(TT::RPAREN)){
                    e->args.push_back(parseExpr());
                    while(match(TT::COMMA))e->args.push_back(parseExpr());
                }
                expect(TT::RPAREN,"Expected ')'");
                base=std::move(e);
            } else if(check(TT::INC)||check(TT::DEC)){
                bool isInc=(cur().type==TT::INC);consume();
                auto e=std::make_unique<Expr>();e->kind=ExprKind::PostInc;
                e->incDir=isInc;e->incPost=true;e->line=l;
                e->left=std::move(base);base=std::move(e);
            } else break;
        }
        return base;
    }

    ExprPtr parseUnary(){
        int l=cur().line;
        if(check(TT::MINUS)||check(TT::NOT)||check(TT::BNOT)){
            std::string op=consume().val;
            auto e=std::make_unique<Expr>();e->kind=ExprKind::Unary;
            e->op=op;e->left=parseUnary();e->line=l;return e;
        }
        return parsePostfix(parsePrimary());
    }

    int opPrec(TT t){
        switch(t){
            case TT::NULLCOAL: return 1;
            case TT::OR:       return 2;
            case TT::AND:      return 3;
            case TT::BOR:      return 4;
            case TT::BXOR:     return 5;
            case TT::BAND:     return 6;
            case TT::EQ:case TT::NEQ: return 7;
            case TT::LT:case TT::GT:case TT::LEQ:case TT::GEQ: return 8;
            case TT::LSHIFT:case TT::RSHIFT: return 9;
            case TT::PLUS:case TT::MINUS: return 10;
            case TT::STAR:case TT::SLASH:case TT::PERCENT: return 11;
            default:return -1;
        }
    }

    ExprPtr parseBinary(int prec){
        auto left=parseUnary();
        while(true){
            int p=opPrec(cur().type);
            if(p<prec)break;
            std::string op=consume().val;int l=cur().line;
            auto right=parseBinary(p+1);
            auto e=std::make_unique<Expr>();
            e->kind=(op=="??")?ExprKind::NullCoal:ExprKind::Binary;
            e->op=op;e->left=std::move(left);e->right=std::move(right);e->line=l;
            left=std::move(e);
        }
        return left;
    }

    ExprPtr parseTernary(){
        auto cond=parseBinary(0);
        if(match(TT::QUESTION)){
            int l=cur().line;
            auto then=parseExpr();
            expect(TT::COLON,"Expected ':' in ternary");
            auto els=parseTernary(); // right-assoc
            auto e=std::make_unique<Expr>();e->kind=ExprKind::Ternary;
            e->left=std::move(cond);e->right=std::move(then);e->extra=std::move(els);e->line=l;
            return e;
        }
        return cond;
    }

    ExprPtr parseExpr(){
        auto left=parseTernary();
        int l=cur().line;
        if(check(TT::ASSIGN)||check(TT::PLUS_ASSIGN)||check(TT::MINUS_ASSIGN)||
           check(TT::STAR_ASSIGN)||check(TT::SLASH_ASSIGN)||check(TT::PERCENT_ASSIGN)){
            std::string op=consume().val;
            auto right=parseExpr();
            auto e=std::make_unique<Expr>();e->kind=ExprKind::Assign;
            e->op=op;e->left=std::move(left);e->right=std::move(right);e->line=l;
            return e;
        }
        return left;
    }

    // ── Statements ────────────────────────────────────────────
    StmtPtr parseBlock(){
        auto s=std::make_unique<Stmt>();s->kind=StmtKind::Block;s->line=cur().line;
        expect(TT::LBRACE,"Expected '{'");
        while(!check(TT::RBRACE)&&!check(TT::END))s->body.push_back(parseStmt());
        expect(TT::RBRACE,"Expected '}'");
        return s;
    }

    StmtPtr parseVarDecl(TypeNode type,bool expectSemi=true){
        auto s=std::make_unique<Stmt>();s->kind=StmtKind::VarDecl;s->line=cur().line;
        s->varType=type;
        s->varName=expect(TT::IDENT,"Expected variable name").val;
        if(match(TT::ASSIGN))s->varInit=parseExpr();
        if(expectSemi)expect(TT::SEMI,"Expected ';' after variable declaration");
        return s;
    }

    // Detect if the next tokens are "type? ident in"
    bool isForIn(){
        size_t saved=pos;
        try{
            // Check: [type] ident 'in'
            // First try to skip an optional type keyword
            size_t p=pos;
            // If current is a type keyword or ident possibly followed by ident+in
            // Just look ahead: skip 1 or 2 tokens to find KW_IN
            // Pattern 1: IDENT 'in'   (untyped)
            if(p<toks.size() && toks[p].type==TT::IDENT && p+1<toks.size() && toks[p+1].type==TT::KW_IN){pos=saved;return true;}
            // Pattern 2: type_kw IDENT 'in'
            bool isTypeKw=(toks[p].type==TT::KW_INT||toks[p].type==TT::KW_FLOAT||
                           toks[p].type==TT::KW_BOOL||toks[p].type==TT::KW_STRING||
                           toks[p].type==TT::KW_VAR||toks[p].type==TT::KW_VOID||
                           toks[p].type==TT::IDENT);
            if(isTypeKw && p+1<toks.size() && toks[p+1].type==TT::IDENT &&
               p+2<toks.size() && toks[p+2].type==TT::KW_IN){pos=saved;return true;}
            pos=saved;return false;
        }catch(...){pos=saved;return false;}
    }

    StmtPtr parseStmt(){
        int l=cur().line;

        // import "path";
        if(check(TT::KW_IMPORT)){
            consume();
            auto s=std::make_unique<Stmt>();s->kind=StmtKind::Import;s->line=l;
            s->importPath=expect(TT::STRING_LIT,"Expected import path string").val;
            expect(TT::SEMI,"Expected ';' after import");
            return s;
        }

        // class Name [extends Parent] { ... }
        if(check(TT::KW_CLASS)){
            consume();
            auto s=std::make_unique<Stmt>();s->kind=StmtKind::ClassDecl;s->line=l;
            s->className=expect(TT::IDENT,"Expected class name").val;
            if(match(TT::KW_EXTENDS))
                s->parentClass=expect(TT::IDENT,"Expected parent class name").val;
            expect(TT::LBRACE,"Expected '{'");
            while(!check(TT::RBRACE)&&!check(TT::END)){
                // method
                if(check(TT::KW_FUNC)){
                    s->classMethods.push_back(parseStmt());
                } else {
                    // field
                    Param p;p.type=parseType();
                    p.name=expect(TT::IDENT,"Expected field name").val;
                    expect(TT::SEMI,"Expected ';' after field");
                    s->classFields.push_back(std::move(p));
                }
            }
            expect(TT::RBRACE,"Expected '}'");
            return s;
        }

        // struct Name { ... }
        if(check(TT::KW_STRUCT)){
            consume();
            auto s=std::make_unique<Stmt>();s->kind=StmtKind::StructDecl;s->line=l;
            s->structName=expect(TT::IDENT,"Expected struct name").val;
            expect(TT::LBRACE,"Expected '{'");
            while(!check(TT::RBRACE)&&!check(TT::END)){
                Param p;p.type=parseType();
                p.name=expect(TT::IDENT,"Expected field name").val;
                expect(TT::SEMI,"Expected ';'");
                s->fields.push_back(std::move(p));
            }
            expect(TT::RBRACE,"Expected '}'");
            return s;
        }

        // func Name(params) -> ret { body }
        if(check(TT::KW_FUNC)){
            consume();
            auto s=std::make_unique<Stmt>();s->kind=StmtKind::FuncDecl;s->line=l;
            s->funcName=expect(TT::IDENT,"Expected function name").val;
            expect(TT::LPAREN,"Expected '('");
            if(!check(TT::RPAREN)){
                do{Param p;p.type=parseType();p.name=expect(TT::IDENT,"Expected param name").val;s->params.push_back(std::move(p));}
                while(match(TT::COMMA));
            }
            expect(TT::RPAREN,"Expected ')'");
            if(match(TT::ARROW))s->retType=parseType();
            else s->retType={"void",false};
            s->funcBody=parseBlock();
            return s;
        }

        // native name = (types) -> ret @ addr;
        if(check(TT::KW_NATIVE)){
            consume();
            auto s=std::make_unique<Stmt>();s->kind=StmtKind::NativeDecl;s->line=l;
            s->nativeName=expect(TT::IDENT,"Expected native name").val;
            expect(TT::ASSIGN,"Expected '='");
            expect(TT::LPAREN,"Expected '('");
            if(!check(TT::RPAREN)){
                do{s->nativeParams.push_back(parseType());}while(match(TT::COMMA));
            }
            expect(TT::RPAREN,"Expected ')'");
            if(match(TT::ARROW))s->nativeRet=parseType();
            else s->nativeRet={"void",false};
            expect(TT::AT,"Expected '@'");
            if(check(TT::STRING_LIT)){s->nativeSymbol=consume().val;}
            else{
                auto t=expect(TT::INT_LIT,"Expected address");
                if(t.val.size()>2&&t.val[0]=='0'&&(t.val[1]=='x'||t.val[1]=='X'))
                    s->nativeAddr=(uint64_t)strtoull(t.val.c_str(),nullptr,16);
                else s->nativeAddr=(uint64_t)strtoull(t.val.c_str(),nullptr,10);
            }
            expect(TT::SEMI,"Expected ';'");
            return s;
        }

        // if
        if(check(TT::KW_IF)){
            consume();
            auto s=std::make_unique<Stmt>();s->kind=StmtKind::If;s->line=l;
            expect(TT::LPAREN,"Expected '('");s->cond=parseExpr();expect(TT::RPAREN,"Expected ')'");
            s->then=parseBlock();
            if(check(TT::KW_ELSE)){
                consume();
                if(check(TT::KW_IF))s->els=parseStmt();
                else s->els=parseBlock();
            }
            return s;
        }

        // while
        if(check(TT::KW_WHILE)){
            consume();
            auto s=std::make_unique<Stmt>();s->kind=StmtKind::While;s->line=l;
            expect(TT::LPAREN,"Expected '('");s->cond=parseExpr();expect(TT::RPAREN,"Expected ')'");
            s->body.push_back(parseBlock());
            return s;
        }

        // for
        if(check(TT::KW_FOR)){
            consume();
            expect(TT::LPAREN,"Expected '('");
            // Detect for-in
            if(isForIn()){
                auto s=std::make_unique<Stmt>();s->kind=StmtKind::ForIn;s->line=l;
                // Check if typed: type_kw IDENT 'in' OR IDENT 'in'
                size_t p=pos;
                bool hasType=(toks[p].type!=TT::IDENT ||
                              (p+1<toks.size()&&toks[p+1].type==TT::KW_IN?false:true));
                // Simpler: if next is type keyword (not plain ident followed immediately by 'in')
                bool nextIsTypeKw=(toks[p].type==TT::KW_INT||toks[p].type==TT::KW_FLOAT||
                                   toks[p].type==TT::KW_BOOL||toks[p].type==TT::KW_STRING||
                                   toks[p].type==TT::KW_VAR||toks[p].type==TT::KW_VOID);
                bool plainIdentIn=(toks[p].type==TT::IDENT && p+1<toks.size() && toks[p+1].type==TT::KW_IN);
                if(!plainIdentIn && (nextIsTypeKw || (toks[p].type==TT::IDENT && p+1<toks.size() && toks[p+1].type==TT::IDENT))){
                    s->forInType=parseType();
                }
                s->forInVar=expect(TT::IDENT,"Expected variable in for-in").val;
                expect(TT::KW_IN,"Expected 'in'");
                s->forInExpr=parseExpr();
                expect(TT::RPAREN,"Expected ')'");
                s->forInBody=parseBlock();
                return s;
            }
            // Traditional for
            auto s=std::make_unique<Stmt>();s->kind=StmtKind::For;s->line=l;
            if(!check(TT::SEMI)){
                size_t saved=pos;
                bool gotDecl=false;
                try{
                    TypeNode t=parseType();
                    if(check(TT::IDENT)){s->forInit=parseVarDecl(t,true);gotDecl=true;}
                    else{pos=saved;}
                }catch(...){pos=saved;}
                if(!gotDecl){
                    auto ie=parseExpr();expect(TT::SEMI,"Expected ';'");
                    auto es=std::make_unique<Stmt>();es->kind=StmtKind::ExprStmt;es->expr=std::move(ie);
                    s->forInit=std::move(es);
                }
            } else consume();
            if(!check(TT::SEMI))s->forCond=parseExpr();
            expect(TT::SEMI,"Expected ';' in for");
            if(!check(TT::RPAREN))s->forStep=parseExpr();
            expect(TT::RPAREN,"Expected ')'");
            s->forBody=parseBlock();
            return s;
        }

        // try { } catch (var) { }
        if(check(TT::KW_TRY)){
            consume();
            auto s=std::make_unique<Stmt>();s->kind=StmtKind::TryCatch;s->line=l;
            s->tryBody=parseBlock();
            expect(TT::KW_CATCH,"Expected 'catch'");
            expect(TT::LPAREN,"Expected '('");
            s->catchVar=expect(TT::IDENT,"Expected catch variable").val;
            expect(TT::RPAREN,"Expected ')'");
            s->catchBody=parseBlock();
            return s;
        }

        // throw expr;
        if(check(TT::KW_THROW)){
            consume();
            auto s=std::make_unique<Stmt>();s->kind=StmtKind::Throw;s->line=l;
            s->expr=parseExpr();
            expect(TT::SEMI,"Expected ';'");
            return s;
        }

        // return
        if(check(TT::KW_RETURN)){
            consume();
            auto s=std::make_unique<Stmt>();s->kind=StmtKind::Return;s->line=l;
            if(!check(TT::SEMI))s->expr=parseExpr();
            expect(TT::SEMI,"Expected ';'");
            return s;
        }

        // break / continue
        if(check(TT::KW_BREAK)){consume();auto s=std::make_unique<Stmt>();s->kind=StmtKind::Break;s->line=l;expect(TT::SEMI,"Expected ';'");return s;}
        if(check(TT::KW_CONTINUE)){consume();auto s=std::make_unique<Stmt>();s->kind=StmtKind::Continue;s->line=l;expect(TT::SEMI,"Expected ';'");return s;}

        // block
        if(check(TT::LBRACE))return parseBlock();

        // var declaration (type-inferred)
        if(check(TT::KW_VAR)){
            consume();
            TypeNode t; t.name="var";
            return parseVarDecl(t);
        }

        // Try typed variable declaration
        {
            size_t saved=pos;
            try{
                TypeNode t=parseType();
                if(check(TT::IDENT))return parseVarDecl(t);
                pos=saved;
            }catch(...){pos=saved;}
        }

        // Expression statement
        {
            auto s=std::make_unique<Stmt>();s->kind=StmtKind::ExprStmt;s->line=l;
            s->expr=parseExpr();
            expect(TT::SEMI,"Expected ';'");
            return s;
        }
    }

public:
    Parser(std::vector<Token> t):toks(std::move(t)){}

    Program parse(){
        Program prog;
        while(!check(TT::END))prog.stmts.push_back(parseStmt());
        return prog;
    }
};
