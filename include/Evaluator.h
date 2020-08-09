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

    bool loopIssued, exitIssued;
    std::optional<NamePool::Id> blockGoto;
    std::optional<NodeVal> blockPassVal;

    TypeTable* getTypeTable() { return symbolTable->getTypeTable(); }
    const TypeTable* getTypeTable() const { return symbolTable->getTypeTable(); }

    TypeTable::Id getPrimTypeId(TypeTable::PrimIds primId) const { return getTypeTable()->getPrimTypeId(primId); }

    bool isGotoIssued() const {
        return loopIssued || exitIssued || blockPassVal.has_value();
    }

    void resetGotoIssuing() {
        loopIssued = false;
        exitIssued = false;
        blockGoto.reset();
        blockPassVal.reset();
    }

public:
    std::optional<NamePool::Id> getId(const AstNode *ast, bool orError);
    std::optional<Token::Type> getKeyword(const AstNode *ast, bool orError);
    std::optional<Token::Oper> getOper(const AstNode *ast, bool orError);
    std::optional<KnownVal> getKnownVal(const AstNode *ast, bool orError);
    std::optional<TypeTable::Id> getType(const AstNode *ast, bool orError);

    bool cast(KnownVal &val, TypeTable::Id t) const;

    NodeVal calculateOperUnary(CodeLoc codeLoc, Token::Oper op, KnownVal known);
    NodeVal calculateOper(CodeLoc codeLoc, Token::Oper op, KnownVal knownL, KnownVal knownR);
    NodeVal calculateCast(CodeLoc codeLoc, KnownVal known, TypeTable::Id type);

private:
    NodeVal evaluateMac(AstNode *ast);
    NodeVal evaluateImport(const AstNode *ast);
    NodeVal evaluateKnownVal(const AstNode *ast);
    NodeVal evaluateOperUnary(const AstNode *ast, const NodeVal &first);
    NodeVal evaluateOper(CodeLoc codeLoc, Token::Oper op, const NodeVal &lhs, const NodeVal &rhs);
    NodeVal evaluateOper(const AstNode *ast, const NodeVal &first);
    NodeVal evaluateCast(const AstNode *ast);
    NodeVal evaluateExit(const AstNode *ast);
    NodeVal evaluatePass(const AstNode *ast);
    NodeVal evaluateLoop(const AstNode *ast);
    NodeVal evaluateBlock(const AstNode *ast);

    std::optional<NodeVal> evaluateTypeDescr(const AstNode *ast, const NodeVal &first);

    NodeVal evaluateExpr(const AstNode *ast, const NodeVal &first);

    NodeVal evaluateAll(const AstNode *ast);

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