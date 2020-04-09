#include "CompileMessages.h"
#include <sstream>
#include <filesystem>
#include "NamePool.h"
using namespace std;

inline string toString(CodeLoc loc) {
    stringstream ss;
    ss << filesystem::relative(*loc.file);
    ss << ':' << loc.ln << ':' << loc.col << ':';
    return ss.str();
}

inline void CompileMessages::error(CodeLoc loc, const string &str) {
    stringstream ss;
    ss << toString(loc) << ' ' << str;
    errors.push_back(ss.str());
}

void CompileMessages::errorUnexpectedTokenType(CodeLoc loc, Token::Type exp, Token see) {
    stringstream ss;
    ss << "Unexpected symbol found. Expected '" << errorString(exp) << "', instead found '" << errorString(see) << "'.";
    error(loc, ss.str());
}

void CompileMessages::errorNotSimple(CodeLoc loc) {
    error(loc, "Statement not one of: declaration, expression, empty.");
}

void CompileMessages::errorNotPrim(CodeLoc loc) {
    error(loc, "Expected an expression, could not parse one.");
}

void CompileMessages::errorNotTypeId(CodeLoc loc, NamePool::Id name) {
    stringstream ss;
    ss << "Expected a type identifier, instead found '" << namePool->get(name) << "'.";
    error(loc, ss.str());
}

void CompileMessages::errorBadArraySize(CodeLoc loc, long int size) {
    stringstream ss;
    ss << "Array size must be a non-negative integer. Size " << size << " is invalid.";
    error(loc, ss.str());
}

void CompileMessages::errorUnknown(CodeLoc loc) {
    error(loc, "Unknown error occured.");
}