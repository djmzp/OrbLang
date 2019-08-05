#include "Parser.h"
#include <iostream>
using namespace std;

Parser::Parser(NamePool *namePool, SymbolTable *symbolTable, Lexer *lexer) : namePool(namePool), symbolTable(symbolTable), lex(lexer), panic(false) {
    codegen = std::make_unique<CodeGen>(namePool, symbolTable);
}

// Eats the next token and panics if it doesn't match the expected type.
bool Parser::mismatch(Token::Type expected) {
    if (!lex->match(expected)) panic = true;
    return panic;
}

unique_ptr<ExprAST> Parser::prim() {
    Token tok = lex->next();
    if (tok.type == Token::T_NUM) {
        return make_unique<LiteralExprAST>(tok.num);
    } else if (tok.type == Token::T_ID) {
        return make_unique<VarExprAST>(tok.nameId);
    } else {
        panic = true;
        return nullptr;
    }
}

unique_ptr<ExprAST> Parser::expr() {
    unique_ptr<ExprAST> first = prim();
    if (panic) return nullptr;

    return expr(move(first), minOperPrec);
}

unique_ptr<ExprAST> Parser::expr(unique_ptr<ExprAST> lhs, OperPrec min_prec) {
    Token lookOp = lex->peek();
    while (lookOp.type == Token::T_OPER && operInfos.at(lookOp.op).prec >= min_prec) {
        Token op = lex->next();
        
        unique_ptr<ExprAST> rhs = prim();
        if (panic) return 0;

        lookOp = lex->peek();
        while (lookOp.type == Token::T_OPER && 
            (operInfos.at(lookOp.op).prec > operInfos.at(op.op).prec ||
            (operInfos.at(lookOp.op).prec == operInfos.at(op.op).prec && !operInfos.at(lookOp.op).l_assoc))) {
            rhs = expr(move(rhs), operInfos.at(lookOp.op).prec);
            lookOp = lex->peek();
        }

        lhs = make_unique<BinExprAST>(move(lhs), move(rhs), op.op);
    }
    return lhs;
}

unique_ptr<DeclAST> Parser::decl() {
    if (mismatch(Token::T_VAR)) return nullptr;

    unique_ptr<DeclAST> ret = make_unique<DeclAST>();

    while (true) {
        Token id = lex->next();
        if (id.type != Token::T_ID) {
            panic = true;
            return nullptr;
        }

        unique_ptr<ExprAST> init;
        Token look = lex->next();
        if (look.type == Token::T_OPER && look.op == Token::O_ASGN) {
            init = expr();
            if (broken(init)) return nullptr;
            look = lex->next();
        }
        ret->add(make_pair(id.nameId, move(init)));

        if (look.type == Token::T_SEMICOLON) {
            return ret;
        } else if (look.type != Token::T_COMMA) {
            panic = true;
            return nullptr;
        }
    }
}

std::unique_ptr<RetAST> Parser::ret() {
    if (mismatch(Token::T_RET)) return nullptr;

    unique_ptr<ExprAST> val;
    if (lex->peek().type != Token::T_SEMICOLON) {
        val = expr();
        if (broken(val)) return nullptr;
    }

    if (mismatch(Token::T_SEMICOLON)) return nullptr;

    return make_unique<RetAST>(move(val));
}

unique_ptr<BlockAST> Parser::block() {
    if (mismatch(Token::T_BRACE_L_CUR)) return nullptr;

    unique_ptr<BlockAST> ret = make_unique<BlockAST>();

    while (lex->peek().type != Token::T_BRACE_R_CUR) {
        unique_ptr<StmntAST> st = stmnt();
        if (broken(st)) return nullptr;

        ret->add(move(st));
    }
    lex->next();

    return ret;
}

unique_ptr<StmntAST> Parser::stmnt() {
    if (lex->peek().type == Token::T_VAR) return decl();

    if (lex->peek().type == Token::T_RET) return ret();

    if (lex->peek().type == Token::T_BRACE_L_CUR) return block();
    
    unique_ptr<StmntAST> st = expr();
    if (broken(st)) return nullptr;
    if (mismatch(Token::T_SEMICOLON)) return nullptr;
    return st;
}

unique_ptr<BaseAST> Parser::func() {
    if (mismatch(Token::T_FNC)) return nullptr;

    Token name = lex->next();
    if (name.type != Token::T_ID) {
        panic = true;
        return nullptr;
    }

    unique_ptr<FuncProtoAST> proto = make_unique<FuncProtoAST>(name.nameId);

    if (mismatch(Token::T_BRACE_L_REG)) return nullptr;

    bool first = true;
    while (true) {
        if (lex->peek().type == Token::T_BRACE_R_REG) {
            lex->next();
            break;
        }

        if (!first && mismatch(Token::T_COMMA)) return nullptr;

        if (mismatch(Token::T_VAR)) return nullptr;

        Token arg = lex->next();
        if (arg.type != Token::T_ID) {
            panic = true;
            return nullptr;
        }

        proto->addArg(arg.nameId);

        first = false;
    }

    Token look = lex->peek();
    if (look.type == Token::T_VAR) {
        proto->setRetVal(true);
        lex->next();
        look = lex->peek();
    } else {
        proto->setRetVal(false);
    }

    if (look.type == Token::T_BRACE_L_CUR) {
        unique_ptr<BlockAST> body = block();
        if (broken(body)) return nullptr;

        return make_unique<FuncAST>(move(proto), move(body));
    } else if (look.type == Token::T_SEMICOLON) {
        lex->next();
        return proto;
    } else {
        panic = true;
        return nullptr;
    }
}

void Parser::parse(std::istream &istr) {
    lex->start(istr);

    while (lex->peek().type != Token::T_END) {
        unique_ptr<BaseAST> next;

        if (lex->peek().type == Token::T_FNC) next = func();
        else next = decl();
        
        if (panic || !next) {
            panic = true;
            break;
        }

        next->print();
        cout << endl;

        codegen->codegen(next.get());
        if (codegen->isPanic()) { panic = true; }
        if (panic) break;
    }

    if (codegen->isPanic()) panic = true;
    if (panic) {
        cout << "ERROR!" << endl;
        return;
    }

    cout << endl;
    codegen->printout();
}