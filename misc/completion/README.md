# Shell completions for Janet

This directory contains shell completion scripts for the `janet` interpreter.

## Bash

```sh
# Temporary (current session only)
source /path/to/janet.bash

# Permanent (system-wide)
sudo cp janet.bash /etc/bash_completion.d/janet

# Permanent (user only)
cp janet.bash ~/.local/share/bash-completion/completions/janet
```

## Zsh

```sh
# Copy to a directory in your $fpath, e.g.:
cp janet.zsh ~/.zsh/completions/_janet

# Ensure the directory is in $fpath (add to ~/.zshrc if needed):
# fpath=(~/.zsh/completions $fpath)
# autoload -Uz compinit && compinit
```

## Fish

```sh
cp janet.fish ~/.config/fish/completions/janet.fish
```
