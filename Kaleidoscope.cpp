// Driver for LLVM tutorial

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <memory>
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"

using namespace llvm;

//------------------------------------------------------------------------------------------------------//
// Lexer
//------------------------------------------------------------------------------------------------------//

// The lexer returns tokens [0-255] if it is an unknown character (the character's ASCII value),
// otherwise one of these for knwon things.
enum Token {
    tok_eof = -1,

    // commands
    tok_def = -2,
    tok_extern = -3,

    // primary
    tok_identifier = -4,
    tok_number = -5
};

static std::string IdentifierStr;  // Filled in if tok_identifier
static double NumVal;              // Filled in if tok_number

// gettok - Return the next token from standard input.
static int gettok(){
    static int LastChar = ' ';

    // Skip any whitespace.
    while(isspace(LastChar)){
        LastChar = getchar();
    }

    // Identifiers must start with a letter.
    if(isalpha(LastChar)) { // identifier: [a-zA-Z][a-zA-Z0-9]*
        IdentifierStr = LastChar;
        while(isalnum((LastChar = getchar())))
            IdentifierStr += LastChar;

        if(IdentifierStr == "def")
            return tok_def;
        if( IdentifierStr == "extern")
            return tok_extern;
        return tok_identifier;
    }

    // Numbers may start with either a number or a decimal point
    // There is extra logic that the LLVM tutorial doesn't contain to handle
    // cases of improperly formated numbers such as 1.23.45.67
    if(isdigit(LastChar) || LastChar == '.'){ // Number: [0-9.]+
        std::string NumStr;
        bool hasDecimal = false;
        bool decimalError = false;
        do{
            if(LastChar == '.' && hasDecimal)
                decimalError = true;
            if(LastChar == '.')
                hasDecimal = true;
            NumStr += LastChar;
            LastChar = getchar();
        } while(isdigit(LastChar) || LastChar == '.');

        NumVal = strtod(NumStr.c_str(), 0);
        if(decimalError){
            std::cerr << "Number Syntax Error! Too many decimals: " << NumVal << std::endl;
            exit(1);
        }
        return tok_number;
    }

    // Handle Comments
    if(LastChar == '#'){
        // Comment until end of line.
        do
            LastChar = getchar();
        while(LastChar != EOF && LastChar != '\n' && LastChar != '\r');

        if(LastChar != EOF)
            return gettok();
    }

    // Check for end of file. Don't eat the EOF.
    if(LastChar == EOF)
        return tok_eof;

    // Otherwise, just return the character as its ascii value.
    int ThisChar = LastChar;
    LastChar = getchar();
    return ThisChar;
} // end of gettok()

//------------------------------------------------------------------------------------------------------//
// End of Lexer
//------------------------------------------------------------------------------------------------------//

//------------------------------------------------------------------------------------------------------//
// Abstract Syntax Tree (aka Parse Tree)
//------------------------------------------------------------------------------------------------------//

namespace{
    /// ExprAST - Base class for all expression nodes.
    class ExprAST{
        public:
            virtual ~ExprAST() = default;
            virtual Value* codegen() = 0;
    };

    /// NumberExprAST - Expression class for numeric literals like "1.0"
    class NumberExprAST : public ExprAST{
        private:
            double Val;

        public:
            NumberExprAST(double Val) : Val(Val) {}
            Value* codegen() override;
    };

    /// VariableExprAST - Expression class for referencing a variable, like "a".
    class VariableExprAST : public ExprAST{
        private:
            std::string Name;

        public:
            VariableExprAST(const std::string& Name) : Name(Name) {}
            Value* codegen() override;
    };

    /// BinaryExprAST - Expression class for a binary operator.
    class BinaryExprAST : public ExprAST{
        private:
            char Op;
            std::unique_ptr<ExprAST> LHS, RHS;

        public:
            BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS, std::unique_ptr<ExprAST> RHS)
                : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
            Value* codegen() override;

    };

    /// CallExprAST - Expression class for function calls.
    class CallExprAST : public ExprAST {
        private:
            std::string Callee;
            std::vector<std::unique_ptr<ExprAST> > Args;

        public:
            CallExprAST(const std::string& Callee, std::vector<std::unique_ptr<ExprAST> > Args)
                : Callee(Callee), Args(std::move(Args)) {}
            Value* codegen() override;
    };

    /// PrototypeAST - This class represents the "prototype" for a function,
    /// which captures its name, and its argument names (thus implicitly the number
    /// of arguments the function takes).
    class PrototypeAST{
        private:
            std::string Name;
            std::vector<std::string> Args;

        public:
            PrototypeAST(const std::string& Name, std::vector<std::string> Args)
                : Name(Name), Args(std::move(Args)) {}
            Function* codegen();
            const std::string& getName() const {return Name;}
            const std::vector<std::string> getArgs() const {return Args;}
    };

    /// FunctionAST - This class represents a function definition itself.
    class FunctionAST{
        private:
            std::unique_ptr<PrototypeAST> Proto;
            std::unique_ptr<ExprAST> Body;

        public:
            FunctionAST(std::unique_ptr<PrototypeAST> Proto, std::unique_ptr<ExprAST> Body)
                : Proto(std::move(Proto)), Body(std::move(Body)) {}
            Function* codegen();
    };
} // end of anonymous namespace

//------------------------------------------------------------------------------------------------------//
// End of AST
//------------------------------------------------------------------------------------------------------//

//------------------------------------------------------------------------------------------------------//
// Parser
//------------------------------------------------------------------------------------------------------//

/// CurTok/getNextToken - Provide a simple toekn buffer, CurTok is the current token the parser is looking at.
/// getNextToken reads another token from the lexer and updates CurTok with its results.
static int CurTok;
static int getNextToken(){
    return CurTok = gettok();
}

/// BinopPrecedence - This holds the precedence for each binary operator that is
/// defined.
static std::map<char, int> BinopPrecedence;

/// LogError* - These are little helper functions for error handling.
std::unique_ptr<ExprAST> LogError(const char* Str){
    std::cout << "Error: " << Str << std::endl;
    return nullptr;
}

std::unique_ptr<PrototypeAST> LogErrorP(const char* Str){
    LogError(Str);
    return nullptr;
}

/// Declared here so the compiler can resolve the function name, defintion below.
static std::unique_ptr<ExprAST> ParseExpression();

/// numberexpr ::= number
static std::unique_ptr<ExprAST> ParseNumberExpr(){
    auto Result = std::make_unique<NumberExprAST>(NumVal);
    getNextToken();
    return std::move(Result);
}

/// parenexpr ::= '(' expression ')'
static std::unique_ptr<ExprAST> ParseParenExpr(){
    getNextToken(); // eat (.
    auto V = ParseExpression();
    if(!V)
        return nullptr;

    if(CurTok != ')')
        return LogError("expected ')'");

    getNextToken(); // eat ).
    return V;
}

/// identifierexpr
///     ::= identifier
///     ::= identifier '(' expression* ')'
static std::unique_ptr<ExprAST> ParseIdentifierExpr(){
    std::string IdName = IdentifierStr;

    getNextToken(); // eat identifier.

    if(CurTok != '(') // Simple variable ref.
        return std::make_unique<VariableExprAST>(IdName);

    // Call.
    getNextToken(); // eat (
    std::vector<std::unique_ptr<ExprAST> > Args;
    if(CurTok != ')'){
        while(true){
            if(auto Arg = ParseExpression())
                Args.push_back(std::move(Arg));
            else
                return nullptr;

            if(CurTok == ')')
                break;

            if(CurTok != ',')
                return LogError("Expected ')' or ',' in argument list");

            getNextToken();
       }
    }

    // Eat the ')'.
    getNextToken();

    return std::make_unique<CallExprAST>(IdName, std::move(Args));
}

/// primary
///     ::= identifierexpr
///     ::= numberexpr
///     ::= parenexpr
static std::unique_ptr<ExprAST> ParsePrimary(){
    switch(CurTok){
        default:
            return LogError("Unknown token when expecting an expression");
        case tok_identifier:
            return ParseIdentifierExpr();
        case tok_number:
            return ParseNumberExpr();
        case '(':
            return ParseParenExpr();
    }
}

static int getTokPrecedence(){
    if(!isascii(CurTok))
        return -1;

    // Make sure it's a declared binop.
    int TokPrec = BinopPrecedence[CurTok];
    if(TokPrec <= 0) return -1;
    return TokPrec;
}

/// binoprhs
/// ::= ('+' primary)*
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, std::unique_ptr<ExprAST> LHS){
    // If this is a binop, find its precedence.
    while(true){
        int TokPrec = getTokPrecedence();

        // If this is a binop that binds at least as tightly as the current binop,
        // consume it, otherwise we are done.
        if(TokPrec < ExprPrec)
            return LHS;

        // Okay, we know this is a binop.
        int BinOp = CurTok; // remember binop
        getNextToken(); // eat binop

        // Parse the primary expression after the binary operator.
        auto RHS = ParsePrimary();
        if(!RHS)
            return nullptr;

        // If BinOp binds less tightly with RHS that the operator after RHS, let
        // the pending operator take RHS as its LHS.
        int NextPrec = getTokPrecedence();
        if(TokPrec < NextPrec){
            RHS = ParseBinOpRHS(TokPrec+1, std::move(RHS));
            if(!RHS)
                return nullptr;
        }

        // Merge LHS/RHS.
        LHS = std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
    } // loop around to the top of the while loop.
}

/// expression
///     ::= primary binoprhs
static std::unique_ptr<ExprAST> ParseExpression(){
    auto LHS = ParsePrimary();
    if(!LHS)
        return nullptr;

    return ParseBinOpRHS(0, std::move(LHS));
}

/// prototype
///     ::= id '(' id* ')'
static std::unique_ptr<PrototypeAST> ParsePrototype(){
    // Remember if function is externl of defined internally
    int FnType = CurTok;
    getNextToken(); // eat extern or def
    if(CurTok != tok_identifier)
        return LogErrorP("Expected function name in prototype");

    std::string FnName = IdentifierStr;
    switch(FnType){
        case tok_def:
            FnName.append("_def");
            break;
        case tok_extern:
            FnName.append("_ext");
            break;
    }
    getNextToken();

    if(CurTok != '(')
        return LogErrorP("Expected '(' in protype");

    // Read the list of argument names.
    std::vector<std::string> ArgNames;
    while(getNextToken() == tok_identifier)
        ArgNames.push_back(IdentifierStr);

    if(CurTok != ')')
        return LogErrorP("Expected ')' in prototype");

    // success.
    getNextToken(); // eat ')'

    return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

/// definition ::= 'def' prototype expression
static std::unique_ptr<FunctionAST> ParseDefinition(){
    auto Proto = ParsePrototype();
    if(!Proto) return nullptr;

    if(auto E = ParseExpression())
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));

    return nullptr;
}

/// external ::= 'extern' prototype
static std::unique_ptr<PrototypeAST> ParseExtern(){
    return ParsePrototype();
}

/// toplevelexpr ::= expression
static std::unique_ptr<FunctionAST> ParseTopLevelExpr(){
    if(auto E = ParseExpression()){
        // Make an anonymous proto.
        auto Proto = std::make_unique<PrototypeAST>("", std::vector<std::string>());
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}

//------------------------------------------------------------------------------------------------------//
// End of parser
//------------------------------------------------------------------------------------------------------//

//------------------------------------------------------------------------------------------------------//
// Code Generation
//------------------------------------------------------------------------------------------------------//

static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<IRBuilder<> > Builder;
static std::unique_ptr<Module> TheModule;
static std::map<std::string, Value*> NamedValues;

Value* LogErrorV(const char* Str){
    LogError(Str);
    return nullptr;
}

Value* NumberExprAST::codegen(){
    return ConstantFP::get(*TheContext, APFloat(Val));
}

Value* VariableExprAST::codegen(){
    // Look this variable up in the function.
    Value* V = NamedValues[Name];
    if(!V)
        LogErrorV("Unknown variable name");
    return V;
}

Value* BinaryExprAST::codegen(){
    Value* L = LHS->codegen();
    Value* R = RHS->codegen();
    if(!L || !R)
        return nullptr;

    switch(Op){
        case '+':
            return Builder->CreateFAdd(L, R, "addtmp");
        case '-':
            return Builder->CreateFSub(L, R, "subtmp");
        case '*':
            return Builder->CreateFMul(L, R, "multmp");
        case '/':
            return Builder->CreateFDiv(L, R, "divtmp");
        case '<':
            L = Builder->CreateFCmpULT(L, R, "cmptmp");
            // Convert bool 0/1 to double 0.0 or 1.0
            return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
        default:
        return LogErrorV("invalid binary operator");
    }
}

Value* CallExprAST::codegen(){
    // Look up the name in the global module table.
    Function* CalleeF = TheModule->getFunction(Callee);
    if(!CalleeF)
        return LogErrorV("Unknown function referenced");

    // If argument mismatch error.
    if(CalleeF->arg_size() != Args.size())
        return LogErrorV("Incorrect # arguments passed");

    std::vector<Value*> ArgsV;
    for(unsigned i =0, e=Args.size(); i != e; ++i){
        ArgsV.push_back(Args[i]->codegen());
        if(!ArgsV.back())
            return nullptr;
    }

    return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

Function* PrototypeAST::codegen(){
    // Make the function type: double(double, double) etc.
    std::vector<Type*> Doubles(Args.size(), Type::getDoubleTy(*TheContext));
    FunctionType* FT = FunctionType::get(Type::getDoubleTy(*TheContext), Doubles, false);
    Function* F = Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

    // Set names for all arguments.
    unsigned Idx = 0;
    for(auto &Arg : F->args())
        Arg.setName(Args[Idx++]);

    return F;
}

Function* FunctionAST::codegen(){
    // First, check for an existing function from a previous 'extern' declaration.
    Function* TheFunction = TheModule->getFunction(Proto->getName());

    if(!TheFunction)
        TheFunction = Proto->codegen();

    if(!TheFunction)
        return nullptr;

    if(!TheFunction->empty())
        return (Function*)LogErrorV("Function cannot be redefined.");

    BasicBlock* BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
    Builder->SetInsertPoint(BB);

    // Record the function arguments in the NamedValues map.
    NamedValues.clear();
    for(auto &Arg : TheFunction->args())
        NamedValues[std::string(Arg.getName())] = &Arg;

    if(Value* RetVal = Body->codegen()){
        // Finish off the function.
        Builder->CreateRet(RetVal);

        // Validate the generated code, checking for consistency.
        verifyFunction(*TheFunction);

        return TheFunction;
    }

    // Error reading body, remove function.
    TheFunction->eraseFromParent();
    return nullptr;
}

//------------------------------------------------------------------------------------------------------//
// End of code generation
//------------------------------------------------------------------------------------------------------//

//------------------------------------------------------------------------------------------------------//
// Top-Level parsing
//------------------------------------------------------------------------------------------------------//

static void InitializeModule(){
    // Open a new context and module.
    TheContext = std::make_unique<LLVMContext>();
    TheModule = std::make_unique<Module>("my cool jit", *TheContext);

    //Create a new builder for the module.
    Builder = std::make_unique<IRBuilder<> >(*TheContext);
}


static void HandleDefinition(){
    if(auto FnAST = ParseDefinition()){
        if(auto* FnIR = FnAST->codegen()){
            std::cout << "Parsed a function definition:" << std::endl;
            FnIR->print(errs());
            std::cout << std::endl;
        }
    }else{
        // Skip token for error recovery.
        getNextToken();
    }
}

static void HandleExtern(){
    if(auto ProtoAST = ParseExtern()){
        if(auto* FnIR = ProtoAST->codegen()){
            std::cout << "Parsed an extern:" << std::endl;
            FnIR->print(errs());
            std::cout << std::endl;
        }
    }else{
        // Skip token for error recovery
        getNextToken();
    }
}

static void HandleTopLevelExpression(){
    // Evaluate a top-level expression into an anonymous function.
    if(auto FnAST = ParseTopLevelExpr()){
        if(auto* FnIR = FnAST->codegen()){
            std::cout << "Parsed a top-level expression:" << std::endl;
            FnIR->print(errs());
            std::cout << std::endl;

            // Remove the anonymous expression.
            FnIR->eraseFromParent();
        }
    }else{
        // Skip token for error recovery.
        getNextToken();
    }
}

/// top ::= definition | external | expression | ';'
static void MainLoop(){
    while(true){
        switch(CurTok){
            case tok_eof:
                return;
            case ';': // ignore top-level semicolons.
                std::cout << "ready> ";
                getNextToken();
                break;
            case tok_def:
                HandleDefinition();
                break;
            case tok_extern:
                HandleExtern();
                break;
            default:
                HandleTopLevelExpression();
                break;
        }
    }
}

//------------------------------------------------------------------------------------------------------//
// End of top-level parsing
//------------------------------------------------------------------------------------------------------//

//------------------------------------------------------------------------------------------------------//
// Main driver code.
//------------------------------------------------------------------------------------------------------//

int main(int argc, char* argv[]){
    // Install standard binary operators.
    // 1 is lowest precedence.
    BinopPrecedence['<'] = 10;
    BinopPrecedence['+'] = 20;
    BinopPrecedence['-'] = 20;
    BinopPrecedence['*'] = 40;
    BinopPrecedence['/'] = 40; // highest.

    // Prime the first token.
    std::cout << "ready> ";
    getNextToken();

    // Make the module, which holds all the code.
    InitializeModule();

    // run the main "interpreter loop" now.
    MainLoop();

    // Print out all of the generated code.
    TheModule->print(errs(), nullptr);

    return 0;
}
