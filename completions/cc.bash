#!/usr/bin/env bash
# cc 命令的 Tab 补全脚本

_cc_complete() {
    local cur prev
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    # 第一层：子命令补全
    if [ "$COMP_CWORD" -eq 1 ]; then
        COMPREPLY=($(compgen -W "list ls current cur add use switch edit test exec rm del remove rename mv show info export import-file backup update import help" -- "$cur"))
        return
    fi

    # 第二层：需要 profile 名称的子命令
    case "${COMP_WORDS[1]}" in
        use|switch|rm|del|remove|show|info|edit|test|exec|rename|mv)
            if [ "$COMP_CWORD" -eq 2 ]; then
                local names
                names=$(jq -r '.profiles | keys[]' ~/.cc-profiles/profiles.json 2>/dev/null)
                COMPREPLY=($(compgen -W "$names" -- "$cur"))
                return
            fi
            ;;
    esac

    # 选项补全
    case "$prev" in
        --key|--url|--tag)
            # 这些选项需要用户输入值，不补全
            return
            ;;
        --file|-f)
            COMPREPLY=($(compgen -f -- "$cur"))
            return
            ;;
    esac

    case "${COMP_WORDS[1]}" in
        add)
            COMPREPLY=($(compgen -W "--key --url --oauth --tag" -- "$cur"))
            ;;
        edit)
            COMPREPLY=($(compgen -W "--key --url --tag" -- "$cur"))
            ;;
        list|ls)
            COMPREPLY=($(compgen -W "--tag" -- "$cur"))
            ;;
        export)
            COMPREPLY=($(compgen -W "--file -f" -- "$cur"))
            ;;
        import-file)
            COMPREPLY=($(compgen -f -- "$cur"))
            ;;
        exec)
            if [ "$COMP_CWORD" -eq 3 ] && [ "$prev" != "--" ]; then
                COMPREPLY=($(compgen -W "--" -- "$cur"))
            fi
            ;;
    esac
}

complete -F _cc_complete cc
