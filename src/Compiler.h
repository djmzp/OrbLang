#pragma once

#include <string>
#include "Processor.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

class Compiler : public Processor {
    llvm::LLVMContext llvmContext;
    llvm::IRBuilder<> llvmBuilder, llvmBuilderAlloca;
    std::unique_ptr<llvm::Module> llvmModule;
    std::unique_ptr<llvm::PassManagerBuilder> llvmPmb;
    std::unique_ptr<llvm::legacy::FunctionPassManager> llvmFpm;
    llvm::TargetMachine *targetMachine;

    bool initLlvmTargetMachine();

    bool isLlvmBlockTerminated() const;
    llvm::Function* getLlvmCurrFunction() { return llvmBuilder.GetInsertBlock()->getParent(); }
    llvm::Constant* getLlvmConstB(bool val);
    // generates a constant for a string literal
    llvm::Constant* makeLlvmConstString(const std::string &str);
    llvm::Type* makeLlvmType(TypeTable::Id typeId);
    llvm::Type* makeLlvmPrimType(TypeTable::PrimIds primTypeId) { return makeLlvmType(typeTable->getPrimTypeId(primTypeId)); }
    llvm::Type* makeLlvmTypeOrError(CodeLoc codeLoc, TypeTable::Id typeId);
    llvm::GlobalValue* makeLlvmGlobal(llvm::Type *type, llvm::Constant *init, bool isConstant, const std::string &name);
    llvm::AllocaInst* makeLlvmAlloca(llvm::Type *type, const std::string &name);
    llvm::Value* makeLlvmCast(llvm::Value *srcLlvmVal, TypeTable::Id srcTypeId, llvm::Type *dstLlvmType, TypeTable::Id dstTypeId);

    std::string getNameForLlvm(NamePool::Id name) const;

    NodeVal promoteEvalVal(CodeLoc codeLoc, const EvalVal &eval);
    NodeVal promoteEvalVal(const NodeVal &node);
    NodeVal promoteIfEvalValAndCheckIsLlvmVal(const NodeVal &node, bool orError);

    NodeVal performLoad(CodeLoc codeLoc, NamePool::Id id, NodeVal &ref) override;
    NodeVal performZero(CodeLoc codeLoc, TypeTable::Id ty) override;
    NodeVal performRegister(CodeLoc codeLoc, NamePool::Id id, TypeTable::Id ty) override;
    NodeVal performRegister(CodeLoc codeLoc, NamePool::Id id, const NodeVal &init) override;
    NodeVal performCast(CodeLoc codeLoc, const NodeVal &node, TypeTable::Id ty) override;
    bool performBlockSetUp(CodeLoc codeLoc, SymbolTable::Block &block) override;
    std::optional<bool> performBlockBody(CodeLoc codeLoc, const SymbolTable::Block &block, const NodeVal &nodeBody) override;
    NodeVal performBlockTearDown(CodeLoc codeLoc, const SymbolTable::Block &block, bool success) override;
    bool performExit(CodeLoc codeLoc, const SymbolTable::Block &block, const NodeVal &cond) override;
    bool performLoop(CodeLoc codeLoc, const SymbolTable::Block &block, const NodeVal &cond) override;
    bool performPass(CodeLoc codeLoc, SymbolTable::Block &block, const NodeVal &val) override;
    NodeVal performCall(CodeLoc codeLoc, const FuncValue &func, const std::vector<NodeVal> &args) override;
    NodeVal performInvoke(CodeLoc codeLoc, const MacroValue &macro, const std::vector<NodeVal> &args) override;
    bool performFunctionDeclaration(CodeLoc codeLoc, FuncValue &func) override;
    bool performFunctionDefinition(const NodeVal &args, const NodeVal &body, FuncValue &func) override;
    bool performMacroDefinition(const NodeVal &args, const NodeVal &body, MacroValue &macro) override;
    bool performRet(CodeLoc codeLoc) override;
    bool performRet(CodeLoc codeLoc, const NodeVal &node) override;
    NodeVal performOperUnary(CodeLoc codeLoc, const NodeVal &oper, Oper op) override;
    NodeVal performOperUnaryDeref(CodeLoc codeLoc, const NodeVal &oper, TypeTable::Id resTy) override;
    void* performOperComparisonSetUp(CodeLoc codeLoc, std::size_t opersCnt) override;
    std::optional<bool> performOperComparison(CodeLoc codeLoc, const NodeVal &lhs, const NodeVal &rhs, Oper op, void *signal) override;
    NodeVal performOperComparisonTearDown(CodeLoc codeLoc, bool success, void *signal) override;
    NodeVal performOperAssignment(CodeLoc codeLoc, NodeVal &lhs, const NodeVal &rhs) override;
    NodeVal performOperIndex(CodeLoc codeLoc, NodeVal &base, const NodeVal &ind, TypeTable::Id resTy) override;
    NodeVal performOperMember(CodeLoc codeLoc, NodeVal &base, std::uint64_t ind, TypeTable::Id resTy) override;
    NodeVal performOperRegular(CodeLoc codeLoc, const NodeVal &lhs, const NodeVal &rhs, Oper op) override;
    NodeVal performTuple(CodeLoc codeLoc, TypeTable::Id ty, const std::vector<NodeVal> &membs) override;
    std::optional<std::uint64_t> performSizeOf(CodeLoc codeLoc, TypeTable::Id ty) override;

public:
    Compiler(NamePool *namePool, StringPool *stringPool, TypeTable *typeTable, SymbolTable *symbolTable, CompilationMessages *msgs);

    llvm::Type* genPrimTypeBool();
    llvm::Type* genPrimTypeI(unsigned bits);
    llvm::Type* genPrimTypeU(unsigned bits);
    llvm::Type* genPrimTypeC(unsigned bits);
    llvm::Type* genPrimTypeF32();
    llvm::Type* genPrimTypeF64();
    llvm::Type* genPrimTypePtr();

    void printout() const;
    bool binary(const std::string &filename);
};