#pragma once

#include <iostream>
#include <string>
#include <fstream>
#include "CodeLoc.h"
#include "NamePool.h"
#include "Token.h"
#include "CompilationMessages.h"

class Lexer {
    NamePool *namePool;
    StringPool *stringPool;
    CompilationMessages *msgs;
    std::ifstream in;
    std::string line;
    CodeIndex ln, col;
    char ch;
    Token tok;
    CodeLocPoint codeLocPoint;

    bool over() const { return ch == EOF; }

    char peekCh() const { return ch; }
    char nextCh();
    void skipLine();

    void lexNum(CodeIndex from);

public:
    Lexer(NamePool *namePool, StringPool *stringPool, CompilationMessages *msgs, const std::string &file);

    bool start();

    const Token& peek() const { return tok; }
    Token next();
    bool match(Token::Type type);

    // Returns the location of the start of the token that would be returned by next().
    CodeLocPoint loc() const { return codeLocPoint; }
};