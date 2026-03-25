###
### example/filewatch.janet ...files
###
### Watch for all changes in a list of files and directories. Behavior
### depends on the filewatch module, and different operating systems will
### report different events.

(def chan (ev/chan 1000))
(def fw (filewatch/new chan))
(each arg (drop 1 (dyn *args* []))
    (filewatch/add fw arg :all))
(filewatch/listen fw)

(forever (let [event (ev/take chan)] (pp event)))
