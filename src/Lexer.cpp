#include "Lexer.h"
#include <cstdlib>
using namespace std;

const OperPrec minOperPrec = -1000;
const unordered_map<Token::Oper, OperInfo> operInfos = {
    {Token::O_ASGN, {1, false}},
    {Token::O_ADD, {2}},
    {Token::O_SUB, {2}},
    {Token::O_MUL, {3}},
    {Token::O_DIV, {3}}
};

const unordered_map<string, Token::Type> keywords = {
    {"var", {Token::T_VAR}},
    {"fnc", {Token::T_FNC}},
    {"ret", {Token::T_RET}}
};

Lexer::Lexer(NamePool *namePool) : namePool(namePool) {
}

void Lexer::start(std::istream &istr) {
    in = &istr;
    col = 0;
    ch = 0; // not EOF
    tok.type = Token::T_NUM; // not END

    nextCh();
    next();
}

char Lexer::nextCh() {
    if (ch == EOF) return ch;

    char old = ch;

    ++col;

    if (col > line.size()) {
        if (!getline(*in, line)) ch = EOF;
        col = 0;
    }

    if (ch != EOF) {
        ch = col == line.size() ? '\n' : line[col];
    }

    return old;
}

Token Lexer::next() {
    if (tok.type == Token::T_END) return tok;

    Token old = tok;

    char ch;
    do {
        ch = nextCh();
    } while (isspace(ch));
    
    if (ch == EOF) {
        tok.type = Token::T_END;
        return old;
    }
    
    if (ch >= '0' && ch <= '9') {
        tok.type = Token::T_NUM;
        int l = col-1;
        while (peekCh() >= '0' && peekCh() <= '9') nextCh();
        
        char *end = nullptr;
        tok.num = strtol(line.substr(l, col-l).c_str(), &end, 10);
        if (*end) tok.type = Token::T_UNKNOWN;
    } else if (ch == '+') {
        tok = {Token::T_OPER, Token::O_ADD};
    } else if (ch == '-') {
        tok = {Token::T_OPER, Token::O_SUB};
    } else if (ch == '*') {
        tok = {Token::T_OPER, Token::O_MUL};
    } else if (ch == '/') {
        tok = {Token::T_OPER, Token::O_DIV};
    } else if (ch == '=') {
        tok = {Token::T_OPER, Token::O_ASGN};
    } else if (ch == ',') {
        tok = {Token::T_COMMA};
    } else if (ch == ';') {
        tok = {Token::T_SEMICOLON};
    } else if (ch == '(') {
        tok = {Token::T_BRACE_L_REG};
    } else if (ch == ')') {
        tok = {Token::T_BRACE_R_REG};
    } else if (ch == '{') {
        tok = {Token::T_BRACE_L_CUR};
    } else if (ch == '}') {
        tok = {Token::T_BRACE_R_CUR};
    } else if (isalpha(ch) || ch == '_') {
        int l = col-1;
        while (isalnum(peekCh()) || peekCh() == '_') nextCh();
        string id = line.substr(l, col-l);

        auto loc = keywords.find(id);
        if (loc != keywords.end()) {
            tok.type = loc->second;
        } else {
            tok.type = Token::T_ID;
            tok.nameId = namePool->add(id);
        }
    } else {
        tok.type = Token::T_UNKNOWN;
    }

    return old;
}

// Eats the next token and returns whether it matches the type.
bool Lexer::match(Token::Type type) {
    return next().type == type;
}