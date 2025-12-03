# Generator.c Implementation Plan

## Document Overview
This document describes the implementation and architecture of `Generator.c`, a code generator that transforms Abstract Syntax Trees (AST) into IFJcode25 intermediate code.

---

## 1. System Initialization

### 1.1 Generator Initialization
**Function:** `init_generator(Symtable *symtable)`
**Location:** Lines 15-43
**Purpose:** Initialize the generator structure with default values

**Implementation Details:**
- Allocate memory for Generator structure
- Initialize symbol table reference
- Set counters to zero (labels, temp variables)
- Initialize scope tracking (current_scope = 0)
- Set function state flags (in_function = 0, has_return = false)
- Initialize global variable tracking arrays
- Set all boolean flags to false

**Key State Variables:**
- `label_counter`: Unique label generation
- `temp_var_counter`: Temporary variable naming
- `current_scope`: Scope hierarchy tracking
- `function_params`: Parameter name storage
- `global_vars`: Global variable collection

---

## 2. Utility Functions

### 2.1 Function Type Detection
**Function:** `diverse_function(Generator *generator, tree_node_t *node)`
**Location:** Lines 51-62
**Returns:** 0 (function), 1 (getter), 2 (setter)
**Implementation:** Check child node rules to determine function type

### 2.2 Global Variable Collection
**Function:** `generate_global_vars(Generator *generator)`
**Location:** Lines 65-112
**Purpose:** Extract all global variables from symbol table
**Implementation:**
- Iterate through symbol table rows
- Filter for global variables (is_global && IDENTIF_T_VARIABLE)
- Remove duplicates using string comparison
- Dynamic array allocation with capacity doubling
- Store in generator->global_vars array

### 2.3 Memory Management
**Function:** `generator_free(Generator *generator)`
**Location:** Lines 116-129
**Purpose:** Clean up all allocated memory
**Implementation:**
- Free current_function string
- Free string_variable
- Free global_vars array
- Free generator structure

### 2.4 Label Generation
**Function:** `get_next_label(Generator *generator)`
**Location:** Lines 132-140
**Returns:** Allocated string "LABEL_N" where N is counter value
**Implementation:**
- Increment label_counter
- Format string with sprintf
- Return allocated label string

### 2.5 Temporary Variable Generation
**Function:** `get_temp_var(Generator *generator)`
**Location:** Lines 143-157
**Returns:** Allocated string "TF@tmp_N"
**Implementation:**
- Check if TF frame created (tf_created flag)
- If not, emit CREATEFRAME instruction
- Set tf_created = true
- Generate temp variable name
- Increment temp_var_counter

### 2.6 Code Emission
**Function:** `generator_emit(Generator *generator, const char *format, ...)`
**Location:** Lines 160-169
**Purpose:** Output formatted instructions
**Implementation:**
- Use variadic arguments for formatting
- Write to generator->output (or stdout if NULL)
- Append newline after each instruction
- Uses vfprintf for formatted output

### 2.7 Float Conversion
**Function:** `convert_float_to_hex_format(const char *float_str)`
**Location:** Lines 172-185
**Purpose:** Convert decimal float to hexadecimal representation
**Implementation:**
- Parse string to double using strtod
- Format using %a specifier (hexadecimal scientific notation)
- Return allocated hex string

### 2.8 String Escape Sequence Conversion
**Function:** `convert_string(const char *str)`
**Location:** Lines 188-299
**Purpose:** Convert C-style escape sequences to IFJcode25 format
**Implementation:**
- Handle escape sequences: \n, \t, \r, \\, \"
- Convert to octal format: \010, \009, \013, \092, \034
- Handle hex escapes: \xHH
- Convert spaces to \032
- Convert non-printable characters to \XXX format
- Preserve printable ASCII characters

### 2.9 Operator Instruction Mapping
**Function:** `get_operator_instruction(const char *operator)`
**Location:** Lines 302-326
**Returns:** IFJcode25 instruction name
**Mapping:**
- Arithmetic: + → ADDS, - → SUBS, * → MULS, / → DIVS
- Comparison: == → EQS, != → NEQS, < → LTS, > → GTS, <= → LTEQS, >= → GTEQS
- Type check: is → EQS

### 2.10 Symbol Table Search
**Function:** `search_prefixed_symbol(Symtable *symtable, const char *base_name, const char *prefix, IDENTIF_TYPE target_type)`
**Location:** Lines 329-357
**Purpose:** Find getter/setter symbols with prefix
**Implementation:**
- Construct prefixed name (prefix + base_name)
- Search symbol table for matching lexeme
- Verify symbol type matches target
- Return found symbol or NULL

### 2.11 String Comparison Generation
**Function:** `generate_strcmp_comparison(Generator *generator, tree_node_t *node)`
**Location:** Lines 360-423
**Purpose:** Generate lexicographic string comparison
**Implementation:**
- Evaluate both string operands
- Pop into temporary variables
- Compare using LTS (less than)
- If less: push -1, jump to end
- If equal: push 0, jump to end
- Otherwise: push 1
- Uses three labels for control flow

### 2.12 Scope Resolution
**Function:** `get_scope_suffix(Generator *generator, Token *token)`
**Location:** Lines 426-530
**Purpose:** Determine correct scope for variable reference
**Implementation:**
- Collect all candidate symbols with matching name
- Check if declared in current scope before usage (line number comparison)
- If found in current scope: return current scope
- Otherwise: traverse parent scopes from token->previous_scope_arr
- Find highest scope number with declaration before usage line
- Fallback to most recent declaration or current scope

### 2.13 Parameter Checking
**Function:** `is_function_parameter(Generator *generator, const char *var_name)`
**Location:** Lines 533-546
**Purpose:** Determine if variable is function parameter
**Implementation:**
- Check if in function context
- Compare against generator->function_params array
- Return true if match found, false otherwise

### 2.14 Variable Name Formatting
**Function:** `format_local_var(Generator *generator, Token *token, char *buffer, size_t buffer_size)`
**Location:** Lines 549-562
**Purpose:** Format local variable name with scope suffix
**Implementation:**
- If parameter: format as "LF@param" (no suffix)
- If local variable: format as "LF@var$N" where N is scope suffix
- Use get_scope_suffix() to determine N

---

## 3. Terminal Node Generation

### 3.1 Terminal Handler
**Function:** `generate_terminal(Generator *generator, tree_node_t *node)`
**Location:** Lines 566-691
**Purpose:** Generate code for terminal nodes (literals, identifiers)

**Implementation by Token Type:**

#### 3.1.1 Numbers (TOKEN_T_NUM)
- Convert to hexadecimal format using convert_float_to_hex_format()
- Set generator->is_float = true
- Emit: PUSHS float@<hex_value>
- Free converted hex string

#### 3.1.2 Strings (TOKEN_T_STRING)
- Remove surrounding quotes if present
- Convert escape sequences using convert_string()
- Store converted string in generator->string_variable
- Emit: PUSHS string@<converted_string>
- Handle memory allocation errors

#### 3.1.3 Keywords (TOKEN_T_KEYWORD)
- "true" → PUSHS bool@true
- "false" → PUSHS bool@false
- "null" → PUSHS nil@nil

#### 3.1.4 Identifiers (TOKEN_T_IDENTIFIER, TOKEN_T_GLOBAL_VAR)
- Search symbol table for identifier
- Check for getter with "getter+" prefix
- If getter found:
  - Set is_called = true
  - Emit: CALL <name>_
  - Reset is_called flag
- Else if global variable:
  - Emit: PUSHS GF@<name>
- Else (local variable):
  - Format with scope suffix using format_local_var()
  - Emit: PUSHS LF@<name>$<scope>

---

## 4. Expression Generation

### 4.1 Type Checking
**Function:** `generate_type_check(Generator *generator, tree_node_t *node)`
**Location:** Lines 694-740
**Purpose:** Generate code for "is" type checking operator

**Implementation:**
- Evaluate left operand (expression)
- Check right operand type:
  - "Num": Check if type is "int" OR "float" using TYPES and EQS
  - "String": Check if type is "string"
  - "Null": Compare with nil@nil
  - Other: Compare types of both operands
- Push boolean result on stack

### 4.2 Type Inference
**Function:** `is_string_type(Generator *generator, tree_node_t *node)`
**Location:** Lines 745-787
**Purpose:** Determine if expression evaluates to string type

**Implementation:**
- Terminal check: TOKEN_T_STRING or string variable
- Binary +: Either operand is string → result is string
- Binary *: Left is string AND right is number → result is string
- Function calls: Check for string-returning functions (read_str, str, substring, chr)

**Function:** `is_number_type(Generator *generator, tree_node_t *node)`
**Location:** Lines 790-828
**Purpose:** Determine if expression evaluates to number type

**Implementation:**
- Terminal check: TOKEN_T_NUM or numeric variable
- Binary arithmetic (+, -, *, /): Both operands are numbers → result is number
- Function calls: Check for numeric-returning functions (read_num, floor, length, ord)

### 4.3 Type Conversion for Output
**Function:** `convert_float_to_int_if_needed(Generator *generator, const char *temp)`
**Location:** Lines 831-853
**Purpose:** Convert float to int before WRITE instruction

**Implementation:**
- Get type of value using TYPES
- Compare with "float"
- If float: Emit FLOAT2INTS, pop result back to temp variable
- Use label to skip conversion if not float

### 4.4 Concatenation Operand Preparation
**Function:** `get_operand_for_concat(Generator *generator, tree_node_t *node)`
**Location:** Lines 856-923
**Purpose:** Prepare operand for string concatenation

**Implementation:**
- If string literal: Convert and return "string@<converted>"
- If identifier (not getter): Return variable reference ("GF@var" or "LF@var$N")
- If getter: Evaluate and store in temp
- If complex expression: Evaluate and store in temp
- Return allocated string with operand reference

### 4.5 Main Expression Generator
**Function:** `generate_expression(Generator *generator, tree_node_t *node)`
**Location:** Lines 926-1108
**Purpose:** Generate code for any expression

**Implementation Flow:**

1. **Terminal Check:**
   - If terminal (and not operator with children): Call generate_terminal()
   - Return

2. **Predicate Unwrapping:**
   - If NONTERMINAL_T_PREDICATE: Process first child as expression
   - Return

3. **Unary Operators:**
   - Unary minus (-): Evaluate operand, emit NEGS
   - Unary NOT (!): Evaluate operand, emit NOTS

4. **Binary Operators:**
   - **Type check (is):** Call generate_type_check()
   - **String concatenation (+):**
     - If either operand is string: Use CONCAT instruction
     - Get operands using get_operand_for_concat()
     - Emit: CONCAT result_var op1 op2
     - Push result
   - **String repetition (*):**
     - If left is string AND right is number:
       - Generate loop: Initialize result = "", counter = 0
       - Loop: while counter < count: result = result + str, counter++
       - Push result
   - **Other operators:**
     - Evaluate left operand
     - Evaluate right operand
     - Get operator instruction using get_operator_instruction()
     - Emit instruction (ADDS, SUBS, MULS, DIVS, EQS, etc.)

5. **Function Calls:**
   - If NONTERMINAL_T_FUN_CALL or NONTERMINAL_T_EXPRESSION_OR_FN:
     - Call generator_generate() to handle function call

6. **Recursive Processing:**
   - For other non-terminals: Recursively process all children

---

## 5. Main Generation Entry Points

### 5.1 Generator Start
**Function:** `generator_start(Generator *generator, tree_node_t *tree)`
**Location:** Lines 1111-1127
**Purpose:** Entry point for code generation

**Implementation:**
1. Validate generator and tree pointers
2. Emit header: ".IFJcode25"
3. Emit jump to main: "JUMP main"
4. Iterate through tree children
5. Call generator_generate() for each child
6. Return error code

### 5.2 Node Router
**Function:** `generator_generate(Generator *generator, tree_node_t *node)`
**Location:** Lines 1130-1190
**Purpose:** Route to appropriate generator based on node type

**Routing Table:**
- NONTERMINAL_T_CODE_BLOCK → generate_code_block()
- NONTERMINAL_T_SEQUENCE → generate_sequence()
- NONTERMINAL_T_INSTRUCTION → generate_instruction()
- NONTERMINAL_T_EXPRESSION → generate_expression()
- NONTERMINAL_T_EXPRESSION_OR_FN → Recursive generator_generate()
- NONTERMINAL_T_ASSIGNMENT → generate_assignment()
- NONTERMINAL_T_DECLARATION → generate_declaration() or generate_function_declaration()
- NONTERMINAL_T_FUN_CALL → generate_function_call()
- NONTERMINAL_T_IF → generate_if()
- NONTERMINAL_T_WHILE → generate_while()
- NONTERMINAL_T_RETURN → generate_return()
- NONTERMINAL_T_PREDICATE → generate_expression()
- Default: Recursive generator_generate() for all children

---

## 6. Statement Generation

### 6.1 Code Block Generation
**Function:** `generate_code_block(Generator *generator, tree_node_t *node)`
**Location:** Lines 1192-1217
**Purpose:** Generate code for code blocks (scope management)

**Implementation:**
1. Increment current_scope
2. Iterate through children
3. Skip terminal tokens: "{", "}", "\n", "n"
4. Recursively call generator_generate() for non-terminals
5. Decrement current_scope

### 6.2 Sequence Generation
**Function:** `generate_sequence(Generator *generator, tree_node_t *node)`
**Location:** Lines 1220-1236
**Purpose:** Generate sequential statements

**Implementation:**
1. Iterate through children
2. Skip newline terminals
3. Recursively process all other children

### 6.3 Instruction Generation
**Function:** `generate_instruction(Generator *generator, tree_node_t *node)`
**Location:** Lines 1239-1246
**Purpose:** Process instruction nodes

**Implementation:**
- Recursively process all children using generator_generate()

### 6.4 Assignment Generation
**Function:** `generate_assignment(Generator *generator, tree_node_t *node)`
**Location:** Lines 1249-1319
**Purpose:** Generate assignment statements

**Implementation:**
1. Extract identifier node (first child)
2. Find expression node (skip "=" terminal)
3. Search for setter with "setter+" prefix
4. Save old variable context
5. Set generator->variable and generator->is_global
6. Evaluate expression:
   - If function call: Use generator_generate()
   - Otherwise: Use generate_expression()
7. Handle assignment:
   - If setter found: Call setter function (CALL <name>__)
   - Else if global: POPS GF@<name>
   - Else: Format local var, POPS LF@<name>$<scope>
8. Restore variable context

### 6.5 Declaration Generation
**Function:** `generate_declaration(Generator *generator, tree_node_t *node)`
**Location:** Lines 1322-1365
**Purpose:** Generate variable declarations

**Implementation:**
1. Check if function declaration (GR_FUN_DECLARATION):
   - Call generate_function_declaration()
   - Return
2. For variable declarations (GR_DECLARATION):
   - Extract identifier token
   - Search symbol table
   - If global: Skip DEFVAR (handled in main)
   - If local (and not in while loop): Emit DEFVAR LF@<name>$<scope>
3. If initialization present:
   - Evaluate expression
   - Store result: POPS GF@<name> (global) or POPS LF@<name>$<scope> (local)

---

## 7. Function Handling

### 7.1 Function Declaration Generation
**Function:** `generate_function_declaration(Generator *generator, tree_node_t *node)`
**Location:** Lines 1368-1626
**Purpose:** Generate complete function code

**Implementation Steps:**

#### Step 1: Function Identification (Lines 1372-1393)
- Extract function name from terminal node
- Store in generator->current_function
- Set in_function = 1
- Reset has_return = false
- Clear previous parameter list

#### Step 2: Function Type Detection (Lines 1410-1412)
- Call diverse_function() to determine type (function/getter/setter)
- Strip prefix if present ("getter+" or "setter+")

#### Step 3: Label Generation (Lines 1422-1446)
- Getter: LABEL <name>_
- Setter: LABEL <name>__
- Regular function:
  - If overloaded (declaration_count > 1):
    - Find overload index by line number
    - LABEL <name>$<index>
  - Otherwise: LABEL <name>

#### Step 4: Frame Setup (Lines 1448-1458)
- Emit: CREATEFRAME
- Emit: PUSHFRAME
- Set tf_created = false (TF is now LF)
- If main function:
  - For each global variable:
    - DEFVAR GF@<name>
    - Initialize with nil: PUSHS nil@nil, POPS GF@<name>

#### Step 5: Parameter Handling (Lines 1465-1566)

**For Setters (Lines 1465-1494):**
- Extract parameter from GR_SETTER_DECLARATION
- Store parameter name in function_params array
- Emit: DEFVAR LF@<param>
- Emit: POPS LF@<param>

**For Regular Functions (Lines 1496-1566):**
- Find GR_FUN_PARAM node in AST
- Collect all parameter tokens (traverse linked list)
- Store in generator->function_params array
- For each parameter:
  - Emit: DEFVAR LF@<param>
- Pop parameters in reverse order (last pushed first):
  - For i = count-1 to 0: POPS LF@<param[i]>

#### Step 6: Body Generation (Lines 1568-1591)
- Find CODE_BLOCK node in children
- Call generate_code_block() recursively

#### Step 7: Cleanup and Return (Lines 1593-1626)
- If main function:
  - Emit: POPFRAME
- Else if no explicit return (has_return == false):
  - Emit: PUSHS nil@nil (implicit return)
  - Emit: POPFRAME
  - Emit: RETURN
- Reset in_function = 0
- Free parameter list
- Free current_function string

---

## 8. Function Call Generation

### 8.1 Built-in Parameter Validation
**Function:** `check_builtin_params(Generator *generator, char *func_name, tree_node_t *node)`
**Location:** Lines 1629-1782
**Purpose:** Validate built-in function parameter types

**Implementation:**
- Extract function suffix (skip "Ifj." prefix)
- Define expected types array:
  - length, ord: [string]
  - chr: [int]
  - floor: [float]
  - substring: [string, int, int]
  - strcmp: [string, string]
- Iterate through arguments:
  - Drill down to terminal token
  - Check literal types (TOKEN_T_NUM, TOKEN_T_STRING)
  - Check variable types from symbol table
  - Set error if type mismatch
- Skip validation for write, read functions

### 8.2 Function Call Generation
**Function:** `generate_function_call(Generator *generator, tree_node_t *node)`
**Location:** Lines 1785-2225
**Purpose:** Generate code for function calls

**Implementation:**

#### Step 1: Function Name Resolution (Lines 1789-1827)
- Extract function name from terminal node
- Check for "Ifj." prefix keyword
- If prefix found: Construct "Ifj.<name>"
- If no prefix but built-in name: Add "Ifj." prefix
- Built-in names: write, read_num, read_str, length, floor, str, ord, substring, strcmp, chr

#### Step 2: Built-in Function Handling (Lines 1841-2142)

**Ifj.write (Lines 1848-1881):**
- Iterate through arguments
- Evaluate each expression
- Store in temp variable
- Convert float to int if needed
- Emit: WRITE <temp>
- Emit: PUSHS nil@nil (return value)

**Ifj.read_num (Lines 1882-1888):**
- Emit: DEFVAR <temp>
- Emit: READ <temp> float
- Emit: PUSHS <temp>

**Ifj.read_str (Lines 1889-1895):**
- Emit: DEFVAR <temp>
- Emit: READ <temp> string
- Emit: PUSHS <temp>

**Ifj.length (Lines 1896-1921):**
- Evaluate string argument
- Emit: STRLEN <len_var> <str_var>
- Emit: PUSHS <len_var>
- Emit: INT2FLOATS (convert to float)

**Ifj.floor (Lines 1922-1937):**
- Evaluate float argument
- Emit: FLOAT2INTS

**Ifj.substring (Lines 1938-2027):**
- Evaluate all three arguments (string, pos1, pos2)
- Pop into variables: s, p1, p2
- Validate bounds:
  - Check p1 < 0, p2 < 0 → return nil
  - Check p1 > p2 → return nil
  - Check p1 >= len, p2 >= len → return nil
- Generate loop:
  - Initialize result = ""
  - Loop: while p1 <= p2: result += s[p1], p1++
  - Use GETCHAR and CONCAT instructions
- Push result or nil

**Ifj.str (Lines 2028-2047):**
- Evaluate argument
- If is_float flag set: Emit FLOAT2STRS
- Otherwise: Emit INT2STRS
- Reset is_float flag

**Ifj.strcmp (Lines 2048-2050):**
- Call generate_strcmp_comparison()

**Ifj.ord (Lines 2051-2110):**
- Evaluate string and index arguments
- Validate: index < 0 or index >= length → return 0
- Emit: STRI2INT <result> <string> <index>
- Emit: INT2FLOATS
- Push result

**Ifj.chr (Lines 2111-2139):**
- Evaluate integer argument
- Emit: INT2CHAR <result> <num>
- Push result

#### Step 3: User Function Calls (Lines 2144-2225)
- Evaluate all arguments (push on stack in order)
- Search symbol table for function
- If overloaded (declaration_count > 1):
  - Count arguments in call
  - Find overload with matching parameter count
  - Emit: CALL <name>$<overload_index>
- Otherwise:
  - Emit: CALL <name>
- Set is_called = true during call, reset after

---

## 9. Control Flow Generation

### 9.1 If Statement Generation
**Function:** `generate_if(Generator *generator, tree_node_t *node)`
**Location:** Lines 2228-2313
**Purpose:** Generate if-else statements

**Implementation:**
1. Generate labels: else_label, end_label
2. Extract predicate, then_block, else_block nodes
3. Evaluate predicate:
   - If contains "!=" operator: Emit JUMPIFNEQS <else_label>
   - Otherwise:
     - Pop result to temp variable
     - Check for nil: JUMPIFEQS <else_label>
     - If has operator: Check for false: JUMPIFEQS <else_label>
4. Generate then block: Call generate_code_block()
5. Emit: JUMP <end_label>
6. Emit: LABEL <else_label>
7. Generate else block (if present): Call generate_code_block()
8. Emit: LABEL <end_label>
9. Free label strings

### 9.2 While Loop Generation
**Function:** `generate_while(Generator *generator, tree_node_t *node)`
**Location:** Lines 2361-2441
**Purpose:** Generate while loops

**Implementation:**
1. Generate labels: loop_label, end_label
2. Extract predicate and body_block nodes
3. Collect local variable declarations in body:
   - Call collect_local_vars_in_subtree()
   - Store in local_var_tokens array
4. Emit DEFVAR for all local variables BEFORE loop label
5. Emit: LABEL <loop_label>
6. Emit: CREATEFRAME (clear TF at start of iteration)
7. Set tf_created = true
8. Evaluate predicate:
   - If contains "!=": Emit JUMPIFNEQS <end_label>
   - Otherwise: Emit PUSHS bool@false, JUMPIFEQS <end_label>
9. Set in_while_loop = 1
10. Generate body: Call generate_code_block()
11. Reset in_while_loop = 0
12. Emit: JUMP <loop_label>
13. Emit: LABEL <end_label>
14. Free local_var_tokens array and labels

### 9.3 Return Statement Generation
**Function:** `generate_return(Generator *generator, tree_node_t *node)`
**Location:** Lines 2444-2530
**Purpose:** Generate return statements

**Implementation:**
1. Find return expression node:
   - Search for terminal with value token
   - Search for NONTERMINAL_T_EXPRESSION
   - Search for operator node
2. Evaluate expression:
   - If terminal: Call generate_terminal()
   - If operator: Call generate_expression()
   - If function call: Call generator_generate()
3. If not in call context (is_called == false):
   - If no expression: Emit PUSHS nil@nil
   - Emit: POPFRAME
   - Emit: RETURN
   - Set has_return = true

### 9.4 Local Variable Collection
**Function:** `collect_local_vars_in_subtree(tree_node_t *node, Token ***tokens, int *count, Symtable *symtable)`
**Location:** Lines 2316-2358
**Purpose:** Collect all local variable declarations in subtree

**Implementation:**
- Recursively traverse tree
- If node is GR_DECLARATION:
  - Extract identifier token
  - Skip temporary variables (starting with "tmp_")
  - Check if local (not global) in symbol table
  - Add to tokens array if not duplicate
- Process all children recursively

---

## 10. System Architecture

### 10.1 Execution Flow
1. **Initialization Phase:**
   - init_generator() creates generator structure
   - generate_global_vars() collects global variables

2. **Code Generation Phase:**
   - generator_start() emits header and jump to main
   - generator_generate() routes nodes to specific generators
   - Recursive tree traversal generates all code

3. **Cleanup Phase:**
   - generator_free() releases all memory

### 10.2 Stack-Based Evaluation
- All expressions push results onto stack
- Assignments pop from stack
- Function arguments pushed in order
- Function parameters popped in reverse order
- Return values pushed before RETURN

### 10.3 Scope Management
- Global variables: GF@<name> (no scope suffix)
- Function parameters: LF@<name> (no scope suffix)
- Local variables: LF@<name>$<scope> (with scope suffix)
- Scope numbers increment in nested blocks
- get_scope_suffix() resolves correct scope for references

### 10.4 Type System
- Numbers: Always stored as floats, converted when needed
- Strings: Handled with escape sequence conversion
- Booleans: true/false literals
- Nil: null keyword
- Type checking: "is" operator with TYPES instruction
- Type inference: is_string_type(), is_number_type()

### 10.5 Error Handling
- generator->error field stores error codes
- ERR_T_MALLOC_ERR: Memory allocation failure
- ERR_T_SEMANTIC_ERR_BAD_OPERAND_TYPES: Invalid operator usage
- ERR_T_SEMANTIC_ERR_BUILTIN_FN_BAD_PARAM: Built-in parameter type mismatch
- Functions return error codes for propagation

---

## 11. Key Design Patterns

### 11.1 Visitor Pattern
- generator_generate() acts as visitor router
- Each node type has dedicated generator function
- Recursive traversal of AST

### 11.2 State Management
- Generator structure maintains compilation state
- Scope tracking for variable resolution
- Function context for parameter handling
- Flags for special cases (in_while_loop, is_called, etc.)

### 11.3 Code Generation Strategy
- Instruction-by-instruction emission
- Label-based control flow
- Temporary variable management
- Frame-based memory model (GF, LF, TF)

---

## 12. Implementation Notes

### 12.1 Memory Management
- All allocated strings must be freed
- Labels and temp variables are allocated per use
- Parameter arrays allocated per function
- Global vars array allocated once

### 12.2 Special Cases
- Main function: Initializes global variables
- Getters/Setters: Special naming convention (_ and __ suffixes)
- Overloaded functions: Resolved by parameter count
- String operations: Special handling for + and * operators
- Type conversions: Automatic float/int conversion for output

### 12.3 Code Quality
- Error checking for malloc failures
- Null pointer validation
- Bounds checking for arrays
- Proper cleanup in all code paths

