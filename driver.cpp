// Driver for LLVM tutorial

#include "lexer.h"
#include "AST.h"
#include "parser.h"

int main(int argc, char* argv[]){
    std::cout << "Test" << std::endl;
    std::cout << tok_eof << std::endl;

    Token tmp = tok_extern;

    std::cout << tok_extern << std::endl;
}
