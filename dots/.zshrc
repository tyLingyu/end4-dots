# ── History ────────────────────────────────────────────────
HISTFILE=~/.zsh_history HISTSIZE=50000 SAVEHIST=50000
setopt share_history hist_ignore_all_dups hist_ignore_space hist_reduce_blanks extended_history

# ── Shell options ──────────────────────────────────────────
setopt autocd auto_pushd pushd_ignore_dups interactive_comments no_beep
bindkey -e

# ── Completion ─────────────────────────────────────────────
autoload -Uz compinit && compinit -d "${XDG_CACHE_HOME:-$HOME/.cache}/zcompdump"
eval "$(dircolors -b)"
zstyle ':completion:*' menu no   # required by fzf-tab
zstyle ':completion:*' matcher-list 'm:{a-z}={A-Za-z}'
zstyle ':completion:*' list-colors ${(s.:.)LS_COLORS}
zstyle ':completion:*' group-name ''
zstyle ':completion:*:descriptions' format '%F{blue}%B── %d ──%b%f'
zstyle ':completion:*:warnings' format '%F{red}no matches%f'
zstyle ':completion:*' use-cache yes
zstyle ':completion:*' cache-path "${XDG_CACHE_HOME:-$HOME/.cache}/zsh/compcache"

# ── Key bindings ───────────────────────────────────────────
autoload -Uz up-line-or-beginning-search down-line-or-beginning-search
zle -N up-line-or-beginning-search
zle -N down-line-or-beginning-search
bindkey '^[[A' up-line-or-beginning-search
bindkey '^[OA' up-line-or-beginning-search
bindkey '^[[B' down-line-or-beginning-search
bindkey '^[OB' down-line-or-beginning-search
bindkey '^[[1;5C' forward-word
bindkey '^[[1;5D' backward-word
bindkey '^[[H' beginning-of-line
bindkey '^[[F' end-of-line
bindkey '^[[3~' delete-char

# ── Plugins & tools ────────────────────────────────────────
# fzf-tab: must come after compinit and before autosuggestions / syntax-highlighting
source ~/.local/share/fzf-tab/fzf-tab.plugin.zsh
zstyle ':fzf-tab:*' switch-group '<' '>'
zstyle ':fzf-tab:complete:cd:*' fzf-preview 'eza -1 --color=always --icons $realpath'
zstyle ':fzf-tab:complete:*:*' fzf-preview 'bat -n --color=always --line-range :100 $realpath 2>/dev/null || eza -1 --color=always --icons $realpath'
source /usr/share/zsh/plugins/zsh-autosuggestions/zsh-autosuggestions.zsh
ZSH_AUTOSUGGEST_HIGHLIGHT_STYLE='fg=#5b6b82'

# fzf: uses the terminal's ANSI palette, so it follows the wallpaper theme
export FZF_DEFAULT_OPTS="--height=45% --layout=reverse --border=rounded --info=inline-right \
--prompt='❯ ' --pointer='▶' --marker='✓' \
--color=fg:-1,bg:-1,hl:blue,fg+:-1,bg+:-1,hl+:blue:bold,info:cyan,prompt:blue,pointer:magenta,marker:green,spinner:cyan,header:cyan,border:blue,gutter:-1"
export FZF_CTRL_T_OPTS="--preview 'bat -n --color=always --line-range :200 {}'"
export FZF_ALT_C_OPTS="--preview 'eza --tree --level=2 --color=always --icons {}'"
source <(fzf --zsh)

eval "$(zoxide init zsh)"
eval "$(starship init zsh)"

# ── Transient prompt: submitted prompts collapse to "> command" ──
_transient_prompt='%B%F{green}>%f%b '
_transient_line_finish() {
  [[ -n $BUFFER ]] || return
  _full_prompt=$PROMPT _full_rprompt=$RPROMPT
  PROMPT=$_transient_prompt RPROMPT=''
  zle .reset-prompt
}
_transient_restore() {
  [[ -n $_full_prompt ]] || return
  PROMPT=$_full_prompt RPROMPT=$_full_rprompt
  unset _full_prompt _full_rprompt
}
autoload -Uz add-zle-hook-widget
add-zle-hook-widget zle-line-finish _transient_line_finish
precmd_functions+=(_transient_restore)

# ── Appearance helpers ─────────────────────────────────────
export BAT_THEME=ansi
export MANPAGER="sh -c 'col -bx | bat -l man -p'"

# window title: cwd at the prompt, command while running
precmd()  { print -Pn '\e]0;%~\a' }
preexec() { print -Pn "\e]0;${1[1,60]}\a" }

# ── Aliases ────────────────────────────────────────────────
alias ls='eza --icons=auto --group-directories-first'
alias ll='eza -lah --icons=auto --git --group-directories-first'
alias la='eza -a --icons=auto --group-directories-first'
alias lt='eza --tree --level=2 --icons=auto --group-directories-first'
alias cat='bat -pp'
alias grep='grep --color=auto'
alias diff='diff --color=auto'
alias ip='ip -c'
alias ..='cd ..'
alias ...='cd ../..'
# kitty doesn't clear scrollback properly with plain clear
alias clear="printf '\033[2J\033[3J\033[1;1H'"
[[ $TERM == xterm-kitty ]] && alias ssh='kitten ssh'

# ── Syntax highlighting (must be sourced last) ─────────────
typeset -A ZSH_HIGHLIGHT_STYLES
ZSH_HIGHLIGHT_STYLES[command]='fg=green,bold'
ZSH_HIGHLIGHT_STYLES[builtin]='fg=green,bold'
ZSH_HIGHLIGHT_STYLES[alias]='fg=green,bold'
ZSH_HIGHLIGHT_STYLES[function]='fg=green,bold'
ZSH_HIGHLIGHT_STYLES[precommand]='fg=green,underline'
ZSH_HIGHLIGHT_STYLES[path]='fg=blue,underline'
ZSH_HIGHLIGHT_STYLES[single-quoted-argument]='fg=yellow'
ZSH_HIGHLIGHT_STYLES[double-quoted-argument]='fg=yellow'
ZSH_HIGHLIGHT_STYLES[globbing]='fg=magenta'
ZSH_HIGHLIGHT_STYLES[unknown-token]='fg=red,bold'
source /usr/share/zsh/plugins/zsh-syntax-highlighting/zsh-syntax-highlighting.zsh

# ── Greeting ───────────────────────────────────────────────
fastfetch
