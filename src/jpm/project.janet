(declare-project
  :name "jpm")

(declare-source
  :source ["cc.janet"
           "cli.janet"
           "commands.janet"
           "config.janet"
           "dagbuild.janet"
           "declare.janet"
           "pm.janet"
           "rules.janet"
           "shutil.janet"]
  :prefix "jpm")

(declare-binscript
  :main "jpm"
  :hardcode-syspath true
  :auto-shebang true
  :is-janet true)
