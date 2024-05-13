// Driver for LLVM tutorial

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <memory>

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
}

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
    };

    /// NumberExprAST - Expression class for numeric literals like "1.0"
    class NumberExprAST : public ExprAST{
        private:
            double Val;

        public:
            NumberExprAST(double Val) : Val(Val) {}
    };

    /// VariableExprAST - Expression class for referencing a variable, like "a".
    class VariableExprAST : public ExprAST{
        std::string Name;

        public:
            VariableExprAST(const std::string& Name) : Name(Name) {}
    };

    /// BinaryExprAST - Expression class for a binary operator.
    class BinaryExprAST : public ExprAST{
        private:
            char Op;
            std::unique_ptr<ExprAST> LHS, RHS;

        public:
            BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS, std::unique_ptr<ExprAST> RHS)
                : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}

    };

    /// CallExprAST - Expression class for function calls.
    class CallExprAST : public ExprAST {
        private:
            std::string Callee;
            std::vector<std::unique_ptr<ExprAST> > Args;

        public:
            CallExprAST(const std::string& Callee, std::vector<std::unique_ptr<ExprAST> > Args)
                : Callee(Callee), Args(std::move(Args)) {}
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
            const std::string& getName() const {return Name;}
    };

    /// FunctionAST - This class represents a function definition itself.
    class FunctionAST{
        private:
            std::unique_ptr<PrototypeAST> Proto;
            std::unique_ptr<ExprAST> Body;

        public:
            FunctionAST(std::unique_ptr<PrototypeAST> Proto, std::unique_ptr<ExprAST> Body)
                : Proto(std::move(Proto)), Body(std::move(Body)) {}
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

/// Definition starts on line  , declared here so the compiler can resolve the function name.
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

//------------------------------------------------------------------------------------------------------//

int main(int argc, char* argv[]){
    std::cout << "Test" << std::endl;
    std::cout << tok_eof << std::endl;

    Token tmp = tok_extern;

    std::cout << tok_extern << std::endl;
}
