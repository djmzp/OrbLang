#pragma once

#include <memory>
#include <vector>
#include "Lexer.h"
#include "SymbolTable.h"

enum ASTType {
    AST_LiteralExpr,
    AST_VarExpr,
    AST_BinExpr,
    AST_Decl,
    AST_FuncProto,
    AST_Func,
    AST_Block,
    AST_Ret
};

class BaseAST {
public:

    virtual ASTType type() const =0;

    virtual void print() const =0;

    virtual ~BaseAST() {}
};

class StmntAST : public BaseAST {
public:

    virtual ~StmntAST() {}
};

class ExprAST : public StmntAST {
public:

    virtual ~ExprAST() {}
};

class LiteralExprAST : public ExprAST {
    int val;

public:
    LiteralExprAST(int v) : val(v) {}

    ASTType type() const { return AST_LiteralExpr; }

    int getVal() const { return val; }

    void print() const;
};

class VarExprAST : public ExprAST {
    NamePool::Id nameId;

public:
    VarExprAST(NamePool::Id id) : nameId(id) {}

    ASTType type() const { return AST_VarExpr; }

    NamePool::Id getNameId() const { return nameId; }

    void print() const;
};

class BinExprAST : public ExprAST {
    std::unique_ptr<ExprAST> lhs, rhs;
    Token::Oper op;

public:
    BinExprAST(
        std::unique_ptr<ExprAST> _lhs, 
        std::unique_ptr<ExprAST>  _rhs, 
        Token::Oper _op);

    ASTType type() const { return AST_BinExpr; }

    const ExprAST* getL() const { return lhs.get(); }
    const ExprAST* getR() const { return rhs.get(); }
    Token::Oper getOp() const { return op; }

    void print() const;
};

class DeclAST : public StmntAST {
    std::vector<std::pair<NamePool::Id, std::unique_ptr<ExprAST>>> decls;

public:
    DeclAST();

    void add(std::pair<NamePool::Id, std::unique_ptr<ExprAST>> decl);
    const std::vector<std::pair<NamePool::Id, std::unique_ptr<ExprAST>>>& getDecls() const { return decls; }

    ASTType type() const { return AST_Decl; }

    void print() const;
};

class BlockAST : public StmntAST {
    std::vector<std::unique_ptr<StmntAST>> body;

public:

    void add(std::unique_ptr<StmntAST> st) { body.push_back(std::move(st)); }
    const std::vector<std::unique_ptr<StmntAST>>& getBody() const { return body; }

    ASTType type() const { return AST_Block; }

    void print() const;
};

class FuncProtoAST : public BaseAST {
    NamePool::Id name;
    std::vector<NamePool::Id> args;
    bool ret;

public:
    FuncProtoAST(NamePool::Id name) : name(name) {}

    ASTType type() const { return AST_FuncProto; }

    NamePool::Id getName() const { return name; }

    void addArg(NamePool::Id arg) { args.push_back(arg); }
    const std::vector<NamePool::Id> getArgs() const { return args; }

    void setRetVal(bool r) { ret = r; }
    bool hasRetVal() const { return ret; }

    void print() const;
};

class FuncAST : public BaseAST {
    std::unique_ptr<FuncProtoAST> proto;
    std::unique_ptr<BlockAST> body;

public:
    FuncAST(
        std::unique_ptr<FuncProtoAST> proto,
        std::unique_ptr<BlockAST> body);

    ASTType type() const { return AST_Func; }

    const FuncProtoAST* getProto() const { return proto.get(); }
    const BlockAST* getBody() const { return body.get(); }

    void print() const;
};

class RetAST : public StmntAST {
    std::unique_ptr<ExprAST> val;

public:
    RetAST(std::unique_ptr<ExprAST> v) : val(std::move(v)) {}

    const ExprAST* getVal() const { return val.get(); }

    ASTType type() const { return AST_Ret; }

    void print() const;
};