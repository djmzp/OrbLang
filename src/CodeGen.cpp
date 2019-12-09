// TODO!!! write tests for recent changes
// TODO split this file in two, VSCode is glitching on me
#include "Codegen.h"
#include "Parser.h"
#include <limits>
#include <cmath>
#include "llvm/ADT/APInt.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
using namespace std;

CodeGen::CodeGen(NamePool *namePool, SymbolTable *symbolTable) : namePool(namePool), symbolTable(symbolTable), 
        llvmBuilder(llvmContext), llvmBuilderAlloca(llvmContext), panic(false) {
    llvmModule = std::make_unique<llvm::Module>(llvm::StringRef("test"), llvmContext);
}

bool CodeGen::valueBroken(const ExprGenPayload &e) {
    if (e.val == nullptr && !e.isLitVal()) panic = true;
    return panic;
}

bool CodeGen::valBroken(const ExprGenPayload &e) {
    if (e.val == nullptr) panic = true;
    return panic;
}

bool CodeGen::refBroken(const ExprGenPayload &e) {
    if (e.ref == nullptr) panic = true;
    return panic;
}

bool CodeGen::promoteLiteral(ExprGenPayload &e, TypeTable::Id t) {
    if (!e.isLitVal()) {
        panic = true;
        return false;
    }

    switch (e.litVal.type) {
    case LiteralVal::T_BOOL:
        if (t != TypeTable::P_BOOL) {
            panic = true;
        } else {
            e.val = getConstB(e.litVal.val_b);
        }
        break;
    case LiteralVal::T_SINT:
        if ((!TypeTable::isTypeI(t) && !TypeTable::isTypeU(t)) || !TypeTable::fitsType(e.litVal.val_si, t)) {
            panic = true;
        } else {
            e.val = llvm::ConstantInt::get(symbolTable->getTypeTable()->getType(t), e.litVal.val_si, TypeTable::isTypeI(t));
        }
        break;
    case LiteralVal::T_FLOAT:
        // no precision checks for float types, this makes float literals somewhat unsafe
        if (!TypeTable::isTypeF(t)) {
            panic = true;
        } else {
            e.val = llvm::ConstantFP::get(symbolTable->getTypeTable()->getType(t), e.litVal.val_f);
        }
        break;
    default:
        panic = true;
    }

    e.resetLitVal();
    e.type = t;
    return !panic;
}

llvm::Value* CodeGen::getConstB(bool val) {
    if (val) return llvm::ConstantInt::getTrue(llvmContext);
    else return llvm::ConstantInt::getFalse(llvmContext);
}

llvm::Type* CodeGen::genPrimTypeBool() {
    return llvm::IntegerType::get(llvmContext, 1);
}

llvm::Type* CodeGen::genPrimTypeI(unsigned bits) {
    return llvm::IntegerType::get(llvmContext, bits);
}

llvm::Type* CodeGen::genPrimTypeU(unsigned bits) {
    // LLVM makes no distinction between signed and unsigned int
    return llvm::IntegerType::get(llvmContext, bits);
}

llvm::Type* CodeGen::genPrimTypeF16() {
    return llvm::Type::getHalfTy(llvmContext);
}

llvm::Type* CodeGen::genPrimTypeF32() {
    return llvm::Type::getFloatTy(llvmContext);
}

llvm::Type* CodeGen::genPrimTypeF64() {
    return llvm::Type::getDoubleTy(llvmContext);
}

llvm::AllocaInst* CodeGen::createAlloca(llvm::Type *type, const string &name) {
    return llvmBuilderAlloca.CreateAlloca(type, 0, name);
}

bool CodeGen::isBlockTerminated() const {
    return !llvmBuilder.GetInsertBlock()->empty() && llvmBuilder.GetInsertBlock()->back().isTerminator();
}

llvm::GlobalValue* CodeGen::createGlobal(llvm::Type *type, const std::string &name) {
    return new llvm::GlobalVariable(
        *llvmModule,
        type,
        false,
        llvm::GlobalValue::CommonLinkage,
        // llvm demands global vars be initialized, but by deafult we don't init them
        // TODO this is very hacky
        llvm::ConstantInt::get(type, 0),
        name);
}

void CodeGen::createCast(llvm::Value *&val, TypeTable::Id srcTypeId, llvm::Type *type, TypeTable::Id dstTypeId) {
    if (srcTypeId == dstTypeId) return;

    if (val == nullptr || type == nullptr) {
        panic = true;
        return;
    }

    if (TypeTable::isTypeI(srcTypeId)) {
        if (TypeTable::isTypeI(dstTypeId))
            val = llvmBuilder.CreateIntCast(val, type, true, "i2i_cast");
        else if (TypeTable::isTypeU(dstTypeId))
            val = llvmBuilder.CreateIntCast(val, type, false, "i2u_cast");
        else if (TypeTable::isTypeF(dstTypeId))
            val = llvmBuilder.Insert(llvm::CastInst::Create(llvm::Instruction::SIToFP, val, type, "i2f_cast"));
        else if (dstTypeId == TypeTable::P_BOOL) {
            llvm::Value *z = llvmBuilder.CreateIntCast(getConstB(false), val->getType(), true);
            val = llvmBuilder.CreateICmpNE(val, z, "i2b_cast");
        } else {
            panic = true;
            val = nullptr;
        }
    } else if (TypeTable::isTypeU(srcTypeId)) {
        if (TypeTable::isTypeI(dstTypeId))
            val = llvmBuilder.CreateIntCast(val, type, true, "u2i_cast");
        else if (TypeTable::isTypeU(dstTypeId))
            val = llvmBuilder.CreateIntCast(val, type, false, "u2u_cast");
        else if (TypeTable::isTypeF(dstTypeId))
            val = llvmBuilder.Insert(llvm::CastInst::Create(llvm::Instruction::UIToFP, val, type, "u2f_cast"));
        else if (dstTypeId == TypeTable::P_BOOL) {
            llvm::Value *z = llvmBuilder.CreateIntCast(getConstB(false), val->getType(), false);
            val = llvmBuilder.CreateICmpNE(val, z, "i2b_cast");
        } else {
            panic = true;
            val = nullptr;
        }
    } else if (TypeTable::isTypeF(srcTypeId)) {
        if (TypeTable::isTypeI(dstTypeId))
            val = llvmBuilder.Insert(llvm::CastInst::Create(llvm::Instruction::FPToSI, val, type, "f2i_cast"));
        else if (TypeTable::isTypeU(dstTypeId))
            val = llvmBuilder.Insert(llvm::CastInst::Create(llvm::Instruction::FPToUI, val, type, "f2u_cast"));
        else if (TypeTable::isTypeF(dstTypeId))
            val = llvmBuilder.CreateFPCast(val, type, "f2f_cast");
        else {
            panic = true;
            val = nullptr;
        }
    } else if (srcTypeId == TypeTable::P_BOOL) {
        if (TypeTable::isTypeI(dstTypeId))
            val = llvmBuilder.CreateIntCast(val, type, false, "b2i_cast");
        else if (TypeTable::isTypeU(dstTypeId))
            val = llvmBuilder.CreateIntCast(val, type, false, "b2u_cast");
        else {
            panic = true;
            val = nullptr;
        }
    } else {
        panic = true;
        val = nullptr;
    }
}

void CodeGen::createCast(llvm::Value *&val, TypeTable::Id srcTypeId, TypeTable::Id dstTypeId) {
    createCast(val, srcTypeId, symbolTable->getTypeTable()->getType(dstTypeId), dstTypeId);
}

CodeGen::ExprGenPayload CodeGen::codegenExpr(const ExprAST *ast) {
    switch (ast->type()) {
    case AST_LiteralExpr:
        return codegen((const LiteralExprAST*)ast);
    case AST_VarExpr:
        return codegen((const VarExprAST*)ast);
    case AST_UnExpr:
        return codegen((const UnExprAST*)ast);
    case AST_BinExpr:
        return codegen((const BinExprAST*)ast);
    case AST_TernCondExpr:
        return codegen((const TernCondExprAST*)ast);
    case AST_CallExpr:
        return codegen((const CallExprAST*)ast);
    case AST_CastExpr:
        return codegen((const CastExprAST*)ast);
    default:
        panic = true;
        return {};
    }
}

void CodeGen::codegenNode(const BaseAST *ast, bool blockMakeScope) {
    switch (ast->type()) {
    case AST_NullExpr:
        return;
    case AST_Decl:
        codegen((const DeclAST*)ast);
        return;
    case AST_If:
        codegen((const IfAST*)ast);
        return;
    case AST_For:
        codegen((const ForAST*) ast);
        return;
    case AST_While:
        codegen((const WhileAST*) ast);
        return;
    case AST_DoWhile:
        codegen((const DoWhileAST*) ast);
        return;
    case AST_Ret:
        codegen((const RetAST*)ast);
        return;
    case AST_Block:
        codegen((const BlockAST*)ast, blockMakeScope);
        return;
    case AST_FuncProto:
        codegen((const FuncProtoAST*)ast, false);
        return;
    case AST_Func:
        codegen((const FuncAST*)ast);
        return;
    default:
        codegenExpr((const ExprAST*)ast);
    }
}

CodeGen::ExprGenPayload CodeGen::codegen(const LiteralExprAST *ast) {
    if (ast->getVal().type == LiteralVal::T_NONE) {
        panic = true;
        return {};
    }

    return { .litVal = ast->getVal() };
}

CodeGen::ExprGenPayload CodeGen::codegen(const VarExprAST *ast) {
    const SymbolTable::VarPayload *var = symbolTable->getVar(ast->getNameId());
    if (broken(var)) return {};
    return {var->type, llvmBuilder.CreateLoad(var->val, namePool->get(ast->getNameId())), var->val};
}

CodeGen::ExprGenPayload CodeGen::codegen(const UnExprAST *ast) {
    ExprGenPayload exprPay = codegenExpr(ast->getExpr());
    if (valueBroken(exprPay)) return {};

    if (exprPay.isLitVal()) return codegenLiteralUn(ast->getOp(), exprPay.litVal);

    ExprGenPayload exprRet;
    exprRet.type = exprPay.type;
    if (ast->getOp() == Token::O_ADD) {
        if (!(TypeTable::isTypeI(exprPay.type) || TypeTable::isTypeU(exprPay.type) || TypeTable::isTypeF(exprPay.type))) {
            panic = true;
            return {};
        }
        exprRet.val = exprPay.val;
    } else if (ast->getOp() == Token::O_SUB) {
        if (TypeTable::isTypeI(exprPay.type)) {
            exprRet.val = llvmBuilder.CreateNeg(exprPay.val, "sneg_tmp");
        } else if (TypeTable::isTypeF(exprPay.type)) {
            exprRet.val = llvmBuilder.CreateFNeg(exprPay.val, "fneg_tmp");
        } else {
            panic = true;
            return {};
        }
    } else if (ast->getOp() == Token::O_INC) {
        if (!(TypeTable::isTypeI(exprPay.type) || TypeTable::isTypeU(exprPay.type)) || refBroken(exprPay)) {
            panic = true;
            return {};
        }
        exprRet.val = llvmBuilder.CreateAdd(exprPay.val, llvm::ConstantInt::get(symbolTable->getTypeTable()->getType(exprPay.type), 1), "inc_tmp");
        exprRet.ref = exprPay.ref;
        llvmBuilder.CreateStore(exprRet.val, exprRet.ref);
    } else if (ast->getOp() == Token::O_DEC) {
        if (!(TypeTable::isTypeI(exprPay.type) || TypeTable::isTypeU(exprPay.type)) || refBroken(exprPay)) {
            panic = true;
            return {};
        }
        exprRet.val = llvmBuilder.CreateSub(exprPay.val, llvm::ConstantInt::get(symbolTable->getTypeTable()->getType(exprPay.type), 1), "dec_tmp");
        exprRet.ref = exprPay.ref;
        llvmBuilder.CreateStore(exprRet.val, exprRet.ref);
    } else if (ast->getOp() == Token::O_BIT_NOT) {
        if (!(TypeTable::isTypeI(exprPay.type) || TypeTable::isTypeU(exprPay.type))) {
            panic = true;
            return {};
        }
        exprRet.val = llvmBuilder.CreateNot(exprPay.val, "bit_not_tmp");
    } else if (ast->getOp() == Token::O_NOT) {
        if (exprPay.type != TypeTable::P_BOOL) {
            panic = true;
            return {};
        }
        exprRet.val = llvmBuilder.CreateNot(exprPay.val, "not_tmp");
    } else {
        panic = true;
        return {};
    }
    return exprRet;
}

CodeGen::ExprGenPayload CodeGen::codegenLiteralUn(Token::Oper op, LiteralVal lit) {
    ExprGenPayload exprRet;
    exprRet.litVal.type = lit.type;
    if (op == Token::O_ADD) {
        if (lit.type != LiteralVal::T_SINT && lit.type != LiteralVal::T_FLOAT) {
            panic = true;
            return {};
        }
        exprRet.litVal = lit;
    } else if (op == Token::O_SUB) {
        if (lit.type == LiteralVal::T_SINT) {
            exprRet.litVal.val_si = -lit.val_si;
        } else if (lit.type == LiteralVal::T_FLOAT) {
            exprRet.litVal.val_f = -lit.val_f;
        } else {
            panic = true;
            return {};
        }
    } else if (op == Token::O_BIT_NOT) {
        if (lit.type == LiteralVal::T_SINT) {
            exprRet.litVal.val_si = ~lit.val_si;
        } else {
            panic = true;
            return {};
        }
    } else if (op == Token::O_NOT) {
        if (lit.type == LiteralVal::T_BOOL) {
            exprRet.litVal.val_b = !lit.val_b;
        } else {
            panic = true;
            return {};
        }
    } else {
        panic = true;
        return {};
    }
    return exprRet;
}

CodeGen::ExprGenPayload CodeGen::codegen(const BinExprAST *ast) {
    if (ast->getOp() == Token::O_AND || ast->getOp() == Token::O_OR) {
        return codegenLogicShortCircuit(ast);
    }

    ExprGenPayload exprPayL, exprPayR, exprPayRet;

    bool assignment = operInfos.at(ast->getOp()).assignment;

    exprPayL = codegenExpr(ast->getL());
    if (assignment) {
        if (refBroken(exprPayL)) return {};
    } else {
        if (valueBroken(exprPayL)) return {};
    }

    exprPayR = codegenExpr(ast->getR());
    if (valueBroken(exprPayR)) return {};

    if (exprPayL.isLitVal() && !exprPayR.isLitVal()) {
        if (!promoteLiteral(exprPayL, exprPayR.type)) return {};
    } else if (!exprPayL.isLitVal() && exprPayR.isLitVal()) {
        if (!promoteLiteral(exprPayR, exprPayL.type)) return {};
    } else if (exprPayL.isLitVal() && exprPayR.isLitVal()) {
        return codegenLiteralBin(ast->getOp(), exprPayL.litVal, exprPayR.litVal);
    }

    llvm::Value *valL = exprPayL.val, *valR = exprPayR.val;
    exprPayRet.type = exprPayL.type;
    exprPayRet.val = nullptr;
    exprPayRet.ref = assignment ? exprPayL.ref : nullptr;

    if (exprPayL.type != exprPayR.type) {
        if (TypeTable::isImplicitCastable(exprPayR.type, exprPayL.type)) {
            createCast(valR, exprPayR.type, exprPayL.type);
            exprPayRet.type = exprPayL.type;
        } else if (TypeTable::isImplicitCastable(exprPayL.type, exprPayR.type) && !assignment) {
            createCast(valL, exprPayL.type, exprPayR.type);
            exprPayRet.type = exprPayR.type;
        } else {
            panic = true;
            return {};
        }
    }

    if (exprPayRet.type == TypeTable::P_BOOL) {
        switch (ast->getOp()) {
        case Token::O_ASGN:
            exprPayRet.val = valR;
            break;
        case Token::O_EQ:
            exprPayRet.val = llvmBuilder.CreateICmpEQ(valL, valR, "bcmp_eq_tmp");
            break;
        case Token::O_NEQ:
            exprPayRet.val = llvmBuilder.CreateICmpNE(valL, valR, "bcmp_neq_tmp");
            break;
        default:
            break;
        }
    } else {
        bool isTypeI = TypeTable::isTypeI(exprPayRet.type);
        bool isTypeU = TypeTable::isTypeU(exprPayRet.type);
        bool isTypeF = TypeTable::isTypeF(exprPayRet.type);

        switch (ast->getOp()) {
            case Token::O_ASGN:
                exprPayRet.val = valR;
                break;
            case Token::O_ADD:
            case Token::O_ADD_ASGN:
                if (isTypeF)
                    exprPayRet.val = llvmBuilder.CreateFAdd(valL, valR, "fadd_tmp");
                else if (isTypeI || isTypeU)
                    exprPayRet.val = llvmBuilder.CreateAdd(valL, valR, "add_tmp");
                break;
            case Token::O_SUB:
            case Token::O_SUB_ASGN:
                if (isTypeF)
                    exprPayRet.val = llvmBuilder.CreateFSub(valL, valR, "fsub_tmp");
                else if (isTypeI || isTypeU)
                    exprPayRet.val = llvmBuilder.CreateSub(valL, valR, "sub_tmp");
                break;
            case Token::O_SHL:
            case Token::O_SHL_ASGN:
                if (isTypeI || isTypeU)
                    exprPayRet.val = llvmBuilder.CreateShl(valL, valR, "shl_tmp");
                break;
            case Token::O_SHR:
            case Token::O_SHR_ASGN:
                if (isTypeI)
                    exprPayRet.val = llvmBuilder.CreateAShr(valL, valR, "ashr_tmp");
                else if (isTypeU)
                    exprPayRet.val = llvmBuilder.CreateLShr(valL, valR, "lshr_tmp");
                break;
            case Token::O_BIT_AND:
            case Token::O_BIT_AND_ASGN:
                if (isTypeI || isTypeU)
                    exprPayRet.val = llvmBuilder.CreateAnd(valL, valR, "and_tmp");
                break;
            case Token::O_BIT_XOR:
            case Token::O_BIT_XOR_ASGN:
                if (isTypeI || isTypeU)
                    exprPayRet.val = llvmBuilder.CreateXor(valL, valR, "xor_tmp");
                break;
            case Token::O_BIT_OR:
            case Token::O_BIT_OR_ASGN:
                if (isTypeI || isTypeU)
                    exprPayRet.val = llvmBuilder.CreateOr(valL, valR, "or_tmp");
                break;
            case Token::O_MUL:
            case Token::O_MUL_ASGN:
                if (isTypeF)
                    exprPayRet.val = llvmBuilder.CreateFMul(valL, valR, "fmul_tmp");
                else if (isTypeI || isTypeU)
                    exprPayRet.val = llvmBuilder.CreateMul(valL, valR, "mul_tmp");
                break;
            case Token::O_DIV:
            case Token::O_DIV_ASGN:
                if (isTypeF)
                    exprPayRet.val = llvmBuilder.CreateFDiv(valL, valR, "fdiv_tmp");
                else if (isTypeI)
                    exprPayRet.val = llvmBuilder.CreateSDiv(valL, valR, "sdiv_tmp");
                else if (isTypeU)
                    exprPayRet.val = llvmBuilder.CreateUDiv(valL, valR, "udiv_tmp");
                break;
            case Token::O_REM:
            case Token::O_REM_ASGN:
                if (isTypeI)
                    exprPayRet.val = llvmBuilder.CreateSRem(valL, valR, "srem_tmp");
                else if (isTypeU)
                    exprPayRet.val = llvmBuilder.CreateURem(valL, valR, "urem_tmp");
                else if (isTypeF)
                    exprPayRet.val = llvmBuilder.CreateFRem(valL, valR, "frem_tmp");
                break;
            case Token::O_EQ:
                if (isTypeF)
                    exprPayRet.val = llvmBuilder.CreateFCmpOEQ(valL, valR, "fcmp_eq_tmp");
                else if (isTypeI || isTypeU)
                    exprPayRet.val = llvmBuilder.CreateICmpEQ(valL, valR, "cmp_eq_tmp");
                exprPayRet.type = TypeTable::P_BOOL;
                break;
            case Token::O_NEQ:
                if (isTypeF)
                    exprPayRet.val = llvmBuilder.CreateFCmpONE(valL, valR, "fcmp_neq_tmp");
                else if (isTypeI || isTypeU)
                    exprPayRet.val = llvmBuilder.CreateICmpNE(valL, valR, "cmp_neq_tmp");
                exprPayRet.type = TypeTable::P_BOOL;
                break;
            case Token::O_LT:
                if (isTypeF)
                    exprPayRet.val = llvmBuilder.CreateFCmpOLT(valL, valR, "fcmp_lt_tmp");
                else if (isTypeI)
                    exprPayRet.val = llvmBuilder.CreateICmpSLT(valL, valR, "scmp_lt_tmp");
                else if (isTypeU)
                    exprPayRet.val = llvmBuilder.CreateICmpULT(valL, valR, "ucmp_lt_tmp");
                exprPayRet.type = TypeTable::P_BOOL;
                break;
            case Token::O_LTEQ:
                if (isTypeF)
                    exprPayRet.val = llvmBuilder.CreateFCmpOLE(valL, valR, "fcmp_lteq_tmp");
                else if (isTypeI)
                    exprPayRet.val = llvmBuilder.CreateICmpSLE(valL, valR, "scmp_lteq_tmp");
                else if (isTypeU)
                    exprPayRet.val = llvmBuilder.CreateICmpULE(valL, valR, "ucmp_lteq_tmp");
                exprPayRet.type = TypeTable::P_BOOL;
                break;
            case Token::O_GT:
                if (isTypeF)
                    exprPayRet.val = llvmBuilder.CreateFCmpOGT(valL, valR, "fcmp_gt_tmp");
                else if (isTypeI)
                    exprPayRet.val = llvmBuilder.CreateICmpSGT(valL, valR, "scmp_gt_tmp");
                else if (isTypeU)
                    exprPayRet.val = llvmBuilder.CreateICmpUGT(valL, valR, "ucmp_gt_tmp");
                exprPayRet.type = TypeTable::P_BOOL;
                break;
            case Token::O_GTEQ:
                if (isTypeF)
                    exprPayRet.val = llvmBuilder.CreateFCmpOGE(valL, valR, "fcmp_gteq_tmp");
                else if (isTypeI)
                    exprPayRet.val = llvmBuilder.CreateICmpSGE(valL, valR, "scmp_gteq_tmp");
                else if (isTypeU)
                    exprPayRet.val = llvmBuilder.CreateICmpUGE(valL, valR, "ucmp_gteq_tmp");
                exprPayRet.type = TypeTable::P_BOOL;
                break;
            default:
                panic = true;
                return {};
        }
    }

    if (valueBroken(exprPayRet)) return {};

    if (assignment) {
        llvmBuilder.CreateStore(exprPayRet.val, exprPayRet.ref);
    }

    return exprPayRet;
}

CodeGen::ExprGenPayload CodeGen::codegenLogicShortCircuit(const BinExprAST *ast) {
    ExprGenPayload exprPayL, exprPayR, exprPayRet;

    llvm::Function *func = llvmBuilder.GetInsertBlock()->getParent();

    llvm::BasicBlock *firstBlock = llvm::BasicBlock::Create(llvmContext, "start", func);
    llvm::BasicBlock *otherBlock = llvm::BasicBlock::Create(llvmContext, "other");
    llvm::BasicBlock *afterBlock = llvm::BasicBlock::Create(llvmContext, "after");

    llvmBuilder.CreateBr(firstBlock);

    llvmBuilder.SetInsertPoint(firstBlock);
    exprPayL = codegenExpr(ast->getL());
    if (valueBroken(exprPayL) || !exprPayL.isBool()) {
        panic = true;
        return {};
    }

    if (ast->getOp() == Token::O_AND) {
        if (exprPayL.isLitVal()) {
            llvmBuilder.CreateBr(exprPayL.litVal.val_b ? otherBlock : afterBlock);
        } else {
            llvmBuilder.CreateCondBr(exprPayL.val, otherBlock, afterBlock);
        }
    } else if (ast->getOp() == Token::O_OR) {
        if (exprPayL.isLitVal()) {
            llvmBuilder.CreateBr(exprPayL.litVal.val_b ? afterBlock : otherBlock);
        } else {
            llvmBuilder.CreateCondBr(exprPayL.val, afterBlock, otherBlock);
        }
    } else {
        panic = true;
        return {};
    }
    firstBlock = llvmBuilder.GetInsertBlock();

    func->getBasicBlockList().push_back(otherBlock);
    llvmBuilder.SetInsertPoint(otherBlock);
    exprPayR = codegenExpr(ast->getR());
    if (valueBroken(exprPayR) || !exprPayR.isBool()) {
        panic = true;
        return {};
    }
    llvmBuilder.CreateBr(afterBlock);
    otherBlock = llvmBuilder.GetInsertBlock();

    func->getBasicBlockList().push_back(afterBlock);
    llvmBuilder.SetInsertPoint(afterBlock);
    ExprGenPayload ret;
    if (exprPayL.isLitVal() && exprPayR.isLitVal()) {
        // this cannot be moved to other codegen methods,
        // as we don't know whether exprs are litVals until we call codegenExpr,
        // but calling it emits code to LLVM at that point
        ret.litVal.type = LiteralVal::T_BOOL;
        if (ast->getOp() == Token::O_AND)
            ret.litVal.val_b = exprPayL.litVal.val_b && exprPayR.litVal.val_b;
        else
            ret.litVal.val_b = exprPayL.litVal.val_b || exprPayR.litVal.val_b;
    } else {
        llvm::PHINode *phi = llvmBuilder.CreatePHI(symbolTable->getTypeTable()->getType(TypeTable::P_BOOL), 2, "logic_tmp");

        if (ast->getOp() == Token::O_AND)
            phi->addIncoming(getConstB(false), firstBlock);
        else
            phi->addIncoming(getConstB(true), firstBlock);
        
        if (exprPayR.isLitVal()) {
            phi->addIncoming(getConstB(exprPayR.litVal.val_b), otherBlock);
        } else {
            phi->addIncoming(exprPayR.val, otherBlock);
        }

        ret.type = TypeTable::P_BOOL;
        ret.val = phi;
    }

    return ret;
}

CodeGen::ExprGenPayload CodeGen::codegenLiteralBin(Token::Oper op, LiteralVal litL, LiteralVal litR) {
    if (litL.type != litR.type || litL.type == LiteralVal::T_NONE) {
        panic = true;
        return {};
    }

    LiteralVal litValRet;
    litValRet.type = litL.type;

    if (litValRet.type == LiteralVal::T_BOOL) {
        switch (op) {
        case Token::O_EQ:
            litValRet.val_b = litL.val_b == litR.val_b;
            break;
        case Token::O_NEQ:
            litValRet.val_b = litL.val_b != litR.val_b;
            break;
        // AND and OR handled with the non-litVal cases
        default:
            panic = true;
            break;
        }
    } else {
        bool isTypeI = litValRet.type == LiteralVal::T_SINT;
        bool isTypeF = litValRet.type == LiteralVal::T_FLOAT;

        switch (op) {
            case Token::O_ADD:
                if (isTypeF)
                    litValRet.val_f = litL.val_f+litR.val_f;
                else if (isTypeI)
                    litValRet.val_si = litL.val_si+litR.val_si;
                break;
            case Token::O_SUB:
                if (isTypeF)
                    litValRet.val_f = litL.val_f-litR.val_f;
                else if (isTypeI)
                    litValRet.val_si = litL.val_si-litR.val_si;
                break;
            case Token::O_SHL:
                if (isTypeI)
                    litValRet.val_si = litL.val_si<<litR.val_si;
                else
                    panic = true;
                break;
            case Token::O_SHR:
                if (isTypeI)
                    litValRet.val_si = litL.val_si>>litR.val_si;
                else
                    panic = true;
                break;
            case Token::O_BIT_AND:
                if (isTypeI)
                    litValRet.val_si = litL.val_si&litR.val_si;
                else
                    panic = true;
                break;
            case Token::O_BIT_XOR:
                if (isTypeI)
                    litValRet.val_si = litL.val_si^litR.val_si;
                else
                    panic = true;
                break;
            case Token::O_BIT_OR:
                if (isTypeI)
                    litValRet.val_si = litL.val_si|litR.val_si;
                else
                    panic = true;
                break;
            case Token::O_MUL:
                if (isTypeF)
                    litValRet.val_f = litL.val_f*litR.val_f;
                else if (isTypeI)
                    litValRet.val_si = litL.val_si*litR.val_si;
                break;
            case Token::O_DIV:
                if (isTypeF)
                    litValRet.val_f = litL.val_f/litR.val_f;
                else if (isTypeI)
                    litValRet.val_si = litL.val_si/litR.val_si;
                break;
            case Token::O_REM:
                if (isTypeF)
                    litValRet.val_f = fmod(litL.val_f, litR.val_f);
                else if (isTypeI)
                    litValRet.val_si = litL.val_si%litR.val_si;
                break;
            case Token::O_EQ:
                litValRet.type = LiteralVal::T_BOOL;
                if (isTypeF)
                    litValRet.val_b = litL.val_f == litR.val_f;
                else if (isTypeI)
                    litValRet.val_b = litL.val_si == litR.val_si;
                break;
            case Token::O_NEQ:
                litValRet.type = LiteralVal::T_BOOL;
                if (isTypeF)
                    litValRet.val_b = litL.val_f != litR.val_f;
                else if (isTypeI)
                    litValRet.val_b = litL.val_si != litR.val_si;
                break;
            case Token::O_LT:
                litValRet.type = LiteralVal::T_BOOL;
                if (isTypeF)
                    litValRet.val_b = litL.val_f < litR.val_f;
                else if (isTypeI)
                    litValRet.val_b = litL.val_si < litR.val_si;
                break;
            case Token::O_LTEQ:
                litValRet.type = LiteralVal::T_BOOL;
                if (isTypeF)
                    litValRet.val_b = litL.val_f <= litR.val_f;
                else if (isTypeI)
                    litValRet.val_b = litL.val_si <= litR.val_si;
                break;
            case Token::O_GT:
                litValRet.type = LiteralVal::T_BOOL;
                if (isTypeF)
                    litValRet.val_b = litL.val_f > litR.val_f;
                else if (isTypeI)
                    litValRet.val_b = litL.val_si > litR.val_si;
                break;
            case Token::O_GTEQ:
                litValRet.type = LiteralVal::T_BOOL;
                if (isTypeF)
                    litValRet.val_b = litL.val_f >= litR.val_f;
                else if (isTypeI)
                    litValRet.val_b = litL.val_si >= litR.val_si;
                break;
            default:
                panic = true;
        }
    }

    if (panic) return {};
    return { .litVal = litValRet };
}

CodeGen::ExprGenPayload CodeGen::codegen(const TernCondExprAST *ast) {
    ExprGenPayload condExpr = codegenExpr(ast->getCond());
    if (valueBroken(condExpr) || !condExpr.isBool()) {
        panic = true;
        return {};
    }

    llvm::Function *func = llvmBuilder.GetInsertBlock()->getParent();

    llvm::BasicBlock *trueBlock = llvm::BasicBlock::Create(llvmContext, "true", func);
    llvm::BasicBlock *falseBlock = llvm::BasicBlock::Create(llvmContext, "else");
    llvm::BasicBlock *afterBlock = llvm::BasicBlock::Create(llvmContext, "after");

    if (condExpr.isLitVal()) {
        llvmBuilder.CreateBr(condExpr.litVal.val_b ? trueBlock : falseBlock);
    } else {
        llvmBuilder.CreateCondBr(condExpr.val, trueBlock, falseBlock);
    }

    llvmBuilder.SetInsertPoint(trueBlock);
    ExprGenPayload trueExpr = codegenExpr(ast->getOp1());
    if (valueBroken(trueExpr)) return {};
    llvmBuilder.CreateBr(afterBlock);
    trueBlock = llvmBuilder.GetInsertBlock();

    func->getBasicBlockList().push_back(falseBlock);
    llvmBuilder.SetInsertPoint(falseBlock);
    ExprGenPayload falseExpr = codegenExpr(ast->getOp2());
    if (valueBroken(falseExpr)) return {};
    llvmBuilder.CreateBr(afterBlock);
    falseBlock = llvmBuilder.GetInsertBlock();

    if (trueExpr.isLitVal() && !falseExpr.isLitVal()) {
        if (!promoteLiteral(trueExpr, falseExpr.type)) return {};
    } else if (!trueExpr.isLitVal() && falseExpr.isLitVal()) {
        if (!promoteLiteral(falseExpr, trueExpr.type)) return {};
    } else if (!trueExpr.isLitVal() && !falseExpr.isLitVal()) {
        // implicit casts intentionally left out in order not to lose l-valness
        if (falseExpr.type != trueExpr.type) {
            panic = true;
            return {};
        }
    } else {
        if (trueExpr.litVal.type != falseExpr.litVal.type) {
            panic = true;
            return {};
        }

        // if all three litVals, we will return a litVal
        if (!condExpr.isLitVal()) {
            if (trueExpr.litVal.type == LiteralVal::T_BOOL) {
                if (!promoteLiteral(trueExpr, TypeTable::P_BOOL) ||
                    !promoteLiteral(falseExpr, TypeTable::P_BOOL))
                    return {};
            } else if (trueExpr.litVal.type == LiteralVal::T_SINT) {
                TypeTable::Id trueT = TypeTable::shortestFittingTypeI(trueExpr.litVal.val_si);
                TypeTable::Id falseT = TypeTable::shortestFittingTypeI(falseExpr.litVal.val_si);

                if (TypeTable::isImplicitCastable(trueT, falseT)) {
                    if (!promoteLiteral(trueExpr, falseT) ||
                        !promoteLiteral(falseExpr, falseT))
                        return {};
                } else {
                    if (!promoteLiteral(trueExpr, trueT) ||
                        !promoteLiteral(falseExpr, trueT))
                        return {};
                }
            // don't allow float casts, as don't know which to cast to
            } else {
                panic = true;
                return {};
            }
        }
    }

    ExprGenPayload ret;

    func->getBasicBlockList().push_back(afterBlock);
    llvmBuilder.SetInsertPoint(afterBlock);
    if (condExpr.isLitVal()) {
        if (trueExpr.isLitVal()/* && falseExpr.isLitVal()*/) {
            ret.litVal.type = trueExpr.litVal.type;
            if (ret.litVal.type == LiteralVal::T_BOOL)
                ret.litVal.val_b = condExpr.litVal.val_b ? trueExpr.litVal.val_b : falseExpr.litVal.val_b;
            else if (ret.litVal.type == LiteralVal::T_SINT)
                ret.litVal.val_si = condExpr.litVal.val_b ? trueExpr.litVal.val_si : falseExpr.litVal.val_si;
            else {
                panic = true;
                return {};
            }
        } else/* both not litVals */ {
            ret.type = trueExpr.type;
            ret.val = condExpr.litVal.val_b ? trueExpr.val : falseExpr.val;
        }
    } else {
        llvm::PHINode *phi = llvmBuilder.CreatePHI(symbolTable->getTypeTable()->getType(trueExpr.type), 2, "tern_tmp");
        phi->addIncoming(trueExpr.val, trueBlock);
        phi->addIncoming(falseExpr.val, falseBlock);
        ret.type = trueExpr.type;
        ret.val = phi;
    }

    return ret;
}

CodeGen::ExprGenPayload CodeGen::codegen(const CallExprAST *ast) {
    FuncSignature sig;
    sig.name = ast->getName();
    sig.argTypes = vector<TypeTable::Id>(ast->getArgs().size());

    vector<llvm::Value*> args(ast->getArgs().size());
    vector<ExprGenPayload> exprs(args.size());
    vector<LiteralVal> litVals(args.size());

    for (size_t i = 0; i < ast->getArgs().size(); ++i) {
        exprs[i] = codegenExpr(ast->getArgs()[i].get());
        if (valueBroken(exprs[i])) return {};

        sig.argTypes[i] = exprs[i].type;
        args[i] = exprs[i].val;
        litVals[i] = exprs[i].litVal;
    }

    pair<const FuncSignature*, const FuncValue*> func = symbolTable->getFuncCastsAllowed(sig, litVals.data());
    if (broken(func.second)) return {};

    for (size_t i = 0; i < ast->getArgs().size(); ++i) {
        if (exprs[i].isLitVal()) {
            // this also checks whether sint/uint literals fit into the arg type size
            if (!promoteLiteral(exprs[i], func.first->argTypes[i])) return {};
            args[i] = exprs[i].val;
        } else if (sig.argTypes[i] != func.first->argTypes[i]) {
            createCast(args[i], sig.argTypes[i], func.first->argTypes[i]);
        }
    }

    // reminder, it's lvalue if returning a lvalue (by ref)
    return {func.second->retType, llvmBuilder.CreateCall(func.second->func, args, 
        func.second->hasRet ? "call_tmp" : ""), nullptr};
}

CodeGen::ExprGenPayload CodeGen::codegen(const CastExprAST *ast) {
    llvm::Type *type = codegenType(ast->getType());
    if (broken(type)) return {};

    ExprGenPayload exprVal = codegenExpr(ast->getVal());
    if (valueBroken(exprVal)) return {};

    if (exprVal.isLitVal()) {
        TypeTable::Id promoType;
        switch (exprVal.litVal.type) {
        case LiteralVal::T_BOOL:
            promoType = TypeTable::P_BOOL;
            break;
        case LiteralVal::T_SINT:
            promoType = TypeTable::shortestFittingTypeI(exprVal.litVal.val_si);
            break;
        case LiteralVal::T_FLOAT:
            // cast to widest float type
            // TODO is this the best way?
            promoType = TypeTable::P_F64;
            break;
        default:
            panic = true;
            return {};
        }
        if (!promoteLiteral(exprVal, promoType)) return {};
    }
    
    llvm::Value *val = exprVal.val;
    createCast(val, exprVal.type, type, ast->getType()->getTypeId());

    if (val == nullptr) panic = true;
    return {ast->getType()->getTypeId(), val, nullptr};
}

llvm::Type* CodeGen::codegenType(const TypeAST *ast) {
    llvm::Type *type = symbolTable->getTypeTable()->getType(ast->getTypeId());
    if (broken(type)) return {};

    return type;
}

void CodeGen::codegen(const DeclAST *ast) {
    TypeTable::Id typeId = ast->getType()->getTypeId();
    llvm::Type *type = codegenType(ast->getType());
    if (broken(type)) return;

    for (const auto &it : ast->getDecls()) {
        if (symbolTable->varNameTaken(it.first)) {
            panic = true;
            return;
        }

        const string &name = namePool->get(it.first);

        llvm::Value *val;
        if (symbolTable->inGlobalScope()) {
            val = createGlobal(type, name);

            if (it.second.get() != nullptr) {
                // TODO allow init global vars
                panic = true;
                return;
            }
        } else {
            val = createAlloca(type, name);

            const ExprAST *init = it.second.get();
            if (init != nullptr) {
                ExprGenPayload initPay = codegenExpr(init);
                if (valueBroken(initPay)) return;

                if (initPay.isLitVal()) {
                    if (!promoteLiteral(initPay, typeId)) return;
                }

                llvm::Value *src = initPay.val;

                if (initPay.type != typeId) {
                    if (!TypeTable::isImplicitCastable(initPay.type, typeId)) {
                        panic = true;
                        return;
                    }

                    createCast(src, initPay.type, type, typeId);
                }

                llvmBuilder.CreateStore(src, val);
            }
        }

        symbolTable->addVar(it.first, {typeId, val});
    }
}

void CodeGen::codegen(const IfAST *ast) {
    // unlike C++, then and else may eclipse vars declared in if's init
    ScopeControl scope(ast->hasInit() ? symbolTable : nullptr);

    if (ast->hasInit()) {
        codegenNode(ast->getInit());
        if (panic) return;
    }

    ExprGenPayload condExpr = codegenExpr(ast->getCond());
    if (condExpr.isLitVal() && !promoteLiteral(condExpr, TypeTable::P_BOOL)) {
        panic = true;
        return;
    }
    if (valBroken(condExpr) || condExpr.type != TypeTable::P_BOOL) {
        panic = true;
        return;
    }

    llvm::Function *func = llvmBuilder.GetInsertBlock()->getParent();

    llvm::BasicBlock *thenBlock = llvm::BasicBlock::Create(llvmContext, "then", func);
    llvm::BasicBlock *elseBlock = ast->hasElse() ? llvm::BasicBlock::Create(llvmContext, "else") : nullptr;
    llvm::BasicBlock *afterBlock = llvm::BasicBlock::Create(llvmContext, "after");

    llvmBuilder.CreateCondBr(condExpr.val, thenBlock, ast->hasElse() ? elseBlock : afterBlock);

    {
        ScopeControl thenScope(symbolTable);
        llvmBuilder.SetInsertPoint(thenBlock);
        codegenNode(ast->getThen(), false);
        if (panic) return;
        if (!isBlockTerminated()) llvmBuilder.CreateBr(afterBlock);
    }

    if (ast->hasElse()) {
        ScopeControl elseScope(symbolTable);
        func->getBasicBlockList().push_back(elseBlock);
        llvmBuilder.SetInsertPoint(elseBlock);
        codegenNode(ast->getElse(), false);
        if (panic) return;
        if (!isBlockTerminated()) llvmBuilder.CreateBr(afterBlock);
    }

    func->getBasicBlockList().push_back(afterBlock);
    llvmBuilder.SetInsertPoint(afterBlock);
}

void CodeGen::codegen(const ForAST *ast) {
    ScopeControl scope(ast->getInit()->type() == AST_Decl ? symbolTable : nullptr);

    codegenNode(ast->getInit());
    if (panic) return;

    llvm::Function *func = llvmBuilder.GetInsertBlock()->getParent();

    llvm::BasicBlock *condBlock = llvm::BasicBlock::Create(llvmContext, "cond", func);
    llvm::BasicBlock *bodyBlock = llvm::BasicBlock::Create(llvmContext, "body");
    llvm::BasicBlock *afterBlock = llvm::BasicBlock::Create(llvmContext, "after");

    llvmBuilder.CreateBr(condBlock);
    llvmBuilder.SetInsertPoint(condBlock);

    ExprGenPayload condExpr;
    if (ast->hasCond()) {
        condExpr = codegenExpr(ast->getCond());
        if (condExpr.isLitVal() && !promoteLiteral(condExpr, TypeTable::P_BOOL)) {
            panic = true;
            return;
        }
        if (valBroken(condExpr) || condExpr.type != TypeTable::P_BOOL) {
            panic = true;
            return;
        }
    } else {
        condExpr.type = TypeTable::P_BOOL;
        condExpr.val = getConstB(true);
    }

    llvmBuilder.CreateCondBr(condExpr.val, bodyBlock, afterBlock);

    {
        ScopeControl scopeBody(symbolTable);
        func->getBasicBlockList().push_back(bodyBlock);
        llvmBuilder.SetInsertPoint(bodyBlock);

        codegenNode(ast->getBody(), false);
        if (panic) return;
    }
        
    if (ast->hasIter()) {
        codegenNode(ast->getIter());
        if (panic) return;
    }

    if (!isBlockTerminated()) llvmBuilder.CreateBr(condBlock);

    func->getBasicBlockList().push_back(afterBlock);
    llvmBuilder.SetInsertPoint(afterBlock);
}

void CodeGen::codegen(const WhileAST *ast) {
    llvm::Function *func = llvmBuilder.GetInsertBlock()->getParent();

    llvm::BasicBlock *condBlock = llvm::BasicBlock::Create(llvmContext, "cond", func);
    llvm::BasicBlock *bodyBlock = llvm::BasicBlock::Create(llvmContext, "body");
    llvm::BasicBlock *afterBlock = llvm::BasicBlock::Create(llvmContext, "after");

    llvmBuilder.CreateBr(condBlock);
    llvmBuilder.SetInsertPoint(condBlock);

    ExprGenPayload condExpr = codegenExpr(ast->getCond());
    if (condExpr.isLitVal() && !promoteLiteral(condExpr, TypeTable::P_BOOL)) {
        panic = true;
        return;
    }
    if (valBroken(condExpr) || condExpr.type != TypeTable::P_BOOL) {
        panic = true;
        return;
    }

    llvmBuilder.CreateCondBr(condExpr.val, bodyBlock, afterBlock);

    {
        ScopeControl scope(symbolTable);
        func->getBasicBlockList().push_back(bodyBlock);
        llvmBuilder.SetInsertPoint(bodyBlock);
        codegenNode(ast->getBody(), false);
        if (panic) return;
        if (!isBlockTerminated()) llvmBuilder.CreateBr(condBlock);
    }

    func->getBasicBlockList().push_back(afterBlock);
    llvmBuilder.SetInsertPoint(afterBlock);
}

void CodeGen::codegen(const DoWhileAST *ast) {
    llvm::Function *func = llvmBuilder.GetInsertBlock()->getParent();

    llvm::BasicBlock *bodyBlock = llvm::BasicBlock::Create(llvmContext, "body", func);
    llvm::BasicBlock *afterBlock = llvm::BasicBlock::Create(llvmContext, "after");

    llvmBuilder.CreateBr(bodyBlock);
    llvmBuilder.SetInsertPoint(bodyBlock);

    {
        ScopeControl scope(symbolTable);
        codegenNode(ast->getBody(), false);
        if (panic) return;
    }

    ExprGenPayload condExpr = codegenExpr(ast->getCond());
    if (condExpr.isLitVal() && !promoteLiteral(condExpr, TypeTable::P_BOOL)) {
        panic = true;
        return;
    }
    if (valBroken(condExpr) || condExpr.type != TypeTable::P_BOOL) {
        panic = true;
        return;
    }

    llvmBuilder.CreateCondBr(condExpr.val, bodyBlock, afterBlock);

    func->getBasicBlockList().push_back(afterBlock);
    llvmBuilder.SetInsertPoint(afterBlock);
}

void CodeGen::codegen(const RetAST *ast) {
    const FuncValue *currFunc = symbolTable->getCurrFunc();
    if (currFunc == nullptr) {
        panic = true;
        return;
    }

    if (!ast->getVal()) {
        if (currFunc->hasRet) {
            panic = true;
            return;
        }
        llvmBuilder.CreateRetVoid();
        return;
    }

    ExprGenPayload retExpr = codegenExpr(ast->getVal());
    if (retExpr.isLitVal() && !promoteLiteral(retExpr, currFunc->retType)) return;
    if (valBroken(retExpr)) return;

    llvm::Value *retVal = retExpr.val;
    if (retExpr.type != currFunc->retType) {
        if (!TypeTable::isImplicitCastable(retExpr.type, currFunc->retType)) {
            panic = true;
            return;
        }
        createCast(retVal, retExpr.type, currFunc->retType);
    }

    llvmBuilder.CreateRet(retVal);
}

void CodeGen::codegen(const BlockAST *ast, bool makeScope) {
    ScopeControl scope(makeScope ? symbolTable : nullptr);

    for (const auto &it : ast->getBody()) codegenNode(it.get());
}

FuncValue* CodeGen::codegen(const FuncProtoAST *ast, bool definition) {
    if (symbolTable->funcNameTaken(ast->getName())) {
        panic = true;
        return nullptr;
    }

    FuncSignature sig;
    sig.name = ast->getName();
    sig.argTypes = vector<TypeTable::Id>(ast->getArgCnt());
    for (size_t i = 0; i < ast->getArgCnt(); ++i) sig.argTypes[i] = ast->getArgType(i)->getTypeId();

    FuncValue *prev = symbolTable->getFunc(sig);

    // check for definition clashes with existing functions
    if (prev != nullptr) {
        if ((prev->defined && definition) ||
            (prev->hasRet != ast->hasRetVal()) ||
            (prev->hasRet && prev->retType != ast->getRetType()->getTypeId())) {
            panic = true;
            return nullptr;
        }

        if (definition) {
            prev->defined = true;
        }

        return prev;
    }

    // can't have args with same name
    for (size_t i = 0; i+1 < ast->getArgCnt(); ++i) {
        for (size_t j = i+1; j < ast->getArgCnt(); ++j) {
            if (ast->getArgName(i) == ast->getArgName(j)) {
                panic = true;
                return nullptr;
            }
        }
    }

    vector<llvm::Type*> argTypes(ast->getArgCnt());
    for (size_t i = 0; i < argTypes.size(); ++i)
        argTypes[i] = symbolTable->getTypeTable()->getType(ast->getArgType(i)->getTypeId());
    llvm::Type *retType = ast->hasRetVal() ? symbolTable->getTypeTable()->getType(ast->getRetType()->getTypeId()) : llvm::Type::getVoidTy(llvmContext);
    llvm::FunctionType *funcType = llvm::FunctionType::get(retType, argTypes, false);

    llvm::Function *func = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, 
            namePool->get(ast->getName()), llvmModule.get());
    
    size_t i = 0;
    for (auto &arg : func->args()) {
        arg.setName(namePool->get(ast->getArgName(i)));
        ++i;
    }

    FuncValue val;
    val.func = func;
    val.hasRet = ast->hasRetVal();
    if (ast->hasRetVal()) val.retType = ast->getRetType()->getTypeId();
    val.defined = definition;

    symbolTable->addFunc(sig, val);
    // cannot return &val, as it is a local var
    return symbolTable->getFunc(sig);
}

void CodeGen::codegen(const FuncAST *ast) {
    FuncValue *funcVal = codegen(ast->getProto(), true);
    if (broken(funcVal)) {
        return;
    }

    ScopeControl scope(*symbolTable, *funcVal);

    llvmBuilderAlloca.SetInsertPoint(llvm::BasicBlock::Create(llvmContext, "alloca", funcVal->func));

    llvm::BasicBlock *body = llvm::BasicBlock::Create(llvmContext, "entry", funcVal->func);
    llvmBuilder.SetInsertPoint(body);

    size_t i = 0;
    for (auto &arg : funcVal->func->args()) {
        const TypeAST *astArgType = ast->getProto()->getArgType(i);
        NamePool::Id astArgName = ast->getProto()->getArgName(i);
        const string &name = namePool->get(astArgName);
        llvm::AllocaInst *alloca = createAlloca(arg.getType(), name);
        llvmBuilder.CreateStore(&arg, alloca);
        symbolTable->addVar(astArgName, {astArgType->getTypeId(), alloca});

        ++i;
    }

    codegen(ast->getBody(), false);
    if (panic) {
        funcVal->func->eraseFromParent();
        return;
    }

    llvmBuilderAlloca.CreateBr(body);

    if (!ast->getProto()->hasRetVal() && !isBlockTerminated())
            llvmBuilder.CreateRetVoid();

    if (llvm::verifyFunction(*funcVal->func, &llvm::errs())) cout << endl;
}

void CodeGen::printout() const {
    llvmModule->print(llvm::outs(), nullptr);
}

bool CodeGen::binary(const std::string &filename) {
    // TODO add optimizer passes
    
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
    llvm::Optional<llvm::Reloc::Model> RM = llvm::Optional<llvm::Reloc::Model>();
    llvm::TargetMachine *targetMachine = target->createTargetMachine(targetTriple, cpu, features, options, RM);

    llvmModule->setDataLayout(targetMachine->createDataLayout());

    std::error_code EC;
    llvm::raw_fd_ostream dest(filename, EC, llvm::sys::fs::F_None);
    if (EC) {
        llvm::errs() << "Could not open file: " << EC.message();
        return false;
    }

    llvm::legacy::PassManager pass;
    llvm::TargetMachine::CodeGenFileType fileType = llvm::TargetMachine::CGFT_ObjectFile;

    bool fail = targetMachine->addPassesToEmitFile(pass, dest, nullptr, fileType);
    if (fail) {
        llvm::errs() << "Target machine can't emit to this file type!";
        return false;
    }

    pass.run(*llvmModule);
    dest.flush();

    return true;
}