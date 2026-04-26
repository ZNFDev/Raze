#pragma once
#include "lexer.hpp"
#include "ast.hpp"
#include <stdexcept>
#include <cstdlib>
#include <functional>
#include <sstream>

class Parser {
    std::vector<Token> toks;
    size_t pos=0;

    Token& cur()  { return toks[pos]; }
    Token  consume(){ return toks[pos++]; }
    bool   check(TT t){ return cur().type==t; }
    bool   match(TT t){ if(check(t)){consume();return true;}return false; }
    Token  expect(TT t,const char* msg){
        if(cur().type!=t)
            throw std::runtime_error(std::string(msg)+" — got '"+cur().val+"' at line "+std::to_string(cur().line));
        return consume();
    }
    bool   checkAny(std::initializer_list<TT> ts){ for(auto t:ts)if(check(t))return true;return false; }

    // ── Types ─────────────────────────────────────────────────
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
        if(match(TT::QUESTION))tn.isNullable=true;
        return tn;
    }

    // ── Param with optional default & variadics ───────────────
    Param parseParam(){
        Param p;
        if(check(TT::ELLIPSIS)){consume();p.isVariadic=true;p.type.name="var";}
        else p.type=parseType();
        p.name=expect(TT::IDENT,"Expected param name").val;
        if(match(TT::ASSIGN))p.defaultVal=parseExpr();
        return p;
    }

    // ── Interpolated string expansion ─────────────────────────
    ExprPtr buildInterp(const std::string& tmpl, int l){
        // Parse template: split on ${...} respecting nested strings and braces
        std::vector<std::string> parts;
        std::vector<std::string> exprSrcs;
        size_t i=0;
        std::string cur_part;
        while(i<tmpl.size()){
            if(tmpl[i]=='$'&&i+1<tmpl.size()&&tmpl[i+1]=='{'){
                parts.push_back(cur_part); cur_part=""; i+=2;
                std::string expr_src; int depth=1;
                while(i<tmpl.size()&&depth>0){
                    char c=tmpl[i];
                    // Handle strings inside expression — skip over them
                    if(c=='"'){
                        expr_src+=c; i++;
                        while(i<tmpl.size()&&tmpl[i]!='"'){
                            if(tmpl[i]=='\\'&&i+1<tmpl.size()){expr_src+=tmpl[i];expr_src+=tmpl[i+1];i+=2;}
                            else{expr_src+=tmpl[i++];}
                        }
                        if(i<tmpl.size()){expr_src+=tmpl[i++];}
                        continue;
                    }
                    if(c=='\''){
                        expr_src+=c; i++;
                        while(i<tmpl.size()&&tmpl[i]!='\''){
                            if(tmpl[i]=='\\'&&i+1<tmpl.size()){expr_src+=tmpl[i];expr_src+=tmpl[i+1];i+=2;}
                            else{expr_src+=tmpl[i++];}
                        }
                        if(i<tmpl.size()){expr_src+=tmpl[i++];}
                        continue;
                    }
                    if(c=='{')depth++;
                    else if(c=='}'){depth--;if(depth==0){i++;break;}}
                    if(depth>0)expr_src+=tmpl[i];
                    i++;
                }
                exprSrcs.push_back(expr_src);
            }else{cur_part+=tmpl[i++];}
        }
        parts.push_back(cur_part);

        // Build concat tree
        auto makeStr=[&](const std::string& s)->ExprPtr{
            auto e=std::make_unique<Expr>(); e->kind=ExprKind::StrLit; e->sval=s; e->line=l; return e;
        };
        auto strCall=[&](ExprPtr inner)->ExprPtr{
            // str(inner)
            auto e=std::make_unique<Expr>(); e->kind=ExprKind::Call; e->line=l;
            auto fn=std::make_unique<Expr>(); fn->kind=ExprKind::Ident; fn->name="str"; fn->line=l;
            e->left=std::move(fn); e->args.push_back(std::move(inner)); return e;
        };

        ExprPtr result=makeStr(parts[0]);
        for(size_t k=0;k<exprSrcs.size();k++){
            // Parse the expression inside ${}
            Lexer lex(exprSrcs[k]); auto subtoks=lex.tokenize();
            Parser subp(std::move(subtoks));
            auto sub=subp.parseExpr();
            // str(sub)
            auto strSub=strCall(std::move(sub));
            // result = result + strSub
            auto cat=std::make_unique<Expr>(); cat->kind=ExprKind::Binary; cat->op="+"; cat->line=l;
            cat->left=std::move(result); cat->right=std::move(strSub);
            result=std::move(cat);
            // + next part
            if(!parts[k+1].empty()){
                auto cat2=std::make_unique<Expr>(); cat2->kind=ExprKind::Binary; cat2->op="+"; cat2->line=l;
                cat2->left=std::move(result); cat2->right=makeStr(parts[k+1]);
                result=std::move(cat2);
            }
        }
        return result;
    }

    // ── Primary ───────────────────────────────────────────────
    ExprPtr parsePrimary(){
        int l=cur().line;

        if(check(TT::INT_LIT)){
            auto t=consume(); auto e=std::make_unique<Expr>(); e->kind=ExprKind::IntLit; e->line=l;
            const std::string& v=t.val;
            if(v.size()>2&&v[0]=='0'&&(v[1]=='x'||v[1]=='X'))e->ival=(int64_t)strtoull(v.c_str(),nullptr,16);
            else if(v.size()>2&&v[0]=='0'&&(v[1]=='b'||v[1]=='B'))e->ival=(int64_t)strtoull(v.c_str()+2,nullptr,2);
            else if(v.size()>2&&v[0]=='0'&&(v[1]=='o'||v[1]=='O'))e->ival=(int64_t)strtoull(v.c_str()+2,nullptr,8);
            else e->ival=std::stoll(v);
            return e;
        }
        if(check(TT::FLOAT_LIT)){auto t=consume();auto e=std::make_unique<Expr>();e->kind=ExprKind::FloatLit;e->fval=std::stod(t.val);e->line=l;return e;}
        if(check(TT::STRING_LIT)){auto e=std::make_unique<Expr>();e->kind=ExprKind::StrLit;e->sval=consume().val;e->line=l;return e;}
        if(check(TT::INTERP_STRING)){auto t=consume();return buildInterp(t.val,l);}
        if(check(TT::KW_TRUE)) {consume();auto e=std::make_unique<Expr>();e->kind=ExprKind::BoolLit;e->bval=true;e->line=l;return e;}
        if(check(TT::KW_FALSE)){consume();auto e=std::make_unique<Expr>();e->kind=ExprKind::BoolLit;e->bval=false;e->line=l;return e;}
        if(check(TT::KW_NULL)) {consume();auto e=std::make_unique<Expr>();e->kind=ExprKind::NullLit;e->line=l;return e;}
        if(check(TT::KW_THIS)) {consume();auto e=std::make_unique<Expr>();e->kind=ExprKind::Ident;e->name="this";e->line=l;return e;}
        if(check(TT::KW_SUPER)){consume();auto e=std::make_unique<Expr>();e->kind=ExprKind::Super;e->line=l;return e;}

        // Type keywords used as conversion calls: int(x), float(x), bool(x), string(x)
        if(checkAny({TT::KW_INT,TT::KW_FLOAT,TT::KW_BOOL,TT::KW_STRING})){
            std::string fname;
            switch(cur().type){
                case TT::KW_INT:    fname="int";    break;
                case TT::KW_FLOAT:  fname="float";  break;
                case TT::KW_BOOL:   fname="bool";   break;
                case TT::KW_STRING: fname="str";    break;
                default: break;
            }
            consume(); // eat the type keyword
            if(check(TT::LPAREN)){
                // It's a call: int(expr)
                consume();
                auto e=std::make_unique<Expr>();e->kind=ExprKind::Call;e->line=l;
                auto fn=std::make_unique<Expr>();fn->kind=ExprKind::Ident;fn->name=fname;fn->line=l;
                e->left=std::move(fn);
                if(!check(TT::RPAREN)){
                    e->args.push_back(parseExpr());
                    while(match(TT::COMMA))e->args.push_back(parseExpr());
                }
                expect(TT::RPAREN,"Expected ')'");
                return e;
            }
            // Not a call — treat as ident (should be rare)
            auto e=std::make_unique<Expr>();e->kind=ExprKind::Ident;e->name=fname;e->line=l;return e;
        }

        // typeof / sizeof as prefix
        if(check(TT::KW_TYPEOF)){
            consume();
            auto e=std::make_unique<Expr>();e->kind=ExprKind::Call;e->line=l;
            auto fn=std::make_unique<Expr>();fn->kind=ExprKind::Ident;fn->name="typeof";fn->line=l;
            e->left=std::move(fn);
            expect(TT::LPAREN,"Expected '('");
            e->args.push_back(parseExpr());
            expect(TT::RPAREN,"Expected ')'");
            return e;
        }

        // Array literal
        if(check(TT::LBRACKET)){
            consume();auto e=std::make_unique<Expr>();e->kind=ExprKind::ArrayLit;e->line=l;
            if(!check(TT::RBRACKET)){
                do {
                    if(check(TT::ELLIPSIS)){consume();auto sp=std::make_unique<Expr>();sp->kind=ExprKind::Spread;sp->line=cur().line;sp->left=parseExpr();e->args.push_back(std::move(sp));}
                    else e->args.push_back(parseExpr());
                } while(match(TT::COMMA)&&!check(TT::RBRACKET));
            }
            expect(TT::RBRACKET,"Expected ']'");return e;
        }

        // Map literal
        if(check(TT::LBRACE)){
            consume();auto e=std::make_unique<Expr>();e->kind=ExprKind::MapLit;e->line=l;
            if(!check(TT::RBRACE)){
                do{
                    e->mapKeys.push_back(parseExpr());
                    expect(TT::COLON,"Expected ':' in map literal");
                    e->args.push_back(parseExpr());
                }while(match(TT::COMMA)&&!check(TT::RBRACE));
            }
            expect(TT::RBRACE,"Expected '}'");return e;
        }

        // Lambda: func(params)->ret{body}
        if(check(TT::KW_FUNC)){
            consume();auto e=std::make_unique<Expr>();e->kind=ExprKind::Lambda;e->line=l;
            expect(TT::LPAREN,"Expected '('");
            if(!check(TT::RPAREN)){
                do{
                    e->funcParams.push_back(parseParam());
                    if(e->funcParams.back().isVariadic)break;
                }while(match(TT::COMMA));
            }
            expect(TT::RPAREN,"Expected ')'");
            if(match(TT::ARROW))e->funcRetType=parseType();
            else e->funcRetType={"void",false};
            e->lambdaBody=parseBlock();return e;
        }

        // Spread: ...expr
        if(check(TT::ELLIPSIS)){
            consume();auto e=std::make_unique<Expr>();e->kind=ExprKind::Spread;e->line=l;
            e->left=parseUnary();return e;
        }

        // Cast or grouped expr
        if(check(TT::LPAREN)){
            size_t saved=pos;
            try{
                consume();TypeNode tn=parseType();
                if(check(TT::RPAREN)){
                    consume();auto inner=parseUnary();
                    auto e=std::make_unique<Expr>();e->kind=ExprKind::Cast;
                    e->castType=tn;e->left=std::move(inner);e->line=l;return e;
                }
                pos=saved;
            }catch(...){pos=saved;}
            consume();auto e=parseExpr();expect(TT::RPAREN,"Expected ')'");return e;
        }

        // new Type(args)
        if(check(TT::KW_NEW)){
            consume();auto e=std::make_unique<Expr>();e->kind=ExprKind::New;e->line=l;
            e->castType.name=expect(TT::IDENT,"Expected class name after 'new'").val;
            if(match(TT::LPAREN)){
                if(!check(TT::RPAREN)){
                    do{
                        if(check(TT::ELLIPSIS)){consume();auto sp=std::make_unique<Expr>();sp->kind=ExprKind::Spread;sp->line=cur().line;sp->left=parseExpr();e->args.push_back(std::move(sp));}
                        else e->args.push_back(parseExpr());
                    }while(match(TT::COMMA));
                }
                expect(TT::RPAREN,"Expected ')'");
            }
            return e;
        }

        // Pre-inc/dec
        if(check(TT::INC)||check(TT::DEC)){
            bool isInc=(cur().type==TT::INC);consume();
            auto e=std::make_unique<Expr>();e->kind=ExprKind::PreInc;
            e->incDir=isInc;e->line=l;e->left=parsePrimary();return e;
        }

        // Identifier (may be ENUM::VARIANT)
        if(check(TT::IDENT)){
            std::string n=consume().val;
            if(check(TT::DCOLON)){
                consume();
                std::string variant=expect(TT::IDENT,"Expected enum variant").val;
                auto e=std::make_unique<Expr>();e->kind=ExprKind::Member;e->line=l;
                auto base=std::make_unique<Expr>();base->kind=ExprKind::Ident;base->name=n;base->line=l;
                e->left=std::move(base);e->name=variant;return e;
            }
            auto e=std::make_unique<Expr>();e->kind=ExprKind::Ident;e->name=n;e->line=l;return e;
        }

        throw std::runtime_error("Unexpected '"+cur().val+"' at line "+std::to_string(l));
    }

    ExprPtr parsePostfix(ExprPtr base){
        while(true){
            int l=cur().line;
            if(check(TT::DOT)){
                consume();std::string f=expect(TT::IDENT,"Expected field name").val;
                auto e=std::make_unique<Expr>();e->kind=ExprKind::Member;
                e->left=std::move(base);e->name=f;e->line=l;base=std::move(e);
            } else if(check(TT::OPT_DOT)){
                consume();std::string f=expect(TT::IDENT,"Expected field name after ?.").val;
                auto e=std::make_unique<Expr>();e->kind=ExprKind::OptMember;
                e->left=std::move(base);e->name=f;e->line=l;base=std::move(e);
            } else if(check(TT::LBRACKET)){
                consume();auto idx=parseExpr();expect(TT::RBRACKET,"Expected ']'");
                auto e=std::make_unique<Expr>();e->kind=ExprKind::Index;
                e->left=std::move(base);e->right=std::move(idx);e->line=l;base=std::move(e);
            } else if(check(TT::OPT_BRACKET)){
                consume();auto idx=parseExpr();expect(TT::RBRACKET,"Expected ']'");
                auto e=std::make_unique<Expr>();e->kind=ExprKind::OptIndex;
                e->left=std::move(base);e->right=std::move(idx);e->line=l;base=std::move(e);
            } else if(check(TT::LPAREN)){
                consume();auto e=std::make_unique<Expr>();e->kind=ExprKind::Call;e->line=l;
                e->left=std::move(base);
                if(!check(TT::RPAREN)){
                    do{
                        if(check(TT::ELLIPSIS)){consume();auto sp=std::make_unique<Expr>();sp->kind=ExprKind::Spread;sp->line=cur().line;sp->left=parseExpr();e->args.push_back(std::move(sp));}
                        else e->args.push_back(parseExpr());
                    }while(match(TT::COMMA));
                }
                expect(TT::RPAREN,"Expected ')'");base=std::move(e);
            } else if(check(TT::INC)||check(TT::DEC)){
                bool isInc=(cur().type==TT::INC);consume();
                auto e=std::make_unique<Expr>();e->kind=ExprKind::PostInc;
                e->incDir=isInc;e->incPost=true;e->line=l;e->left=std::move(base);base=std::move(e);
            } else if(check(TT::KW_IS)){
                consume();std::string ty;
                if(check(TT::KW_INT)||check(TT::KW_FLOAT)||check(TT::KW_BOOL)||
                   check(TT::KW_STRING)||check(TT::IDENT))ty=consume().val;
                else throw std::runtime_error("Expected type after 'is'");
                auto e=std::make_unique<Expr>();e->kind=ExprKind::Is;e->isType=ty;e->line=l;
                e->left=std::move(base);base=std::move(e);
            } else break;
        }
        return base;
    }

    ExprPtr parseUnary(){
        int l=cur().line;
        if(check(TT::MINUS)||check(TT::NOT)||check(TT::BNOT)){
            std::string op=consume().val;
            auto e=std::make_unique<Expr>();e->kind=ExprKind::Unary;e->op=op;e->line=l;
            e->left=parseUnary();return e;
        }
        return parsePostfix(parsePrimary());
    }

    int opPrec(TT t){
        switch(t){
            case TT::NULLCOAL:  return 1;
            case TT::OR:        return 2;
            case TT::AND:       return 3;
            case TT::BOR:       return 4;
            case TT::BXOR:      return 5;
            case TT::BAND:      return 6;
            case TT::EQ: case TT::NEQ: return 7;
            case TT::LT: case TT::GT: case TT::LEQ: case TT::GEQ:
            case TT::KW_IN:     return 8;
            case TT::LSHIFT: case TT::RSHIFT: return 9;
            case TT::PLUS: case TT::MINUS: return 10;
            case TT::STAR: case TT::SLASH: case TT::PERCENT: return 11;
            case TT::STARSTAR:  return 12;
            default:            return -1;
        }
    }

    ExprPtr parseBinary(int prec){
        auto left=parseUnary();
        while(true){
            int p=opPrec(cur().type);
            if(p<prec)break;
            // 'in' operator
            if(check(TT::KW_IN)){
                consume();int l=cur().line;
                auto right=parseBinary(p+1);
                auto e=std::make_unique<Expr>();e->kind=ExprKind::In;e->line=l;
                e->left=std::move(left);e->right=std::move(right);left=std::move(e);
                continue;
            }
            std::string op=consume().val;int l=cur().line;
            auto right=parseBinary(p+1);
            auto e=std::make_unique<Expr>();e->kind=ExprKind::Binary;
            e->op=op;e->left=std::move(left);e->right=std::move(right);e->line=l;
            left=std::move(e);
        }
        return left;
    }

    ExprPtr parseTernary(){
        auto cond=parseBinary(0);
        if(match(TT::QUESTION)){
            int l=cur().line;
            auto t=parseExpr();
            expect(TT::COLON,"Expected ':' in ternary");
            auto e2=parseTernary();
            auto e=std::make_unique<Expr>();e->kind=ExprKind::Ternary;e->line=l;
            e->left=std::move(cond);e->right=std::move(t);e->extra=std::move(e2);return e;
        }
        return cond;
    }

public:
    ExprPtr parseExpr(){
        auto left=parseTernary();
        int l=cur().line;
        if(checkAny({TT::ASSIGN,TT::PLUS_ASSIGN,TT::MINUS_ASSIGN,TT::STAR_ASSIGN,
                     TT::SLASH_ASSIGN,TT::PERCENT_ASSIGN,TT::STARSTAR_ASSIGN,
                     TT::AND_ASSIGN,TT::OR_ASSIGN})){
            std::string op=consume().val;
            auto right=parseExpr();
            auto e=std::make_unique<Expr>();e->kind=ExprKind::Assign;
            e->op=op;e->left=std::move(left);e->right=std::move(right);e->line=l;return e;
        }
        return left;
    }

private:
    // ── Statements ────────────────────────────────────────────
    StmtPtr parseBlock(){
        auto s=std::make_unique<Stmt>();s->kind=StmtKind::Block;s->line=cur().line;
        expect(TT::LBRACE,"Expected '{'");
        while(!check(TT::RBRACE)&&!check(TT::END))s->body.push_back(parseStmt());
        expect(TT::RBRACE,"Expected '}'");return s;
    }

    StmtPtr parseVarDecl(TypeNode type,bool semi=true){
        auto s=std::make_unique<Stmt>();s->kind=StmtKind::VarDecl;s->line=cur().line;
        s->varType=type;s->varName=expect(TT::IDENT,"Expected variable name").val;
        if(match(TT::ASSIGN))s->varInit=parseExpr();
        if(semi)expect(TT::SEMI,"Expected ';'");return s;
    }

    bool isForIn(){
        size_t p=pos;
        bool typeKw=(toks[p].type==TT::KW_INT||toks[p].type==TT::KW_FLOAT||
                     toks[p].type==TT::KW_BOOL||toks[p].type==TT::KW_STRING||
                     toks[p].type==TT::KW_VAR||toks[p].type==TT::KW_VOID);
        // plain: IDENT 'in'
        if(toks[p].type==TT::IDENT&&p+1<toks.size()&&toks[p+1].type==TT::KW_IN)return true;
        // typed: TYPE IDENT 'in'
        if((typeKw||toks[p].type==TT::IDENT)&&p+1<toks.size()&&toks[p+1].type==TT::IDENT
           &&p+2<toks.size()&&toks[p+2].type==TT::KW_IN)return true;
        return false;
    }

    StmtPtr parseStmt(){
        int l=cur().line;

        // import "path" [as alias];
        if(check(TT::KW_IMPORT)){
            consume();auto s=std::make_unique<Stmt>();s->kind=StmtKind::Import;s->line=l;
            s->importPath=expect(TT::STRING_LIT,"Expected import path").val;
            if(match(TT::KW_AS))s->importAlias=expect(TT::IDENT,"Expected alias").val;
            expect(TT::SEMI,"Expected ';'");return s;
        }

        // enum Name { A, B=5, C }
        if(check(TT::KW_ENUM)){
            consume();auto s=std::make_unique<Stmt>();s->kind=StmtKind::EnumDecl;s->line=l;
            s->enumName=expect(TT::IDENT,"Expected enum name").val;
            expect(TT::LBRACE,"Expected '{'");
            while(!check(TT::RBRACE)&&!check(TT::END)){
                std::string vname=expect(TT::IDENT,"Expected enum variant").val;
                s->enumValues.push_back(vname);
                if(match(TT::ASSIGN))s->enumInits.push_back(parseExpr());
                else s->enumInits.push_back(nullptr);
                match(TT::COMMA);
            }
            expect(TT::RBRACE,"Expected '}'");return s;
        }

        // interface Name { methodName, ... }
        if(check(TT::KW_INTERFACE)){
            consume();auto s=std::make_unique<Stmt>();s->kind=StmtKind::InterfaceDecl;s->line=l;
            s->ifaceName=expect(TT::IDENT,"Expected interface name").val;
            expect(TT::LBRACE,"Expected '{'");
            while(!check(TT::RBRACE)&&!check(TT::END)){
                // Each line: func name(...);  or just name;
                if(match(TT::KW_FUNC)){
                    std::string m=expect(TT::IDENT,"Expected method name").val;
                    // consume signature (ignored at duck-typing level)
                    if(check(TT::LPAREN)){
                        int depth=1;consume();
                        while(!check(TT::END)&&depth>0){if(check(TT::LPAREN))depth++;else if(check(TT::RPAREN))depth--;consume();}
                    }
                    if(match(TT::ARROW)){consume();} // skip return type
                    match(TT::SEMI);
                    s->ifaceMethods.push_back(m);
                } else {
                    s->ifaceMethods.push_back(expect(TT::IDENT,"Expected method name").val);
                    match(TT::SEMI);
                }
            }
            expect(TT::RBRACE,"Expected '}'");return s;
        }

        // class Name [extends Parent] [implements I1, I2] { ... }
        if(check(TT::KW_CLASS)){
            consume();auto s=std::make_unique<Stmt>();s->kind=StmtKind::ClassDecl;s->line=l;
            s->className=expect(TT::IDENT,"Expected class name").val;
            if(match(TT::KW_EXTENDS))s->parentClass=expect(TT::IDENT,"Expected parent").val;
            if(match(TT::KW_IMPLEMENTS)){
                s->interfaces.push_back(expect(TT::IDENT,"Expected interface name").val);
                while(match(TT::COMMA))s->interfaces.push_back(expect(TT::IDENT,"Expected interface name").val);
            }
            expect(TT::LBRACE,"Expected '{'");
            while(!check(TT::RBRACE)&&!check(TT::END)){
                bool isStatic=match(TT::KW_STATIC);
                if(check(TT::KW_FUNC)){
                    auto m=parseStmt();m->isStatic=isStatic;s->classMethods.push_back(std::move(m));
                } else {
                    Param p;p.type=parseType();p.name=expect(TT::IDENT,"Expected field").val;
                    if(match(TT::ASSIGN))p.defaultVal=parseExpr();
                    expect(TT::SEMI,"Expected ';'");s->classFields.push_back(std::move(p));
                }
            }
            expect(TT::RBRACE,"Expected '}'");return s;
        }

        // struct Name { ... }
        if(check(TT::KW_STRUCT)){
            consume();auto s=std::make_unique<Stmt>();s->kind=StmtKind::StructDecl;s->line=l;
            s->structName=expect(TT::IDENT,"Expected struct name").val;
            expect(TT::LBRACE,"Expected '{'");
            while(!check(TT::RBRACE)&&!check(TT::END)){
                Param p;p.type=parseType();p.name=expect(TT::IDENT,"Expected field").val;
                expect(TT::SEMI,"Expected ';'");s->fields.push_back(std::move(p));
            }
            expect(TT::RBRACE,"Expected '}'");return s;
        }

        // [static] func Name(params) -> ret { body }
        if(check(TT::KW_FUNC)){
            consume();auto s=std::make_unique<Stmt>();s->kind=StmtKind::FuncDecl;s->line=l;
            s->funcName=expect(TT::IDENT,"Expected function name").val;
            expect(TT::LPAREN,"Expected '('");
            if(!check(TT::RPAREN)){
                do{
                    s->params.push_back(parseParam());
                    if(s->params.back().isVariadic)break; // variadic must be last
                }while(match(TT::COMMA)&&!check(TT::RPAREN));
            }
            expect(TT::RPAREN,"Expected ')'");
            if(match(TT::ARROW))s->retType=parseType();
            else s->retType={"void",false};
            s->funcBody=parseBlock();return s;
        }

        // native name = (types) -> ret @ addr;
        if(check(TT::KW_NATIVE)){
            consume();auto s=std::make_unique<Stmt>();s->kind=StmtKind::NativeDecl;s->line=l;
            s->nativeName=expect(TT::IDENT,"Expected native name").val;
            expect(TT::ASSIGN,"Expected '='");
            expect(TT::LPAREN,"Expected '('");
            if(!check(TT::RPAREN)){do{s->nativeParams.push_back(parseType());}while(match(TT::COMMA));}
            expect(TT::RPAREN,"Expected ')'");
            if(match(TT::ARROW))s->nativeRet=parseType();
            else s->nativeRet={"void",false};
            expect(TT::AT,"Expected '@'");
            if(check(TT::STRING_LIT)){s->nativeSymbol=consume().val;}
            else{auto t=expect(TT::INT_LIT,"Expected address");
                if(t.val.size()>2&&t.val[0]=='0'&&(t.val[1]=='x'||t.val[1]=='X'))
                    s->nativeAddr=(uint64_t)strtoull(t.val.c_str(),nullptr,16);
                else s->nativeAddr=(uint64_t)strtoull(t.val.c_str(),nullptr,10);}
            expect(TT::SEMI,"Expected ';'");return s;
        }

        // switch(expr) { case v: ... default: ... }
        if(check(TT::KW_SWITCH)){
            consume();auto s=std::make_unique<Stmt>();s->kind=StmtKind::Switch;s->line=l;
            expect(TT::LPAREN,"Expected '('");s->switchExpr=parseExpr();expect(TT::RPAREN,"Expected ')'");
            expect(TT::LBRACE,"Expected '{'");
            while(!check(TT::RBRACE)&&!check(TT::END)){
                SwitchCase sc;
                if(check(TT::KW_DEFAULT)){consume();expect(TT::COLON,"Expected ':'");sc.isDefault=true;}
                else{
                    expect(TT::KW_CASE,"Expected 'case'");
                    sc.values.push_back(parseExpr());
                    while(match(TT::COMMA))sc.values.push_back(parseExpr());
                    expect(TT::COLON,"Expected ':'");
                }
                while(!check(TT::KW_CASE)&&!check(TT::KW_DEFAULT)&&!check(TT::RBRACE)&&!check(TT::END))
                    sc.body.push_back(parseStmt());
                s->cases.push_back(std::move(sc));
            }
            expect(TT::RBRACE,"Expected '}'");return s;
        }

        // match expr { pattern => body, ... }
        if(check(TT::KW_MATCH)){
            consume();auto s=std::make_unique<Stmt>();s->kind=StmtKind::Match;s->line=l;
            s->matchVal=parseExpr();
            expect(TT::LBRACE,"Expected '{'");
            while(!check(TT::RBRACE)&&!check(TT::END)){
                MatchArm arm;
                // default / wildcard _
                if(check(TT::KW_DEFAULT)||(check(TT::IDENT)&&cur().val=="_")){
                    consume();arm.isDefault=true;
                }else{
                    // Parse patterns separated by | (NOT as bitwise OR — stop at prec 3)
                    // parseBinary(4) stops before BOR (prec=4), treating | as separator
                    arm.patterns.push_back(parseBinary(5)); // prec>4 means | won't be consumed
                    while(check(TT::BOR)){consume();arm.patterns.push_back(parseBinary(5));}
                    if(check(TT::KW_WHEN)){consume();arm.guard=parseExpr();}
                }
                expect(TT::FAT_ARROW,"Expected '=>'");
                arm.body=parseBlock();
                match(TT::COMMA);
                s->arms.push_back(std::move(arm));
            }
            expect(TT::RBRACE,"Expected '}'");return s;
        }

        // if / else
        if(check(TT::KW_IF)){
            consume();auto s=std::make_unique<Stmt>();s->kind=StmtKind::If;s->line=l;
            expect(TT::LPAREN,"Expected '('");s->cond=parseExpr();expect(TT::RPAREN,"Expected ')'");
            s->then=parseBlock();
            if(check(TT::KW_ELSE)){consume();s->els=check(TT::KW_IF)?parseStmt():parseBlock();}
            return s;
        }

        // while
        if(check(TT::KW_WHILE)){
            consume();auto s=std::make_unique<Stmt>();s->kind=StmtKind::While;s->line=l;
            expect(TT::LPAREN,"Expected '('");s->cond=parseExpr();expect(TT::RPAREN,"Expected ')'");
            s->body.push_back(parseBlock());return s;
        }

        // for
        if(check(TT::KW_FOR)){
            consume();expect(TT::LPAREN,"Expected '('");
            if(isForIn()){
                auto s=std::make_unique<Stmt>();s->kind=StmtKind::ForIn;s->line=l;
                size_t p=pos;
                bool typeKw=(toks[p].type==TT::KW_INT||toks[p].type==TT::KW_FLOAT||
                             toks[p].type==TT::KW_BOOL||toks[p].type==TT::KW_STRING||
                             toks[p].type==TT::KW_VAR||toks[p].type==TT::KW_VOID);
                bool plainIn=(toks[p].type==TT::IDENT&&p+1<toks.size()&&toks[p+1].type==TT::KW_IN);
                if(!plainIn&&(typeKw||(toks[p].type==TT::IDENT&&p+1<toks.size()&&toks[p+1].type==TT::IDENT)))
                    s->forInType=parseType();
                s->forInVar=expect(TT::IDENT,"Expected variable in for-in").val;
                expect(TT::KW_IN,"Expected 'in'");
                s->forInExpr=parseExpr();expect(TT::RPAREN,"Expected ')'");
                s->forInBody=parseBlock();return s;
            }
            auto s=std::make_unique<Stmt>();s->kind=StmtKind::For;s->line=l;
            if(!check(TT::SEMI)){
                size_t saved=pos;bool gotDecl=false;
                try{TypeNode t=parseType();if(check(TT::IDENT)){s->forInit=parseVarDecl(t,true);gotDecl=true;}else pos=saved;}
                catch(...){pos=saved;}
                if(!gotDecl){auto ie=parseExpr();expect(TT::SEMI,"Expected ';'");
                    auto es=std::make_unique<Stmt>();es->kind=StmtKind::ExprStmt;es->expr=std::move(ie);s->forInit=std::move(es);}
            }else consume();
            if(!check(TT::SEMI))s->forCond=parseExpr();
            expect(TT::SEMI,"Expected ';'");
            if(!check(TT::RPAREN))s->forStep=parseExpr();
            expect(TT::RPAREN,"Expected ')'");s->forBody=parseBlock();return s;
        }

        // try/catch/finally
        if(check(TT::KW_TRY)){
            consume();auto s=std::make_unique<Stmt>();s->kind=StmtKind::TryCatch;s->line=l;
            s->then=parseBlock();
            expect(TT::KW_CATCH,"Expected 'catch'");
            expect(TT::LPAREN,"Expected '('");
            s->forInVar=expect(TT::IDENT,"Expected catch variable").val;
            expect(TT::RPAREN,"Expected ')'");
            s->els=parseBlock();
            return s;
        }

        // throw
        if(check(TT::KW_THROW)){consume();auto s=std::make_unique<Stmt>();s->kind=StmtKind::Throw;s->line=l;s->expr=parseExpr();expect(TT::SEMI,"Expected ';'");return s;}

        // defer
        if(check(TT::KW_DEFER)){consume();auto s=std::make_unique<Stmt>();s->kind=StmtKind::Defer;s->line=l;s->expr=parseExpr();expect(TT::SEMI,"Expected ';'");return s;}

        // return
        if(check(TT::KW_RETURN)){consume();auto s=std::make_unique<Stmt>();s->kind=StmtKind::Return;s->line=l;if(!check(TT::SEMI))s->expr=parseExpr();expect(TT::SEMI,"Expected ';'");return s;}
        if(check(TT::KW_BREAK)){consume();auto s=std::make_unique<Stmt>();s->kind=StmtKind::Break;s->line=l;expect(TT::SEMI,"Expected ';'");return s;}
        if(check(TT::KW_CONTINUE)){consume();auto s=std::make_unique<Stmt>();s->kind=StmtKind::Continue;s->line=l;expect(TT::SEMI,"Expected ';'");return s;}

        // block
        if(check(TT::LBRACE))return parseBlock();

        // const
        if(check(TT::KW_CONST)){
            consume();TypeNode t;t.name="var";
            auto s=parseVarDecl(t);s->isConst=true;return s;
        }
        // var/let/auto/val
        if(check(TT::KW_VAR)){consume();TypeNode t;t.name="var";return parseVarDecl(t);}

        // typed var decl: only if TYPE followed by IDENT (not LPAREN - that's a cast/call)
        {size_t saved=pos;
         try{
             TypeNode t=parseType();
             // Must be IDENT next AND not followed by something that makes it a cast/call
             if(check(TT::IDENT)&&pos+1<toks.size()&&toks[pos+1].type!=TT::LPAREN)
                 return parseVarDecl(t);
             pos=saved;
         }
         catch(...){pos=saved;}}

        // expr stmt
        {auto s=std::make_unique<Stmt>();s->kind=StmtKind::ExprStmt;s->line=l;
         s->expr=parseExpr();expect(TT::SEMI,"Expected ';'");return s;}
    }

public:
    Parser(std::vector<Token> t):toks(std::move(t)){}
    Program parse(){
        Program prog;
        while(!check(TT::END))prog.stmts.push_back(parseStmt());
        return prog;
    }
};
