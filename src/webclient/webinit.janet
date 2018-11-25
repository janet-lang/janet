# Copyright 2017-2018 (C) Calvin Rose
(print (string "Janet " janet.version "  Copyright (C) 2017-2018 Calvin Rose"))

(fiber.new 
  (fn [&]
    (repl (fn [buf p]
            (def [line] (parser.where p))
            (def prompt (string "janet:" line ":" (parser.state p) "> "))
            (repl-yield prompt buf)
            buf)))
  :9e) # stop fiber on error signals and user9 signals
