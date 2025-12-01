#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Generator.h"
#include "symbol.h"
#include "symtable.h"
#include "token.h"
#include "tree.h"
#include "utils.h"

// Inicializácia generátora
Generator *init_generator(Symtable *symtable) {
  Generator *gen = malloc(sizeof(Generator));
  if (!gen) {
    return NULL;
  }

  gen->symtable = symtable;
  gen->label_counter = 0;
  gen->temp_var_counter = 0;
  gen->current_scope = 0;
  gen->current_function = NULL; // Názov aktuálnej funkcie
  gen->output = NULL;           // NULL = stdout
  gen->error = 0;
  gen->in_function = 0;
  gen->variable = NULL;
  gen->is_global = false;
  gen->string_variable = NULL;
  gen->is_called = false;
  gen->is_float = false;
  gen->has_return = false;
  gen->buffer = NULL;
  gen->buffer_len = 0;
  gen->buffer_cap = 0;
  gen->buffering = 0;
  gen->in_while_loop = 0;
  gen->global_vars = NULL;
  gen->global_count = 0;
  gen->tf_created = false;
  gen->function_params = NULL;
  gen->function_param_count = 0;

  return gen;
}

/*
 * 0 - FUNCTION
 * 1 - GETTER
 * 2 - SETTER
 */
int diverse_function(Generator *generator, tree_node_t *node) {
  if (node->children_count >= 2 &&
      node->children[1]->rule == GR_GETTER_DECLARATION) {
    return 1;
  } else if (node->children_count >= 2 &&
             node->children[1]->rule == GR_SETTER_DECLARATION) {
    return 2;
  } else {
    return 0;
  }
}

char **generate_global_vars(Generator *generator) {
  if (!generator || !generator->symtable) {
    return NULL;
  }

  Symtable *symtable = generator->symtable;
  int capacity = 10;
  int count = 0;
  char **global_vars = malloc(capacity * sizeof(char *));

  if (!global_vars) {
    generator->error = ERR_T_MALLOC_ERR;
    return NULL;
  }

  for (int i = 0; i < symtable->symtable_size; i++) {
    if (symtable->symtable_rows[i].symbol != NULL) {
      Symbol *sym = symtable->symtable_rows[i].symbol;
      if (sym->is_global && sym->sym_identif_type == IDENTIF_T_VARIABLE) {
        // Check for duplicates
        int exists = 0;
        for (int k = 0; k < count; k++) {
            if (strcmp(global_vars[k], sym->sym_lexeme) == 0) {
                exists = 1;
                break;
            }
        }

        if (!exists) {
            if (count >= capacity) {
              capacity *= 2;
              char **temp = realloc(global_vars, capacity * sizeof(char *));
              if (!temp) {
                free(global_vars);
                generator->error = ERR_T_MALLOC_ERR;
                return NULL;
              }
              global_vars = temp;
            }
            global_vars[count++] = sym->sym_lexeme;
        }
      }
    }
  }

  generator->global_count = count;
  generator->global_vars = global_vars;
  return global_vars;
}


// Uvoľnenie generátora
void generator_free(Generator *generator) {
  if (generator) {
    if (generator->current_function) {
      free(generator->current_function);
    }
    if (generator->string_variable) {
      free(generator->string_variable);
    }
    if (generator->global_vars) {
      free(generator->global_vars);
    }
    free(generator);
  }
}

// Generovanie jedinečného labelu ---
char *get_next_label(Generator *generator) {
  char *label = malloc(32);
  if (!label) {
    generator->error = ERR_T_MALLOC_ERR;
    return NULL;
  }
  sprintf(label, "LABEL_%d", generator->label_counter++); 
  return label;
}

char *get_temp_var(Generator *generator) {
  char *temp = malloc(32);
  if (!temp) {
    generator->error = ERR_T_MALLOC_ERR;
    return NULL;
  }
  if (generator->tf_created) {
    sprintf(temp, "TF@tmp_%d", generator->temp_var_counter++);
  } else {
    generator_emit(generator, "CREATEFRAME");
    generator->tf_created = true;
    sprintf(temp, "TF@tmp_%d", generator->temp_var_counter++);
  }
  return temp;
}

// Emitovanie inštrukcie (s podporou variadických argumentov) --- OKEY ---
void generator_emit(Generator *generator, const char *format, ...) {
  FILE *out = generator->output ? generator->output : stdout;

  va_list args;
  va_start(args, format);
  vfprintf(out, format, args);
  va_end(args);

  fprintf(out, "\n");
}

// Pomocná funkcia: kontrola či je číslo celé číslo (int) --- OKEY ---
static int is_integer(const char *num_str) {
  if (!num_str || strlen(num_str) == 0)
    return 0;

  // Skontrolovať či obsahuje desatinnú bodku alebo exponent
  int has_dot = 0;
  int has_exp = 0;

  for (int i = 0; num_str[i] != '\0'; i++) {
    if (num_str[i] == '.') {
      has_dot = 1;
    } else if (num_str[i] == 'e' || num_str[i] == 'E') {
      has_exp = 1;
    }
  }

  // Ak nemá bodku ani exponent, je to int
  return !has_dot && !has_exp;
}

// konverzia float na hexadecimálny formát--- OKEY ---
static char *convert_float_to_hex_format(const char *float_str) {
  if (!float_str)
    return NULL;

  double value = strtod(float_str, NULL);

  // Alokovať buffer pre hex formát
  char *hex_str = malloc(64);
  if (!hex_str)
    return NULL;

  // Konvertovať na hex formát pomocou %a formátu
  // %a generuje hexadecimálny floating point formát
  snprintf(hex_str, 64, "%a", value);

  return hex_str;
}

// konverzia stringu s escape sekvenciami --- OKEY ---
static char *convert_string(const char *str) {
  if (!str)
    return NULL;

  // Alokovať buffer (max 4x veľkosť pre escape sekvencie)
  int len = strlen(str);
  char *result = malloc(len * 4 + 1);
  if (!result)
    return NULL;

  int pos = 0;
  int i = 0;

  while (i < len) {
    unsigned char c = (unsigned char)str[i];

    // Spracovať escape sekvencie z lexera
    if (c == '\\' && i + 1 < len) {
      char next = str[i + 1];
      switch (next) {
      case 'n':
        // Nový riadok -> \010
        result[pos++] = '\\';
        result[pos++] = '0';
        result[pos++] = '1';
        result[pos++] = '0';
        i += 2;
        continue;
      case 't':
        // Tab -> \009
        result[pos++] = '\\';
        result[pos++] = '0';
        result[pos++] = '0';
        result[pos++] = '9';
        i += 2;
        continue;
      case 'r':
        // Carriage return -> \013 --- CHECK ---
        result[pos++] = '\\';
        result[pos++] = '0';
        result[pos++] = '1';
        result[pos++] = '3';
        i += 2;
        continue;
      case '\\':
        // Spätné lomítko -> \092
        result[pos++] = '\\';
        result[pos++] = '0';
        result[pos++] = '9';
        result[pos++] = '2';
        i += 2;
        continue;
      case '"':
        // Úvodzovka -> \034
        result[pos++] = '\\';
        result[pos++] = '0';
        result[pos++] = '3';
        result[pos++] = '4';
        i += 2;
        continue;
      case 'x':
        // Hex escape sekvencia \xHH - nechat ako je
        if (i + 3 < len && isxdigit(str[i + 2]) && isxdigit(str[i + 3])) {
          result[pos++] = '\\';
          result[pos++] = 'x';
          result[pos++] = str[i + 2];
          result[pos++] = str[i + 3];
          i += 4;
          continue;
        }
        break;
      }
    }

    // Špeciálne znaky
    if (c == ' ') {
      // Medzera -> \032
      result[pos++] = '\\';
      result[pos++] = '0';
      result[pos++] = '3';
      result[pos++] = '2';
    } else if (c == '\n') {
      // Nový riadok -> \010
      result[pos++] = '\\';
      result[pos++] = '0';
      result[pos++] = '1';
      result[pos++] = '0';
    } else if (c == '#') {
      // Mriežka -> \035
      result[pos++] = '\\';
      result[pos++] = '0';
      result[pos++] = '3';
      result[pos++] = '5';
    } else if (c == '\\') {
      // Spätné lomítko -> \092
      result[pos++] = '\\';
      result[pos++] = '0';
      result[pos++] = '9';
      result[pos++] = '2';
    } else if (c < 32 || c > 126) {
      // Ostatné netlačiteľné znaky -> \XXX (desiatkový ASCII kód) --- CHECK ---
      result[pos++] = '\\';
      int ascii = (int)c;
      if (ascii < 10) {
        result[pos++] = '0';
        result[pos++] = '0';
        result[pos++] = '0' + ascii;
      } else if (ascii < 100) {
        result[pos++] = '0';
        result[pos++] = '0' + (ascii / 10);
        result[pos++] = '0' + (ascii % 10);
      } else {
        result[pos++] = '0' + (ascii / 100);
        result[pos++] = '0' + ((ascii / 10) % 10);
        result[pos++] = '0' + (ascii % 10);
      }
    } else {
      // normalny znak --- OKEY ---
      result[pos++] = c;
    }

    i++;
  }

  result[pos] = '\0';
  return result;
}

// Získanie inštrukcie pre operátor
const char *get_operator_instruction(const char *operator) {
  if (strcmp(operator, "+") == 0)
    return "ADDS";
  if (strcmp(operator, "-") == 0)
    return "SUBS";
  if (strcmp(operator, "*") == 0)
    return "MULS";
  if (strcmp(operator, "/") == 0)
    return "DIVS";
  if (strcmp(operator, "==") == 0)
    return "EQS";
  if (strcmp(operator, "!=") == 0)
    return "NEQS";
  if (strcmp(operator, "<") == 0)
    return "LTS";
  if (strcmp(operator, ">") == 0)
    return "GTS";
  if (strcmp(operator, "<=") == 0)
    return "LTEQS";
  if (strcmp(operator, ">=") == 0)
    return "GTEQS";
  if (strcmp(operator, "&&") == 0)
    return "ANDS";
  if (strcmp(operator, "||") == 0)
    return "ORS";
  if (strcmp(operator, "is") == 0)
    return "EQS"; // Operátor "is" používa EQS --- CHECK --- kde je not alebo
                  // while atd.
  return NULL;
}

// Helper function to search for getter/setter with prefix (compatible with kebab_compiler22)
// Returns the symbol if found, NULL otherwise
static Symbol *search_prefixed_symbol(Symtable *symtable, const char *base_name, const char *prefix, IDENTIF_TYPE target_type) {
  if (!symtable || !base_name || !prefix) {
    return NULL;
  }

  // Build prefixed name
  size_t prefix_len = strlen(prefix);
  size_t base_len = strlen(base_name);
  char *prefixed_name = malloc(prefix_len + base_len + 1);
  if (!prefixed_name) {
    return NULL;
  }
  sprintf(prefixed_name, "%s%s", prefix, base_name);

  // Search through symtable (similar to search_table_for_setter_or_getter)
  Symbol *result = NULL;
  for (int i = 0; i < symtable->symtable_size; i++) {
    Symbol *sym = symtable->symtable_rows[i].symbol;
    if (!sym || !sym->sym_lexeme) continue;

    if (strcmp(sym->sym_lexeme, prefixed_name) == 0) {
      if (sym->sym_identif_type == target_type) {
        result = sym;
        break;
      }
    }
  }

  free(prefixed_name);
  return result;
}

// Generovanie strcmp-like porovnania (vracia -1, 0, alebo 1)
// Očakáva dva stringy na zásobníku (prvý string je hlbšie, druhý je na vrchole)
// Výsledok (-1, 0, alebo 1) je na zásobníku
// Používa TF@ (Temporary Frame) pre dočasné premenné podľa špecifikácie
void generate_strcmp_comparison(Generator *generator, tree_node_t *node) {
  if (!generator || !node)
    return;

  // Generate arguments
  for (int i = 0; i < node->children_count; i++) {
    tree_node_t *child = node->children[i];
    // Skip function name node (usually first child, but check pointer or type)
    
    if (child->type == NODE_T_TERMINAL)
        continue;

    if (child->nonterm_type == NONTERMINAL_T_EXPRESSION ||
        child->nonterm_type == NONTERMINAL_T_FUN_PARAM) {
      generate_expression(generator, child);
    }
  }

  // Uložiť stringy do dočasných premenných v TF@ (Temporary Frame)
  char *temp_var1 = get_temp_var(generator);
  char *temp_var2 = get_temp_var(generator);

  if (!temp_var1 || !temp_var2) {
    if (temp_var1)
      free(temp_var1);
    if (temp_var2)
      free(temp_var2);
    return;
  }

  // Na zásobníku sú: [str1, str2] (str2 je na vrchole)
  // Uložiť str2 do temp2 (popne sa ako prvý, lebo je na vrchole)
  generator_emit(generator, "DEFVAR %s", temp_var2);
  generator_emit(generator, "POPS %s", temp_var2);
  // Uložiť str1 do temp1
  generator_emit(generator, "DEFVAR %s", temp_var1);
  generator_emit(generator, "POPS %s", temp_var1);

  // Generovať labely pre skoky
  char *label_equal = get_next_label(generator);
  char *label_greater = get_next_label(generator);
  char *label_end = get_next_label(generator);

  // Skontrolovať či str1 < str2
  generator_emit(generator, "PUSHS %s", temp_var1);
  generator_emit(generator, "PUSHS %s", temp_var2);
  generator_emit(generator, "LTS");
  generator_emit(generator, "PUSHS bool@false");
  generator_emit(generator, "JUMPIFEQS %s", label_equal);
  // str1 < str2, vrátiť -1
  generator_emit(generator, "PUSHS int@-1");
  generator_emit(generator, "JUMP %s", label_end);

  // Skontrolovať či str1 == str2
  generator_emit(generator, "LABEL %s", label_equal);
  generator_emit(generator, "PUSHS %s", temp_var1);
  generator_emit(generator, "PUSHS %s", temp_var2);
  generator_emit(generator, "EQS");
  generator_emit(generator, "PUSHS bool@false");
  generator_emit(generator, "JUMPIFEQS %s", label_greater);
  // str1 == str2, vrátiť 0
  generator_emit(generator, "PUSHS int@0");
  generator_emit(generator, "JUMP %s", label_end);

  // str1 > str2, vrátiť 1
  generator_emit(generator, "LABEL %s", label_greater);
  generator_emit(generator, "PUSHS int@1");

  generator_emit(generator, "LABEL %s", label_end);

  free(temp_var1);
  free(temp_var2);
  free(label_equal);
  free(label_greater);
  free(label_end);
}


static int get_scope_suffix(Generator *generator, Token *token) {
  if (!token) return generator->current_scope;
  
  // Collect all symbols with matching name
  Symbol *candidates[32];
  int candidate_count = 0;
  for (int i = 0; i < generator->symtable->symtable_size && candidate_count < 32; i++) {
      Symbol *s = generator->symtable->symtable_rows[i].symbol;
      if (!s || !s->sym_lexeme) continue;
      if (strcmp(s->sym_lexeme, token->token_lexeme) == 0 && 
          s->sym_identif_type == IDENTIF_T_VARIABLE && !s->is_global && !s->is_parameter) {
          candidates[candidate_count++] = s;
      }
  }
  
  if (candidate_count == 0) {
  return generator->current_scope;
}

  int token_scope = token->scope;
  int token_line = token->token_line_number;
  
  // Simple strategy: Check if variable is declared in current scope BEFORE this usage
  // If yes, use current scope. If no, use the highest parent scope that has a declaration.
  
  // First: Check if declared in current scope BEFORE the token
  for (int c = 0; c < candidate_count; c++) {
      Symbol *s = candidates[c];
      if (!s->sym_identif_declaration_count) continue;
      
      for (int i = 0; i < s->sym_identif_declaration_count; i++) {
          if (s->sym_identif_declared_at_scope_arr[i] == token_scope) {
              int decl_line = s->sym_identif_declared_at_line_arr[i];
              if (decl_line < token_line) {
                  // Found declaration in current scope before usage - use it
                  return token_scope;
              }
          }
      }
  }
  
  // Not declared in current scope before usage - find declaration in parent scopes
  // Build list of parent scopes
  int parent_scopes[64];
  int parent_count = 0;
  
  if (token->previous_scope_arr && token->scope_count > 0) {
      for (int j = 0; j < token->scope_count; j++) {
          parent_scopes[parent_count++] = token->previous_scope_arr[j];
      }
  }
  
  // Also check generator->current_scope if it's different and might be a parent
  if (generator->current_scope != token_scope) {
      // Check if it's already in parent_scopes
      bool found = false;
      for (int j = 0; j < parent_count; j++) {
          if (parent_scopes[j] == generator->current_scope) {
              found = true;
              break;
          }
      }
      if (!found) {
          parent_scopes[parent_count++] = generator->current_scope;
      }
  }
  
  // Find the highest scope number (most recent) among parent scopes that has a declaration
  // IMPORTANT: Only consider declarations that occur BEFORE the usage line
  int best_parent_scope = -1;
  for (int c = 0; c < candidate_count; c++) {
      Symbol *s = candidates[c];
      if (!s->sym_identif_declaration_count) continue;
      
      for (int i = 0; i < s->sym_identif_declaration_count; i++) {
          int decl_scope = s->sym_identif_declared_at_scope_arr[i];
          int decl_line = s->sym_identif_declared_at_line_arr[i];
          
          // Skip declarations that occur AFTER the usage line
          if (decl_line >= token_line) {
              continue;
          }
          
          // Check if this declaration scope is in parent scopes
          for (int p = 0; p < parent_count; p++) {
              if (parent_scopes[p] == decl_scope) {
                  // Found declaration in a parent scope BEFORE usage
                  // Prefer the highest scope number (most recent/closest parent)
                  if (best_parent_scope < 0 || decl_scope > best_parent_scope) {
                      best_parent_scope = decl_scope;
                  }
              }
          }
      }
  }
  
  if (best_parent_scope >= 0) {
      return best_parent_scope;
  }
  
  // Fallback: return the most recent declaration from first candidate
  Symbol *sym = candidates[0];
  if (sym && sym->sym_identif_declaration_count > 0) {
      return sym->sym_identif_declared_at_scope_arr[sym->sym_identif_declaration_count - 1];
  }
  
  // Last resort: return current scope
  return generator->current_scope;
}

// Helper function to check if a variable name is a parameter of the current function
static bool is_function_parameter(Generator *generator, const char *var_name) {
  if (!generator->in_function || !var_name || !generator->function_params) {
    return false;
  }
  
  // Check against stored parameter names
  for (int i = 0; i < generator->function_param_count; i++) {
    if (generator->function_params[i] && strcmp(generator->function_params[i], var_name) == 0) {
      return true;
    }
  }
  
  return false;
}

// Helper function to format local variable name (parameters use no suffix, locals use suffix)
static void format_local_var(Generator *generator, Token *token, char *buffer, size_t buffer_size) {
  if (!token) {
    snprintf(buffer, buffer_size, "LF@");
    return;
  }
  
  // Check if it's a function parameter using our stored parameter list
  // This is the only reliable way since symbol table lookup has scope issues
  if (generator->in_function && is_function_parameter(generator, token->token_lexeme)) {
    // Parameters: no suffix
    snprintf(buffer, buffer_size, "LF@%s", token->token_lexeme);
    return;
  }
  
  // Local variables: with scope suffix
  int scope = get_scope_suffix(generator, token);
  snprintf(buffer, buffer_size, "LF@%s$%d", token->token_lexeme, scope);
}


// Generovanie terminalu (identifikátor, literál, operátor) --- OKEY ---
void generate_terminal(Generator *generator, tree_node_t *node) {
  if (!node || !node->token) {
    return;
  }
  Token *token = node->token;

  switch (token->token_type) {
  case TOKEN_T_NUM: {
    // Vždy konvertovať na float hex formát
    char *hex_str = convert_float_to_hex_format(token->token_lexeme);
    if (hex_str) {
      generator->is_float = true;
      generator_emit(generator, "PUSHS float@%s", hex_str);
      free(hex_str);
    } else {
      generator->error = ERR_T_MALLOC_ERR;
    }
    break;
  }
  case TOKEN_T_STRING: {
    // Reťazec môže mať úvodzovky - odstrániť ich ak existujú
    char *str_value = token->token_lexeme;
    char *clean_str = NULL;

    // Odstrániť úvodzovky na začiatku a konci ak existujú
    if (str_value[0] == '"' && str_value[strlen(str_value) - 1] == '"') {
      // Vytvoriť kópiu bez úvodzoviek
      int len = strlen(str_value) - 2;
      clean_str = malloc(len + 1);
      if (clean_str) {
        strncpy(clean_str, str_value + 1, len);
        clean_str[len] = '\0';
      }
    } else {
      // Kópia pôvodného stringu
      clean_str = malloc(strlen(str_value) + 1);
      if (clean_str) {
        strcpy(clean_str, str_value);
      }
    }

    if (clean_str) {
      // Konvertovať na string s escape sekvenciami
      char *converted = convert_string(clean_str);
      if (converted) {
        // Uvoľniť starú hodnotu ak existuje
        if (generator->string_variable) {
          free(generator->string_variable);
        }
        // Uložiť kópiu pre generator->string_variable (nesmie sa uvoľniť)
        generator->string_variable = malloc(strlen(converted) + 1);
        if (generator->string_variable) {
          strcpy(generator->string_variable, converted);
        }
        generator_emit(generator, "PUSHS string@%s", converted);
        free(converted);
      } else {
        // Uvoľniť starú hodnotu ak existuje
        if (generator->string_variable) {
          free(generator->string_variable);
        }
        // Uložiť kópiu pre generator->string_variable (nesmie sa uvoľniť)
        generator->string_variable = malloc(strlen(clean_str) + 1);
        if (generator->string_variable) {
          strcpy(generator->string_variable, clean_str);
        }
        generator->error = ERR_T_MALLOC_ERR; // --- CHECK --- odkial su ERRORY
        generator_emit(generator, "PUSHS string@%s", clean_str);
      }
      free(clean_str);
    } else {
      generator->error = ERR_T_MALLOC_ERR;
      // Uvoľniť starú hodnotu ak existuje
      if (generator->string_variable) {
        free(generator->string_variable);
      }
      generator->string_variable = NULL;
      generator_emit(generator, "PUSHS string@%s", str_value);
    }
    break;
  }
  case TOKEN_T_KEYWORD: {
    // Spracovať bool literály a nil --- OKEY ---
    if (strcmp(token->token_lexeme, "true") == 0) {
      generator_emit(generator, "PUSHS bool@true");
    } else if (strcmp(token->token_lexeme, "false") == 0) {
      generator_emit(generator, "PUSHS bool@false");
    } else if (strcmp(token->token_lexeme, "null") == 0) {
      generator_emit(generator, "PUSHS nil@nil");
    }
    // Ostatné keywords sa nespracovávajú tu --- CHECK --- ci naozaj
    break;
  }
  case TOKEN_T_IDENTIFIER:
  case TOKEN_T_GLOBAL_VAR: {
    // Vyhľadanie symbolu v symbolovej tabuľke
    Symbol *sym = search_table(
        token, generator->symtable); // --- CHECK --- preco to je dole
    
    if (sym) {
    }
    
    // Ak sa nenašiel symbol alebo to nie je lokálna premenná/parameter, skúsime nájsť getter
    int is_getter = 0;
    if (!sym || (sym->sym_identif_type != IDENTIF_T_VARIABLE && sym->sym_identif_type != IDENTIF_T_FUNCTION)) {
        // Skúsiť nájsť getter s prefixom "getter+" pomocou helper funkcie
        Symbol *getter_sym = search_prefixed_symbol(generator->symtable, token->token_lexeme, "getter+", IDENTIF_T_GETTER);
        if (getter_sym) {
            sym = getter_sym;
            is_getter = 1;
        }
    }

    if (sym) {
      // Ak je to getter, volať ho ako funkciu bez parametrov
      fprintf(stderr, "DEBUG GETTER: sym_identif_type=%d\ntoken: %s\n %d\n", sym->sym_identif_type, token->token_lexeme, is_getter);

      if (sym->sym_identif_type == IDENTIF_T_GETTER || is_getter) {
        generator->is_called = true;
        // Pre getter voláme funkciu s pôvodným názvom (bez prefixu ak sme ho našli cez prefix)
        // Alebo s názvom funkcie, ktorý sa vygeneroval (treba skontrolovať ako sa generujú labely pre gettery)
        // V generate_function_declaration sa pre getter generuje label s pôvodným názvom (bez prefixu) + "_"
        generator_emit(generator, "CALL %s_", token->token_lexeme);
        generator->is_called = false;  // Reset after call

      } else if (sym->is_global) { // co je is_global a kde to je definovane
        generator_emit(generator, "PUSHS GF@%s", token->token_lexeme);
      } else {
        char var_name[256];
        format_local_var(generator, token, var_name, sizeof(var_name));
        generator_emit(generator, "PUSHS %s", var_name);
      }
    } else { // ---- DOUBLE CHECK ---- podla zadania skontrolovat ci ak sa
             // nenajde symbol ma to byt GF alebo LF
      char var_name[256];
      format_local_var(generator, token, var_name, sizeof(var_name));
      generator_emit(generator, "PUSHS %s", var_name);
    }
    break;
  }
  case TOKEN_T_OPERATOR:
    // Operátory sa spracovávajú v generate_expression
    break;
  default:
    break;
  }
}

// generovanie "is" pre typovú kontrolu
static void generate_type_check(Generator *generator, tree_node_t *node) {
  if (!node || node->children_count < 2)
    return;

  // Vygenerovať ľavý operand (hodnota)
  generate_expression(generator, node->children[0]);

  // Skontrolovať pravý operand (typ - napr. Null, Num, String)
  tree_node_t *right_operand = node->children[1];
  if (right_operand && right_operand->type == NODE_T_TERMINAL &&
      right_operand->token &&
      right_operand->token->token_type == TOKEN_T_KEYWORD) {
    
    if (strcmp(right_operand->token->token_lexeme, "Num") == 0) {
      // Pre typ Num skontrolujeme či je to int alebo float
      // Stack: [value] -> TYPES -> [type_string]
      generator_emit(generator, "TYPES");
      
      char *type_var = get_temp_var(generator);
      generator_emit(generator, "DEFVAR %s", type_var);
      generator_emit(generator, "POPS %s", type_var);

      // Check int
      generator_emit(generator, "PUSHS %s", type_var);
      generator_emit(generator, "PUSHS string@int");
      generator_emit(generator, "EQS"); 

      // Check float
      generator_emit(generator, "PUSHS %s", type_var);
      generator_emit(generator, "PUSHS string@float");
      generator_emit(generator, "EQS"); 

      // OR
      generator_emit(generator, "ORS");
      
      free(type_var);
    } else if (strcmp(right_operand->token->token_lexeme, "String") == 0) {
      // Pre typ String použijeme TYPES + EQS s string@string
      generator_emit(generator, "TYPES");
      generator_emit(generator, "PUSHS string@string");
      generator_emit(generator, "EQS");
    } else if (strcmp(right_operand->token->token_lexeme, "Null") == 0) {
      // Pre typ Null použijeme priame porovnanie s nil@nil
      generator_emit(generator, "PUSHS nil@nil");
      generator_emit(generator, "EQS");
    } else {
        // Fallback pre iné typy (ak existujú)
        generate_expression(generator, node->children[1]);
        generator_emit(generator, "TYPES");
        generator_emit(generator, "EQS");
    }
  } else {
    // Pre ostatné prípady (nie keyword)
    generate_expression(generator, node->children[1]);
    generator_emit(generator, "TYPES");
    generator_emit(generator, "EQS");
  }
}

// Forward declaration
static bool is_number_type(Generator *generator, tree_node_t *node);

// Helper to check if a node evaluates to a string
static bool is_string_type(Generator *generator, tree_node_t *node) {
  if (!node) return false;

  if (node->type == NODE_T_TERMINAL) {
    if (node->token) {
      if (node->token->token_type == TOKEN_T_STRING) return true;
      if (node->token->token_type == TOKEN_T_IDENTIFIER || node->token->token_type == TOKEN_T_GLOBAL_VAR) {
        Symbol *sym = search_table(node->token, generator->symtable);
        if (sym) {
          fprintf(stderr, "DEBUG: sym_variable_type=%d\ntoken: %s\n", sym->sym_variable_type, node->token->token_lexeme);
          if (sym->sym_variable_type == VAR_T_STRING) return true;
        }
      }
    }
    return false;
  }

  // Binary +
  if (node->token && strcmp(node->token->token_lexeme, "+") == 0 && node->children_count == 2) {
    // If either operand is a string, the result is a string (concatenation)
    return is_string_type(generator, node->children[0]) || is_string_type(generator, node->children[1]);
  }

  // Binary * with string
  if (node->token && strcmp(node->token->token_lexeme, "*") == 0 && node->children_count == 2) {
    // If left is string and right is number, result is string
    if (is_string_type(generator, node->children[0]) && is_number_type(generator, node->children[1])) {
      return true;
    }
  }

  // Function calls
  if (node->nonterm_type == NONTERMINAL_T_FUN_CALL || node->nonterm_type == NONTERMINAL_T_EXPRESSION_OR_FN) {
    if (node->children_count > 0 && node->children[0]->token) {
      char *name = node->children[0]->token->token_lexeme;
      if (strcmp(name, "read_str") == 0 || strcmp(name, "str") == 0 || 
          strcmp(name, "substring") == 0 || strcmp(name, "chr") == 0) {
        return true;
      }
    }
  }
  
  return false;
}

// Helper to check if a node evaluates to a number (int or float)
static bool is_number_type(Generator *generator, tree_node_t *node) {
  if (!node) return false;

  if (node->type == NODE_T_TERMINAL) {
    if (node->token) {
      if (node->token->token_type == TOKEN_T_NUM) return true;
      if (node->token->token_type == TOKEN_T_IDENTIFIER || node->token->token_type == TOKEN_T_GLOBAL_VAR) {
        Symbol *sym = search_table(node->token, generator->symtable);
        if (sym && sym->sym_variable_type == VAR_T_NUM) return true;
      }
    }
    return false;
  }

  // Binary arithmetic operations with numbers result in numbers
  if (node->token && node->children_count == 2) {
    const char *op = node->token->token_lexeme;
    if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0 || 
        strcmp(op, "*") == 0 || strcmp(op, "/") == 0) {
      // If both operands are numbers, result is number
      if (is_number_type(generator, node->children[0]) && is_number_type(generator, node->children[1])) {
        return true;
      }
    }
  }

  // Function calls that return numbers
  if (node->nonterm_type == NONTERMINAL_T_FUN_CALL || node->nonterm_type == NONTERMINAL_T_EXPRESSION_OR_FN) {
    if (node->children_count > 0 && node->children[0]->token) {
      char *name = node->children[0]->token->token_lexeme;
      if (strcmp(name, "read_num") == 0 || strcmp(name, "floor") == 0 || 
          strcmp(name, "length") == 0 || strcmp(name, "ord") == 0) {
        return true;
      }
    }
  }
  
  return false;
}

// Helper to get operand string for CONCAT
static char *get_operand_for_concat(Generator *generator, tree_node_t *node) {
  if (node->type == NODE_T_TERMINAL) {
    Token *token = node->token;
    if (token->token_type == TOKEN_T_STRING) {
      char *str_value = token->token_lexeme;
      char *clean_str = NULL;
      if (str_value[0] == '"' && str_value[strlen(str_value) - 1] == '"') {
        int len = strlen(str_value) - 2;
        clean_str = malloc(len + 1);
        if (clean_str) {
          strncpy(clean_str, str_value + 1, len);
          clean_str[len] = '\0';
        }
      } else {
        clean_str = malloc(strlen(str_value) + 1);
        if (clean_str) strcpy(clean_str, str_value);
      }
      
      char *converted = convert_string(clean_str);
      free(clean_str);
      
      if (!converted) {
        return NULL;
      }
      
      char *result = malloc(strlen(converted) + 10);
      if (!result) {
        free(converted);
        return NULL;
      }
      sprintf(result, "string@%s", converted);
      free(converted);
      return result;
    } else if (token->token_type == TOKEN_T_IDENTIFIER || token->token_type == TOKEN_T_GLOBAL_VAR) {
       Symbol *sym = search_table(token, generator->symtable);
       
       bool is_getter = false;
       if (!sym || (sym->sym_identif_type != IDENTIF_T_VARIABLE && sym->sym_identif_type != IDENTIF_T_FUNCTION)) {
         Symbol *getter_sym = search_prefixed_symbol(generator->symtable, token->token_lexeme, "getter+", IDENTIF_T_GETTER);
         if (getter_sym) is_getter = true;
       }
       if (sym && sym->sym_identif_type == IDENTIF_T_GETTER) is_getter = true;
       
       if (!is_getter) {
         char *result = malloc(strlen(token->token_lexeme) + 30);
         if (!result) {
           return NULL;
         }
         if (sym && sym->is_global) {
           sprintf(result, "GF@%s", token->token_lexeme);
         } else {
          format_local_var(generator, token, result, strlen(token->token_lexeme) + 30);
         }
         return result;
       }
    }
  }
  
  generate_expression(generator, node);
  char *temp = get_temp_var(generator);
  if (!temp) {
    return NULL;
  }
  generator_emit(generator, "DEFVAR %s", temp);
  generator_emit(generator, "POPS %s", temp);
  return temp;
}

// Generovanie výrazu (rekurzívne, zásobníkový prístup)
void generate_expression(Generator *generator, tree_node_t *node) {
  if (!node) {
    return;
  }

  // DÔLEŽITÉ: Checkujeme operátor node PRVÝ, lebo type môže byť 0 (uninitialized) čo sa zhoduje s NODE_T_TERMINAL
  // Ak má operátor token a deti, je to určite výraz s operátorom, nie terminál
  // (Tento check sa robí nižšie v kóde, ale musíme preskočiť terminal check ak je to operátor)
  
  // Ak je to terminál (a nie operátor)
  if (node->type == NODE_T_TERMINAL && 
      !(node->token && node->token->token_type == TOKEN_T_OPERATOR && node->children_count > 0)) {
    generate_terminal(generator, node);
    return;
  }

  // Ak je to NONTERMINAL_T_PREDICATE, spracovať jeho dieťa (výraz)
  if (node->type == NODE_T_NONTERMINAL &&
      node->nonterm_type == NONTERMINAL_T_PREDICATE) {
    if (node->children_count > 0) {
      generate_expression(generator, node->children[0]);
    }
    return;
  }

  // Pre neterminálne uzly - prechádzame deti
  // Výraz môže mať operátor ako root a operandy ako deti
  // Poznámka: tree_init() nenastavuje type, takže checkujeme operátor token namiesto type
  if (node->children_count > 0) {
    // Ak má operátor ako token (binárny operátor)
    // Necheckujeme node->type lebo tree_init() ho nenastavuje
    if (node->token && node->token->token_type == TOKEN_T_OPERATOR) {
      // Spracovanie unárneho mínusu
      if (strcmp(node->token->token_lexeme, "-") == 0 &&
          node->children_count == 1) {
        generate_expression(generator, node->children[0]);
        generator_emit(generator, "NEGS");
        return;
      }
      // Spracovanie unárneho NOT
      if (strcmp(node->token->token_lexeme, "!") == 0 &&
          node->children_count == 1) {
        generate_expression(generator, node->children[0]);
        generator_emit(generator, "NOTS");
        return;
      }

      // Binárny operátor - najprv ľavý operand, potom pravý
      if (node->children_count >= 2) {
        // Špeciálne spracovanie pre operátor "is" (typová kontrola)
        if (strcmp(node->token->token_lexeme, "is") == 0) {
          generate_type_check(generator, node);
          return;
        }

        if (strcmp(node->token->token_lexeme, "+") == 0) {
             // If either operand is a string, use CONCAT
             if (is_string_type(generator, node->children[0]) || is_string_type(generator, node->children[1])) {
                 char *op1 = get_operand_for_concat(generator, node->children[0]);
                 char *op2 = get_operand_for_concat(generator, node->children[1]);
                 
                 char *temp_res = get_temp_var(generator);
                 generator_emit(generator, "DEFVAR %s", temp_res);
                 generator_emit(generator, "CONCAT %s %s %s", temp_res, op1, op2);
                 generator_emit(generator, "PUSHS %s", temp_res);
                 
                 free(op1);
                 free(op2);
                 free(temp_res);
                 return;
             }
        }

        if (strcmp(node->token->token_lexeme, "*") == 0) {
             // If left operand is string and right is number, repeat string
             fprintf(stderr, "DEBUG: childer[0]: %s\n", node->children[0]->token->token_lexeme);
             if (is_string_type(generator, node->children[0]) && is_number_type(generator, node->children[1])) {
                 // Generate string operand
                 char *str_op = get_operand_for_concat(generator, node->children[0]);
                 if (!str_op) {
                   generator->error = ERR_T_MALLOC_ERR;
                   return;
                 }
                 
                 // Generate number operand
                 generate_expression(generator, node->children[1]);
                 
                 // Convert to int if needed (FLOAT2INTS)
                 generator_emit(generator, "FLOAT2INTS");
                 
                 // Variables for the loop
                 char *str_var = get_temp_var(generator);
                 char *count_var = get_temp_var(generator);
                 char *result_var = get_temp_var(generator);
                 char *i_var = get_temp_var(generator);
                 char *cond_var = get_temp_var(generator);
                 
                 if (!str_var || !count_var || !result_var || !i_var || !cond_var) {
                   if (str_var) free(str_var);
                   if (count_var) free(count_var);
                   if (result_var) free(result_var);
                   if (i_var) free(i_var);
                   if (cond_var) free(cond_var);
                   free(str_op);
                   generator->error = ERR_T_MALLOC_ERR;
                   return;
                 }
                 
                 int label_idx = generator->label_counter++;
                 
                 // Define variables
                 generator_emit(generator, "DEFVAR %s", str_var);
                 generator_emit(generator, "DEFVAR %s", count_var);
                 generator_emit(generator, "DEFVAR %s", result_var);
                 generator_emit(generator, "DEFVAR %s", i_var);
                 generator_emit(generator, "DEFVAR %s", cond_var);
                 
                 // Pop operands: count (top)
                 generator_emit(generator, "POPS %s", count_var);
                 
                 // Load string operand (can be literal or variable reference)
                 generator_emit(generator, "MOVE %s %s", str_var, str_op);
                 
                 // Initialize result as empty string
                 generator_emit(generator, "MOVE %s string@", result_var);
                 
                 // Initialize counter i = 0
                 generator_emit(generator, "MOVE %s int@0", i_var);
                 
                 // Loop label
                 generator_emit(generator, "LABEL strmul_loop_%d", label_idx);
                 
                 // Check if i < count
                 generator_emit(generator, "LT %s %s %s", cond_var, i_var, count_var);
                 generator_emit(generator, "JUMPIFEQ strmul_end_%d %s bool@false", label_idx, cond_var);
                 
                 // Concatenate: result = result + str
                 generator_emit(generator, "CONCAT %s %s %s", result_var, result_var, str_var);
                 
                 // Increment i
                 generator_emit(generator, "ADD %s %s int@1", i_var, i_var);
                 
                 // Jump back to loop
                 generator_emit(generator, "JUMP strmul_loop_%d", label_idx);
                 
                 // End label
                 generator_emit(generator, "LABEL strmul_end_%d", label_idx);
                 
                 // Push result on stack
                 generator_emit(generator, "PUSHS %s", result_var);
                 
                 // Free variables
                 free(str_var);
                 free(count_var);
                 free(result_var);
                 free(i_var);
                 free(cond_var);
                 free(str_op);
                 return;
             }
        }

        generate_expression(generator, node->children[0]);
        generate_expression(generator, node->children[1]);

        const char *op_inst = get_operator_instruction(node->token->token_lexeme);
        if (op_inst) {
          generator_emit(generator, "%s", op_inst);
        } else {
          generator->error = ERR_T_SEMANTIC_ERR_BAD_OPERAND_TYPES;
        }
        return;
      }
    } else {
      // Inak rekurzívne spracuj všetky deti
      // Pre FUN_CALL a EXPRESSION_OR_FN použijeme generator_generate
      for (int i = 0; i < node->children_count; i++) {
        tree_node_t *child = node->children[i];
        if (child && child->type == NODE_T_NONTERMINAL) {
          if (child->nonterm_type == NONTERMINAL_T_FUN_CALL ||
              child->nonterm_type == NONTERMINAL_T_EXPRESSION_OR_FN) {
            generator_generate(generator, child);
          } else {
            generate_expression(generator, child);
          }
        } else {
          generate_expression(generator, child);
        }
      }
    }
  }
}

// Generovanie hlavného kódu --- OKEY ---
int generator_start(Generator *generator, tree_node_t *tree) {
  if (!generator || !tree) {
    return ERR_T_SYNTAX_ERR;
  }

  generator_emit(generator, ".IFJcode25");

  // Jump to main at the start to ensure execution begins at main
  generator_emit(generator, "JUMP main");

  // prechadzanie celeho stromu
  for (int i = 0; i < tree->children_count; i++) {
    generator_generate(generator, tree->children[i]);
  }

  return generator->error;
}

// Hlavná funkcia pre generovanie - rozdeľuje podľa typu uzla
void generator_generate(Generator *generator, tree_node_t *node) {
  if (!generator || !node) {
    return;
  }

  // Ak je to node terminal skip -> resi generate_terminal()
  if (node->type == NODE_T_TERMINAL) { // nodes su z Tree.c
    return;
  }

  // DELETE Rozdelenie podľa typu neterminalov
  switch (node->nonterm_type) {
  case NONTERMINAL_T_CODE_BLOCK:
    generate_code_block(generator, node);
    break;
  case NONTERMINAL_T_SEQUENCE:
    generate_sequence(generator, node);
    break;
  case NONTERMINAL_T_INSTRUCTION:
    generate_instruction(generator, node);
    break;
  case NONTERMINAL_T_EXPRESSION:
    generate_expression(generator, node);
    break;
  case NONTERMINAL_T_EXPRESSION_OR_FN:
    // Expression_or_function môže byť výraz alebo volanie funkcie
    // Rekurzívne spracovať deti - môže obsahovať FUN_CALL alebo EXPRESSION
    for (int i = 0; i < node->children_count; i++) {
      generator_generate(generator, node->children[i]);
    }
    break;
  case NONTERMINAL_T_ASSIGNMENT:
    generate_assignment(generator, node);
    break;
  case NONTERMINAL_T_DECLARATION:
    // Deklarácia môže byť premenná alebo funkcia - rozlíšiť podľa grammar rule
    if (node->rule == GR_FUN_DECLARATION) { // funkcia ?
      generate_function_declaration(generator, node);

    } else {
      generate_declaration(generator, node);
    }
    break;
  case NONTERMINAL_T_FUN_CALL:
    generate_function_call(generator, node);
    break;
  case NONTERMINAL_T_IF:
    generate_if(generator, node);
    break;
  case NONTERMINAL_T_WHILE:
    generate_while(generator, node);
    break;
  case NONTERMINAL_T_RETURN:
    generate_return(generator, node);
    break;
  case NONTERMINAL_T_PREDICATE:
    // Predikát je vlastne výraz -- CHECK -- is it ? xdd
    generate_expression(generator, node);
    break;
  default:
    // Pre ostatné typy - rekurzívne spracovanie ostatnych
    for (int i = 0; i < node->children_count; i++) {
      generator_generate(generator, node->children[i]);
    }
    break;
  }
}
// Generovanie bloku kódu --- OKEY ---
void generate_code_block(Generator *generator, tree_node_t *node) {
  if (!node)
    return;

  generator->current_scope++;

  // Prechádzanie - sekvencie alebo inštrukcie
  for (int i = 0; i < node->children_count; i++) {
    tree_node_t *child = node->children[i];

    // Preskočiť terminály ako {, }, \n
    if (child->type == NODE_T_TERMINAL) {
      Token *token = child->token;
      if (token &&
          (strcmp(token->token_lexeme, "{") == 0 ||
           strcmp(token->token_lexeme, "}") == 0 ||
           strcmp(token->token_lexeme, "n") == 0 ||
           strcmp(token->token_lexeme, "\n") == 0)) {
        continue;
      }
    }

    generator_generate(generator, child);
  }

  generator->current_scope--;
}

// Generovanie sekvencie --- OKEY ---
void generate_sequence(Generator *generator, tree_node_t *node) {
  if (!node)
    return;

  // Prechádzanie všetkých detí v sekvencii
  for (int i = 0; i < node->children_count; i++) {
    tree_node_t *child = node->children[i];

    // Preskočiť terminály ako \n
    if (child->type == NODE_T_TERMINAL) {
      Token *token = child->token;
      if (token && (strcmp(token->token_lexeme, "n") == 0 || strcmp(token->token_lexeme, "\n") == 0)) {
        continue;
      }
    }

    generator_generate(generator, child);
  }
}

// Generovanie inštrukcie - netreba nic skipovat, to filtruje tree --- OKEY ---
void generate_instruction(Generator *generator, tree_node_t *node) {
  if (!node)
    return;

  // Inštrukcia môže obsahovať rôzne typy uzlov
  for (int i = 0; i < node->children_count; i++) {
    generator_generate(generator, node->children[i]);
  }
}

// Generovanie priradenia --- ASI OKEY ---
void generate_assignment(Generator *generator, tree_node_t *node) {
  if (!node || node->children_count < 1)
    return;

  // Prvé dieťa je identifikátor - rule_assignment v Syntactic.c
  tree_node_t *id_node = node->children[0];
  if (!id_node || !id_node->token)
    return;

  Token *id_token = id_node->token;

  // Nájsť výraz (môže byť v ďalších deťoch)
  // Výraz môže byť NONTERMINAL_T_EXPRESSION, NONTERMINAL_T_EXPRESSION_OR_FN,
  // NONTERMINAL_T_FUN_CALL alebo terminálny uzol (identifikátor, číslo, string)
  tree_node_t *expr_node = NULL;
  for (int i = 1; i < node->children_count; i++) {
    tree_node_t *child = node->children[i];
    // Preskočiť terminálny uzol "="
    if (child->type == NODE_T_TERMINAL && child->token &&
        strcmp(child->token->token_lexeme, "=") == 0) {
      continue;
    }
    if (child->type ==
            NODE_T_NONTERMINAL && // podla rule_assignment v Syntactic.c
        (child->nonterm_type == NONTERMINAL_T_EXPRESSION ||
         child->nonterm_type == NONTERMINAL_T_EXPRESSION_OR_FN ||
         child->nonterm_type == NONTERMINAL_T_FUN_CALL)) {
      expr_node = child;
      break;
    } else if (child->type == NODE_T_TERMINAL) {
      // Terminálny uzol (identifikátor, číslo, string) je výraz
      expr_node = child;
      break;
    }
  }

  if (expr_node) {
    // Vyhľadať symbol pre premennú alebo setter
    Symbol *sym = search_table(id_token, generator->symtable);
    
    // Skúsiť nájsť setter s prefixom "setter+" pomocou helper funkcie
    Symbol *setter_sym = search_prefixed_symbol(generator->symtable, id_token->token_lexeme, "setter+", IDENTIF_T_SETTER);

    // Uložiť starú hodnotu premennej (ak existujú)
    char *old_variable = generator->variable;
    bool old_is_global = generator->is_global;

    // Nastaviť premennú PRED generovaním výrazu, aby funkcie vnútri výrazu
    if ((sym && sym->sym_identif_type != IDENTIF_T_SETTER) && !setter_sym) {
      generator->variable = id_token->token_lexeme;
      generator->is_global = (sym && sym->is_global) ? true : false;
    }

    // Vygenerovať výraz alebo volanie funkcie (vloží výsledok na zásobník)
    if (expr_node->nonterm_type == NONTERMINAL_T_FUN_CALL ||
               expr_node->nonterm_type == NONTERMINAL_T_EXPRESSION_OR_FN) {
      generator_generate(generator, expr_node);
    } else {
      generate_expression(generator, expr_node);
    }

    // Generovať inštrukciu pre priradenie
    if (setter_sym && setter_sym->sym_identif_type == IDENTIF_T_SETTER) {
      // Ak je to setter, volať ho ako funkciu s parametrom (hodnota je už na
      // zásobníku)
      generator->is_called = true;
      // Pre setter voláme funkciu s pôvodným názvom + "__" (podľa generate_function_declaration)
      generator_emit(generator, "CALL %s__", id_token->token_lexeme);
      // Obnoviť staré hodnoty (setter nepotrebuje nastavenú premennú)
      generator->variable = old_variable;
      generator->is_global = old_is_global;
    } else if (sym && sym->sym_identif_type == IDENTIF_T_SETTER) {
       // Fallback pre priame nájdenie (asi sa nestane ak sú prefixované)
       generator->is_called = true;
       generator_emit(generator, "CALL %s__", id_token->token_lexeme);
       generator->variable = old_variable;
       generator->is_global = old_is_global;
    } else if (sym && sym->is_global) {
      // generator->variable a generator->is_global sú už nastavené vyššie
      generator_emit(generator, "POPS GF@%s", id_token->token_lexeme);
    } else {
      // generator->variable a generator->is_global sú už nastavené vyššie
      // Parameters use no suffix, locals use suffix
      char var_name[256];
      format_local_var(generator, id_token, var_name, sizeof(var_name));
      generator_emit(generator, "POPS %s", var_name);
    }
  }
}

// Generovanie deklarácie --- OKEY ---
void generate_declaration(Generator *generator, tree_node_t *node) {
  if (!node)
    return;

  // Deklarácia môže byť premenná alebo funkcia
  // Kontrola gramatického pravidla
  if (node->rule == GR_FUN_DECLARATION) {
    generate_function_declaration(generator, node);
  } else if (node->rule == GR_DECLARATION) {
    // Deklarácia premennej - môže mať inicializáciu
    if (node->children_count > 0) {
      tree_node_t *id_node = node->children[0];
      if (id_node && id_node->token) {
        Token *id_token = id_node->token;

        // Vyhľadať symbol pre premennú
        Symbol *sym = search_table(id_token, generator->symtable);

        // Generovať DEFVAR pre premennú
        if (sym && sym->is_global) {
          // Global variables are handled separately
        } else if (!generator->in_while_loop) {
          // Only emit DEFVAR if not inside a while loop (already hoisted)
          // For declarations, use the token's own scope (where it's being declared)
          // Don't use get_scope_suffix which searches for previous declarations
          int scope = id_token->scope;
          generator_emit(generator, "DEFVAR LF@%s$%d", id_token->token_lexeme, scope);
        }

        // Ak je premenná inicializovaná, generovať aj priradenie
        // Hľadať výraz alebo priradenie v deťoch
        for (int i = 1; i < node->children_count; i++) {
          tree_node_t *child = node->children[i];
          if (child->nonterm_type == NONTERMINAL_T_EXPRESSION) {
            generate_expression(generator, child);
            if (sym && sym->is_global) {
              generator_emit(generator, "POPS GF@%s", id_token->token_lexeme);
            } else {
              char var_name[256];
              format_local_var(generator, id_token, var_name, sizeof(var_name));
              generator_emit(generator, "POPS %s", var_name);
            }
            break;
          } else if (child->nonterm_type == NONTERMINAL_T_ASSIGNMENT) {
            // For assignment nodes, use generator_generate to handle it properly
            generator_generate(generator, child);
            break;
          }
        }
      }
    }
  }
}

// Generovanie deklarácie funkcie
void generate_function_declaration(Generator *generator, tree_node_t *node) {
  if (!node)
    return;

  // Nájsť názov funkcie (identifikátor)
  tree_node_t *func_name_node = NULL;
  for (int i = 0; i < node->children_count; i++) {
    tree_node_t *child = node->children[i];
    if (child->type == NODE_T_TERMINAL && child->token &&
        (child->token->token_type == TOKEN_T_IDENTIFIER)) {
      func_name_node = child;
      break;
    }
  }

  if (!func_name_node || !func_name_node->token)
    return;

  char *func_name = func_name_node->token->token_lexeme;

  // Nastaviť aktuálnu funkciu
  if (generator->current_function) {
    free(generator->current_function);
  }
  generator->current_function = malloc(strlen(func_name) + 1);
  if (generator->current_function) {
    strcpy(generator->current_function, func_name);
  }
  generator->in_function = 1;
  generator->has_return = false;  // Reset return flag for each function

  // Clear parameter list at the start of each function
  // (will be populated if function has parameters)
  if (generator->function_params) {
      for (int i = 0; i < generator->function_param_count; i++) {
          if (generator->function_params[i]) {
              free(generator->function_params[i]);
          }
      }
      free(generator->function_params);
      generator->function_params = NULL;
      generator->function_param_count = 0;
  }

  Symbol *sym = search_table(func_name_node->token, generator->symtable);

  int type = diverse_function(generator, node);
  
  // Strip prefix for label generation if needed
  char *label_name = func_name;
  if (strncmp(func_name, "getter+", 7) == 0) {
      label_name = func_name + 7;
  } else if (strncmp(func_name, "setter+", 7) == 0) {
      label_name = func_name + 7;
  }

  // Generovať label funkcie
  if (type == 1) {
    generator_emit(generator, "LABEL %s_", label_name);

  } else if (type == 2) {

    generator_emit(generator, "LABEL %s__", label_name);
  } else {
    if (sym && sym->sym_identif_declaration_count > 1) {
        // Find overload index
        int overload_index = 0;
        if (func_name_node && func_name_node->token) {
            for (int i = 0; i < sym->sym_identif_declaration_count; i++) {
                if (sym->sym_identif_declared_at_line_arr[i] == func_name_node->token->token_line_number) {
                    overload_index = i;
                    break;
                }
            }
        }
        generator_emit(generator, "LABEL %s$%d", func_name, overload_index);
    } else {
        generator_emit(generator, "LABEL %s", func_name);
    }

  }
  // Vytvoriť a pushnúť rámec
  generator_emit(generator, "CREATEFRAME");
  generator_emit(generator, "PUSHFRAME");
  generator->tf_created = false; // TF is now LF, so TF is undefined
  if(strcmp(func_name, "main") == 0){
    for(int i = 0; i < generator->global_count; i++) {
      generator_emit(generator, "DEFVAR GF@%s", generator->global_vars[i]);
      generator_emit(generator, "PUSHS nil@nil");
      generator_emit(generator, "POPS GF@%s", generator->global_vars[i]);
  
    }
  }

  // Získať symbol funkcie pre parametre
  Symbol *func_sym = NULL;
  if (func_name_node && func_name_node->token && generator->symtable) {
    func_sym = search_table(func_name_node->token, generator->symtable);
  }

  if (type == 2) {
      // Setter parameter generation
      // node is GR_FUN_DECLARATION
      // node->children[1] is GR_SETTER_DECLARATION
      // GR_SETTER_DECLARATION children: 0: IDENTIFIER (val), 1: CODE_BLOCK (based on debug output)
      // Parameters use no suffix - they're declared as LF@param (not LF@param$N)
      // Local variables in the function body will get suffixes to avoid conflicts
      if (node->children_count >= 2 && node->children[1]->children_count >= 1) {
          tree_node_t *param_node = node->children[1]->children[0];
          if (param_node && param_node->token && param_node->token->token_type == TOKEN_T_IDENTIFIER) {
              char *param_name = param_node->token->token_lexeme;
              
              // Store parameter name for later reference checking
              if (generator->function_params) {
                  for (int i = 0; i < generator->function_param_count; i++) {
                      if (generator->function_params[i]) {
                          free(generator->function_params[i]);
                      }
                  }
                  free(generator->function_params);
              }
              generator->function_param_count = 1;
              generator->function_params = malloc(sizeof(char *));
              generator->function_params[0] = strdup(param_name);
              
              generator_emit(generator, "DEFVAR LF@%s", param_name);
              generator_emit(generator, "POPS LF@%s", param_name);
          }
      }
  }

  // Generovať DEFVAR pre parametre a POPS pre hodnoty zo zásobníka
  if (func_sym && func_sym->sym_identif_type == IDENTIF_T_FUNCTION) {
    int overload_index = 0;
    // Find the correct overload based on line number
    if (func_name_node && func_name_node->token) {
        for (int i = 0; i < func_sym->sym_identif_declaration_count; i++) {
            if (func_sym->sym_identif_declared_at_line_arr[i] == func_name_node->token->token_line_number) {
                overload_index = i;
                break;
            }
        }
    }

    int param_count = func_sym->sym_function_number_of_params[overload_index];
    // Use AST to find parameter names because sym_function_param_names is uninitialized
    // Find GR_FUN_PARAM node
    tree_node_t *param_node = NULL;
    for (int i = 0; i < node->children_count; i++) {
        if (node->children[i]->rule == GR_FUN_PARAM) {
            param_node = node->children[i];
            break;
        }
    }

    if (param_node) {
        // Collect parameters first to handle order
        // char *params[32]; // Assuming max 32 params for simplicity, or use dynamic array
        Token *params[32];
        int p_count = 0;
        
        tree_node_t *current = param_node;
        while (current && p_count < 32) {
            // Check children for identifier
            for (int i = 0; i < current->children_count; i++) {
                tree_node_t *child = current->children[i];
                if (child->type == NODE_T_TERMINAL && child->token && child->token->token_type == TOKEN_T_IDENTIFIER) {
                     params[p_count++] = child->token;
                }
            }
            
            // Find next GR_FUN_PARAM (prime)
            tree_node_t *next = NULL;
            for (int i = 0; i < current->children_count; i++) {
                 if (current->children[i]->rule == GR_FUN_PARAM) {
                     next = current->children[i];
                     break;
                 }
            }
            current = next;
        }

        // Store parameter names for later reference checking
        // Free old parameter list if any
        if (generator->function_params) {
            for (int i = 0; i < generator->function_param_count; i++) {
                if (generator->function_params[i]) {
                    free(generator->function_params[i]);
                }
            }
            free(generator->function_params);
        }
        generator->function_param_count = p_count;
        if (p_count > 0) {
            generator->function_params = malloc(sizeof(char *) * p_count);
            for (int i = 0; i < p_count; i++) {
                generator->function_params[i] = strdup(params[i]->token_lexeme);
            }
        } else {
            generator->function_params = NULL;
        }

        // DEFVAR for all params
        // Parameters use no suffix - they're declared as LF@param (not LF@param$N)
        // Local variables in the function body will get suffixes to avoid conflicts
        for (int i = 0; i < p_count; i++) {
            generator_emit(generator, "DEFVAR LF@%s", params[i]->token_lexeme);
        }

        // POPS in reverse order (last param is on top of stack)
        for (int i = p_count - 1; i >= 0; i--) {
            generator_emit(generator, "POPS LF@%s", params[i]->token_lexeme);
        }
    }
  }

  // Generovať telo funkcie (code block)
  // CODE_BLOCK môže byť priamo dieťa alebo hlbšie v strome
  tree_node_t *code_block = NULL;
  for (int i = 0; i < node->children_count; i++) {
    tree_node_t *child = node->children[i];
      if (child && child->type == NODE_T_NONTERMINAL) {
        if (child->nonterm_type == NONTERMINAL_T_CODE_BLOCK) {
          code_block = child;
          break;
      }
      // Skúsiť hľadať CODE_BLOCK aj medzi deťmi (pre gettery/settery)
      for (int j = 0; j < child->children_count; j++) {
        tree_node_t *grandchild = child->children[j];
          if (grandchild && grandchild->type == NODE_T_NONTERMINAL &&
              grandchild->nonterm_type == NONTERMINAL_T_CODE_BLOCK) {
            code_block = grandchild;
            break;
        }
      }
      if (code_block)
        break;
    }
  }

  if (code_block) {
    generate_code_block(generator, code_block);
  }

  // Popnúť rámec pred returnom
  // Implicit return nil if execution reaches here (only if no explicit return was generated)
  if (strcmp(generator->current_function, "main") == 0) {
      // For main, just pop frame (optional, but good practice)
      generator_emit(generator, "POPFRAME");
      generator->tf_created = false;
      // generator_emit(generator, "EXIT"); // Or just end
  } else if (!generator->has_return) {
      generator_emit(generator, "PUSHS nil@nil");
      generator_emit(generator, "POPFRAME");
      generator_emit(generator, "RETURN");
  } else {
      // Don't emit anything - return already handled POPFRAME and RETURN
  }

  generator->in_function = 0;
  
  // Free parameter list
  if (generator->function_params) {
      for (int i = 0; i < generator->function_param_count; i++) {
          if (generator->function_params[i]) {
              free(generator->function_params[i]);
          }
      }
      free(generator->function_params);
      generator->function_params = NULL;
      generator->function_param_count = 0;
  }
  
  if (generator->current_function) {
    free(generator->current_function);
    generator->current_function = NULL;
  }
}

// Helper function to check built-in function parameters
void check_builtin_params(Generator *generator, char *func_name, tree_node_t *node) {
    if (!func_name || !node) return;

    char *suffix = func_name + 4; // Skip "Ifj."

    // Define expected types
    // 0: string, 1: int, 2: float
    int expected_types[3] = {-1, -1, -1}; 
    int expected_count = 0;

    if (strcmp(suffix, "length") == 0 || strcmp(suffix, "ord") == 0) {
        expected_types[0] = 0; // string
        expected_count = 1;
    } else if (strcmp(suffix, "chr") == 0) {
        expected_types[0] = 1; // int
        expected_count = 1;
    } else if (strcmp(suffix, "floor") == 0) {
        expected_types[0] = 2; // float
        expected_count = 1;
    } else if (strcmp(suffix, "substring") == 0) {
        expected_types[0] = 0; // string
        expected_types[1] = 1; // int
        expected_types[2] = 1; // int
        expected_count = 3;
    } else if (strcmp(suffix, "strcmp") == 0) {
        expected_types[0] = 0; // string
        expected_types[1] = 0; // string
        expected_count = 2;
    } else {
        return; // Skip write, read, etc.
    }

    int arg_index = 0;
    for (int i = 0; i < node->children_count; i++) {
        tree_node_t *child = node->children[i];
        
        // Skip function name and parentheses/commas
        if (child->type == NODE_T_TERMINAL && child->token &&
            (child->token->token_type == TOKEN_T_KEYWORD || // Ifj
             strcmp(child->token->token_lexeme, "(") == 0 ||
             strcmp(child->token->token_lexeme, ")") == 0 ||
             strcmp(child->token->token_lexeme, ",") == 0 ||
             (child->token->token_type == TOKEN_T_IDENTIFIER && strstr(func_name, child->token->token_lexeme)))) {
            continue;
        }
        if (arg_index >= expected_count) {
             // Too many arguments - could be an error, but let's stick to type checking for now
             // generator->error = ERR_T_SEMANTIC_ERR_BUILTIN_FN_BAD_PARAM;
             // return;
             continue; 
        }

        int expected_type = expected_types[arg_index];
        
        // Check literal types
        Token *token_to_check = NULL;
        tree_node_t *current = child;
        
        // Drill down to find the terminal node
        while (current && current->type == NODE_T_NONTERMINAL) {
            if (current->children_count > 0) {
                // Heuristic: take the first child that looks like a value
                // This might need refinement for complex expressions
                tree_node_t *next = NULL;
                for(int k=0; k<current->children_count; k++) {
                    tree_node_t *c = current->children[k];
                    if (c->token && (c->token->token_type == TOKEN_T_NUM || 
                                     c->token->token_type == TOKEN_T_STRING || 
                                     c->token->token_type == TOKEN_T_IDENTIFIER ||
                                     c->token->token_type == TOKEN_T_GLOBAL_VAR)) {
                        next = c;
                        break;
                    }
                    if (c->type == NODE_T_NONTERMINAL) {
                        next = c; // Go deeper
                        // Don't break immediately, prefer terminals if found in this level? 
                        // Actually, usually it's just one path.
                        break; 
                    }
                }
                current = next;
            } else {
                current = NULL;
            }
        }
        
        if (current && current->token) {
            token_to_check = current->token;
        }

        if (token_to_check) {
            if (expected_type == 0) { // Expect string
                if (token_to_check->token_type == TOKEN_T_NUM) { // int or float
                     generator->error = ERR_T_SEMANTIC_ERR_BUILTIN_FN_BAD_PARAM;
                     return;
                }
            } else if (expected_type == 1) { // Expect int
                if (token_to_check->token_type == TOKEN_T_STRING) {
                     generator->error = ERR_T_SEMANTIC_ERR_BUILTIN_FN_BAD_PARAM;
                     return;
                }
                if (token_to_check->token_type == TOKEN_T_NUM) {
                    // Check if it looks like a float
                    if (strchr(token_to_check->token_lexeme, '.') || strchr(token_to_check->token_lexeme, 'e') || strchr(token_to_check->token_lexeme, 'E')) {
                         generator->error = ERR_T_SEMANTIC_ERR_BUILTIN_FN_BAD_PARAM;
                         return;
                    }
                }
            } else if (expected_type == 2) { // Expect float
                if (token_to_check->token_type == TOKEN_T_STRING) {
                     generator->error = ERR_T_SEMANTIC_ERR_BUILTIN_FN_BAD_PARAM;
                     return;
                }
                if (token_to_check->token_type == TOKEN_T_NUM) {
                    // Check if it looks like an int (no dot or exponent)
                    if (!strchr(token_to_check->token_lexeme, '.') && !strchr(token_to_check->token_lexeme, 'e') && !strchr(token_to_check->token_lexeme, 'E')) {
                         // Strict check: Int passed where float expected
                         generator->error = ERR_T_SEMANTIC_ERR_BUILTIN_FN_BAD_PARAM;
                         return;
                    }
                }
            }
        }
        
        // Check variable types from symbol table
        if (child->type == NODE_T_TERMINAL && child->token && 
            (child->token->token_type == TOKEN_T_IDENTIFIER || child->token->token_type == TOKEN_T_GLOBAL_VAR)) {
            Symbol *sym = search_table(child->token, generator->symtable);
            if (sym) {
                if (expected_type == 0) { // Expect string
                    if (sym->sym_variable_type == VAR_T_NUM) { // Assuming VAR_T_NUM covers both int and float
                         generator->error = ERR_T_SEMANTIC_ERR_BUILTIN_FN_BAD_PARAM;
                         return;
                    }
                } else if (expected_type == 1) { // Expect int
                    if (sym->sym_variable_type == VAR_T_STRING) {
                         generator->error = ERR_T_SEMANTIC_ERR_BUILTIN_FN_BAD_PARAM;
                         return;
                    }
                } else if (expected_type == 2) { // Expect float
                    if (sym->sym_variable_type == VAR_T_STRING) {
                         generator->error = ERR_T_SEMANTIC_ERR_BUILTIN_FN_BAD_PARAM;
                         return;
                    }
                }
            }
        }
        
        // Handle expressions (non-terminals) - difficult to check without type inference result
        // For now, we only check literals and variables.

        arg_index++;
    }
}

// Generovanie volania funkcie
void generate_function_call(Generator *generator, tree_node_t *node) {
  if (!node)
    return;

  // Nájsť názov funkcie
  tree_node_t *func_name_node = NULL;
  char *func_name = NULL;
  char full_func_name[256] = {0};
  int has_ifj_prefix = 0;

  for (int i = 0; i < node->children_count; i++) {
    tree_node_t *child = node->children[i];
    if (child->type == NODE_T_TERMINAL && child->token &&
        child->token->token_lexeme) {
      char *lexeme = child->token->token_lexeme;
      if (child->token->token_type == TOKEN_T_IDENTIFIER) {
        func_name_node = child;
        func_name = lexeme;
      } else if (child->token->token_type == TOKEN_T_KEYWORD &&
                 strcmp(lexeme, "Ifj") == 0) {
        has_ifj_prefix = 1;
      }
    }
  }

  if (func_name && has_ifj_prefix) {
    snprintf(full_func_name, sizeof(full_func_name), "Ifj.%s", func_name);
    func_name = full_func_name;
  } else if (func_name) {
    // Check if it's a built-in function without Ifj prefix
    if (strcmp(func_name, "write") == 0 ||
        strcmp(func_name, "read_num") == 0 ||
        strcmp(func_name, "read_str") == 0 ||
        strcmp(func_name, "length") == 0 ||
        strcmp(func_name, "floor") == 0 ||
        strcmp(func_name, "str") == 0 ||
        strcmp(func_name, "ord") == 0 ||
        strcmp(func_name, "substring") == 0 ||
        strcmp(func_name, "strcmp") == 0 ||
        strcmp(func_name, "chr") == 0) {
        snprintf(full_func_name, sizeof(full_func_name), "Ifj.%s", func_name);
        func_name = full_func_name;
    }
  }

  if (!func_name) {
    // Možno je funkcia v expression_or_function
    for (int i = 0; i < node->children_count; i++) {
      tree_node_t *child = node->children[i];
      if (child->nonterm_type == NONTERMINAL_T_EXPRESSION_OR_FN) {
        generator_generate(generator, child);
        return;
      }
    }
    return;
  }

  // Check for built-in functions
  if (strncmp(func_name, "Ifj.", 4) == 0) {
    
    check_builtin_params(generator, func_name, node);
    if (generator->error != 0) return;

    char *suffix = func_name + 4;

    if (strcmp(suffix, "write") == 0) {
      // WRITE - iterate over arguments
      Symbol *sym = search_table(func_name_node->token, generator->symtable);  
      for (int i = 0; i < node->children_count; i++) {
        tree_node_t *child = node->children[i];
        if (child == func_name_node)
          continue;
        if (child->type == NODE_T_TERMINAL && child->token &&
            (strcmp(child->token->token_lexeme, "(") == 0 ||
             strcmp(child->token->token_lexeme, ")") == 0 ||
             strcmp(child->token->token_lexeme, "Ifj") == 0))
          continue;

        if (child->nonterm_type == NONTERMINAL_T_EXPRESSION ||
            child->nonterm_type == NONTERMINAL_T_FUN_PARAM ||
            child->nonterm_type == NONTERMINAL_T_EXPRESSION_OR_FN ||
            (child->type == NODE_T_TERMINAL && child->token &&
             (child->token->token_type == TOKEN_T_NUM ||
              child->token->token_type == TOKEN_T_STRING ||
              child->token->token_type == TOKEN_T_IDENTIFIER ||
              child->token->token_type == TOKEN_T_GLOBAL_VAR))) {

          generate_expression(generator, child);
          char *temp = get_temp_var(generator);
          generator_emit(generator, "DEFVAR %s", temp);
          generator_emit(generator, "POPS %s", temp);
          generator_emit(generator, "WRITE %s", temp);
          free(temp);
        }
      }
      generator_emit(generator, "PUSHS nil@nil"); // Return nil
      return;
    } else if (strcmp(suffix, "read_num") == 0) {
      char *temp = get_temp_var(generator);
      generator_emit(generator, "DEFVAR %s", temp);
      generator_emit(generator, "READ %s float", temp);
      generator_emit(generator, "PUSHS %s", temp);
      free(temp);
      return;
    } else if (strcmp(suffix, "read_str") == 0) {
      char *temp = get_temp_var(generator);
      generator_emit(generator, "DEFVAR %s", temp);
      generator_emit(generator, "READ %s string", temp);
      generator_emit(generator, "PUSHS %s", temp);
      free(temp);
      return;
    } else if (strcmp(suffix, "length") == 0) {
      // STRLEN
      // Expect 1 argument
      for (int i = 0; i < node->children_count; i++) {
        tree_node_t *child = node->children[i];
        if (child == func_name_node)
          continue;
        if (child->type == NODE_T_TERMINAL)
          continue; // Skip tokens
        if (child->nonterm_type == NONTERMINAL_T_EXPRESSION ||
            child->nonterm_type == NONTERMINAL_T_FUN_PARAM) {
          generate_expression(generator, child);
          char *temp_s = get_temp_var(generator);
          char *temp_len = get_temp_var(generator);
          generator_emit(generator, "DEFVAR %s", temp_s);
          generator_emit(generator, "POPS %s", temp_s);
          generator_emit(generator, "DEFVAR %s", temp_len);
          generator_emit(generator, "STRLEN %s %s", temp_len, temp_s);
          generator_emit(generator, "PUSHS %s", temp_len);
          generator_emit(generator, "INT2FLOATS");
          free(temp_s);
          free(temp_len);
          return;
        }
      }
      return;
    } else if (strcmp(suffix, "floor") == 0) {
      // FLOAT2INT
      for (int i = 0; i < node->children_count; i++) {
        tree_node_t *child = node->children[i];
        if (child == func_name_node)
          continue;
        if (child->type == NODE_T_TERMINAL)
          continue;
        if (child->nonterm_type == NONTERMINAL_T_EXPRESSION ||
            child->nonterm_type == NONTERMINAL_T_FUN_PARAM) {
          generate_expression(generator, child);
          generator_emit(generator, "FLOAT2INTS");
          return;
        }
      }
      return;
    } else if (strcmp(suffix, "substring") == 0) {
      // SUBSTRING
      for (int i = 0; i < node->children_count; i++) {
        tree_node_t *child = node->children[i];
        if (child == func_name_node)
          continue;
        if (child->type == NODE_T_TERMINAL)
          continue;
        if (child->nonterm_type == NONTERMINAL_T_EXPRESSION ||
            child->nonterm_type == NONTERMINAL_T_FUN_PARAM) {
          generate_expression(generator, child);
        }
      }
      
      // Inline implementation of substring
      char *s = get_temp_var(generator);
      char *p1 = get_temp_var(generator);
      char *p2 = get_temp_var(generator);
      char *res = get_temp_var(generator);
      char *len = get_temp_var(generator);
      char *cond = get_temp_var(generator);
      char *char_val = get_temp_var(generator);
      
      int label_idx = generator->label_counter++;
      
      generator_emit(generator, "DEFVAR %s", s);
      generator_emit(generator, "DEFVAR %s", p1);
      generator_emit(generator, "DEFVAR %s", p2);
      generator_emit(generator, "DEFVAR %s", res);
      generator_emit(generator, "DEFVAR %s", len);
      generator_emit(generator, "DEFVAR %s", cond);
      generator_emit(generator, "DEFVAR %s", char_val);

      // Pop arguments: s, p1, p2 (top)
      generator_emit(generator, "POPS %s", p2);
      generator_emit(generator, "POPS %s", p1);
      generator_emit(generator, "POPS %s", s);

      // Check p1 < 0
      generator_emit(generator, "LT %s %s int@0", cond, p1);
      generator_emit(generator, "JUMPIFEQ substring_nil_%d %s bool@true", label_idx, cond);
      
      // Check p2 < 0
      generator_emit(generator, "LT %s %s int@0", cond, p2);
      generator_emit(generator, "JUMPIFEQ substring_nil_%d %s bool@true", label_idx, cond);

      // Check p1 > p2
      generator_emit(generator, "GT %s %s %s", cond, p1, p2);
      generator_emit(generator, "JUMPIFEQ substring_nil_%d %s bool@true", label_idx, cond);

      // len = length(s)
      generator_emit(generator, "STRLEN %s %s", len, s);

      // Check p1 >= len
      generator_emit(generator, "LT %s %s %s", cond, p1, len);
      generator_emit(generator, "JUMPIFEQ substring_nil_%d %s bool@false", label_idx, cond);

      // Check p2 >= len
      generator_emit(generator, "LT %s %s %s", cond, p2, len);
      generator_emit(generator, "JUMPIFEQ substring_nil_%d %s bool@false", label_idx, cond);

      // Initialize res = ""
      generator_emit(generator, "MOVE %s string@", res);
      
      // Loop
      generator_emit(generator, "LABEL substring_loop_%d", label_idx);
      
      // if p1 > p2 break
      generator_emit(generator, "GT %s %s %s", cond, p1, p2);
      generator_emit(generator, "JUMPIFEQ substring_end_%d %s bool@true", label_idx, cond);

      // char = s[p1]
      generator_emit(generator, "GETCHAR %s %s %s", char_val, s, p1);
      // res = res + char
      generator_emit(generator, "CONCAT %s %s %s", res, res, char_val);
      // p1++
      generator_emit(generator, "ADD %s %s int@1", p1, p1);
      generator_emit(generator, "JUMP substring_loop_%d", label_idx);

      generator_emit(generator, "LABEL substring_end_%d", label_idx);
      generator_emit(generator, "PUSHS %s", res);
      generator_emit(generator, "JUMP substring_done_%d", label_idx);

      generator_emit(generator, "LABEL substring_nil_%d", label_idx);
      generator_emit(generator, "PUSHS nil@nil");

      generator_emit(generator, "LABEL substring_done_%d", label_idx);

      free(s); free(p1); free(p2); free(res); free(len); free(cond); free(char_val);
      return;
    } else if (strcmp(suffix, "str") == 0) {
      for (int i = 0; i < node->children_count; i++) {
        tree_node_t *child = node->children[i];
        if (child == func_name_node)
          continue;
        if (child->type == NODE_T_TERMINAL)
          continue;
        if (child->nonterm_type == NONTERMINAL_T_EXPRESSION ||
            child->nonterm_type == NONTERMINAL_T_FUN_PARAM) {
          generate_expression(generator, child);
          if(generator->is_float){
            generator_emit(generator, "FLOAT2STRS");
            generator->is_float = false;
          } else {
            generator_emit(generator, "INT2STRS");
          }
          return;
        }
      }
      return;
    } else if (strcmp(suffix, "strcmp") == 0) {
      generate_strcmp_comparison(generator, node);
      return;
    } else if (strcmp(suffix, "ord") == 0){
      // Expect 2 arguments: string s, int index
      // Stack: ..., s, index (top)
      for (int i = 0; i < node->children_count; i++) {
        tree_node_t *child = node->children[i];
        if (child == func_name_node)
          continue;
        if (child->type == NODE_T_TERMINAL)
          continue;
        if (child->nonterm_type == NONTERMINAL_T_EXPRESSION ||
            child->nonterm_type == NONTERMINAL_T_FUN_PARAM) {
          generate_expression(generator, child);
        }
      }

      char *s = get_temp_var(generator);
      char *idx = get_temp_var(generator);
      char *len = get_temp_var(generator);
      char *cond = get_temp_var(generator);
      char *res = get_temp_var(generator);
      
      int label_idx = generator->label_counter++;

      generator_emit(generator, "DEFVAR %s", s);
      generator_emit(generator, "DEFVAR %s", idx);
      generator_emit(generator, "DEFVAR %s", len);
      generator_emit(generator, "DEFVAR %s", cond);
      generator_emit(generator, "DEFVAR %s", res);

      // Pop arguments: index (top), s
      generator_emit(generator, "POPS %s", idx);
      generator_emit(generator, "POPS %s", s);

      // Check index < 0
      generator_emit(generator, "LT %s %s int@0", cond, idx);
      generator_emit(generator, "JUMPIFEQ ord_zero_%d %s bool@true", label_idx, cond);

      // Get length
      generator_emit(generator, "STRLEN %s %s", len, s);

      // Check index >= length
      generator_emit(generator, "LT %s %s %s", cond, idx, len);
      // If index < length is false (i.e. index >= length), jump to zero
      generator_emit(generator, "JUMPIFEQ ord_zero_%d %s bool@false", label_idx, cond);

      // Valid index: use STRI2INT
      generator_emit(generator, "STRI2INT %s %s %s", res, s, idx);
      generator_emit(generator, "PUSHS %s", res);
      generator_emit(generator, "INT2FLOATS");
      generator_emit(generator, "JUMP ord_done_%d", label_idx);

      // Return 0 case
      generator_emit(generator, "LABEL ord_zero_%d", label_idx);
      generator_emit(generator, "PUSHS int@0");
      generator_emit(generator, "INT2FLOATS");

      generator_emit(generator, "LABEL ord_done_%d", label_idx);

      free(s); free(idx); free(len); free(cond); free(res);
      return;
    } else if (strcmp(suffix, "chr") == 0) {
      // Expect 1 argument: int num
      for (int i = 0; i < node->children_count; i++) {
        tree_node_t *child = node->children[i];
        if (child == func_name_node)
          continue;
        if (child->type == NODE_T_TERMINAL)
          continue;
        if (child->nonterm_type == NONTERMINAL_T_EXPRESSION ||
            child->nonterm_type == NONTERMINAL_T_FUN_PARAM) {
          generate_expression(generator, child);
        }
      }

      char *num = get_temp_var(generator);
      char *res = get_temp_var(generator);
      
      generator_emit(generator, "DEFVAR %s", num);
      generator_emit(generator, "DEFVAR %s", res);

      // Pop argument
      generator_emit(generator, "POPS %s", num);

      // Convert int to char
      generator_emit(generator, "INT2CHAR %s %s", res, num);
      generator_emit(generator, "PUSHS %s", res);

      free(num); free(res);
      return;
    }
    // Add other built-ins as needed
  }

  // Normal function call
  // Generovať parametre
  for (int i = 0; i < node->children_count; i++) {
    tree_node_t *child = node->children[i];
    if (child == func_name_node)
      continue;
    if (child->type == NODE_T_TERMINAL && child->token) {
      if (strcmp(child->token->token_lexeme, "(") == 0 ||
          strcmp(child->token->token_lexeme, ")") == 0 ||
          strcmp(child->token->token_lexeme, "Ifj") == 0) {
        continue;
      }
    }
    if (child->nonterm_type == NONTERMINAL_T_EXPRESSION ||
        child->nonterm_type == NONTERMINAL_T_FUN_PARAM ||
        (child->type == NODE_T_TERMINAL && child->token &&
         (child->token->token_type == TOKEN_T_NUM ||
          child->token->token_type == TOKEN_T_STRING ||
          child->token->token_type == TOKEN_T_IDENTIFIER ||
          child->token->token_type == TOKEN_T_GLOBAL_VAR))) {
      generate_expression(generator, child);
    }
  }

  generator->is_called = true;
  
  // Search for the function symbol specifically (ignoring UNSET symbols from local scopes)
  Symbol *sym = NULL;
  for (int i = 0; i < generator->symtable->symtable_size; i++) {
      Symbol *s = generator->symtable->symtable_rows[i].symbol;
      if (s && s->sym_lexeme && strcmp(s->sym_lexeme, func_name) == 0) {
          if (s->sym_identif_type == IDENTIF_T_FUNCTION) {
              sym = s;
              break;
          }
      }
  }

  if (sym && sym->sym_identif_type == IDENTIF_T_FUNCTION && sym->sym_identif_declaration_count > 1) {
      // Resolve overload based on argument count
      int arg_count = 0;
      for (int i = 0; i < node->children_count; i++) {
        tree_node_t *child = node->children[i];
        if (child == func_name_node) continue;
        if (child->type == NODE_T_TERMINAL && child->token) {
             if (strcmp(child->token->token_lexeme, "(") == 0 ||
                 strcmp(child->token->token_lexeme, ")") == 0 ||
                 strcmp(child->token->token_lexeme, "Ifj") == 0) {
                 continue;
             }
        }
        // Count expressions/params
        if (child->nonterm_type == NONTERMINAL_T_EXPRESSION ||
            child->nonterm_type == NONTERMINAL_T_FUN_PARAM ||
            (child->type == NODE_T_TERMINAL && child->token &&
             (child->token->token_type == TOKEN_T_NUM ||
              child->token->token_type == TOKEN_T_STRING ||
              child->token->token_type == TOKEN_T_IDENTIFIER ||
              child->token->token_type == TOKEN_T_GLOBAL_VAR))) {
            arg_count++;
        }
      }

      int overload_index = -1;
      for (int i = 0; i < sym->sym_identif_declaration_count; i++) {
          if (sym->sym_function_number_of_params[i] == arg_count) {
              overload_index = i;
              break;
          }
      }
      
      if (overload_index != -1) {
          generator_emit(generator, "CALL %s$%d", func_name, overload_index);
      } else {
          // Fallback or error?
          generator_emit(generator, "CALL %s", func_name);
      }
  } else {
      generator_emit(generator, "CALL %s", func_name);
  }
  
  generator->is_called = false;
}

// Generovanie if príkazu
void generate_if(Generator *generator, tree_node_t *node) {
  if (!node)
    return;

  char *else_label = get_next_label(generator);
  char *end_label = get_next_label(generator);

  // Generovať predikát (výraz)
  tree_node_t *predicate_node = NULL;
  tree_node_t *then_block = NULL;
  tree_node_t *else_block = NULL;

  for (int i = 0; i < node->children_count; i++) {
    tree_node_t *child = node->children[i];
    if (child->nonterm_type == NONTERMINAL_T_PREDICATE) {
      predicate_node = child;
    } else if (child->nonterm_type == NONTERMINAL_T_CODE_BLOCK) {
      if (!then_block) {
        then_block = child;
      } else {
        else_block = child;
      }
    }
  }

  if (predicate_node) {
    // Predikát má výraz ako dieťa - spracovať ho cez generator_generate
    // (ktorý správne spracuje NONTERMINAL_T_PREDICATE cez generate_expression)
    generator_generate(generator, predicate_node);

    // Skontrolovať, či výraz obsahuje operátor != (pre použitie JUMPIFNEQS)
    int has_neq_operator = 0;
    if (predicate_node->children_count > 0) {
      tree_node_t *expr = predicate_node->children[0];
      // Rekurzívne hľadať operátor != v strome výrazu
      if (expr && expr->token && expr->token->token_type == TOKEN_T_OPERATOR &&
          strcmp(expr->token->token_lexeme, "!=") == 0) {
        has_neq_operator = 1;
      }
    }

    if (has_neq_operator) {
      // Pre operátor != použijeme JUMPIFNEQS (skáče ak sa hodnoty nerovnajú)
      generator_emit(generator, "JUMPIFNEQS %s", else_label);
    } else {
      // Detect if expression has an operator
      int has_operator = 0;
      if (predicate_node->children_count > 0) {
        tree_node_t *expr = predicate_node->children[0];
        if (expr->token && expr->token->token_type == TOKEN_T_OPERATOR) {
          has_operator = 1;
        }
      }
      
      // Pre ostatné operátory: nil aj false sú falsy
      char *cond_res = get_temp_var(generator);
      generator_emit(generator, "DEFVAR %s", cond_res);
      generator_emit(generator, "POPS %s", cond_res);

      // Check if nil
      generator_emit(generator, "PUSHS %s", cond_res);
      generator_emit(generator, "PUSHS nil@nil");
      generator_emit(generator, "JUMPIFEQS %s", else_label);

      // Only check false if there's an operator (expression returns boolean)
      // Single variables don't need false check - not nil = truthy
      if (has_operator) {
        generator_emit(generator, "PUSHS %s", cond_res);
        generator_emit(generator, "PUSHS bool@false");
        generator_emit(generator, "JUMPIFEQS %s", else_label);
      }
      
      free(cond_res);
    }
  }

  // Then blok
  if (then_block) {
    generate_code_block(generator, then_block);
  }

  // Skok na koniec
  generator_emit(generator, "JUMP %s", end_label);

  // Else label
  generator_emit(generator, "LABEL %s", else_label);

  // Else blok
  if (else_block) {
    generate_code_block(generator, else_block);
  }

  // End label
  generator_emit(generator, "LABEL %s", end_label);

  free(else_label);
  free(end_label);
}

// Helper function to collect local variable declarations in a subtree
void collect_local_vars_in_subtree(tree_node_t *node, Token ***tokens, int *count, Symtable *symtable) {
  if (!node) return;

  // Check if this is a variable declaration
  if (node->rule == GR_DECLARATION && node->children_count > 0) {
    tree_node_t *id_node = node->children[0];
    if (id_node && id_node->token && id_node->token->token_type == TOKEN_T_IDENTIFIER) {
      char *var_name = id_node->token->token_lexeme;
      
      // Skip temporary variables
      if (strncmp(var_name, "tmp_", 4) == 0) {
        return;
      }
      
      // Check if it's a local variable (not global)
      Symbol *sym = search_table(id_node->token, symtable);
      if (sym && !sym->is_global) {
        // Check if already in the list
        int found = 0;
        for (int i = 0; i < *count; i++) {
          if ((*tokens)[i] && strcmp((*tokens)[i]->token_lexeme, var_name) == 0) {
            found = 1;
            break;
          }
        }
        
        // Add to list if not found
        if (!found) {
          *tokens = realloc(*tokens, (*count + 1) * sizeof(Token*));
          if (*tokens) {
            (*tokens)[*count] = id_node->token;
            (*count)++;
          }
        }
      }
    }
  }

  // Recursively process children
  for (int i = 0; i < node->children_count; i++) {
    collect_local_vars_in_subtree(node->children[i], tokens, count, symtable);
  }
}

// Generovanie while cyklu
void generate_while(Generator *generator, tree_node_t *node) {
  if (!node)
    return;

  char *loop_label = get_next_label(generator);
  char *end_label = get_next_label(generator);

  // Find the body block first to collect variables
  tree_node_t *predicate_node = NULL;
  tree_node_t *body_block = NULL;

  for (int i = 0; i < node->children_count; i++) {
    tree_node_t *child = node->children[i];
    if (child->nonterm_type == NONTERMINAL_T_PREDICATE) {
      predicate_node = child;
    } else if (child->nonterm_type == NONTERMINAL_T_CODE_BLOCK) {
      body_block = child;
    }
  }

  // Collect all local variable declarations in the loop body
  Token **local_var_tokens = NULL;
  int var_count = 0;
  if (body_block) {
    collect_local_vars_in_subtree(body_block, &local_var_tokens, &var_count, generator->symtable);
  }

  // Emit DEFVAR for all collected variables BEFORE the loop label
  for (int i = 0; i < var_count; i++) {
    if (local_var_tokens[i]) {
      int scope = get_scope_suffix(generator, local_var_tokens[i]);
      generator_emit(generator, "DEFVAR LF@%s$%d", local_var_tokens[i]->token_lexeme, scope);
    }
  }

  // Loop label (začiatok cyklu)
  generator_emit(generator, "LABEL %s", loop_label);
  
  // Clear TF at the start of each iteration
  generator_emit(generator, "CREATEFRAME");
  generator->tf_created = true;

  // Generovať predikát
  if (predicate_node) {
    // Predikát má výraz ako dieťa - spracovať ho cez generator_generate
    // (ktorý správne spracuje NONTERMINAL_T_PREDICATE cez generate_expression)
    generator_generate(generator, predicate_node);

    // Skontrolovať, či výraz obsahuje operátor != (pre použitie JUMPIFNEQS)
    int has_neq_operator = 0;
    if (predicate_node->children_count > 0) {
      tree_node_t *expr = predicate_node->children[0];
      // Rekurzívne hľadať operátor != v strome výrazu
      if (expr && expr->token && expr->token->token_type == TOKEN_T_OPERATOR &&
          strcmp(expr->token->token_lexeme, "!=") == 0) {
        has_neq_operator = 1;
      }
    }

    if (has_neq_operator) {
      // Pre operátor != použijeme JUMPIFNEQS (skáče ak sa hodnoty nerovnajú)
      generator_emit(generator, "JUMPIFNEQS %s", end_label);
    } else {
      // Pre ostatné operátory použijeme štandardný prístup
      // Vložiť false na zásobník pre porovnanie
      generator_emit(generator, "PUSHS bool@false");
      // Porovnať - ak sa rovnajú (t.j. predikát je false), skočiť na koniec
      generator_emit(generator, "JUMPIFEQS %s", end_label);
    }
  }

  // Telo cyklu
  if (body_block) {
    generator->in_while_loop = 1;  // Set flag before generating body
    generate_code_block(generator, body_block);
    generator->in_while_loop = 0;  // Reset flag after body
  }

  // Skok späť na začiatok
  generator_emit(generator, "JUMP %s", loop_label);

  // End label
  generator_emit(generator, "LABEL %s", end_label);

  // Free allocated memory
  if (local_var_tokens) {
    free(local_var_tokens);
  }

  free(loop_label);
  free(end_label);
}

// Generovanie return príkazu
void generate_return(Generator *generator, tree_node_t *node) {
  if (!node)
    return;

  if (node->token) {
  }

  // Nájsť výraz (ak existuje)
  // Výraz môže byť NONTERMINAL_T_EXPRESSION, NONTERMINAL_T_EXPRESSION_OR_FN alebo terminálny uzol (číslo,
  // string, keyword)
  // Alebo môže byť node s operátorom (z rule_expression_1) ktorý nemá explicitne nastavený nonterm_type
  // Poznámka: tree_init() nenastavuje type, takže nemôžeme spoliehať na child->type
  tree_node_t *expr_node = NULL;
  for (int i = 0; i < node->children_count; i++) {
    tree_node_t *child = node->children[i];
    if (child->token) {
    }
    // Check token type FIRST (more reliable than node type which may not be set)
    if (child->token && (child->token->token_type == TOKEN_T_NUM ||
                         child->token->token_type == TOKEN_T_STRING ||
                         child->token->token_type == TOKEN_T_IDENTIFIER ||
                         child->token->token_type == TOKEN_T_GLOBAL_VAR ||
                         child->token->token_type == TOKEN_T_KEYWORD)) {
      // Terminálny uzol (aj keď type nie je nastavený) - check token type first!
      expr_node = child;
      break;
    } else if (child->type == NODE_T_NONTERMINAL &&
        (child->nonterm_type == NONTERMINAL_T_EXPRESSION ||
         child->nonterm_type == NONTERMINAL_T_EXPRESSION_OR_FN)) {
      expr_node = child;
      break;
    } else if (child->type == NODE_T_TERMINAL) {
      // Terminálny uzol (číslo, string, keyword) je výraz
      expr_node = child;
      break;
    } else if (child->token && child->token->token_type == TOKEN_T_OPERATOR && child->children_count > 0) {
      // Node s operátorom a deťmi je výraz (z rule_expression_1)
      // Necheckujeme child->type lebo tree_init() ho nenastavuje
      expr_node = child;
      break;
    }
  }

  if (expr_node) {
    // Generovať výraz (vloží výsledok na zásobník)
    // DÔLEŽITÉ: Checkujeme token type FIRST, lebo node type môže byť nesprávne nastavený
    if (expr_node->token && (expr_node->token->token_type == TOKEN_T_STRING ||
                             expr_node->token->token_type == TOKEN_T_NUM ||
                             expr_node->token->token_type == TOKEN_T_IDENTIFIER ||
                             expr_node->token->token_type == TOKEN_T_GLOBAL_VAR ||
                             expr_node->token->token_type == TOKEN_T_KEYWORD)) {
      // Terminálny uzol - check token type first (more reliable)
      generate_terminal(generator, expr_node);
    } else if (expr_node->token && expr_node->token->token_type == TOKEN_T_OPERATOR && expr_node->children_count > 0) {
      // Pre operátorové uzly (z rule_expression_1) ktoré nemajú type nastavený
      generate_expression(generator, expr_node);
    } else if (expr_node->type == NODE_T_TERMINAL) {
      generate_terminal(generator, expr_node);
    } else if (expr_node->nonterm_type == NONTERMINAL_T_EXPRESSION_OR_FN) {
      generator_generate(generator, expr_node);
    } else {
      // Pre ostatné uzly
      generate_expression(generator, expr_node);
    }
  } else {
    // Ak sme nenašli výraz, skúsime nájsť akýkoľvek child ktorý nie je terminálny token
    // (môže to byť výraz ktorý nemá správne nastavené polia)
    // Skúsime aj rekurzívne hľadať v deťoch
    for (int i = 0; i < node->children_count; i++) {
      tree_node_t *child = node->children[i];
      // Preskočiť terminálne tokeny ako "return"
      if (child->type == NODE_T_TERMINAL && child->token && 
          strcmp(child->token->token_lexeme, "return") == 0) {
        continue;
      }
      // Ak má child token alebo deti, je to pravdepodobne výraz
      if (child->token || child->children_count > 0) {
        // Ak má operátor token, je to určite výraz
        if (child->token && child->token->token_type == TOKEN_T_OPERATOR) {
          expr_node = child;
          generate_expression(generator, expr_node);
          break;
        }
        // Inak skúsime rekurzívne hľadať v deťoch
        if (child->children_count > 0) {
          for (int j = 0; j < child->children_count; j++) {
            tree_node_t *grandchild = child->children[j];
            if (grandchild->token && grandchild->token->token_type == TOKEN_T_OPERATOR) {
              expr_node = grandchild;
              generate_expression(generator, expr_node);
              goto found_expr;
            }
          }
        }
        // Ak nič iné, skúsime tento child ako výraz
        expr_node = child;
        generate_expression(generator, expr_node);
        break;
      }
    }
    found_expr:;
  }

  // Return inštrukcia
  if (!generator->is_called) {
    if (!expr_node) {
        generator_emit(generator, "PUSHS nil@nil"); // Default return value if no expression
    }
    generator_emit(generator, "POPFRAME");
    generator_emit(generator, "RETURN");
    generator->has_return = true;  // Mark that return was generated
  }
}

// Generovanie EXIT inštrukcie
// void generate_exit(Generator *generator, tree_node_t *node) {
//   if (!node)
//     return;

//   // Nájsť výraz (exit code)
//   tree_node_t *expr_node = NULL;
//   for (int i = 0; i < node->children_count; i++) {
//     tree_node_t *child = node->children[i];
//     if (child->nonterm_type == NONTERMINAL_T_EXPRESSION) {
//       expr_node = child;
//       break;
//     }
//   }

//   if (expr_node) {
//     // Generovať výraz (exit code na zásobníku)
//     generate_expression(generator, expr_node);
//   } else {
//     // Default exit code 0
//     generator_emit(generator, "PUSHS int@0");
//   }

//   // EXIT inštrukcia
//   generator_emit(generator, "EXIT");
// }

// Generovanie BREAK inštrukcie
// void generate_break(Generator *generator, tree_node_t *node) {
//   if (!node)
//     return;
//   generator_emit(generator, "BREAK");
// }

// Generovanie DPRINT inštrukcie
// void generate_dprint(Generator *generator, tree_node_t *node) {
//   if (!node)
//     return;
//   // Nájsť výraz (hodnota na výpis)
//   tree_node_t *expr_node = NULL;
//   for (int i = 0; i < node->children_count; i++) {
//     tree_node_t *child = node->children[i];
//     if (child->nonterm_type == NONTERMINAL_T_EXPRESSION) {
//       expr_node = child;
//       break;
//     }
//   }
//   if (expr_node) {
//     generate_expression(generator, expr_node);
//   }
//   generator_emit(generator, "DPRINT");
// }
