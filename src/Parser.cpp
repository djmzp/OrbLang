#include "Parser.h"
#include <iostream>
#include <sstream>
#include <cstdint>
#include "AST.h"
using namespace std;

Parser::Parser(NamePool *namePool, StringPool *stringPool, SymbolTable *symbolTable, CompileMessages *msgs) 
    : namePool(namePool), stringPool(stringPool), symbolTable(symbolTable), msgs(msgs), lex(nullptr) {
}

const Token& Parser::peek() const {
    return lex->peek();
}

Token Parser::next() {
    return lex->next();
}

// If the next token matches the type, eats it and returns true.
// Otherwise, returns false.
bool Parser::match(Token::Type type) {
    if (peek().type != type) return false;
    next();
    return true;
}

// If the next token matches the type, eats it and returns true.
// Otherwise, files an error and returns false.
bool Parser::matchOrError(Token::Type type) {
    if (!match(type)) {
        msgs->errorUnexpectedTokenType(loc(), type, peek());
        return false;
    }
    return true;
}

CodeLoc Parser::loc() const {
    return lex->loc();
}

unique_ptr<AstNode> Parser::makeEmptyTerm() {
    unique_ptr<AstNode> empty = make_unique<AstNode>(loc(), AstNode::Kind::kTerminal);
    empty->terminal = make_unique<AstTerminal>();
    return empty;
}

// cannot be semicolon-terminated
unique_ptr<AstNode> Parser::parseType() {
    if (peek().type == Token::T_BRACE_L_REG || peek().type == Token::T_BRACE_L_CUR)
        return parseNode();
    else
        return parseTerm();
}

unique_ptr<AstNode> Parser::parseTerm() {
    CodeLoc codeLocTok = loc();
    Token tok = next();

    unique_ptr<AstNode> term = make_unique<AstNode>(codeLocTok, AstNode::Kind::kTerminal);

    UntypedVal val;
    switch (tok.type) {
    case Token::T_ELLIPSIS:
    case Token::T_CN:
    case Token::T_FNC:
    case Token::T_DATA:
    case Token::T_LET:
    case Token::T_ARR:
    case Token::T_CAST:
    case Token::T_IF:
    case Token::T_FOR:
    case Token::T_WHILE:
    case Token::T_DO:
    case Token::T_BREAK:
    case Token::T_CONTINUE:
    case Token::T_RET:
    case Token::T_IMPORT:
        term->terminal = make_unique<AstTerminal>(tok.type);
        break;
    
    case Token::T_NUM:
        val.type = UntypedVal::T_SINT;
        val.val_si = tok.num;
        term->terminal = make_unique<AstTerminal>(move(val));
        break;
    case Token::T_FNUM:
        val.type = UntypedVal::T_FLOAT;
        val.val_f = tok.fnum;
        term->terminal = make_unique<AstTerminal>(move(val));
        break;
    case Token::T_CHAR:
        val.type = UntypedVal::T_CHAR;
        val.val_c = tok.ch;
        term->terminal = make_unique<AstTerminal>(move(val));
        break;
    case Token::T_BVAL:
        val.type = UntypedVal::T_BOOL;
        val.val_b = tok.bval;
        term->terminal = make_unique<AstTerminal>(move(val));
        break;
    case Token::T_STRING:
        {
            stringstream ss;
            ss << stringPool->get(tok.stringId);
            while (peek().type == Token::T_STRING) {
                ss << stringPool->get(next().stringId);
            }
            UntypedVal val;
            val.type = UntypedVal::T_STRING;
            val.val_str = stringPool->add(ss.str());
            term->terminal = make_unique<AstTerminal>(move(val));
            break;
        }
    case Token::T_NULL:
        val.type = UntypedVal::T_NULL;
        term->terminal = make_unique<AstTerminal>(move(val));
        break;
    
    case Token::T_OPER:
        term->terminal = make_unique<AstTerminal>(tok.op);
        break;

    case Token::T_ID:
        term->terminal = make_unique<AstTerminal>(tok.nameId);
        break;
    
    case Token::T_ATTRIBUTE:
        term->terminal = make_unique<AstTerminal>(tok.attr);
        break;
    
    default:
        msgs->errorUnexpectedTokenType(codeLocTok, {Token::T_BRACE_L_REG, Token::T_BRACE_L_CUR}, tok);
        return nullptr;
    }

    if (peek().type == Token::T_COLON) {
        next();
        
        term->type = parseType();
    }

    return term;
}

unique_ptr<AstNode> Parser::parseNode() {
    if (lex == nullptr) {
        return nullptr;
    }

    CodeLoc codeLocNode = loc();
    unique_ptr<AstNode> node = make_unique<AstNode>(codeLocNode, AstNode::Kind::kTuple);

    if (peek().type == Token::T_BRACE_L_REG || peek().type == Token::T_BRACE_L_CUR) {
        Token openBrace = next();

        vector<unique_ptr<AstNode>> children;

        while (peek().type != Token::T_BRACE_R_REG && peek().type != Token::T_BRACE_R_CUR) {
            if (peek().type == Token::T_SEMICOLON) {
                next();

                if (children.empty()) {
                    node->children.push_back(makeEmptyTerm());
                } else {
                    unique_ptr<AstNode> tuple = make_unique<AstNode>(children[0]->codeLoc, AstNode::Kind::kTuple);
                    tuple->children = move(children);

                    node->children.push_back(move(tuple));
                }
            } else {
                if (peek().type == Token::T_BRACE_L_REG || peek().type == Token::T_BRACE_L_CUR) {
                    unique_ptr<AstNode> child = parseNode();
                    if (child == nullptr) return nullptr;
                    children.push_back(move(child));
                } else {
                    unique_ptr<AstNode> child = parseTerm();
                    if (child == nullptr) return nullptr;
                    children.push_back(move(child));
                }
            }
        }
        
        CodeLoc codeLocCloseBrace = loc();
        Token closeBrace = next();

        if (openBrace.type == Token::T_BRACE_L_REG && closeBrace.type != Token::T_BRACE_R_REG) {
            msgs->errorUnexpectedTokenType(codeLocCloseBrace, Token::T_BRACE_R_REG, closeBrace);
            return nullptr;
        } else if (openBrace.type == Token::T_BRACE_L_CUR && closeBrace.type != Token::T_BRACE_R_CUR) {
            msgs->errorUnexpectedTokenType(codeLocCloseBrace, Token::T_BRACE_R_CUR, closeBrace);
            return nullptr;
        }

        for (size_t i = 0; i < children.size(); ++i) {
            node->children.push_back(move(children[i]));
        }
    } else {
        while (peek().type != Token::T_SEMICOLON) {
            if (peek().type == Token::T_BRACE_L_REG || peek().type == Token::T_BRACE_L_CUR) {
                unique_ptr<AstNode> child = parseNode();
                if (child == nullptr) return nullptr;
                node->children.push_back(move(child));
            } else {
                unique_ptr<AstNode> child = parseTerm();
                if (child == nullptr) return nullptr;
                node->children.push_back(move(child));
            }
        }
        next();
    }

    if (node->children.empty()) {
        node = makeEmptyTerm();
    }

    if (peek().type == Token::T_COLON) {
        // TODO nextIf - eats if matching and rets true, otherwise rets false
        next();

        node->type = parseType();
    }

    return node;
}