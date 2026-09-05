# Fish completion for janet

# Disable file completion by default; re-enable for specific flags
complete -c janet -f

# Flags
complete -c janet -s h -l help -d 'Show usage and exit'
complete -c janet -s v -l version -d 'Show version and exit'
complete -c janet -s s -l stdin -d 'Read raw stdin (no readline features)'
complete -c janet -s e -l eval -r -d 'Execute Janet source string'
complete -c janet -s E -l expression -r -d 'Execute Janet expression as short-fn with remaining args'
complete -c janet -s d -l debug -d 'Enable debug mode'
complete -c janet -s n -l nocolor -d 'Disable ANSI colors in REPL'
complete -c janet -s N -l color -d 'Enable ANSI colors in REPL'
complete -c janet -s r -l repl -d 'Open REPL after executing sources'
complete -c janet -s R -l noprofile -d 'Disable loading user profile in REPL'
complete -c janet -s p -l persistent -d 'Persistent mode (continue after errors)'
complete -c janet -s q -l quiet -d 'Hide logo in REPL'
complete -c janet -s k -l flycheck -d 'Compile only (lint), do not execute'
complete -c janet -s i -l image -d 'Treat script as a .jimage file'
complete -c janet -s m -l syspath -r -d 'Set syspath for module loading' -a '(__fish_complete_directories)'
complete -c janet -s c -l compile -r -d 'Precompile source to .jimage'
complete -c janet -s l -l library -r -d 'Import module before script/REPL' -a '(find . -name "*.janet" -printf "%f\n" 2>/dev/null)'
complete -c janet -s w -l lint-warn -r -d 'Set warning linting level' -a ':none :relaxed :normal :strict'
complete -c janet -s x -l lint-error -r -d 'Set error linting level' -a ':none :relaxed :normal :strict'

# File arguments: .janet files and directories
complete -c janet -F -a '*.janet'
