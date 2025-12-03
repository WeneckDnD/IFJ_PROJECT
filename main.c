// #include <stdio.h>d
// #include <string.h>

#include "lexer.h"
#include "symtable.h"
#include "syntactic.h"
#include "semantic.h"

//--------------------------------- added ---------------------------------
#include "Generator.h"
#include "utils.h"
//--------------------------------- added ---------------------------------

// #include "Tree.h"
    

int main(int argc, char *argv[]) {
    Symtable *symtable;
    symtable = init_sym_table();

    // the lexer enters the initial state of the FSM
    Lexer *lexer = init_lexer(symtable);
    lexer_start(lexer);

    if(lexer->error != 0){
        return lexer->error;
    }
    
    // Syntactic analyser
    Syntactic *syntactic = init_syntactic(symtable);
    syntactic_start(syntactic, lexer);

    int main_declared = check_main_function(syntactic->symtable);
    if(main_declared != 0){
        //printf("Main not declared, code: %i\n", main_declared);
        return main_declared;
    }

    if(syntactic->error != 0){
         return syntactic->error;
    }

    Semantic *semantic = init_semantic(syntactic->symtable);
    traverse_tree(syntactic->tree->children[0], syntactic->symtable, semantic);
    if(semantic->error != 0){
         return semantic->error;
    } 

    // added ------------ Code generator ------------ //
    Generator *generator = init_generator(symtable);
    if (!generator) {
        return ERR_T_MALLOC_ERR;
    }
    generate_global_vars(generator);
    
    int gen_error = generator_start(generator, syntactic->tree);
    if (gen_error != 0) {
        generator_free(generator);
        return gen_error;
    }
    
    generator_free(generator);
    // added ------------ Code generator ------------ //




    /*      EXPRESSION TEST
    tree_node_t *t = rule_expression(syntactic, lexer);
    puts("\n========================================================\n");
    tree_print_tree(t, "", 1);
    */


    // after parsing the entire input
    // print_token_table(lexer); // print parsed tokens
    // print_symtable(symtable); // print the symbol table ODKOMENTOVAT !!!

    //get next token (for parser)
    /* print_token(get_next_token(lexer));
    print_token(get_lookahead_token(lexer));
    print_token(get_next_token(lexer));
    print_token(get_lookahead_token(lexer));
    print_token(get_next_token(lexer));
    print_token(get_lookahead_token(lexer));
    print_token(get_next_token(lexer));
    print_token(get_lookahead_token(lexer)); */







    /*Token *t1 = create_token(TOKEN_T_IDENTIFIER, "variable_a", 10, 1, 4);
    Token *t12 = create_token(TOKEN_T_IDENTIFIER, "variable_a", 10, 48, 16);
    Token *t123 = create_token(TOKEN_T_IDENTIFIER, "variable_b", 10, 51, 3);
    Token *t2 = create_token(TOKEN_T_NUM, "7.358", 5, 3, 12);
    Token *t3 = create_token(TOKEN_T_STRING, "stringovyliteral", 16, 27, 16);
    Token *t4 = create_token(TOKEN_T_KEYWORD, "if", 2, 11, 6);
    Token *t5 = create_token(TOKEN_T_OPERATOR, "+", 1, 21, 6);
    Token *t6 = create_token(TOKEN_T_GLOBAL_VAR, "__randomglobal", 14, 31, 2);

    add_token_to_token_table(lexer, t1);
    add_token_to_token_table(lexer, t2);
    add_token_to_token_table(lexer, t123);
    add_token_to_token_table(lexer, t2);
    add_token_to_token_table(lexer, t3);
    add_token_to_token_table(lexer, t4);
    add_token_to_token_table(lexer, t5);
    add_token_to_token_table(lexer, t6);

    print_token_table(lexer);*/

   /* printf("===================================\n");
    printf("==              TOKENY           ==\n");
    printf("===================================\n");
    print_token(t1);
    print_token(t2);
    print_token(t3);
    print_token(t4);
    print_token(t5);
    print_token(t6);

    printf("===================================\n");
    printf("==              SYMBOLY          ==\n");
    printf("===================================\n");
    Symbol *s1 = lexer_create_identifier_sym_from_token(t1);
    Symbol *s12 = lexer_create_identifier_sym_from_token(t12);
    Symbol *s123 = lexer_create_identifier_sym_from_token(t123);
    Symbol *s2 = lexer_create_global_var_sym_from_token(t6);
    Symbol *s3 = lexer_create_num_literal_sym_from_token(t2);
    Symbol *s4 = lexer_create_string_literal_sym_from_token(t3);

    print_symbol(s1);
    print_symbol(s12);
    print_symbol(s123);
    print_symbol(s2);
    print_symbol(s3);
    print_symbol(s4);

    Symtable *symtable = init_sym_table();

    insert_into_symtable(symtable, s1);
    insert_into_symtable(symtable, s12);
    insert_into_symtable(symtable, s123);
    insert_into_symtable(symtable, s2);
    insert_into_symtable(symtable, s3);
    insert_into_symtable(symtable, s4);
    
    print_symtable(symtable);

    free_token(t1);
    free_token(t2);
    free_token(t3);
    free_token(t4);
    free_token(t5);
    free_token(t6);*/

    return 0;
}
