#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <cctype>

enum class TT {
    // Literals
    INT_LIT, FLOAT_LIT, STRING_LIT,
    // Primitive types
    KW_INT, KW_FLOAT, KW_BOOL, KW_STRING, KW_VOID, KW_VAR,
    // Control flow
    KW_FUNC, KW_RETURN, KW_IF, KW_ELSE, KW_WHILE, KW_FOR, KW_BREAK, KW_CONTINUE,
    // OOP
    KW_CLASS, KW_EXTENDS, KW_THIS, KW_SUPER, KW_NEW,
    // Error handling
    KW_TRY, KW_CATCH, KW_THROW,
    // Modules
    KW_IMPORT,
    // Misc keywords
    KW_STRUCT, KW_NATIVE, KW_IN,
    KW_TRUE, KW_FALSE, KW_NULL,
    // Arithmetic
    PLUS, MINUS, STAR, SLASH, PERCENT,
    // Comparison
    EQ, NEQ, LT, GT, LEQ, GEQ,
    // Logical
    AND, OR, NOT,
    // Bitwise
    BAND, BOR, BXOR, BNOT, LSHIFT, RSHIFT,
    // Assignment
    ASSIGN, PLUS_ASSIGN, MINUS_ASSIGN, STAR_ASSIGN, SLASH_ASSIGN, PERCENT_ASSIGN,
    // Inc/dec
    INC, DEC,
    // Ternary / null-coalescing
    QUESTION, NULLCOAL, COLON,
    // Punctuation
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET,
    SEMI, COMMA, DOT, AT, ARROW,
    // Special
    IDENT, END
};

struct Token {
    TT          type;
    std::string val;
    int         line, col;
};

class Lexer {
    std::string src;
    size_t pos = 0;
    int line = 1, col = 1;

    char cur()           { return pos < src.size() ? src[pos] : '\0'; }
    char peekAt(int off) { size_t i=pos+off; return i<src.size()?src[i]:'\0'; }
    char advance() {
        char c=src[pos++];
        if(c=='\n'){line++;col=1;}else col++;
        return c;
    }

    void skipWS() {
        again:
        while(pos<src.size()&&(cur()==' '||cur()=='\t'||cur()=='\r'||cur()=='\n'))advance();
        if(cur()=='/'&&peekAt(1)=='/'){while(pos<src.size()&&cur()!='\n')advance();goto again;}
        if(cur()=='/'&&peekAt(1)=='*'){
            advance();advance();
            while(pos<src.size()&&!(cur()=='*'&&peekAt(1)=='/'))advance();
            if(pos<src.size()){advance();advance();}
            goto again;
        }
    }

    Token makeStr() {
        int l=line,c=col; advance();
        std::string s;
        while(pos<src.size()&&cur()!='"'){
            if(cur()=='\\'){
                advance();
                switch(cur()){
                    case 'n':s+='\n';break;case 't':s+='\t';break;
                    case '"':s+='"';break;case '\\':s+='\\';break;
                    case 'r':s+='\r';break;case '0':s+='\0';break;
                    default:s+='\\';s+=cur();break;
                }
            }else s+=cur();
            advance();
        }
        if(pos<src.size())advance();
        return {TT::STRING_LIT,s,l,c};
    }

    Token makeNum() {
        int l=line,c=col; std::string s; bool isFloat=false;
        if(cur()=='0'&&(peekAt(1)=='x'||peekAt(1)=='X')){
            s+=advance();s+=advance();
            while(pos<src.size()&&isxdigit(cur()))s+=advance();
            return {TT::INT_LIT,s,l,c};
        }
        while(pos<src.size()&&isdigit(cur()))s+=advance();
        if(cur()=='.'&&isdigit(peekAt(1))){isFloat=true;s+=advance();while(pos<src.size()&&isdigit(cur()))s+=advance();}
        if(cur()=='e'||cur()=='E'){isFloat=true;s+=advance();if(cur()=='+'||cur()=='-')s+=advance();while(pos<src.size()&&isdigit(cur()))s+=advance();}
        if(cur()=='f'||cur()=='F'){isFloat=true;advance();}
        return {isFloat?TT::FLOAT_LIT:TT::INT_LIT,s,l,c};
    }

    Token makeIdent() {
        int l=line,c=col; std::string s;
        while(pos<src.size()&&(isalnum(cur())||cur()=='_'))s+=advance();
        static const std::unordered_map<std::string,TT> kw={
            {"int",TT::KW_INT},{"float",TT::KW_FLOAT},{"bool",TT::KW_BOOL},
            {"string",TT::KW_STRING},{"void",TT::KW_VOID},
            {"var",TT::KW_VAR},{"val",TT::KW_VAR},{"auto",TT::KW_VAR},{"let",TT::KW_VAR},
            {"func",TT::KW_FUNC},{"return",TT::KW_RETURN},
            {"if",TT::KW_IF},{"else",TT::KW_ELSE},
            {"while",TT::KW_WHILE},{"for",TT::KW_FOR},
            {"break",TT::KW_BREAK},{"continue",TT::KW_CONTINUE},
            {"class",TT::KW_CLASS},{"extends",TT::KW_EXTENDS},
            {"this",TT::KW_THIS},{"super",TT::KW_SUPER},
            {"new",TT::KW_NEW},
            {"try",TT::KW_TRY},{"catch",TT::KW_CATCH},{"throw",TT::KW_THROW},
            {"import",TT::KW_IMPORT},{"in",TT::KW_IN},
            {"struct",TT::KW_STRUCT},{"native",TT::KW_NATIVE},
            {"true",TT::KW_TRUE},{"false",TT::KW_FALSE},{"null",TT::KW_NULL},
        };
        auto it=kw.find(s);
        if(it!=kw.end())return {it->second,s,l,c};
        return {TT::IDENT,s,l,c};
    }

public:
    Lexer(const std::string& src):src(src){}

    std::vector<Token> tokenize(){
        std::vector<Token> toks;
        while(true){
            skipWS();
            if(pos>=src.size()){toks.push_back({TT::END,"",line,col});break;}
            int l=line,c=col;
            char ch=cur();

            if(ch=='"'){toks.push_back(makeStr());continue;}
            if(isdigit(ch)||(ch=='.'&&isdigit(peekAt(1)))){toks.push_back(makeNum());continue;}
            if(isalpha(ch)||ch=='_'){toks.push_back(makeIdent());continue;}

            advance();
            switch(ch){
                case '+':
                    if(cur()=='='){advance();toks.push_back({TT::PLUS_ASSIGN,"+=",l,c});}
                    else if(cur()=='+'){advance();toks.push_back({TT::INC,"++",l,c});}
                    else toks.push_back({TT::PLUS,"+",l,c});
                    break;
                case '-':
                    if(cur()=='='){advance();toks.push_back({TT::MINUS_ASSIGN,"-=",l,c});}
                    else if(cur()=='-'){advance();toks.push_back({TT::DEC,"--",l,c});}
                    else if(cur()=='>'){advance();toks.push_back({TT::ARROW,"->",l,c});}
                    else toks.push_back({TT::MINUS,"-",l,c});
                    break;
                case '*':
                    if(cur()=='='){advance();toks.push_back({TT::STAR_ASSIGN,"*=",l,c});}
                    else toks.push_back({TT::STAR,"*",l,c});
                    break;
                case '/':
                    if(cur()=='='){advance();toks.push_back({TT::SLASH_ASSIGN,"/=",l,c});}
                    else toks.push_back({TT::SLASH,"/",l,c});
                    break;
                case '%':
                    if(cur()=='='){advance();toks.push_back({TT::PERCENT_ASSIGN,"%=",l,c});}
                    else toks.push_back({TT::PERCENT,"%",l,c});
                    break;
                case '=':
                    if(cur()=='='){advance();toks.push_back({TT::EQ,"==",l,c});}
                    else toks.push_back({TT::ASSIGN,"=",l,c});
                    break;
                case '!':
                    if(cur()=='='){advance();toks.push_back({TT::NEQ,"!=",l,c});}
                    else toks.push_back({TT::NOT,"!",l,c});
                    break;
                case '<':
                    if(cur()=='='){advance();toks.push_back({TT::LEQ,"<=",l,c});}
                    else if(cur()=='<'){advance();toks.push_back({TT::LSHIFT,"<<",l,c});}
                    else toks.push_back({TT::LT,"<",l,c});
                    break;
                case '>':
                    if(cur()=='='){advance();toks.push_back({TT::GEQ,">=",l,c});}
                    else if(cur()=='>'){advance();toks.push_back({TT::RSHIFT,">>",l,c});}
                    else toks.push_back({TT::GT,">",l,c});
                    break;
                case '&':
                    if(cur()=='&'){advance();toks.push_back({TT::AND,"&&",l,c});}
                    else toks.push_back({TT::BAND,"&",l,c});
                    break;
                case '|':
                    if(cur()=='|'){advance();toks.push_back({TT::OR,"||",l,c});}
                    else toks.push_back({TT::BOR,"|",l,c});
                    break;
                case '?':
                    if(cur()=='?'){advance();toks.push_back({TT::NULLCOAL,"??",l,c});}
                    else toks.push_back({TT::QUESTION,"?",l,c});
                    break;
                case '^':toks.push_back({TT::BXOR,"^",l,c});break;
                case '~':toks.push_back({TT::BNOT,"~",l,c});break;
                case '(':toks.push_back({TT::LPAREN,"(",l,c});break;
                case ')':toks.push_back({TT::RPAREN,")",l,c});break;
                case '{':toks.push_back({TT::LBRACE,"{",l,c});break;
                case '}':toks.push_back({TT::RBRACE,"}",l,c});break;
                case '[':toks.push_back({TT::LBRACKET,"[",l,c});break;
                case ']':toks.push_back({TT::RBRACKET,"]",l,c});break;
                case ';':toks.push_back({TT::SEMI,";",l,c});break;
                case ',':toks.push_back({TT::COMMA,",",l,c});break;
                case '.':toks.push_back({TT::DOT,".",l,c});break;
                case '@':toks.push_back({TT::AT,"@",l,c});break;
                case ':':toks.push_back({TT::COLON,":",l,c});break;
                default:
                    throw std::runtime_error("Unexpected char '"+std::string(1,ch)+"' at line "+std::to_string(l)+":"+std::to_string(c));
            }
        }
        return toks;
    }
};
