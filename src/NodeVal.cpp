#include "NodeVal.h"
using namespace std;

NodeVal::NodeVal() : kind(Kind::kInvalid) {
}

NodeVal::NodeVal(CodeLoc codeLoc, StringPool::Id import) : codeLoc(codeLoc), kind(Kind::kImport), importFile(import) {
}

NodeVal::NodeVal(CodeLoc codeLoc) : codeLoc(codeLoc), kind(Kind::kComposite) {
}

NodeVal::NodeVal(CodeLoc codeLoc, const LiteralVal &val) : codeLoc(codeLoc), kind(Kind::kLiteral), literal(val) {
}

NodeVal::NodeVal(CodeLoc codeLoc, const KnownVal &val) : codeLoc(codeLoc), kind(Kind::kKnown), known(val) {
}

NodeVal::NodeVal(CodeLoc codeLoc, const LlvmVal &val) : codeLoc(codeLoc), kind(Kind::kLlvm), llvm(val) {
}

void NodeVal::copyFrom(const NodeVal &other) {
    codeLoc = other.codeLoc;
    kind = other.kind;
    importFile = other.importFile;
    literal = other.literal;
    known = other.known;
    llvm = other.llvm;
    
    children.reserve(other.children.size());
    for (const auto &it : other.children) {
        children.push_back(NodeVal(it));
    }
    
    if (other.hasTypeAttr()) {
        typeAttr = make_unique<NodeVal>(*other.typeAttr);
    }

    escaped = other.escaped;
}

NodeVal::NodeVal(const NodeVal &other) : NodeVal() {
    copyFrom(other);
}

NodeVal& NodeVal::operator=(const NodeVal &other) {
    if (this != &other) {
        copyFrom(other);
    }
    return *this;
}

optional<TypeTable::Id> NodeVal::getType() const {
    if (isKnownVal()) return getKnownVal().getType();
    if (isLlvmVal()) return getLlvmVal().type;
    return nullopt;
}

bool NodeVal::hasRef() const {
    if (isKnownVal()) return getKnownVal().ref != nullptr;
    if (isLlvmVal()) return getLlvmVal().ref != nullptr;
    return false;
}

std::size_t NodeVal::getLength() const {
    if (isInvalid()) return 0;
    if (isComposite()) return getChildrenCnt();
    return 1;
}

void NodeVal::addChild(NodeVal c) {
    children.push_back(move(c));
}

void NodeVal::addChildren(std::vector<NodeVal> c) {
    children.reserve(children.size()+c.size());
    for (auto &it : c) {
        addChild(move(it));
    }
}

void NodeVal::setTypeAttr(NodeVal t) {
    typeAttr = make_unique<NodeVal>(move(t));
}

void NodeVal::escape() {
    escaped = true;
    if (isComposite()) {
        for (auto &child : children) {
            child.escape();
        }
    }
}

void NodeVal::unescape() {
    if (isComposite()) {
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            (*it).unescape();
        }
    }
    escaped = false;
}