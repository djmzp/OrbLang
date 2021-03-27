#include "Parser.h"
#include <iostream>
#include <sstream>
#include <cstdint>
using namespace std;

Parser::Parser(StringPool *stringPool, TypeTable *typeTable, CompilationMessages *msgs) 
    : stringPool(stringPool), typeTable(typeTable), lex(nullptr), msgs(msgs) {
}

const Token& Parser::peek() const {
    return lex->peek();
}

Token Parser::next() {
    return lex->next();
}

CodeLocPoint Parser::loc() const {
    return lex->loc();
}

// If the next token matches the type, eats it and returns true.
// Otherwise, returns false.
bool Parser::match(Token::Type type) {
    if (peek().type != type) return false;
    next();
    return true;
}

bool Parser::matchCloseBraceOrError(Token openBrace) {
    CodeLoc codeLocCloseBrace;
    codeLocCloseBrace.start = loc();
    Token closeBrace = next();
    codeLocCloseBrace.end = loc();

    if (openBrace.type == Token::T_BRACE_L_REG && closeBrace.type != Token::T_BRACE_R_REG) {
        msgs->errorUnexpectedTokenType(codeLocCloseBrace, Token::T_BRACE_R_REG, closeBrace);
        return false;
    } else if (openBrace.type == Token::T_BRACE_L_CUR && closeBrace.type != Token::T_BRACE_R_CUR) {
        msgs->errorUnexpectedTokenType(codeLocCloseBrace, Token::T_BRACE_R_CUR, closeBrace);
        return false;
    }

    return true;
}

EscapeScore Parser::parseEscapeScore() {
    EscapeScore escapeScore = 0;
    while (peek().type == Token::T_BACKSLASH || peek().type == Token::T_COMMA) {
        if (match(Token::T_BACKSLASH)) {
            ++escapeScore;
        } else {
            match(Token::T_COMMA);
            --escapeScore;
        }
    }
    return escapeScore;
}

void Parser::parseTypeAttr(NodeVal &node) {
    if (match(Token::T_COLON)) node.setTypeAttr(parseBare());
}

void Parser::parseNonTypeAttrs(NodeVal &node) {
    if (match(Token::T_DOUBLE_COLON)) node.setNonTypeAttrs(parseBare());
}

NodeVal Parser::parseBare() {
    EscapeScore escapeScore = parseEscapeScore();

    NodeVal nodeVal;
    if (peek().type == Token::T_BRACE_L_REG || peek().type == Token::T_BRACE_L_CUR)
        nodeVal = parseNode(true);
    else
        nodeVal = parseTerm(true);

    NodeVal::escape(nodeVal, typeTable, escapeScore);
    return nodeVal;
}

NodeVal Parser::parseTerm(bool ignoreAttrs) {
    CodeLoc codeLocTok;
    codeLocTok.start = loc();
    Token tok = next();
    codeLocTok.end = loc();

    LiteralVal val;
    switch (tok.type) {
    case Token::T_ID:
        val.kind = LiteralVal::Kind::kId;
        val.val_id = tok.nameId;
        break;
    case Token::T_NUM:
        val.kind = LiteralVal::Kind::kSint;
        val.val_si = tok.num;
        break;
    case Token::T_FNUM:
        val.kind = LiteralVal::Kind::kFloat;
        val.val_f = tok.fnum;
        break;
    case Token::T_CHAR:
        val.kind = LiteralVal::Kind::kChar;
        val.val_c = tok.ch;
        break;
    case Token::T_BVAL:
        val.kind = LiteralVal::Kind::kBool;
        val.val_b = tok.bval;
        break;
    case Token::T_STRING:
        val.kind = LiteralVal::Kind::kString;
        val.val_str = tok.stringId;
        break;
    case Token::T_NULL:
        val.kind = LiteralVal::Kind::kNull;
        break;    
    default:
        msgs->errorUnexpectedTokenType(codeLocTok, tok);
        return NodeVal();
    }

    NodeVal ret(codeLocTok, val);

    if (!ignoreAttrs) {
        parseTypeAttr(ret);
        parseNonTypeAttrs(ret);
    }

    return ret;
}

NodeVal Parser::parseNode(bool ignoreAttrs) {
    CodeLoc codeLoc;
    codeLoc.start = loc();

    NodeVal node = NodeVal::makeEmpty(CodeLoc(), typeTable);

    EscapeScore escapeScore = parseEscapeScore();

    if (peek().type == Token::T_BRACE_L_REG || peek().type == Token::T_BRACE_L_CUR) {
        Token openBrace = next();

        vector<NodeVal> children;

        while (peek().type != Token::T_BRACE_R_REG && peek().type != Token::T_BRACE_R_CUR) {
            if (peek().type == Token::T_SEMICOLON) {
                CodeLoc codeLocSemicolon;
                codeLocSemicolon.start = loc();
                next();
                codeLocSemicolon.end = loc();

                if (children.empty()) {
                    NodeVal::addChild(node, NodeVal::makeEmpty(codeLocSemicolon, typeTable), typeTable);
                } else {
                    CodeLoc codeLoc;
                    codeLoc.start = children.front().getCodeLoc().start;
                    codeLoc.end = codeLocSemicolon.end;

                    NodeVal tuple = NodeVal::makeEmpty(codeLoc, typeTable);
                    NodeVal::addChildren(tuple, move(children), typeTable); // children is emptied here
                    NodeVal::addChild(node, move(tuple), typeTable);
                }
            } else {
                EscapeScore escapeScore = parseEscapeScore();
                NodeVal child;
                if (peek().type == Token::T_BRACE_L_REG || peek().type == Token::T_BRACE_L_CUR) {
                    child = parseNode();
                } else {
                    child = parseTerm();
                }
                if (child.isInvalid()) return NodeVal();
                NodeVal::escape(child, typeTable, escapeScore);
                children.push_back(move(child));
            }
        }

        if (!matchCloseBraceOrError(openBrace)) return NodeVal();

        codeLoc.end = loc(); // code loc point after close brace
        node.setCodeLoc(codeLoc);

        NodeVal::addChildren(node, move(children), typeTable);
    } else {
        while (peek().type != Token::T_SEMICOLON) {
            escapeScore += parseEscapeScore();
            NodeVal child;
            if (peek().type == Token::T_BRACE_L_REG || peek().type == Token::T_BRACE_L_CUR) {
                child = parseNode();
            } else {
                child = parseTerm();
            }
            if (child.isInvalid()) return NodeVal();
            NodeVal::escape(child, typeTable, escapeScore);
            escapeScore = 0;
            NodeVal::addChild(node, move(child), typeTable);
        }

        next();

        codeLoc.end = loc(); // code loc point after semicolon
        node.setCodeLoc(codeLoc);
    }

    NodeVal::escape(node, typeTable, escapeScore);
    escapeScore = 0;

    if (!ignoreAttrs) {
        parseTypeAttr(node);
        parseNonTypeAttrs(node);
    }

    return node;
}