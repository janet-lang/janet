# Copyright 2017-2018 (C) Calvin Rose
(print (string "Janet " janet/version "-" janet/build "  Copyright (C) 2017-2018 Calvin Rose"))

(fiber/new (fn webrepl []
  (repl (fn get-line [buf p]
          (def [line] (parser/where p))
          (def prompt (string "janet:" line ":" (parser/state p) "> "))
          (repl-yield prompt buf)
          (yield)
          buf))))
