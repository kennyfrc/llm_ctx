; Tree-sitter query patterns for JavaScript code entity extraction
; This file defines what code structures to capture for the codemap

; Function declarations
(function_declaration
  name: (identifier) @function.name
  parameters: (formal_parameters) @function.params) @function

; Function expressions assigned to variables  
(variable_declarator
  name: (identifier) @function.name
  value: (function_expression
    parameters: (formal_parameters) @function.params)) @function

; Arrow functions assigned to variables
(variable_declarator
  name: (identifier) @function.name
  value: (arrow_function
    parameters: (formal_parameters) @function.params)) @function

; Class declarations
(class_declaration
  name: (identifier) @class.name) @class

; Methods inside classes (with class context)
(class_declaration
  name: (identifier) @class.container
  body: (class_body
    (method_definition
      name: (property_identifier) @method.name
      parameters: (formal_parameters) @method.params))) @method

; Method definitions in objects  
(pair
  key: (property_identifier) @method.name
  value: (function_expression
    parameters: (formal_parameters) @method.params)) @method.object

(pair
  key: (property_identifier) @method.name  
  value: (arrow_function
    parameters: (formal_parameters) @method.params)) @method.object

; Export statements with functions
(export_statement
  declaration: (function_declaration
    name: (identifier) @function.name
    parameters: (formal_parameters) @function.params)) @function.export

; Default export functions
(export_statement
  (function_expression
    parameters: (formal_parameters) @function.params)) @function.default