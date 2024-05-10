// Here I'm working through the LLVM tutorial found at https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html
//
// This file contains the parser to be used in conjuction with the lexer.

#ifndef __PARSER_H__
#define __PARSER_H__

#include "lexer.h"
#include "AST.h"
#include <iostream>

/// CurTok/getNextToken - Provide a simple toekn buffer, CurTok is the current token the parser is looking at.
/// getNextToken reads another token from the lexer and updates CurTok with its results.
static int CurTok;
static int getNextToken(){
    return CurTok = gettok();
}

/// LogError* - These are little helper functions for error handling.
std::unique_ptr<ExprAST> LogError(const char* Str){
    std::cout << "Error: " << Str << std::endl;
    return nullptr;
}

std::unique_ptr<PrototypeAST> LogErrorP(const char* Str){
    LogError(Str);
    return nullptr;
}

/// numberexpr ::= number
static std::unique_ptr<ExprAST> ParseNumberExpr() {
    auto Result = std::make_unique<NumberExprAST>(NumVal);
    return nullptr;
}

#endif
