// Here I'm working through the LLVM tutorial found at https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html
//
// The first part of the tutorial focuses on the creation of a lexer which processes a text file and breaks it up into tokens

#ifndef __LEXER_H__
#define __LEXER_H__

// #include <cstdio>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

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

#endif
