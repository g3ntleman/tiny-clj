;; tinyclj.runtime - Runtime debugging utilities
;; Provides low-level debugging functions for inspecting internal structures

(ns tinyclj.runtime)

;; print-ast - Print AST structure with internals for debugging
;; Only available in DEBUG builds
^#^{:doc "Prints the AST (Abstract Syntax Tree) structure of an object with internal type information. Only available in DEBUG builds. Usage: (print-ast obj)"}
(defn print-ast [x] :native)

;; ast-string - Return AST structure as a string
;; Only available in DEBUG builds
^#^{:doc "Returns the AST (Abstract Syntax Tree) structure of an object as a string with internal type information. Only available in DEBUG builds. Usage: (ast-string obj)"}
(defn ast-string [x] :native)

