#ifndef LEXER_H
#define LEXER_H

#include "Token.h"
#include "Symtable.h"

typedef struct lexer {
    int current_col;
    int current_row;

    Symtable *symtable;

    int error;

    int scope;

    int lexeme_length;
    char *lexeme;
    char current_char;

    Token *token_table;
    int token_count;

    int token_index;

    int newlines_in_multiline;

    int left_multiline_comment_start_sequence;
    int left_multiline_comment_end_sequence;
} Lexer;

Lexer *init_lexer(Symtable *symtable); 
void extend_lexeme(Lexer *lexer, char curent_char);
int lexer_start(Lexer *lexer);
void add_token_to_token_table(Lexer *lexer, Token *token);
void print_token_table(Lexer *lexer);

Token *get_next_token(Lexer *lexer);
Token *get_lookahead_token(Lexer *lexer);


int final_state_end_of_line(Lexer *lexer);
int final_state_comma(Lexer *lexer);
int final_state_brackets(Lexer *lexer);


int final_state_identif(Lexer *lexer);
int final_state_keyword(Lexer *lexer);
int state_id_start(Lexer *lexer);
int state_id_read(Lexer *lexer);

int final_state_global_identif(Lexer *lexer);
int state_global1(Lexer *lexer);
int state_global2(Lexer *lexer);
int state_global3(Lexer *lexer);

int final_state_number(Lexer *lexer);
int state_zero(Lexer *lexer);
int state_digit(Lexer *lexer);
int state_hex_prefix(Lexer *lexer);
int state_hex_numba(Lexer *lexer);
int state_dot_arrived(Lexer *lexer);
int state_decimal_part(Lexer *lexer);
int state_exponent_prefix(Lexer *lexer);
int state_exponent(Lexer *lexer);



int final_state_string(Lexer *lexer);
int state_string_reading(Lexer *lexer);
int state_special_symbol(Lexer *lexer);
int state_string_hex_prefix(Lexer *lexer);
int state_string_hex_number(Lexer *lexer);
int state_multiline_string_reading(Lexer *lexer);
int state_multiline_string1(Lexer *lexer);
int state_multiline_string2(Lexer *lexer);
int state_multiline_special_symbol(Lexer *lexer);
int state_multiline_string_hex_prefix(Lexer *lexer);
int state_multiline_string_hex_number(Lexer *lexer);



int final_state_comment(Lexer *lexer);
int state_comment_start(Lexer *lexer);
int state_comment_reading(Lexer *lexer);
int state_multiline_comment_reading(Lexer *lexer);
int state_reading_comment_start_sequence(Lexer *lexer);
int state_reading_comment_end_sequence(Lexer *lexer);


int final_state_operator(Lexer *lexer);
int state_two_char_operator(Lexer *lexer);
int state_exclamation_operator(Lexer *lexer);




#endif


