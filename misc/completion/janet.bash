# Bash completion for janet
# Install: source this file, or place it in /etc/bash_completion.d/janet
# or ~/.local/share/bash-completion/completions/janet

_janet() {
    local cur prev words cword
    _init_completion || return

    local flags="-h --help -v --version -s --stdin -e --eval -E --expression -d --debug -n --nocolor -N --color -r --repl -R --noprofile -p --persistent -q --quiet -k --flycheck -m --syspath -c --compile -i --image -l --library -w --lint-warn -x --lint-error --"

    case "$prev" in
        -e|--eval|-E|--expression)
            # Argument is Janet source code — no file completion
            return
            ;;
        -m|--syspath)
            # syspath: complete directories
            _filedir -d
            return
            ;;
        -c|--compile|-l|--library)
            # source file: complete .janet files
            _filedir janet
            return
            ;;
        -w|--lint-warn|-x|--lint-error)
            # linting level
            COMPREPLY=($(compgen -W ":none :relaxed :normal :strict" -- "$cur"))
            return
            ;;
    esac

    if [[ "$cur" == -* ]]; then
        COMPREPLY=($(compgen -W "$flags" -- "$cur"))
        return
    fi

    # Default: complete .janet files and directories
    _filedir janet
}

complete -F _janet janet
