#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <cctype>

enum class TT {
    INT_LIT, FLOAT_LIT, STRING_LIT, INTERP_STRING,
    KW_INT, KW_FLOAT, KW_BOOL, KW_STRING, KW_VOID, KW_VAR,
    KW_FUNC, KW_RETURN, KW_IF, KW_ELSE, KW_WHILE, KW_FOR, KW_BREAK, KW_CONTINUE,
    KW_SWITCH, KW_CASE, KW_DEFAULT, KW_MATCH, KW_WHEN,
    KW_CLASS, KW_EXTENDS, KW_THIS, KW_SUPER, KW_NEW, KW_STATIC,
    KW_INTERFACE, KW_IMPLEMENTS, KW_ENUM,
    KW_TRY, KW_CATCH, KW_FINALLY, KW_THROW,
    KW_IMPORT, KW_EXPORT, KW_FROM, KW_AS,
    KW_STRUCT, KW_NATIVE, KW_IN, KW_IS, KW_NOT,
    KW_TRUE, KW_FALSE, KW_NULL,
    KW_CONST, KW_DEFER, KW_TYPEOF, KW_SIZEOF,
    PLUS, MINUS, STAR, SLASH, PERCENT, STARSTAR,
    EQ, NEQ, LT, GT, LEQ, GEQ,
    AND, OR, NOT,
    BAND, BOR, BXOR, BNOT, LSHIFT, RSHIFT,
    ASSIGN, PLUS_ASSIGN, MINUS_ASSIGN, STAR_ASSIGN, SLASH_ASSIGN,
    PERCENT_ASSIGN, STARSTAR_ASSIGN, AND_ASSIGN, OR_ASSIGN,
    INC, DEC,
    QUESTION, NULLCOAL, COLON,
    OPT_DOT, OPT_BRACKET, ELLIPSIS,
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET,
    SEMI, COMMA, DOT, AT, ARROW, FAT_ARROW, DCOLON, HASH,
    IDENT, END
};

struct Token { TT type; std::string val; int line, col; };

class Lexer {
    std::string src;
    size_t pos=0; int line=1,col=1;

    char cur(){return pos<src.size()?src[pos]:'\0';}
    char peekAt(int o){size_t i=pos+o;return i<src.size()?src[i]:'\0';}
    char advance(){char c=src[pos++];if(c=='\n'){line++;col=1;}else col++;return c;}

    void skipWS(){
        again:
        while(pos<src.size()&&(cur()==' '||cur()=='\t'||cur()=='\r'||cur()=='\n'))advance();
        if(cur()=='/'&&peekAt(1)=='/'){while(pos<src.size()&&cur()!='\n')advance();goto again;}
        if(cur()=='/'&&peekAt(1)=='*'){advance();advance();
            while(pos<src.size()&&!(cur()=='*'&&peekAt(1)=='/'))advance();
            if(pos<src.size()){advance();advance();}goto again;}
        if(cur()=='#'&&peekAt(1)=='!'){while(pos<src.size()&&cur()!='\n')advance();goto again;}
    }

    Token makeStr(){
        int l=line,c=col;advance();
        std::string s;bool interp=false;
        // Scan ahead to check for ${...} — must skip nested quotes
        {
            size_t j=pos; int depth=0;
            while(j<src.size()&&src[j]!='"'){
                if(src[j]=='$'&&j+1<src.size()&&src[j+1]=='{'){interp=true;break;}
                if(depth==0&&src[j]=='\\'){j+=2;continue;}
                j++;
            }
        }
        while(pos<src.size()&&cur()!='"'){
            if(cur()=='\\'){advance();
                switch(cur()){case 'n':s+='\n';break;case 't':s+='\t';break;
                    case '"':s+='"';break;case '\\':s+='\\';break;
                    case 'r':s+='\r';break;case '0':s+='\0';break;
                    case '$':s+='$';break;case 'a':s+='\a';break;
                    default:s+='\\';s+=cur();break;}
            } else {
                // Pass UTF-8 multi-byte sequences through as-is
                unsigned char uc = (unsigned char)cur();
                s += cur(); advance();
                if (uc > 127) {
                    while(pos<src.size()&&((unsigned char)src[pos]&0xC0)==0x80){s+=src[pos];advance();}
                }
                continue;
            }
            advance();
        }
        if(pos<src.size())advance();
        return {interp?TT::INTERP_STRING:TT::STRING_LIT,s,l,c};
    }

    Token makeBacktick(){
        int l=line,c=col;advance();
        std::string s;
        while(pos<src.size()&&cur()!='`'){
            if(cur()=='\\'&&peekAt(1)=='`'){advance();s+='`';}else s+=cur();
            advance();
        }
        if(pos<src.size())advance();
        return {TT::STRING_LIT,s,l,c};
    }

    Token makeNum(){
        int l=line,c=col;std::string s;bool isFloat=false;
        if(cur()=='0'&&(peekAt(1)=='x'||peekAt(1)=='X')){
            s+=advance();s+=advance();
            while(pos<src.size()&&isxdigit(cur()))s+=advance();
            return {TT::INT_LIT,s,l,c};
        }
        if(cur()=='0'&&(peekAt(1)=='b'||peekAt(1)=='B')){
            s+=advance();s+=advance();
            while(pos<src.size()&&(cur()=='0'||cur()=='1'))s+=advance();
            return {TT::INT_LIT,s,l,c};
        }
        if(cur()=='0'&&(peekAt(1)=='o'||peekAt(1)=='O')){
            s+=advance();s+=advance();
            while(pos<src.size()&&cur()>='0'&&cur()<='7')s+=advance();
            return {TT::INT_LIT,s,l,c};
        }
        while(pos<src.size()&&(isdigit(cur())||cur()=='_')){
            char ch=cur();advance();if(ch!='_')s+=ch;
        }
        if(cur()=='.'&&isdigit(peekAt(1))){isFloat=true;s+=advance();
            while(pos<src.size()&&isdigit(cur()))s+=advance();}
        if(cur()=='e'||cur()=='E'){isFloat=true;s+=advance();
            if(cur()=='+'||cur()=='-')s+=advance();
            while(pos<src.size()&&isdigit(cur()))s+=advance();}
        if(cur()=='f'||cur()=='F'){isFloat=true;advance();}
        return {isFloat?TT::FLOAT_LIT:TT::INT_LIT,s,l,c};
    }

    Token makeIdent(){
        int l=line,c=col;std::string s;
        while(pos<src.size()&&(isalnum(cur())||cur()=='_'))s+=advance();
        static const std::unordered_map<std::string,TT> kw={
            {"int",TT::KW_INT},{"float",TT::KW_FLOAT},{"bool",TT::KW_BOOL},
            {"string",TT::KW_STRING},{"void",TT::KW_VOID},
            {"var",TT::KW_VAR},{"val",TT::KW_VAR},{"auto",TT::KW_VAR},{"let",TT::KW_VAR},
            {"const",TT::KW_CONST},
            {"func",TT::KW_FUNC},{"return",TT::KW_RETURN},
            {"if",TT::KW_IF},{"else",TT::KW_ELSE},
            {"while",TT::KW_WHILE},{"for",TT::KW_FOR},
            {"break",TT::KW_BREAK},{"continue",TT::KW_CONTINUE},
            {"switch",TT::KW_SWITCH},{"case",TT::KW_CASE},
            {"default",TT::KW_DEFAULT},{"match",TT::KW_MATCH},{"when",TT::KW_WHEN},
            {"class",TT::KW_CLASS},{"extends",TT::KW_EXTENDS},
            {"this",TT::KW_THIS},{"super",TT::KW_SUPER},
            {"new",TT::KW_NEW},{"static",TT::KW_STATIC},
            {"interface",TT::KW_INTERFACE},{"implements",TT::KW_IMPLEMENTS},
            {"enum",TT::KW_ENUM},
            {"try",TT::KW_TRY},{"catch",TT::KW_CATCH},
            {"finally",TT::KW_FINALLY},{"throw",TT::KW_THROW},
            {"import",TT::KW_IMPORT},{"export",TT::KW_EXPORT},
            {"from",TT::KW_FROM},{"as",TT::KW_AS},
            {"in",TT::KW_IN},{"is",TT::KW_IS},
            {"struct",TT::KW_STRUCT},{"native",TT::KW_NATIVE},
            {"true",TT::KW_TRUE},{"false",TT::KW_FALSE},{"null",TT::KW_NULL},
            {"defer",TT::KW_DEFER},{"typeof",TT::KW_TYPEOF},{"sizeof",TT::KW_SIZEOF},
        };
        auto it=kw.find(s);
        if(it!=kw.end())return{it->second,s,l,c};
        return{TT::IDENT,s,l,c};
    }

public:
    Lexer(const std::string& s):src(s){}

    std::vector<Token> tokenize(){
        std::vector<Token> toks;
        while(true){
            skipWS();
            if(pos>=src.size()){toks.push_back({TT::END,"",line,col});break;}
            int l=line,c=col; char ch=cur();
            if(ch=='"'){toks.push_back(makeStr());continue;}
            if(ch=='`'){toks.push_back(makeBacktick());continue;}
            if(isdigit(ch)||(ch=='.'&&isdigit(peekAt(1)))){toks.push_back(makeNum());continue;}
            if(isalpha(ch)||ch=='_'){toks.push_back(makeIdent());continue;}
            advance();
            switch(ch){
                case '+': if(cur()=='='){advance();toks.push_back({TT::PLUS_ASSIGN,"+=",l,c});}
                          else if(cur()=='+'){advance();toks.push_back({TT::INC,"++",l,c});}
                          else toks.push_back({TT::PLUS,"+",l,c}); break;
                case '-': if(cur()=='='){advance();toks.push_back({TT::MINUS_ASSIGN,"-=",l,c});}
                          else if(cur()=='-'){advance();toks.push_back({TT::DEC,"--",l,c});}
                          else if(cur()=='>'){advance();toks.push_back({TT::ARROW,"->",l,c});}
                          else toks.push_back({TT::MINUS,"-",l,c}); break;
                case '*': if(cur()=='*'){advance();
                              if(cur()=='='){advance();toks.push_back({TT::STARSTAR_ASSIGN,"**=",l,c});}
                              else toks.push_back({TT::STARSTAR,"**",l,c});}
                          else if(cur()=='='){advance();toks.push_back({TT::STAR_ASSIGN,"*=",l,c});}
                          else toks.push_back({TT::STAR,"*",l,c}); break;
                case '/': if(cur()=='='){advance();toks.push_back({TT::SLASH_ASSIGN,"/=",l,c});}
                          else toks.push_back({TT::SLASH,"/",l,c}); break;
                case '%': if(cur()=='='){advance();toks.push_back({TT::PERCENT_ASSIGN,"%=",l,c});}
                          else toks.push_back({TT::PERCENT,"%",l,c}); break;
                case '=': if(cur()=='='){advance();toks.push_back({TT::EQ,"==",l,c});}
                          else if(cur()=='>'){advance();toks.push_back({TT::FAT_ARROW,"=>",l,c});}
                          else toks.push_back({TT::ASSIGN,"=",l,c}); break;
                case '!': if(cur()=='='){advance();toks.push_back({TT::NEQ,"!=",l,c});}
                          else toks.push_back({TT::NOT,"!",l,c}); break;
                case '<': if(cur()=='='){advance();toks.push_back({TT::LEQ,"<=",l,c});}
                          else if(cur()=='<'){advance();toks.push_back({TT::LSHIFT,"<<",l,c});}
                          else toks.push_back({TT::LT,"<",l,c}); break;
                case '>': if(cur()=='='){advance();toks.push_back({TT::GEQ,">=",l,c});}
                          else if(cur()=='>'){advance();toks.push_back({TT::RSHIFT,">>",l,c});}
                          else toks.push_back({TT::GT,">",l,c}); break;
                case '&': if(cur()=='&'){advance();toks.push_back({TT::AND,"&&",l,c});}
                          else if(cur()=='='){advance();toks.push_back({TT::AND_ASSIGN,"&=",l,c});}
                          else toks.push_back({TT::BAND,"&",l,c}); break;
                case '|': if(cur()=='|'){advance();toks.push_back({TT::OR,"||",l,c});}
                          else if(cur()=='='){advance();toks.push_back({TT::OR_ASSIGN,"|=",l,c});}
                          else toks.push_back({TT::BOR,"|",l,c}); break;
                case '?': if(cur()=='?'){advance();toks.push_back({TT::NULLCOAL,"??",l,c});}
                          else if(cur()=='.'){advance();toks.push_back({TT::OPT_DOT,"?.",l,c});}
                          else if(cur()=='['){advance();toks.push_back({TT::OPT_BRACKET,"?[",l,c});}
                          else toks.push_back({TT::QUESTION,"?",l,c}); break;
                case '.': if(cur()=='.'&&peekAt(1)=='.'){advance();advance();toks.push_back({TT::ELLIPSIS,"...",l,c});}
                          else toks.push_back({TT::DOT,".",l,c}); break;
                case ':': if(cur()==':'){advance();toks.push_back({TT::DCOLON,"::",l,c});}
                          else toks.push_back({TT::COLON,":",l,c}); break;
                case '^': toks.push_back({TT::BXOR,"^",l,c}); break;
                case '~': toks.push_back({TT::BNOT,"~",l,c}); break;
                case '(': toks.push_back({TT::LPAREN,"(",l,c}); break;
                case ')': toks.push_back({TT::RPAREN,")",l,c}); break;
                case '{': toks.push_back({TT::LBRACE,"{",l,c}); break;
                case '}': toks.push_back({TT::RBRACE,"}",l,c}); break;
                case '[': toks.push_back({TT::LBRACKET,"[",l,c}); break;
                case ']': toks.push_back({TT::RBRACKET,"]",l,c}); break;
                case ';': toks.push_back({TT::SEMI,";",l,c}); break;
                case ',': toks.push_back({TT::COMMA,",",l,c}); break;
                case '@': toks.push_back({TT::AT,"@",l,c}); break;
                case '#': toks.push_back({TT::HASH,"#",l,c}); break;
                default:
                    // Pass through UTF-8 multi-byte sequences (> 127) as they appear in strings/comments
                    if ((unsigned char)ch > 127) {
                        // Consume rest of UTF-8 sequence (continuation bytes: 10xxxxxx)
                        while (pos < src.size() && ((unsigned char)src[pos] & 0xC0) == 0x80) advance();
                        // Skip silently - non-ASCII outside strings is treated as whitespace
                        break;
                    }
                    throw std::runtime_error(
                        "Unexpected char '"+std::string(1,ch)+"' at "+std::to_string(l)+":"+std::to_string(c));
            }
        }
        return toks;
    }
};
