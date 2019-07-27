(declare-project
  :name "testmod")

(declare-native
    :name "testmod"
    :source @["testmod.c"])

(declare-executable
  :name "testexec"
  :entry "testexec.janet")
