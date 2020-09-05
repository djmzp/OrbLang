#include "Codegen.h"
#include <iostream>
#include <sstream>
#include "llvm/IR/Verifier.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
using namespace std;

Codegen::Codegen(Evaluator *evaluator, NamePool *namePool, StringPool *stringPool, TypeTable *typeTable, SymbolTable *symbolTable, CompileMessages *msgs)
    : Processor(namePool, stringPool, typeTable, symbolTable, msgs), evaluator(evaluator), llvmBuilder(llvmContext), llvmBuilderAlloca(llvmContext) {
    llvmModule = std::make_unique<llvm::Module>(llvm::StringRef("module"), llvmContext);

    llvmPmb = make_unique<llvm::PassManagerBuilder>();
    llvmPmb->OptLevel = 0; // TODO! change to 3

    llvmFpm = make_unique<llvm::legacy::FunctionPassManager>(llvmModule.get());
    llvmPmb->populateFunctionPassManager(*llvmFpm);
}

void Codegen::printout() const {
    llvmModule->print(llvm::outs(), nullptr);
}

bool Codegen::binary(const std::string &filename) {
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    std::string targetTriple = llvm::sys::getDefaultTargetTriple();
    llvmModule->setTargetTriple(targetTriple);

    std::string error;
    const llvm::Target *target = llvm::TargetRegistry::lookupTarget(targetTriple, error);
    if (target == nullptr) {
        llvm::errs() << error;
        return false;
    }

    const std::string cpu = "generic";
    const std::string features = "";
    const llvm::TargetOptions options;
    llvm::Optional<llvm::Reloc::Model> relocModel;
    llvm::TargetMachine *targetMachine = target->createTargetMachine(targetTriple, cpu, features, options, relocModel);
    llvmModule->setDataLayout(targetMachine->createDataLayout());

    std::error_code errorCode;
    llvm::raw_fd_ostream dest(filename, errorCode, llvm::sys::fs::F_None);
    if (errorCode) {
        llvm::errs() << "Could not open file: " << errorCode.message();
        return false;
    }

    llvm::legacy::PassManager llvmPm;
    llvmPmb->populateModulePassManager(llvmPm);

    llvm::CodeGenFileType fileType = llvm::CGFT_ObjectFile;

    bool failed = targetMachine->addPassesToEmitFile(llvmPm, dest, nullptr, fileType);
    if (failed) {
        llvm::errs() << "Target machine can't emit to this file type!";
        return false;
    }

    llvmPm.run(*llvmModule);
    dest.flush();

    return true;
}

llvm::Type* Codegen::genPrimTypeBool() {
    return llvm::IntegerType::get(llvmContext, 1);
}

llvm::Type* Codegen::genPrimTypeI(unsigned bits) {
    return llvm::IntegerType::get(llvmContext, bits);
}

llvm::Type* Codegen::genPrimTypeU(unsigned bits) {
    // LLVM makes no distinction between signed and unsigned int
    return llvm::IntegerType::get(llvmContext, bits);
}

llvm::Type* Codegen::genPrimTypeC(unsigned bits) {
    return llvm::IntegerType::get(llvmContext, bits);
}

llvm::Type* Codegen::genPrimTypeF32() {
    return llvm::Type::getFloatTy(llvmContext);
}

llvm::Type* Codegen::genPrimTypeF64() {
    return llvm::Type::getDoubleTy(llvmContext);
}

llvm::Type* Codegen::genPrimTypePtr() {
    return llvm::Type::getInt8PtrTy(llvmContext);
}

NodeVal Codegen::performLoad(CodeLoc codeLoc, NamePool::Id id, const NodeVal &val) {
    if (!checkInLocalScope(codeLoc, true)) return NodeVal();

    // TODO load KnownVal as non-ref?
    if (!checkIsLlvmVal(val, true)) return NodeVal();

    LlvmVal loadLlvmVal;
    loadLlvmVal.type = val.getLlvmVal().type;
    loadLlvmVal.ref = val.getLlvmVal().ref;
    loadLlvmVal.val = llvmBuilder.CreateLoad(val.getLlvmVal().ref, getNameForLlvm(id));
    return NodeVal(codeLoc, loadLlvmVal);
}

NodeVal Codegen::performRegister(CodeLoc codeLoc, NamePool::Id id, TypeTable::Id ty) {
    llvm::Type *llvmType = getLlvmTypeOrError(codeLoc, ty);
    if (llvmType == nullptr) return NodeVal();
    
    LlvmVal llvmVal;
    llvmVal.type = ty;
    if (symbolTable->inGlobalScope()) {
        llvmVal.ref = makeLlvmGlobal(llvmType, nullptr, typeTable->worksAsTypeCn(ty), getNameForLlvm(id));
    } else {
        llvmVal.ref = makeLlvmAlloca(llvmType, getNameForLlvm(id));
    }

    return NodeVal(codeLoc, llvmVal);
}

NodeVal Codegen::performRegister(CodeLoc codeLoc, NamePool::Id id, const NodeVal &init) {
    NodeVal promo = promoteIfKnownValAndCheckIsLlvmVal(init, true);
    if (promo.isInvalid()) return NodeVal();

    TypeTable::Id ty = promo.getLlvmVal().type;
    llvm::Type *llvmType = getLlvmTypeOrError(promo.getCodeLoc(), ty);
    if (llvmType == nullptr) return NodeVal();

    LlvmVal llvmVal;
    llvmVal.type = ty;
    if (symbolTable->inGlobalScope()) {
        // TODO is the cast to llvm::Constant* always correct?
        llvmVal.ref = makeLlvmGlobal(llvmType, (llvm::Constant*) promo.getLlvmVal().val, typeTable->worksAsTypeCn(ty), getNameForLlvm(id));
    } else {
        llvmVal.ref = makeLlvmAlloca(llvmType, getNameForLlvm(id));
        llvmBuilder.CreateStore(promo.getLlvmVal().val, llvmVal.ref);
    }

    return NodeVal(codeLoc, llvmVal);
}

NodeVal Codegen::performCast(CodeLoc codeLoc, const NodeVal &node, TypeTable::Id ty) {
    if (node.getType().has_value() && node.getType().value() == ty) {
        return node;
    }

    NodeVal promo = promoteIfKnownValAndCheckIsLlvmVal(node, true);
    if (promo.isInvalid()) return NodeVal();

    if (!checkInLocalScope(codeLoc, true)) return NodeVal();

    llvm::Type *llvmType = getLlvmTypeOrError(codeLoc, ty);
    if (llvmType == nullptr) return NodeVal();

    llvm::Value *llvmValueCast = makeLlvmCast(promo.getLlvmVal().val, promo.getLlvmVal().type, llvmType, ty);
    if (llvmValueCast == nullptr) {
        msgs->errorExprCannotCast(codeLoc, promo.getLlvmVal().type, ty);
        return NodeVal();
    }

    LlvmVal llvmVal;
    llvmVal.type = ty;
    llvmVal.val = llvmValueCast;
    return NodeVal(codeLoc, llvmVal);
}

NodeVal Codegen::performCall(CodeLoc codeLoc, const FuncValue &func, const std::vector<NodeVal> &args) {
    if (!checkInLocalScope(codeLoc, true)) return NodeVal();

    vector<llvm::Value*> llvmArgValues(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i].isKnownVal()) {
            NodeVal llvmArg = promoteKnownVal(args[i]);
            if (llvmArg.isInvalid()) return NodeVal();
            llvmArgValues[i] = llvmArg.getLlvmVal().val;
        } else if (args[i].isLlvmVal()) {
            llvmArgValues[i] = args[i].getLlvmVal().val;
        } else {
            msgs->errorUnknown(args[i].getCodeLoc());
            return NodeVal();
        }
    }

    if (func.hasRet()) {
        LlvmVal retLlvmVal;
        retLlvmVal.type = func.retType.value();
        retLlvmVal.val = llvmBuilder.CreateCall(func.func, llvmArgValues, "call_tmp");
        return NodeVal(codeLoc, retLlvmVal);
    } else {
        llvmBuilder.CreateCall(func.func, llvmArgValues, "");
        return NodeVal(codeLoc);
    }
}

bool Codegen::performFunctionDeclaration(CodeLoc codeLoc, FuncValue &func) {
    vector<llvm::Type*> llvmArgTypes(func.argCnt());
    for (size_t i = 0; i < func.argCnt(); ++i) {
        llvmArgTypes[i] = getLlvmTypeOrError(codeLoc, func.argTypes[i]);
        if (llvmArgTypes[i] == nullptr) return false;
    }
    
    llvm::Type *llvmRetType = func.retType.has_value() ? getLlvmTypeOrError(codeLoc, func.retType.value()) : llvm::Type::getVoidTy(llvmContext);
    if (llvmRetType == nullptr) return false;

    llvm::FunctionType *llvmFuncType = llvm::FunctionType::get(llvmRetType, llvmArgTypes, func.variadic);

    func.func = llvm::Function::Create(llvmFuncType, llvm::Function::ExternalLinkage, getNameForLlvm(func.name), llvmModule.get());

    return true;
}

bool Codegen::performFunctionDefinition(const NodeVal &args, const NodeVal &body, FuncValue &func) {
    BlockControl blockCtrl(*symbolTable, func);

    llvmBuilderAlloca.SetInsertPoint(llvm::BasicBlock::Create(llvmContext, "alloca", func.func));

    llvm::BasicBlock *llvmBlockBody = llvm::BasicBlock::Create(llvmContext, "entry", func.func);
    llvmBuilder.SetInsertPoint(llvmBlockBody);

    size_t i = 0;
    for (auto &llvmFuncArg : func.func->args()) {
        llvm::Type *llvmArgType = getLlvmTypeOrError(args.getChild(i).getCodeLoc(), func.argTypes[i]);
        if (llvmArgType == nullptr) return false;
        
        llvm::AllocaInst *llvmAlloca = makeLlvmAlloca(llvmArgType, getNameForLlvm(func.argNames[i]));
        llvmBuilder.CreateStore(&llvmFuncArg, llvmAlloca);

        LlvmVal varLlvmVal;
        varLlvmVal.type = func.argTypes[i];
        varLlvmVal.ref = llvmAlloca;
        NodeVal varNodeVal(args.getChild(i).getCodeLoc(), varLlvmVal);
        symbolTable->addVar(func.argNames[i], move(varNodeVal));

        ++i;
    }

    if (!processChildNodes(body)) {
        func.func->eraseFromParent();
        return false;
    }

    llvmBuilderAlloca.CreateBr(llvmBlockBody);

    if (!func.hasRet() && !isLlvmBlockTerminated())
        llvmBuilder.CreateRetVoid();

    if (llvm::verifyFunction(*func.func, &llvm::errs())) cerr << endl;
    llvmFpm->run(*func.func);

    return true;
}

bool Codegen::performRet(CodeLoc codeLoc) {
    llvmBuilder.CreateRetVoid();
    return true;
}

bool Codegen::performRet(CodeLoc codeLoc, const NodeVal &node) {
    NodeVal promo = promoteIfKnownValAndCheckIsLlvmVal(node, true);
    if (promo.isInvalid()) return false;

    llvmBuilder.CreateRet(promo.getLlvmVal().val);
    return true;
}

NodeVal Codegen::performEvaluation(const NodeVal &node) {
    return evaluator->processNode(node);
}

NodeVal Codegen::performTuple(CodeLoc codeLoc, TypeTable::Id ty, const std::vector<NodeVal> &membs) {
    if (!checkInLocalScope(codeLoc, true)) return NodeVal();

    llvm::StructType *llvmTupType = (llvm::StructType*) getLlvmTypeOrError(codeLoc, ty);
    if (llvmTupType == nullptr) return NodeVal();

    vector<llvm::Value*> llvmMembVals;
    llvmMembVals.reserve(membs.size());
    for (const NodeVal &memb : membs) {
        NodeVal promo = promoteIfKnownValAndCheckIsLlvmVal(memb, true);
        if (promo.isInvalid()) return NodeVal();
        llvmMembVals.push_back(promo.getLlvmVal().val);
    }

    llvm::Value *llvmTupRef = makeLlvmAlloca(llvmTupType, "tup");
    for (size_t i = 0; i < llvmMembVals.size(); ++i) {
        llvmBuilder.CreateStore(llvmMembVals[i], llvmBuilder.CreateStructGEP(llvmTupRef, i));
    }
    llvm::Value *llvmTupVal = llvmBuilder.CreateLoad(llvmTupRef, "tmp_tup");

    LlvmVal llvmVal;
    llvmVal.type = ty;
    llvmVal.val = llvmTupVal;
    return NodeVal(codeLoc, llvmVal);
}

NodeVal Codegen::promoteKnownVal(const NodeVal &node) {
    const KnownVal &known = node.getKnownVal();
    if (!node.getKnownVal().type.has_value()) {
        msgs->errorExprCannotPromote(node.getCodeLoc());
        return NodeVal();
    }
    TypeTable::Id ty = node.getKnownVal().type.value();

    llvm::Constant *llvmConst = nullptr;
    if (KnownVal::isI(known, typeTable)) {
        llvmConst = llvm::ConstantInt::get(getLlvmType(ty), KnownVal::getValueI(known, typeTable).value(), true);
    } else if (KnownVal::isU(known, typeTable)) {
        llvmConst = llvm::ConstantInt::get(getLlvmType(ty), KnownVal::getValueU(known, typeTable).value(), false);
    } else if (KnownVal::isF(known, typeTable)) {
        llvmConst = llvm::ConstantFP::get(getLlvmType(ty), KnownVal::getValueF(known, typeTable).value());
    } else if (KnownVal::isC(known, typeTable)) {
        llvmConst = llvm::ConstantInt::get(getLlvmType(ty), (uint8_t) known.c8, false);
    } else if (KnownVal::isB(known, typeTable)) {
        llvmConst = getLlvmConstB(known.b);
    } else if (KnownVal::isNull(known, typeTable)) {
        llvmConst = llvm::ConstantPointerNull::get((llvm::PointerType*)getLlvmType(ty));
    } else if (KnownVal::isStr(known, typeTable)) {
        const std::string &str = stringPool->get(known.str.value());
        llvmConst = getLlvmConstString(str);
    } else if (KnownVal::isArr(known, typeTable)) {
        // TODO!
    } else if (KnownVal::isTuple(known, typeTable)) {
        // TODO!
    }

    if (llvmConst == nullptr) {
        msgs->errorExprCannotPromote(node.getCodeLoc());
        return NodeVal();
    }

    LlvmVal llvmVal;
    llvmVal.type = ty;
    llvmVal.val = llvmConst;

    return NodeVal(node.getCodeLoc(), llvmVal);
}

NodeVal Codegen::promoteIfKnownValAndCheckIsLlvmVal(const NodeVal &node, bool orError) {
    NodeVal promo = node.isKnownVal() ? promoteKnownVal(node) : node;
    if (promo.isInvalid()) return NodeVal();
    if (!checkIsLlvmVal(promo, orError)) return NodeVal();
    return promo;
}

bool Codegen::checkIsLlvmVal(const NodeVal &node, bool orError) {
    if (!node.isLlvmVal()) {
        if (orError) msgs->errorUnknown(node.getCodeLoc());
        return false;
    }
    return true;
}

string Codegen::getNameForLlvm(NamePool::Id name) const {
    // LLVM is smart enough to put quotes around IDs with special chars, but let's keep this method in anyway.
    return namePool->get(name);
}

bool Codegen::isLlvmBlockTerminated() const {
    return !llvmBuilder.GetInsertBlock()->empty() && llvmBuilder.GetInsertBlock()->back().isTerminator();
}

llvm::Constant* Codegen::getLlvmConstB(bool val) {
    if (val) return llvm::ConstantInt::getTrue(llvmContext);
    else return llvm::ConstantInt::getFalse(llvmContext);
}

llvm::Constant* Codegen::getLlvmConstString(const std::string &str) {
    llvm::GlobalVariable *glob = new llvm::GlobalVariable(
        *llvmModule,
        getLlvmType(typeTable->getTypeCharArrOfLenId(LiteralVal::getStringLen(str))),
        true,
        llvm::GlobalValue::PrivateLinkage,
        nullptr,
        "str_lit"
    );

    llvm::Constant *arr = llvm::ConstantDataArray::getString(llvmContext, str, true);
    glob->setInitializer(arr);
    return llvm::ConstantExpr::getPointerCast(glob, getLlvmType(typeTable->getTypeIdStr()));
}

llvm::Type* Codegen::getLlvmType(TypeTable::Id typeId) {
    llvm::Type *llvmType = typeTable->getType(typeId);
    if (llvmType != nullptr) return llvmType;

    if (typeTable->isTypeDescr(typeId)) {
        const TypeTable::TypeDescr &descr = typeTable->getTypeDescr(typeId);

        llvmType = getLlvmType(descr.base);
        if (llvmType == nullptr) return nullptr;

        for (const TypeTable::TypeDescr::Decor &decor : descr.decors) {
            switch (decor.type) {
            case TypeTable::TypeDescr::Decor::D_PTR:
            case TypeTable::TypeDescr::Decor::D_ARR_PTR:
                llvmType = llvm::PointerType::get(llvmType, 0);
                break;
            case TypeTable::TypeDescr::Decor::D_ARR:
                llvmType = llvm::ArrayType::get(llvmType, decor.len);
                break;
            default:
                return nullptr;
            }
        }
    } else if (typeTable->isTuple(typeId)) {
        const TypeTable::Tuple &tup = typeTable->getTuple(typeId);

        llvm::StructType *structType = llvm::StructType::create(llvmContext, "tuple");

        vector<llvm::Type*> memberTypes(tup.members.size());
        for (size_t i = 0; i < tup.members.size(); ++i) {
            llvm::Type *memberType = getLlvmType(tup.members[i]);
            if (memberType == nullptr) return nullptr;
            memberTypes[i] = memberType;
        }

        structType->setBody(memberTypes);

        llvmType = structType;
    }

    // supported primitive type are codegened at the start of compilation
    // in case of id or type, nullptr is returned    
    typeTable->setType(typeId, llvmType);
    return llvmType;
}

llvm::Type* Codegen::getLlvmTypeOrError(CodeLoc codeLoc, TypeTable::Id typeId) {
    llvm::Type *ret = getLlvmType(typeId);
    if (ret == nullptr) {
        msgs->errorUnknown(codeLoc);
    }
    return ret;
}

llvm::GlobalValue* Codegen::makeLlvmGlobal(llvm::Type *type, llvm::Constant *init, bool isConstant, const std::string &name) {
    if (init == nullptr) init = llvm::Constant::getNullValue(type);

    return new llvm::GlobalVariable(
        *llvmModule,
        type,
        isConstant,
        llvm::GlobalValue::PrivateLinkage,
        init,
        name);
}

llvm::AllocaInst* Codegen::makeLlvmAlloca(llvm::Type *type, const std::string &name) {
    return llvmBuilderAlloca.CreateAlloca(type, nullptr, name);
}

llvm::Value* Codegen::makeLlvmCast(llvm::Value *srcLlvmVal, TypeTable::Id srcTypeId, llvm::Type *dstLlvmType, TypeTable::Id dstTypeId) {
    llvm::Value *dstLlvmVal = nullptr;

    if (typeTable->worksAsTypeI(srcTypeId)) {
        if (typeTable->worksAsTypeI(dstTypeId)) {
            dstLlvmVal = llvmBuilder.CreateIntCast(srcLlvmVal, dstLlvmType, true, "i2i_cast");
        } else if (typeTable->worksAsTypeU(dstTypeId)) {
            dstLlvmVal = llvmBuilder.CreateIntCast(srcLlvmVal, dstLlvmType, false, "i2u_cast");
        } else if (typeTable->worksAsTypeF(dstTypeId)) {
            dstLlvmVal = llvmBuilder.Insert(llvm::CastInst::Create(llvm::Instruction::SIToFP, srcLlvmVal, dstLlvmType, "i2f_cast"));
        } else if (typeTable->worksAsTypeC(dstTypeId)) {
            dstLlvmVal = llvmBuilder.CreateIntCast(srcLlvmVal, dstLlvmType, false, "i2c_cast");
        } else if (typeTable->worksAsTypeB(dstTypeId)) {
            llvm::Value *zero = llvm::Constant::getNullValue(srcLlvmVal->getType());
            dstLlvmVal = llvmBuilder.CreateICmpNE(srcLlvmVal, zero, "i2b_cast");
        } else if (typeTable->worksAsTypeAnyP(dstTypeId)) {
            dstLlvmVal = llvmBuilder.CreateBitOrPointerCast(srcLlvmVal, dstLlvmType, "i2p_cast");
        }
    } else if (typeTable->worksAsTypeU(srcTypeId)) {
        if (typeTable->worksAsTypeI(dstTypeId)) {
            dstLlvmVal = llvmBuilder.CreateIntCast(srcLlvmVal, dstLlvmType, true, "u2i_cast");
        } else if (typeTable->worksAsTypeU(dstTypeId)) {
            dstLlvmVal = llvmBuilder.CreateIntCast(srcLlvmVal, dstLlvmType, false, "u2u_cast");
        } else if (typeTable->worksAsTypeF(dstTypeId)) {
            dstLlvmVal = llvmBuilder.Insert(llvm::CastInst::Create(llvm::Instruction::UIToFP, srcLlvmVal, dstLlvmType, "u2f_cast"));
        } else if (typeTable->worksAsTypeC(dstTypeId)) {
            dstLlvmVal = llvmBuilder.CreateIntCast(srcLlvmVal, dstLlvmType, false, "u2c_cast");
        } else if (typeTable->worksAsTypeB(dstTypeId)) {
            llvm::Value *zero = llvm::Constant::getNullValue(srcLlvmVal->getType());
            dstLlvmVal = llvmBuilder.CreateICmpNE(srcLlvmVal, zero, "u2b_cast");
        } else if (typeTable->worksAsTypeAnyP(dstTypeId)) {
            dstLlvmVal = llvmBuilder.CreateBitOrPointerCast(srcLlvmVal, dstLlvmType, "u2p_cast");
        }
    } else if (typeTable->worksAsTypeF(srcTypeId)) {
        if (typeTable->worksAsTypeI(dstTypeId)) {
            dstLlvmVal = llvmBuilder.Insert(llvm::CastInst::Create(llvm::Instruction::FPToSI, srcLlvmVal, dstLlvmType, "f2i_cast"));
        } else if (typeTable->worksAsTypeU(dstTypeId)) {
            dstLlvmVal = llvmBuilder.Insert(llvm::CastInst::Create(llvm::Instruction::FPToUI, srcLlvmVal, dstLlvmType, "f2u_cast"));
        } else if (typeTable->worksAsTypeF(dstTypeId)) {
            dstLlvmVal = llvmBuilder.CreateFPCast(srcLlvmVal, dstLlvmType, "f2f_cast");
        }
    } else if (typeTable->worksAsTypeC(srcTypeId)) {
        if (typeTable->worksAsTypeI(dstTypeId)) {
            dstLlvmVal = llvmBuilder.CreateIntCast(srcLlvmVal, dstLlvmType, true, "c2i_cast");
        } else if (typeTable->worksAsTypeU(dstTypeId)) {
            dstLlvmVal = llvmBuilder.CreateIntCast(srcLlvmVal, dstLlvmType, false, "c2u_cast");
        } else if (typeTable->worksAsTypeC(dstTypeId)) {
            dstLlvmVal = llvmBuilder.CreateIntCast(srcLlvmVal, dstLlvmType, false, "c2c_cast");
        } else if (typeTable->worksAsTypeB(dstTypeId)) {
            llvm::Value *zero = llvm::Constant::getNullValue(srcLlvmVal->getType());
            dstLlvmVal = llvmBuilder.CreateICmpNE(srcLlvmVal, zero, "c2b_cast");
        }
    } else if (typeTable->worksAsTypeB(srcTypeId)) {
        if (typeTable->worksAsTypeI(dstTypeId)) {
            dstLlvmVal = llvmBuilder.CreateIntCast(srcLlvmVal, dstLlvmType, false, "b2i_cast");
        } else if (typeTable->worksAsTypeU(dstTypeId)) {
            dstLlvmVal = llvmBuilder.CreateIntCast(srcLlvmVal, dstLlvmType, false, "b2u_cast");
        }
    } else if (typeTable->worksAsTypeAnyP(srcTypeId)) {
        if (typeTable->worksAsTypeI(dstTypeId)) {
            dstLlvmVal = llvmBuilder.CreatePtrToInt(srcLlvmVal, dstLlvmType, "p2i_cast");
        } else if (typeTable->worksAsTypeU(dstTypeId)) {
            dstLlvmVal = llvmBuilder.CreatePtrToInt(srcLlvmVal, dstLlvmType, "p2u_cast");
        } else if (typeTable->worksAsTypeAnyP(dstTypeId)) {
            dstLlvmVal = llvmBuilder.CreatePointerCast(srcLlvmVal, dstLlvmType, "p2p_cast");
        } else if (typeTable->worksAsTypeB(dstTypeId)) {
            dstLlvmVal = llvmBuilder.CreateICmpNE(
                llvmBuilder.CreatePtrToInt(srcLlvmVal, getLlvmType(typeTable->getPrimTypeId(TypeTable::WIDEST_I))),
                llvm::ConstantInt::getNullValue(getLlvmType(typeTable->getPrimTypeId(TypeTable::WIDEST_I))),
                "p2b_cast");
        }
    } else if (typeTable->worksAsTypeArr(srcTypeId) || typeTable->worksAsTuple(srcTypeId)) {
        // tuples and arrs are only castable when changing constness
        if (typeTable->isImplicitCastable(srcTypeId, dstTypeId)) {
            // no action is needed in case of a cast
            dstLlvmVal = srcLlvmVal;
        }
    }

    return dstLlvmVal;
}