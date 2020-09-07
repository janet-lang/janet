(declare-project
  :name "testmod")

(declare-native
    :name "testmod"
    :source @["testmod.c"])

(declare-native
    :name "testmod2"
    :source @["testmod2.c"])

(declare-native
    :name "testmod3"
    :source @["testmod3.cpp"])

(declare-native
    :name "test-mod-4"
    :source @["testmod4.c"])

(declare-executable
  :name "testexec"
  :entry "testexec.janet")
