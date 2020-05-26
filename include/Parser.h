#pragma once

#include <memory>
#include "Lexer.h"
#include "AST.h"
#include "CompileMessages.h"

class Parser {
    StringPool *stringPool;
    SymbolTable *symbolTable;
    Lexer *lex;
    CompileMessages *msgs;

    TypeTable* getTypeTable() { return symbolTable->getTypeTable(); }
    const TypeTable* getTypeTable() const { return symbolTable->getTypeTable(); }

    const Token& peek() const;
    Token next();
    bool match(Token::Type type);
    bool matchOrError(Token::Type type);
    CodeLoc loc() const;

    std::unique_ptr<AstNode> makeEmptyTerm();

    std::unique_ptr<AstNode> parseType();
    std::unique_ptr<AstNode> parseTerm();

public:
    Parser(StringPool *stringPool, SymbolTable *symbolTable, CompileMessages *msgs);

    void setLexer(Lexer *lex_) { lex = lex_; }
    Lexer* getLexer() const { return lex; }

    std::unique_ptr<AstNode> parseNode();

    bool isOver() const { return peek().type == Token::T_END; }
};