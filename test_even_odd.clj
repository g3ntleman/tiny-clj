#!/usr/bin/env tiny-clj-repl

; Test even? and odd? functions
(println "Testing even? and odd? functions...")

; Test even? function
(println "even? 0:" (even? 0))
(println "even? 1:" (even? 1))
(println "even? 2:" (even? 2))
(println "even? 3:" (even? 3))
(println "even? 4:" (even? 4))
(println "even? 5:" (even? 5))

; Test odd? function  
(println "odd? 0:" (odd? 0))
(println "odd? 1:" (odd? 1))
(println "odd? 2:" (odd? 2))
(println "odd? 3:" (odd? 3))
(println "odd? 4:" (odd? 4))
(println "odd? 5:" (odd? 5))

; Test with negative numbers
(println "even? -1:" (even? -1))
(println "even? -2:" (even? -2))
(println "odd? -1:" (odd? -1))
(println "odd? -2:" (odd? -2))

(println "All tests completed!")
