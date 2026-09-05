#compdef janet

# Zsh completion for janet

_janet() {
    local -a opts

    opts=(
        '(* -){-h,--help}[Show usage and exit]'
        '(* -){-v,--version}[Show version and exit]'
        '{-s,--stdin}[Read raw stdin (no readline features)]'
        '{-e,--eval}[Execute Janet source string]:source code:'
        '{-E,--expression}[Execute Janet expression as short-fn with remaining args]:expression:'
        '{-d,--debug}[Enable debug mode]'
        '{-n,--nocolor}[Disable ANSI colors in REPL]'
        '{-N,--color}[Enable ANSI colors in REPL]'
        '{-r,--repl}[Open REPL after executing sources]'
        '{-R,--noprofile}[Disable loading user profile in REPL]'
        '{-p,--persistent}[Persistent mode (continue after errors)]'
        '{-q,--quiet}[Hide logo in REPL]'
        '{-k,--flycheck}[Compile only (lint), do not execute]'
        '{-i,--image}[Treat script as a .jimage file]'
        '{-m,--syspath}[Set syspath for module loading]:syspath:_directories'
        '{-c,--compile}[Precompile source to .jimage]:source:_files -g "*.janet" :output:_files'
        '{-l,--library}[Import module before script/REPL]:module:_files -g "*.janet"'
        '{-w,--lint-warn}[Set warning linting level]:level:(none relaxed normal strict)'
        '{-x,--lint-error}[Set error linting level]:level:(none relaxed normal strict)'
        '--[End of options]'
        '*:script:_files -g "*.janet"'
    )

    _arguments -s $opts
}

_janet "$@"
