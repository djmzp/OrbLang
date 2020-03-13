#include "SymbolTable.h"
using namespace std;

bool FuncSignature::operator==(const FuncSignature &other) const {
    if (name != other.name) return false;
    if (argTypes.size() != other.argTypes.size()) return false;
    for (std::size_t i = 0; i < argTypes.size(); ++i)
        if (argTypes[i] != other.argTypes[i]) return false;
    
    return true;
}

std::size_t FuncSignature::Hasher::operator()(const FuncSignature &k) const {
    // le nice hashe functione
    std::size_t sum = k.argTypes.size();
    for (const auto &it : k.argTypes) sum += it;
    return (17*31+k.name)*31+sum;
}

SymbolTable::SymbolTable(TypeTable *typeTable) : typeTable(typeTable), inFunc(false) {
    last = glob = new Scope();
    glob->prev = nullptr;
}

void SymbolTable::newScope() {
    Scope *s = new Scope();
    s->prev = last;
    last = s;
}

void SymbolTable::endScope() {
    Scope *s = last;
    last = last->prev;
    delete s;
}

void SymbolTable::addVar(NamePool::Id name, const VarPayload &var) {
    last->vars.insert(make_pair(name, var));
}

pair<SymbolTable::VarPayload, bool> SymbolTable::getVar(NamePool::Id name) const {
    for (Scope *s = last; s != nullptr; s = s->prev) {
        auto loc = s->vars.find(name);
        if (loc != s->vars.end()) return {loc->second, true};
    }

    return {VarPayload(), false};
}

FuncSignature SymbolTable::makeFuncSignature(NamePool::Id name, const std::vector<TypeTable::Id> &argTypes) const {
    FuncSignature sig;
    sig.name = name;
    sig.argTypes = std::vector<TypeTable::Id>(argTypes.size());
    for (size_t i = 0; i < sig.argTypes.size(); ++i)
        sig.argTypes[i] = typeTable->getTypeFuncSigParam(argTypes[i]);
    return sig;
}

pair<FuncSignature, bool> SymbolTable::makeFuncSignature(const FuncCallSite &call) const {
    for (const UntypedVal &it : call.untypedVals) {
        if (it.type != UntypedVal::T_NONE)
            return {FuncSignature(), false};
    }
    return {makeFuncSignature(call.name, call.argTypes), true};
}

bool nonConflicting(const FuncValue &f1, const FuncValue &f2) {
    if (f1.name != f2.name) return true;

    if (f1.defined && f2.defined) return false;
    if (f1.hasRet != f2.hasRet) return false;
    if (f1.hasRet && f1.retType != f2.retType) return false;
    if (f1.noNameMangle != f2.noNameMangle) return false;

    if (f1.argTypes.size() != f2.argTypes.size()) return false;
    // cn is not part of func sig, but all decls and def must agree on this
    for (size_t i = 0; i < f1.argTypes.size(); ++i) {
        if (f1.argTypes[i] != f2.argTypes[i]) return false;
    }

    return true;
}

bool SymbolTable::canRegisterFunc(const FuncValue &val) const {
    if (val.noNameMangle) {
        auto loc = funcsNoNameMangle.find(val.name);
        if (loc != funcsNoNameMangle.end() && !nonConflicting(val, loc->second))
            return false;
    }

    FuncSignature sig = makeFuncSignature(val.name, val.argTypes);
    auto loc = funcs.find(sig);

    if (loc == funcs.end()) return true;

    if (!nonConflicting(val, loc->second)) return false;

    return true;
}

FuncValue SymbolTable::registerFunc(const FuncValue &val) {
    FuncSignature sig = makeFuncSignature(val.name, val.argTypes);
    funcs[sig] = val;
    if (val.noNameMangle) funcsNoNameMangle[val.name] = val;
    return funcs[sig];
}

llvm::Function* SymbolTable::getFunction(const FuncValue &val) const {
    FuncSignature sig = makeFuncSignature(val.name, val.argTypes);
    auto loc = funcs.find(sig);
    if (loc == funcs.end()) return nullptr;
    return loc->second.func;
}

std::pair<FuncValue, bool> SymbolTable::getFuncForCall(const FuncCallSite &call) {
    // if there are any untypedVal args, we don't know their types in advance
    pair<FuncSignature, bool> sig = makeFuncSignature(call);
    if (sig.second) {
        auto loc = funcs.find(sig.first);
        // if there's a single function and doesn't need casting, return it
        if (loc != funcs.end()) return {loc->second, true};
        // if func with no args and not found, fail (no casting can be applied since no args)
        if (call.argTypes.empty()) return {FuncValue(), false};
    }

    const FuncSignature *foundSig = nullptr;
    FuncValue *foundVal = nullptr;

    for (auto &it : funcs) {
        // need func with that name
        if (it.first.name != call.name) continue;
        // and that num of args
        if (it.first.argTypes.size() != call.argTypes.size()) continue;

        const FuncSignature *candSig = &it.first;
        FuncValue *candVal = &it.second;

        for (size_t i = 0; i < it.second.argTypes.size(); ++i) {
            // if arg of same or implicitly castable type, we're good
            bool argTypeOk = call.untypedVals[i].type == UntypedVal::T_NONE &&
                typeTable->isArgTypeProper(call.argTypes[i], it.second.argTypes[i]);

            // otherwise, if untyped val which can be used for this func, we're also good
            if (!argTypeOk && call.untypedVals[i].type != UntypedVal::T_NONE) {
                switch (call.untypedVals[i].type) {
                case UntypedVal::T_BOOL:
                    argTypeOk = typeTable->isTypeB(it.second.argTypes[i]);
                    break;
                case UntypedVal::T_SINT:
                    argTypeOk = typeTable->isTypeI(it.second.argTypes[i]) ||
                        (typeTable->isTypeU(it.second.argTypes[i]) && call.untypedVals[i].val_si >= 0);
                    break;
                case UntypedVal::T_CHAR:
                    argTypeOk = typeTable->isTypeC(it.second.argTypes[i]);
                    break;
                case UntypedVal::T_FLOAT:
                    argTypeOk = typeTable->isTypeF(it.second.argTypes[i]);
                    break;
                case UntypedVal::T_NULL:
                    argTypeOk = typeTable->isTypeAnyP(it.second.argTypes[i]);
                    break;
                case UntypedVal::T_STRING:
                    argTypeOk = typeTable->isTypeStr(it.second.argTypes[i]) ||
                        typeTable->isTypeCharArrOfLen(it.second.argTypes[i], call.untypedVals[i].getStringLen());
                    break;
                default:
                    argTypeOk = false;
                    break;
                }
            }

            if (!argTypeOk) {
                candSig = nullptr;
                candVal = nullptr;
                break;
            }
        }

        // found no suitable functions
        if (candVal == nullptr) continue;

        // in case of multiple possible funcs, error due to ambiguity
        // TODO ret what the exact error was in ret val
        if (foundVal != nullptr) return {FuncValue(), false};
        
        // found a function that fits this description (which may or may not require casts)
        foundSig = candSig;
        foundVal = candVal;
    }

    if (foundVal) return {*foundVal, true};
    else return {FuncValue(), false};
}

bool SymbolTable::varNameTaken(NamePool::Id name) const {
    if (typeTable->isType(name)) return true;

    if (last == glob) {
        // you can have vars with same name as funcs, except in global
        for (const auto &p : funcs)
            if (p.first.name == name)
                return true;
    }

    return last->vars.find(name) != last->vars.end();
}

// only checks for name collisions with global vars, NOT with funcs of same sig!
bool SymbolTable::funcNameTaken(NamePool::Id name) const {
    return typeTable->isType(name) || last->vars.find(name) != last->vars.end();
}

SymbolTable::~SymbolTable() {
    while (last != nullptr) {
        Scope *s = last;
        last = last->prev;
        delete s;
    }
}