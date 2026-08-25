# Fish completion for janet

# Disable file completion by default; re-enable for specific flags
complete -c janet -f

# Flags
complete -c janet -s h -d 'Show usage and exit'
complete -c janet -s v -d 'Show version and exit'
complete -c janet -s s -d 'Read raw stdin (no readline features)'
complete -c janet -s e -r -d 'Execute Janet source string'
complete -c janet -s E -r -d 'Execute Janet expression as short-fn with remaining args'
complete -c janet -s d -d 'Enable debug mode'
complete -c janet -s n -d 'Disable ANSI colors in REPL'
complete -c janet -s N -d 'Enable ANSI colors in REPL'
complete -c janet -s r -d 'Open REPL after executing sources'
complete -c janet -s R -d 'Disable loading user profile in REPL'
complete -c janet -s p -d 'Persistent mode (continue after errors)'
complete -c janet -s q -d 'Hide logo in REPL'
complete -c janet -s k -d 'Compile only (lint), do not execute'
complete -c janet -s i -d 'Treat script as a .jimage file'
complete -c janet -s m -r -d 'Set syspath for module loading' -a '(__fish_complete_directories)'
complete -c janet -s c -r -d 'Precompile source to .jimage'
complete -c janet -s l -r -d 'Import module before script/REPL' -a '(find . -name "*.janet" -printf "%f\n" 2>/dev/null)'
complete -c janet -s w -r -d 'Set warning linting level' -a ':none :relaxed :normal :strict'
complete -c janet -s x -r -d 'Set error linting level' -a ':none :relaxed :normal :strict'

# File arguments: .janet files and directories
complete -c janet -F -a '*.janet'
