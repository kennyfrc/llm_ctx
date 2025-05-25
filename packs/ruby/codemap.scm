; Tree-sitter query patterns for Ruby code entity extraction
; This file defines what code structures to capture for the codemap

; Method definitions 
(method
  name: (identifier) @method.name
  parameters: (method_parameters)? @method.params) @method

; Class definitions
(class
  name: (constant) @class.name) @class

; Module definitions (captured as types)
(module
  name: (constant) @type.name) @type