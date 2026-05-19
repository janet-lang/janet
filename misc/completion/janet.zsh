#compdef janet

# Zsh completion for janet

_janet() {
    local -a opts

    opts=(
        '-h[Show usage and exit]'
        '-v[Show version and exit]'
        '-s[Read raw stdin (no readline features)]'
        '-e[Execute Janet source string]:source code:'
        '-E[Execute Janet expression as short-fn with remaining args]:expression:'
        '-d[Enable debug mode]'
        '-n[Disable ANSI colors in REPL]'
        '-N[Enable ANSI colors in REPL]'
        '-r[Open REPL after executing sources]'
        '-R[Disable loading user profile in REPL]'
        '-p[Persistent mode (continue after errors)]'
        '-q[Hide logo in REPL]'
        '-k[Compile only (lint), do not execute]'
        '-i[Treat script as a .jimage file]'
        '-m[Set syspath for module loading]:syspath:_directories'
        '-c[Precompile source to .jimage]:source:_files -g "*.janet" :output:_files'
        '-l[Import module before script/REPL]:module:_files -g "*.janet"'
        '-w[Set warning linting level]:level:(none relaxed normal strict)'
        '-x[Set error linting level]:level:(none relaxed normal strict)'
        '--[End of options]'
        '*:script:_files -g "*.janet"'
    )

    _arguments -s $opts
}

_janet "$@"
