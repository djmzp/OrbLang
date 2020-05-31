#pragma once

#include <unordered_map>
#include <vector>
#include <string>
#include "NamePool.h"
#include "StringPool.h"
#include "Values.h"
#include "TypeTable.h"
#include "llvm/IR/Instructions.h"

struct AstNode;

struct FuncCallSite {
    NamePool::Id name;
    std::vector<TypeTable::Id> argTypes;
    std::vector<UntypedVal> untypedVals;

    FuncCallSite() {}
    FuncCallSite(std::size_t sz) : argTypes(sz), untypedVals(sz) {}

    void set(std::size_t ind, TypeTable::Id t) {
        argTypes[ind] = t;
        untypedVals[ind] = {UntypedVal::Kind::kNone};
    }

    void set(std::size_t ind, UntypedVal l) {
        untypedVals[ind] = l;
    }
};

struct FuncSignature {
    NamePool::Id name;
    std::vector<TypeTable::Id> argTypes;
    
    bool operator==(const FuncSignature &other) const;

    struct Hasher {
        std::size_t operator()(const FuncSignature &k) const;
    };
};

struct FuncValue {
    NamePool::Id name;
    bool variadic;
    bool noNameMangle;
    std::vector<NamePool::Id> argNames;
    std::vector<TypeTable::Id> argTypes;
    std::optional<TypeTable::Id> retType;
    bool defined;
    llvm::Function *func;

    bool hasRet() const { return retType.has_value(); }
};

struct MacroSignature {
    NamePool::Id name;
    std::size_t argCount;
    
    bool operator==(const MacroSignature &other) const;

    struct Hasher {
        std::size_t operator()(const MacroSignature &k) const;
    };
};

struct MacroValue {
    NamePool::Id name;
    std::vector<NamePool::Id> argNames;
    const AstNode *body;
};

class SymbolTable {
public:
    struct VarPayload {
        TypeTable::Id type;
        llvm::Value *val;
    };

    struct FuncForCallPayload {
        enum Result {
            kFound,
            kAmbigious,
            kNotFound
        };

        Result res;

        std::optional<FuncValue> funcVal;

        explicit FuncForCallPayload(Result res) : res(res) {}
        explicit FuncForCallPayload(const FuncValue &funcVal) : res(kFound), funcVal(funcVal) {}
    };

    struct BlockOpen {
        std::optional<NamePool::Id> name;
        llvm::BasicBlock *blockExit = nullptr, *blockLoop = nullptr;
    };

    struct Block {
        std::optional<NamePool::Id> name;
        llvm::BasicBlock *blockExit = nullptr, *blockLoop = nullptr;
        std::unordered_map<NamePool::Id, VarPayload> vars;
        Block *prev;
    };

private:
    friend class BlockControl;

    StringPool *stringPool;
    TypeTable *typeTable;

    std::unordered_map<FuncSignature, FuncValue, FuncSignature::Hasher> funcs;
    std::unordered_map<NamePool::Id, FuncValue> funcsNoNameMangle;

    std::unordered_map<MacroSignature, MacroValue, MacroSignature::Hasher> macros;

    Block *last, *glob;

    bool inFunc;
    FuncValue currFunc;

    void setCurrFunc(const FuncValue &func) { inFunc = true; currFunc = func; }
    void clearCurrFunc() { inFunc = false; }

    void newBlock(BlockOpen b);
    void endBlock();

    FuncSignature makeFuncSignature(NamePool::Id name, const std::vector<TypeTable::Id> &argTypes) const;
    std::optional<FuncSignature> makeFuncSignature(const FuncCallSite &call) const;
    bool isCallArgsOk(const FuncCallSite &call, const FuncValue &func) const;

    MacroSignature makeMacroSignature(const MacroValue &val) const;

public:
    SymbolTable(StringPool *stringPool, TypeTable *typeTable);

    void addVar(NamePool::Id name, const VarPayload &var);
    std::optional<VarPayload> getVar(NamePool::Id name) const;

    bool canRegisterFunc(const FuncValue &val) const;
    FuncValue registerFunc(const FuncValue &val);
    llvm::Function* getFunction(const FuncValue &val) const;
    FuncForCallPayload getFuncForCall(const FuncCallSite &call);

    bool canRegisterMacro(const MacroValue &val) const;
    void registerMacro(const MacroValue &val);
    std::optional<MacroValue> getMacro(const MacroSignature &sig) const;

    bool inGlobalScope() const { return last == glob; }
    const Block* getLastBlock() const { return last; }

    std::optional<FuncValue> getCurrFunc() const;

    bool isVarName(NamePool::Id name) const { return getVar(name).has_value(); }
    bool isFuncName(NamePool::Id name) const;
    bool isMacroName(NamePool::Id name) const;
    
    bool varMayTakeName(NamePool::Id name) const;
    // only checks for name collisions with global vars, macros and datas, NOT with funcs of same sig!
    bool funcMayTakeName(NamePool::Id name) const;
    // only checks for name collisions with global vars, functions and datas, NOT with macros of same sig!
    bool macroMayTakeName(NamePool::Id name) const;

    TypeTable* getTypeTable() { return typeTable; }

    ~SymbolTable();
};

class BlockControl {
    SymbolTable *symTable;
    bool funcOpen;

public:
    explicit BlockControl(SymbolTable *symTable = nullptr) : symTable(symTable), funcOpen(false) {
        if (symTable != nullptr) symTable->newBlock(SymbolTable::BlockOpen());
    }
    BlockControl(SymbolTable *symTable, SymbolTable::BlockOpen bo) : symTable(symTable), funcOpen(false) {
        this->symTable->newBlock(bo);
    }
    // ref cuz must not be null
    BlockControl(SymbolTable &symTable, const FuncValue &func) : symTable(&symTable), funcOpen(true) {
        this->symTable->setCurrFunc(func);
        this->symTable->newBlock(SymbolTable::BlockOpen());
    }

    BlockControl(const BlockControl&) = delete;
    void operator=(const BlockControl&) = delete;

    BlockControl(const BlockControl&&) = delete;
    void operator=(const BlockControl&&) = delete;

    ~BlockControl() {
        if (symTable) symTable->endBlock();
        if (funcOpen) symTable->clearCurrFunc();
    }
};