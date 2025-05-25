; Tree-sitter query patterns for [Your Language] code entity extraction
; This file defines what code structures to capture for the codemap
;
; Reference: https://tree-sitter.github.io/tree-sitter/using-parsers/queries/
;
; Common capture names to use:
; - @function.name, @function.params for functions
; - @class.name for classes
; - @method.name, @method.params for methods
; - @type.name for types/interfaces
; - @<entity>.container for parent context

; Example: Function definitions
; Adjust the node types based on your language's grammar
(function_declaration
  name: (identifier) @function.name
  parameters: (parameter_list)? @function.params) @function

; Example: Class definitions
(class_declaration
  name: (identifier) @class.name) @class

; Example: Methods inside classes
(class_declaration
  name: (identifier) @class.container
  body: (class_body
    (method_definition
      name: (identifier) @method.name
      parameters: (parameter_list)? @method.params))) @method

; Add more patterns for your language's constructs:
; - Interfaces/protocols
; - Type definitions
; - Enums
; - Constants
; - etc.

; Tips:
; 1. Use the Tree-sitter playground to test your queries:
;    https://tree-sitter.github.io/tree-sitter/playground
; 2. Run `tree-sitter parse <file>` to see the AST structure
; 3. Start simple and add patterns incrementally
; 4. Test with real code files from your language