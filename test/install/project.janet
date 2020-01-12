(declare-project
  :name "testmod")

(declare-native
    :name "testmod"
    :source @["testmod.c"])

(declare-native
    :name "testmod2"
    :source @["testmod2.c"])

(declare-executable
  :name "testexec"
  :entry "testexec.janet")
