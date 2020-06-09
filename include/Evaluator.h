#pragma once

#include "SymbolTable.h"
#include "AST.h"
#include "Values.h"
#include "CompileMessages.h"

class Codegen;

class Evaluator {
    StringPool *stringPool;
    SymbolTable *symbolTable;
    AstStorage *astStorage;
    CompileMessages *msgs;

    // TODO refactor out
    Codegen *codegen;

    TypeTable* getTypeTable() { return symbolTable->getTypeTable(); }
    const TypeTable* getTypeTable() const { return symbolTable->getTypeTable(); }

    TypeTable::Id getPrimTypeId(TypeTable::PrimIds primId) const { return getTypeTable()->getPrimTypeId(primId); }

public:
    std::optional<NamePool::Id> getId(const AstNode *ast, bool orError);
    std::optional<Token::Type> getKeyword(const AstNode *ast, bool orError);
    std::optional<Token::Oper> getOper(const AstNode *ast, bool orError);
    std::optional<UntypedVal> getUntypedVal(const AstNode *ast, bool orError);
    std::optional<TypeTable::Id> getType(const AstNode *ast, bool orError);

    bool isI(const UntypedVal &val) const;
    bool isU(const UntypedVal &val) const;
    bool isF(const UntypedVal &val) const;
    bool isB(const UntypedVal &val) const;
    bool isC(const UntypedVal &val) const;
    bool isStr(const UntypedVal &val) const;
    bool isNull(const UntypedVal &val) const;
    bool cast(UntypedVal &val, TypeTable::Id t) const;

    NodeVal calculate(CodeLoc codeLoc, Token::Oper op, UntypedVal unty);
    NodeVal calculate(CodeLoc codeLoc, Token::Oper op, UntypedVal untyL, UntypedVal untyR);

private:
    NodeVal evaluateMac(AstNode *ast);
    NodeVal evaluateImport(const AstNode *ast);
    NodeVal evaluateUntypedVal(const AstNode *ast);
    NodeVal evaluateOperUnary(const AstNode *ast, const NodeVal &first);
    NodeVal evaluateOper(CodeLoc codeLoc, Token::Oper op, const NodeVal &lhs, const NodeVal &rhs);
    NodeVal evaluateOper(const AstNode *ast, const NodeVal &first);

    std::optional<NodeVal> evaluateTypeDescr(const AstNode *ast, const NodeVal &first);

    NodeVal evaluateExpr(const AstNode *ast, const NodeVal &first);

    void substitute(std::unique_ptr<AstNode> &body, const std::vector<NamePool::Id> &names, const std::vector<const AstNode*> &values);

public:
    Evaluator(StringPool *stringPool, SymbolTable *symbolTable, AstStorage *astStorage, CompileMessages *msgs);

    void setCodegen(Codegen *c) { codegen = c; }

    // TODO unify with evaluateNode
    CompilerAction evaluateGlobalNode(AstNode *ast);

    NodeVal evaluateType(const AstNode *ast, const NodeVal &first);
    NodeVal evaluateTerminal(const AstNode *ast);
    NodeVal evaluateNode(const AstNode *ast);

    std::unique_ptr<AstNode> evaluateInvoke(NamePool::Id macroName, const AstNode *ast);
};