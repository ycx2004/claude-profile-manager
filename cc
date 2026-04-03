#!/usr/bin/env bash
# cc - Claude Code 账号/API Key 快速切换工具
# 项目地址: https://github.com/ycx2004/ccuse
# 数据目录: ~/.cc-profiles/
# 协议: MIT
# 作者: hyf

set -euo pipefail

PROFILES_DIR="$HOME/.cc-profiles"
PROFILES_FILE="$PROFILES_DIR/profiles.json"
CLAUDE_CONFIG="$HOME/.claude/config.json"
ENV_FILE="$PROFILES_DIR/env.sh"
REPO_URL="https://github.com/ycx2004/ccuse.git"

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

LOCK_FILE="$PROFILES_DIR/.lock"
LOCK_DIR="$PROFILES_DIR/.lockdir"

current_shell_rc() {
    case "$(basename "${SHELL:-bash}")" in
        zsh)
            echo "$HOME/.zshrc"
            ;;
        *)
            echo "$HOME/.bashrc"
            ;;
    esac
}

# 文件锁，防止多终端并发写坏 profiles.json
with_lock() {
    if command -v flock &>/dev/null; then
        exec 200>"$LOCK_FILE"
        flock -w 5 200 || { echo -e "${RED}获取锁超时，可能有其他 cc 进程在运行${NC}"; return 1; }
        "$@"
        local ret=$?
        exec 200>&-
        return $ret
    fi

    local waited=0
    while ! mkdir "$LOCK_DIR" 2>/dev/null; do
        if [ -f "$LOCK_DIR/pid" ]; then
            local holder_pid
            holder_pid=$(cat "$LOCK_DIR/pid" 2>/dev/null || true)
            if [ -n "$holder_pid" ] && ! kill -0 "$holder_pid" 2>/dev/null; then
                rm -rf "$LOCK_DIR" 2>/dev/null || true
                continue
            fi
        fi

        waited=$((waited + 1))
        if [ "$waited" -ge 5 ]; then
            echo -e "${RED}获取锁超时，可能有其他 cc 进程在运行${NC}"
            return 1
        fi
        sleep 1
    done

    echo "$$" > "$LOCK_DIR/pid"
    "$@"
    local ret=$?
    rm -rf "$LOCK_DIR" 2>/dev/null || true
    return $ret
}

# 初始化
init() {
    mkdir -p "$PROFILES_DIR"
    if [ ! -f "$PROFILES_FILE" ]; then
        echo '{"profiles":{},"active":""}' > "$PROFILES_FILE"
    fi
}

# 显示帮助
usage() {
    cat <<'EOF'
cc - Claude Code 账号快速切换工具

用法:
  cc list [--tag <TAG>]                        列出所有 profile（可按标签过滤）
  cc current                                   查看当前激活的 profile
  cc add <名称> --key <KEY> [--url <URL>] [--tag <TAG>]
                                               添加 API Key profile
  cc add <名称> --oauth [--tag <TAG>]          添加官方 OAuth 账号
  cc use [名称]                                切换到指定 profile（无参数用 fzf 选择）
  cc edit <名称> --key|--url|--tag <值>        就地修改 profile 属性
  cc test [名称]                               测试 profile 连通性
  cc exec <名称> -- <命令>                     临时使用某 profile 执行命令
  cc rm <名称>                                 删除 profile
  cc rename <旧名> <新名>                      重命名 profile
  cc show <名称>                               查看 profile 详情
  cc export [--file <路径>]                    导出 profiles（默认输出到 stdout）
  cc import-file <路径>                        从文件导入 profiles（合并模式）
  cc backup                                    备份当前配置
  cc update                                    从 GitHub 更新到最新版本
  cc help                                      显示帮助

示例:
  cc add work --key sk-ant-api03-xxx --tag production
  cc add runapi --key sk-xxx --url https://runapi.co --tag dev
  cc add personal --oauth
  cc use work
  cc use                                       # fzf 交互选择
  cc edit work --key sk-new-key
  cc test work
  cc exec runapi -- claude "你好"
  cc list --tag dev
  cc export --file backup.json
  cc import-file backup.json
EOF
}

# 读取 profiles
read_profiles() {
    jq -r "$1" "$PROFILES_FILE" 2>/dev/null
}

# 写入 profiles
write_profiles() {
    local tmp="$PROFILES_DIR/.tmp_profiles.json"
    echo "$1" > "$tmp"
    mv "$tmp" "$PROFILES_FILE"
}

# 获取当前激活的 profile
get_active() {
    read_profiles '.active // ""'
}

# 获取 key 脱敏显示（前6 + *** + 后4，适配各种 key 格式）
mask_key() {
    local key="$1"
    local len=${#key}
    if [ "$len" -gt 12 ]; then
        echo "${key:0:6}***${key: -4}"
    else
        echo "***"
    fi
}

# 列出所有 profile
cmd_list() {
    local active
    active=$(get_active)
    local tag_filter=""

    # 解析 --tag 参数
    while [ $# -gt 0 ]; do
        case "$1" in
            --tag)
                if [ $# -lt 2 ]; then
                    echo -e "${RED}--tag 后面需要跟标签名${NC}"
                    return 1
                fi
                tag_filter="$2"
                shift 2
                ;;
            *)
                echo -e "${RED}未知参数: $1${NC}"
                return 1
                ;;
        esac
    done

    local count
    count=$(read_profiles '.profiles | length')
    if [ "$count" -eq 0 ] || [ "$count" = "null" ]; then
        echo -e "${YELLOW}还没有任何 profile，用 cc add 添加一个${NC}"
        return
    fi

    echo -e "${BOLD}Profile 列表:${NC}"
    echo ""
    printf "  ${BOLD}%-4s %-20s %-10s %-10s %-30s${NC}\n" "" "名称" "类型" "标签" "标识"
    echo "  ────────────────────────────────────────────────────────────────────────"

    # 单次 jq 调用拿到所有数据，用 | 分隔（避免 bash read 合并连续 tab）
    local SEP='|'
    jq -r --arg active "$active" --arg sep "$SEP" '
      .profiles | to_entries[] |
      [.key, .value.type, (.value.key // ""), (.value.url // ""), (.value.tag // ""),
       (if .key == $active then "1" else "0" end)] | join($sep)
    ' "$PROFILES_FILE" 2>/dev/null | while IFS="$SEP" read -r name type key url tag is_active; do
        # 标签过滤
        if [ -n "$tag_filter" ] && [ "$tag" != "$tag_filter" ]; then
            continue
        fi

        local marker="  "
        local color=""
        if [ "$is_active" = "1" ]; then
            marker="▶ "
            color="$GREEN"
        fi

        local ident=""
        if [ "$type" = "api_key" ]; then
            if [ -n "$url" ]; then
                ident="$(mask_key "$key") @ $url"
            else
                ident=$(mask_key "$key")
            fi
        else
            ident="OAuth 登录"
        fi

        local tag_display="${tag:--}"
        printf "  ${color}${marker}%-20s %-10s %-10s %-30s${NC}\n" "$name" "$type" "$tag_display" "$ident"
    done
    echo ""
}

# 查看当前 profile
cmd_current() {
    local active
    active=$(get_active)

    if [ -z "$active" ]; then
        echo -e "${YELLOW}当前没有激活的 profile${NC}"
        # 检查是否有直接配置的 key
        if [ -f "$CLAUDE_CONFIG" ]; then
            local current_key
            current_key=$(jq -r '.primaryApiKey // empty' "$CLAUDE_CONFIG" 2>/dev/null)
            if [ -n "$current_key" ]; then
                echo -e "但 config.json 中存在 API Key: $(mask_key "$current_key")"
            fi
        fi
        return
    fi

    local type
    type=$(read_profiles ".profiles[\"$active\"].type")

    echo -e "${GREEN}▶ 当前 profile: ${BOLD}$active${NC}"
    echo -e "  类型: $type"

    if [ "$type" = "api_key" ]; then
        local key url_val
        key=$(read_profiles ".profiles[\"$active\"].key")
        url_val=$(read_profiles ".profiles[\"$active\"].url // empty")
        echo -e "  Key:  $(mask_key "$key")"
        if [ -n "$url_val" ]; then
            echo -e "  URL:  $url_val"
        fi
    else
        echo -e "  认证: OAuth 官方登录"
    fi
}

# 添加 profile
cmd_add() {
    if [ $# -lt 2 ]; then
        echo -e "${RED}用法: cc add <名称> --key <API_KEY> 或 cc add <名称> --oauth${NC}"
        return 1
    fi

    local name="$1"
    shift

    # 检查是否已存在
    local exists
    exists=$(read_profiles ".profiles[\"$name\"] // empty")
    if [ -n "$exists" ] && [ "$exists" != "null" ]; then
        echo -e "${RED}Profile '$name' 已存在，先用 cc rm $name 删除${NC}"
        return 1
    fi

    local type=""
    local key=""
    local url=""
    local tag=""

    while [ $# -gt 0 ]; do
        case "$1" in
            --key)
                type="api_key"
                if [ $# -lt 2 ]; then
                    echo -e "${RED}--key 后面需要跟 API Key${NC}"
                    return 1
                fi
                key="$2"
                shift 2
                ;;
            --url)
                if [ $# -lt 2 ]; then
                    echo -e "${RED}--url 后面需要跟 Base URL${NC}"
                    return 1
                fi
                url="$2"
                # 去掉尾部斜杠
                url="${url%/}"
                shift 2
                ;;
            --oauth)
                type="oauth"
                shift
                ;;
            --tag)
                if [ $# -lt 2 ]; then
                    echo -e "${RED}--tag 后面需要跟标签名${NC}"
                    return 1
                fi
                tag="$2"
                shift 2
                ;;
            *)
                echo -e "${RED}未知参数: $1${NC}"
                return 1
                ;;
        esac
    done

    if [ -z "$type" ]; then
        echo -e "${RED}必须指定 --key 或 --oauth${NC}"
        return 1
    fi

    # 构建 profile
    local profiles_data
    profiles_data=$(cat "$PROFILES_FILE")

    if [ "$type" = "api_key" ]; then
        local base_obj='{"type": "api_key", "key": $key, "created": (now | todate)}'
        if [ -n "$url" ]; then
            base_obj='{"type": "api_key", "key": $key, "url": $url, "created": (now | todate)}'
        fi
        if [ -n "$tag" ]; then
            base_obj="${base_obj%\}}, \"tag\": \$tag}"
        fi
        if [ -n "$url" ] && [ -n "$tag" ]; then
            profiles_data=$(echo "$profiles_data" | jq \
                --arg name "$name" --arg key "$key" --arg url "$url" --arg tag "$tag" \
                '.profiles[$name] = {"type": "api_key", "key": $key, "url": $url, "tag": $tag, "created": (now | todate)}')
        elif [ -n "$url" ]; then
            profiles_data=$(echo "$profiles_data" | jq \
                --arg name "$name" --arg key "$key" --arg url "$url" \
                '.profiles[$name] = {"type": "api_key", "key": $key, "url": $url, "created": (now | todate)}')
        elif [ -n "$tag" ]; then
            profiles_data=$(echo "$profiles_data" | jq \
                --arg name "$name" --arg key "$key" --arg tag "$tag" \
                '.profiles[$name] = {"type": "api_key", "key": $key, "tag": $tag, "created": (now | todate)}')
        else
            profiles_data=$(echo "$profiles_data" | jq \
                --arg name "$name" --arg key "$key" \
                '.profiles[$name] = {"type": "api_key", "key": $key, "created": (now | todate)}')
        fi
    else
        if [ -n "$tag" ]; then
            profiles_data=$(echo "$profiles_data" | jq \
                --arg name "$name" --arg tag "$tag" \
                '.profiles[$name] = {"type": "oauth", "tag": $tag, "created": (now | todate)}')
        else
            profiles_data=$(echo "$profiles_data" | jq \
                --arg name "$name" \
                '.profiles[$name] = {"type": "oauth", "created": (now | todate)}')
        fi
    fi

    write_profiles "$profiles_data"
    echo -e "${GREEN}✓ 已添加 profile: ${BOLD}$name${NC} (${type})"
    [ -n "$tag" ] && echo -e "  标签: $tag"

    # 如果是第一个 profile，自动激活
    local count
    count=$(echo "$profiles_data" | jq '.profiles | length')
    if [ "$count" -eq 1 ]; then
        echo -e "${CYAN}  这是第一个 profile，自动切换...${NC}"
        cmd_use "$name"
    fi
}

# 切换 profile
cmd_use() {
    local name=""

    if [ $# -lt 1 ]; then
        # 无参数：尝试 fzf 交互选择
        local names
        names=$(read_profiles '.profiles | keys[]' 2>/dev/null)
        if [ -z "$names" ]; then
            echo -e "${YELLOW}还没有任何 profile${NC}"
            return 1
        fi

        if command -v fzf &>/dev/null; then
            local active
            active=$(get_active)
            name=$(read_profiles '.profiles | keys[]' 2>/dev/null | \
                fzf --prompt="选择 profile: " \
                    --header="当前: ${active:-无}" \
                    --height=~40% --reverse)
            if [ -z "$name" ]; then
                echo -e "${YELLOW}已取消${NC}"
                return
            fi
        else
            echo -e "${RED}用法: cc use <名称>${NC}"
            echo -e "${CYAN}提示: 安装 fzf 后可直接 cc use 交互选择${NC}"
            echo -e "可用的 profile:"
            read_profiles '.profiles | keys[]' 2>/dev/null | sed 's/^/  /'
            return 1
        fi
    else
        name="$1"
    fi

    local profile
    profile=$(read_profiles ".profiles[\"$name\"] // empty")

    if [ -z "$profile" ] || [ "$profile" = "null" ]; then
        echo -e "${RED}Profile '$name' 不存在${NC}"
        echo -e "可用的 profile:"
        read_profiles '.profiles | keys[]' 2>/dev/null | sed 's/^/  /'
        return 1
    fi

    local type
    type=$(read_profiles ".profiles[\"$name\"].type")

    # 备份当前配置
    if [ -f "$CLAUDE_CONFIG" ]; then
        cp "$CLAUDE_CONFIG" "$PROFILES_DIR/.config_backup.json"
    fi

    if [ "$type" = "api_key" ]; then
        _apply_profile "$name"

        local key url
        key=$(read_profiles ".profiles[\"$name\"].key")
        url=$(read_profiles ".profiles[\"$name\"].url // empty")

        if [ -n "$url" ]; then
            echo -e "${GREEN}✓ 已切换到: ${BOLD}$name${NC} (API Key: $(mask_key "$key"))"
            echo -e "  Base URL: $url"
        else
            echo -e "${GREEN}✓ 已切换到: ${BOLD}$name${NC} (API Key: $(mask_key "$key"))"
        fi

    elif [ "$type" = "oauth" ]; then
        # OAuth: 清除 config.json 中的 API Key，让 claude 走 OAuth 流程
        if [ -f "$CLAUDE_CONFIG" ]; then
            local new_config
            new_config=$(jq 'del(.primaryApiKey) | del(.customApiKeyResponses)' "$CLAUDE_CONFIG")
            echo "$new_config" > "$CLAUDE_CONFIG"
        fi

        # OAuth: 清除 settings.json 中的 ANTHROPIC_BASE_URL
        local CLAUDE_SETTINGS="$HOME/.claude/settings.json"
        if [ -f "$CLAUDE_SETTINGS" ]; then
            local new_settings
            new_settings=$(jq 'del(.env.ANTHROPIC_BASE_URL) | if .env == {} then del(.env) else . end' "$CLAUDE_SETTINGS")
            echo "$new_settings" > "$CLAUDE_SETTINGS"
        fi

        # OAuth: 清除 API key 和 base url
        {
            echo "unset ANTHROPIC_API_KEY 2>/dev/null || true"
            echo "unset ANTHROPIC_BASE_URL 2>/dev/null || true"
        } > "$ENV_FILE"

        echo -e "${GREEN}✓ 已切换到: ${BOLD}$name${NC} (OAuth)"
        echo -e "${CYAN}  正在执行 OAuth 登录流程...${NC}"
        echo ""
        claude /logout 2>/dev/null || true
        claude /login
    fi

    # 更新 active
    local profiles_data
    profiles_data=$(jq --arg name "$name" '.active = $name' "$PROFILES_FILE")
    write_profiles "$profiles_data"
}

# 删除 profile
cmd_rm() {
    if [ $# -lt 1 ]; then
        echo -e "${RED}用法: cc rm <名称>${NC}"
        return 1
    fi

    local name="$1"
    local profile
    profile=$(read_profiles ".profiles[\"$name\"] // empty")

    if [ -z "$profile" ] || [ "$profile" = "null" ]; then
        echo -e "${RED}Profile '$name' 不存在${NC}"
        return 1
    fi

    local active
    active=$(get_active)

    local profiles_data
    profiles_data=$(jq --arg name "$name" 'del(.profiles[$name])' "$PROFILES_FILE")

    # 如果删除的是当前激活的，清空 active
    if [ "$name" = "$active" ]; then
        profiles_data=$(echo "$profiles_data" | jq '.active = ""')
    fi

    write_profiles "$profiles_data"
    echo -e "${GREEN}✓ 已删除 profile: $name${NC}"

    if [ "$name" = "$active" ]; then
        echo -e "${YELLOW}  注意: 删除的是当前激活的 profile，请用 cc use 切换到其他 profile${NC}"
    fi
}

# 重命名 profile
cmd_rename() {
    if [ $# -lt 2 ]; then
        echo -e "${RED}用法: cc rename <旧名> <新名>${NC}"
        return 1
    fi

    local old_name="$1"
    local new_name="$2"

    local old_profile
    old_profile=$(read_profiles ".profiles[\"$old_name\"] // empty")
    if [ -z "$old_profile" ] || [ "$old_profile" = "null" ]; then
        echo -e "${RED}Profile '$old_name' 不存在${NC}"
        return 1
    fi

    local new_exists
    new_exists=$(read_profiles ".profiles[\"$new_name\"] // empty")
    if [ -n "$new_exists" ] && [ "$new_exists" != "null" ]; then
        echo -e "${RED}Profile '$new_name' 已存在${NC}"
        return 1
    fi

    local active
    active=$(get_active)

    local profiles_data
    profiles_data=$(jq \
        --arg old "$old_name" \
        --arg new "$new_name" \
        '.profiles[$new] = .profiles[$old] | del(.profiles[$old])' "$PROFILES_FILE")

    if [ "$old_name" = "$active" ]; then
        profiles_data=$(echo "$profiles_data" | jq --arg new "$new_name" '.active = $new')
    fi

    write_profiles "$profiles_data"
    echo -e "${GREEN}✓ 已重命名: $old_name → $new_name${NC}"
}

# 查看 profile 详情
cmd_show() {
    if [ $# -lt 1 ]; then
        echo -e "${RED}用法: cc show <名称>${NC}"
        return 1
    fi

    local name="$1"
    local profile
    profile=$(read_profiles ".profiles[\"$name\"] // empty")

    if [ -z "$profile" ] || [ "$profile" = "null" ]; then
        echo -e "${RED}Profile '$name' 不存在${NC}"
        return 1
    fi

    local type created tag_val
    type=$(read_profiles ".profiles[\"$name\"].type")
    created=$(read_profiles ".profiles[\"$name\"].created // \"unknown\"")
    tag_val=$(read_profiles ".profiles[\"$name\"].tag // empty")
    local active
    active=$(get_active)

    echo -e "${BOLD}Profile: $name${NC}"
    echo -e "  类型:   $type"
    echo -e "  创建:   $created"
    [ -n "$tag_val" ] && echo -e "  标签:   $tag_val"

    if [ "$name" = "$active" ]; then
        echo -e "  状态:   ${GREEN}激活中${NC}"
    else
        echo -e "  状态:   未激活"
    fi

    if [ "$type" = "api_key" ]; then
        local key url_val
        key=$(read_profiles ".profiles[\"$name\"].key")
        url_val=$(read_profiles ".profiles[\"$name\"].url // empty")
        echo -e "  Key:    $(mask_key "$key")"
        if [ -n "$url_val" ]; then
            echo -e "  URL:    $url_val"
        fi
    fi
}

# 测试 profile 连通性
cmd_test() {
    local name=""
    if [ $# -ge 1 ]; then
        name="$1"
    else
        name=$(get_active)
        if [ -z "$name" ]; then
            echo -e "${RED}当前没有激活的 profile，请指定名称: cc test <名称>${NC}"
            return 1
        fi
    fi

    local profile
    profile=$(read_profiles ".profiles[\"$name\"] // empty")
    if [ -z "$profile" ] || [ "$profile" = "null" ]; then
        echo -e "${RED}Profile '$name' 不存在${NC}"
        return 1
    fi

    local type
    type=$(read_profiles ".profiles[\"$name\"].type")

    if [ "$type" != "api_key" ]; then
        echo -e "${YELLOW}OAuth 类型 profile 无法通过 API 测试，请手动验证${NC}"
        return 1
    fi

    local key url_val base_url
    key=$(read_profiles ".profiles[\"$name\"].key")
    url_val=$(read_profiles ".profiles[\"$name\"].url // empty")

    if [ -n "$url_val" ]; then
        base_url="${url_val}"
    else
        base_url="https://api.anthropic.com"
    fi

    echo -e "${CYAN}测试 profile: ${BOLD}$name${NC}"
    echo -e "  Endpoint: ${base_url}/v1/messages"
    echo -ne "  连接中..."

    local http_code body curl_status curl_err host
    local curl_err_file
    curl_err_file=$(mktemp "${TMPDIR:-/tmp}/cc-cli-curl.XXXXXX")
    host="${base_url#*://}"
    host="${host%%/*}"

    set +e
    body=$(curl -sS -w "\n%{http_code}" -X POST "${base_url}/v1/messages" \
        -H "x-api-key: ${key}" \
        -H "anthropic-version: 2023-06-01" \
        -H "content-type: application/json" \
        -d '{"model":"claude-haiku-4-5-20251001","max_tokens":1,"messages":[{"role":"user","content":"hi"}]}' \
        --connect-timeout 10 --max-time 15 2>"$curl_err_file")
    curl_status=$?
    set -e

    http_code=$(printf '%s\n' "$body" | tail -1)
    body=$(printf '%s\n' "$body" | sed '$d')
    curl_err=$(tr '\n' ' ' < "$curl_err_file" 2>/dev/null | sed 's/[[:space:]]\+/ /g; s/^ //; s/ $//')
    rm -f "$curl_err_file"

    echo -ne "\r"

    if [ "$curl_status" -ne 0 ]; then
        echo -e "  ${RED}✗ 连接失败${NC} (curl exit $curl_status)"
        [ -n "$curl_err" ] && echo -e "  错误: $curl_err"

        case "$curl_status" in
            6)
                echo -e "  当前机器无法解析域名: $host"
                echo -e "  请优先检查 DNS、代理、VPN 或本机网络策略"
                ;;
            7)
                echo -e "  域名已解析，但无法建立 TCP 连接"
                echo -e "  请检查目标站点是否可达，或是否被代理/防火墙拦截"
                ;;
            28)
                echo -e "  请求超时，可能是 DNS、代理或网络链路异常"
                ;;
            35|60)
                echo -e "  TLS/SSL 握手失败，请检查证书链、代理或抓包工具"
                ;;
        esac
        return 1
    elif [ "$http_code" = "200" ]; then
        local model_used
        model_used=$(echo "$body" | jq -r '.model // "unknown"' 2>/dev/null)
        echo -e "  ${GREEN}✓ 连接成功${NC} (HTTP $http_code)"
        echo -e "  模型响应: $model_used"
    elif [ "$http_code" = "000" ]; then
        echo -e "  ${RED}✗ 连接失败${NC} (无法到达服务器)"
        echo -e "  请检查网络或 Base URL 是否正确"
    elif [ "$http_code" = "401" ]; then
        echo -e "  ${RED}✗ 认证失败${NC} (HTTP $http_code)"
        echo -e "  API Key 无效或已过期"
    elif [ "$http_code" = "403" ]; then
        echo -e "  ${RED}✗ 权限不足${NC} (HTTP $http_code)"
        local err_msg
        err_msg=$(echo "$body" | jq -r '.error.message // empty' 2>/dev/null)
        [ -n "$err_msg" ] && echo -e "  详情: $err_msg"
    else
        echo -e "  ${YELLOW}⚠ 异常响应${NC} (HTTP $http_code)"
        local err_msg
        err_msg=$(echo "$body" | jq -r '.error.message // empty' 2>/dev/null)
        [ -n "$err_msg" ] && echo -e "  详情: $err_msg"
    fi
}

# 就地修改 profile
cmd_edit() {
    if [ $# -lt 2 ]; then
        echo -e "${RED}用法: cc edit <名称> --key <KEY> | --url <URL> | --tag <TAG>${NC}"
        return 1
    fi

    local name="$1"
    shift

    local profile
    profile=$(read_profiles ".profiles[\"$name\"] // empty")
    if [ -z "$profile" ] || [ "$profile" = "null" ]; then
        echo -e "${RED}Profile '$name' 不存在${NC}"
        return 1
    fi

    local new_key="" new_url="" new_tag="" clear_url=false

    while [ $# -gt 0 ]; do
        case "$1" in
            --key)
                if [ $# -lt 2 ]; then
                    echo -e "${RED}--key 后面需要跟 API Key${NC}"
                    return 1
                fi
                new_key="$2"
                shift 2
                ;;
            --url)
                if [ $# -lt 2 ]; then
                    echo -e "${RED}--url 后面需要跟 Base URL（空字符串表示清除）${NC}"
                    return 1
                fi
                if [ -z "$2" ]; then
                    clear_url=true
                else
                    new_url="${2%/}"
                fi
                shift 2
                ;;
            --tag)
                if [ $# -lt 2 ]; then
                    echo -e "${RED}--tag 后面需要跟标签名${NC}"
                    return 1
                fi
                new_tag="$2"
                shift 2
                ;;
            *)
                echo -e "${RED}未知参数: $1${NC}"
                return 1
                ;;
        esac
    done

    local profiles_data
    profiles_data=$(cat "$PROFILES_FILE")

    if [ -n "$new_key" ]; then
        profiles_data=$(echo "$profiles_data" | jq --arg name "$name" --arg key "$new_key" \
            '.profiles[$name].key = $key')
        echo -e "${GREEN}✓ 已更新 Key: $(mask_key "$new_key")${NC}"
    fi

    if [ -n "$new_url" ]; then
        profiles_data=$(echo "$profiles_data" | jq --arg name "$name" --arg url "$new_url" \
            '.profiles[$name].url = $url')
        echo -e "${GREEN}✓ 已更新 URL: $new_url${NC}"
    elif [ "$clear_url" = true ]; then
        profiles_data=$(echo "$profiles_data" | jq --arg name "$name" \
            'del(.profiles[$name].url)')
        echo -e "${GREEN}✓ 已清除 URL${NC}"
    fi

    if [ -n "$new_tag" ]; then
        profiles_data=$(echo "$profiles_data" | jq --arg name "$name" --arg tag "$new_tag" \
            '.profiles[$name].tag = $tag')
        echo -e "${GREEN}✓ 已更新标签: $new_tag${NC}"
    fi

    write_profiles "$profiles_data"

    # 如果修改的是当前 active profile，重新生效
    local active
    active=$(get_active)
    if [ "$name" = "$active" ]; then
        echo -e "${CYAN}  当前为激活 profile，正在重新生效...${NC}"
        _apply_profile "$name"
    fi
}

# 内部函数：将 profile 配置写入 config.json、settings.json 和 env.sh（不更新 active）
_apply_profile() {
    local name="$1"
    local type
    type=$(read_profiles ".profiles[\"$name\"].type")

    mkdir -p "$HOME/.claude"
    local CLAUDE_SETTINGS="$HOME/.claude/settings.json"

    if [ "$type" = "api_key" ]; then
        local key url
        key=$(read_profiles ".profiles[\"$name\"].key")
        url=$(read_profiles ".profiles[\"$name\"].url // empty")
        local key_tail="${key: -20}"

        # 写入 config.json（API Key）
        local new_config
        if [ -f "$CLAUDE_CONFIG" ]; then
            new_config=$(jq \
                --arg key "$key" \
                --arg tail "$key_tail" \
                '.primaryApiKey = $key | .customApiKeyResponses.approved = [$tail]' \
                "$CLAUDE_CONFIG")
        else
            new_config=$(jq -n \
                --arg key "$key" \
                --arg tail "$key_tail" \
                '{"primaryApiKey": $key, "customApiKeyResponses": {"approved": [$tail]}}')
        fi

        local tmp="$HOME/.claude/.config_tmp.json"
        echo "$new_config" > "$tmp"
        mv "$tmp" "$CLAUDE_CONFIG"

        # 写入 settings.json 的 env 字段（让 IDE 侧边栏也能读到 base URL）
        if [ -n "$url" ]; then
            if [ -f "$CLAUDE_SETTINGS" ]; then
                local new_settings
                new_settings=$(jq --arg url "$url" \
                    '.env.ANTHROPIC_BASE_URL = $url' "$CLAUDE_SETTINGS")
                echo "$new_settings" > "$CLAUDE_SETTINGS"
            fi
        else
            # 无自定义 URL，清除 settings.json 中的 ANTHROPIC_BASE_URL
            if [ -f "$CLAUDE_SETTINGS" ]; then
                local new_settings
                new_settings=$(jq 'del(.env.ANTHROPIC_BASE_URL) | if .env == {} then del(.env) else . end' "$CLAUDE_SETTINGS")
                echo "$new_settings" > "$CLAUDE_SETTINGS"
            fi
        fi

        # 写入 env.sh（终端环境变量）
        {
            echo "export ANTHROPIC_API_KEY=\"$key\""
            if [ -n "$url" ]; then
                echo "export ANTHROPIC_BASE_URL=\"$url\""
            else
                echo "unset ANTHROPIC_BASE_URL 2>/dev/null || true"
            fi
        } > "$ENV_FILE"
    fi
}

# 临时切换执行命令（不修改全局状态）
cmd_exec() {
    if [ $# -lt 1 ]; then
        echo -e "${RED}用法: cc exec <名称> -- <命令>${NC}"
        return 1
    fi

    local name="$1"
    shift

    # 跳过 --
    if [ "${1:-}" = "--" ]; then
        shift
    fi

    if [ $# -eq 0 ]; then
        echo -e "${RED}缺少要执行的命令${NC}"
        echo -e "用法: cc exec <名称> -- <命令>"
        return 1
    fi

    local profile
    profile=$(read_profiles ".profiles[\"$name\"] // empty")
    if [ -z "$profile" ] || [ "$profile" = "null" ]; then
        echo -e "${RED}Profile '$name' 不存在${NC}"
        return 1
    fi

    local type
    type=$(read_profiles ".profiles[\"$name\"].type")

    if [ "$type" != "api_key" ]; then
        echo -e "${RED}exec 仅支持 api_key 类型 profile${NC}"
        return 1
    fi

    local key url
    key=$(read_profiles ".profiles[\"$name\"].key")
    url=$(read_profiles ".profiles[\"$name\"].url // empty")

    echo -e "${CYAN}临时使用 profile: ${BOLD}$name${NC} 执行命令..."

    # 在子 shell 中设置环境变量并执行
    (
        export ANTHROPIC_API_KEY="$key"
        if [ -n "$url" ]; then
            export ANTHROPIC_BASE_URL="$url"
        else
            unset ANTHROPIC_BASE_URL 2>/dev/null || true
        fi
        "$@"
    )
}

# 导出 profiles
cmd_export() {
    local output_file=""

    while [ $# -gt 0 ]; do
        case "$1" in
            --file|-f)
                if [ $# -lt 2 ]; then
                    echo -e "${RED}--file 后面需要跟文件路径${NC}"
                    return 1
                fi
                output_file="$2"
                shift 2
                ;;
            *)
                echo -e "${RED}未知参数: $1${NC}"
                return 1
                ;;
        esac
    done

    # 导出时去掉 active 字段，只导出 profiles
    local export_data
    export_data=$(jq '{profiles: .profiles}' "$PROFILES_FILE")

    if [ -n "$output_file" ]; then
        echo "$export_data" > "$output_file"
        local count
        count=$(echo "$export_data" | jq '.profiles | length')
        echo -e "${GREEN}✓ 已导出 $count 个 profile 到 $output_file${NC}"
    else
        echo "$export_data"
    fi
}

# 从文件导入 profiles（合并模式，不覆盖同名）
cmd_import_file() {
    if [ $# -lt 1 ]; then
        echo -e "${RED}用法: cc import-file <文件路径>${NC}"
        return 1
    fi

    local import_file="$1"
    if [ ! -f "$import_file" ]; then
        echo -e "${RED}文件不存在: $import_file${NC}"
        return 1
    fi

    # 验证 JSON 格式
    if ! jq empty "$import_file" 2>/dev/null; then
        echo -e "${RED}文件不是有效的 JSON${NC}"
        return 1
    fi

    local import_names
    import_names=$(jq -r '.profiles | keys[]' "$import_file" 2>/dev/null)
    if [ -z "$import_names" ]; then
        echo -e "${YELLOW}导入文件中没有 profile${NC}"
        return
    fi

    local added=0 skipped=0

    while IFS= read -r name; do
        local exists
        exists=$(read_profiles ".profiles[\"$name\"] // empty")

        if [ -n "$exists" ] && [ "$exists" != "null" ]; then
            echo -e "${YELLOW}  跳过 '$name'（已存在）${NC}"
            skipped=$((skipped + 1))
        else
            local profile_data
            profile_data=$(jq --arg name "$name" '.profiles[$name]' "$import_file")
            local profiles_data
            profiles_data=$(jq --arg name "$name" --argjson data "$profile_data" \
                '.profiles[$name] = $data' "$PROFILES_FILE")
            write_profiles "$profiles_data"
            echo -e "${GREEN}  ✓ 导入 '$name'${NC}"
            added=$((added + 1))
        fi
    done <<< "$import_names"

    echo -e "\n${GREEN}导入完成: ${added} 个新增, ${skipped} 个跳过${NC}"
}

# 自更新
cmd_update() {
    if ! command -v git &>/dev/null; then
        echo -e "${RED}需要 git 才能更新，请先安装 git${NC}"
        return 1
    fi

    local tmp_dir
    tmp_dir=$(mktemp -d)
    trap 'rm -rf "$tmp_dir"' EXIT

    echo -e "${CYAN}正在检查更新...${NC}"

    if ! git clone --depth 1 "$REPO_URL" "$tmp_dir" 2>/dev/null; then
        echo -e "${RED}✗ 无法连接到 GitHub 仓库${NC}"
        echo -e "  $REPO_URL"
        return 1
    fi

    # 对比版本（用文件 hash）
    local current_hash new_hash
    current_hash=$(md5sum "$0" 2>/dev/null | cut -d' ' -f1)
    new_hash=$(md5sum "$tmp_dir/cc" 2>/dev/null | cut -d' ' -f1)

    if [ "$current_hash" = "$new_hash" ]; then
        echo -e "${GREEN}✓ 已经是最新版本${NC}"
        return
    fi

    echo -e "${CYAN}发现新版本，正在更新...${NC}"

    # 更新主脚本
    cp "$tmp_dir/cc" "$HOME/.local/bin/cc"
    chmod +x "$HOME/.local/bin/cc"
    echo -e "  ${GREEN}✓${NC} cc → ~/.local/bin/cc"

    # 更新补全脚本
    if [ -f "$tmp_dir/completions/cc.bash" ]; then
        mkdir -p "$HOME/.bash_completion.d"
        cp "$tmp_dir/completions/cc.bash" "$HOME/.bash_completion.d/cc"
        echo -e "  ${GREEN}✓${NC} cc.bash → ~/.bash_completion.d/cc"
    fi

    if [ -f "$tmp_dir/completions/_cc" ]; then
        mkdir -p "$HOME/.zsh/completions"
        cp "$tmp_dir/completions/_cc" "$HOME/.zsh/completions/_cc"
        echo -e "  ${GREEN}✓${NC} _cc → ~/.zsh/completions/_cc"
    fi

    echo ""
    echo -e "${GREEN}${BOLD}✓ 更新完成${NC}"
    echo -e "  请执行 ${BOLD}source $(current_shell_rc)${NC} 或重开终端使更改生效"
}

# 备份
cmd_backup() {
    local backup_dir="$PROFILES_DIR/backups"
    mkdir -p "$backup_dir"
    local ts
    ts=$(date +%Y%m%d_%H%M%S)

    cp "$PROFILES_FILE" "$backup_dir/profiles_${ts}.json"

    if [ -f "$CLAUDE_CONFIG" ]; then
        cp "$CLAUDE_CONFIG" "$backup_dir/config_${ts}.json"
    fi

    echo -e "${GREEN}✓ 已备份到 $backup_dir/*_${ts}.json${NC}"

    # 只保留最近 10 个备份
    ls -t "$backup_dir"/profiles_*.json 2>/dev/null | tail -n +11 | xargs rm -f 2>/dev/null || true
    ls -t "$backup_dir"/config_*.json 2>/dev/null | tail -n +11 | xargs rm -f 2>/dev/null || true
}

# 自动导入当前已有的 key
cmd_import_current() {
    if [ ! -f "$CLAUDE_CONFIG" ]; then
        echo -e "${YELLOW}未找到 claude config.json${NC}"
        return
    fi

    local current_key
    current_key=$(jq -r '.primaryApiKey // empty' "$CLAUDE_CONFIG" 2>/dev/null)

    if [ -z "$current_key" ]; then
        echo -e "${YELLOW}当前 config.json 中没有 API Key${NC}"
        return
    fi

    local exists
    exists=$(read_profiles '.profiles | to_entries[] | select(.value.key == "'"$current_key"'") | .key' 2>/dev/null)

    if [ -n "$exists" ]; then
        echo -e "${YELLOW}当前 Key 已存在于 profile: $exists${NC}"
        return
    fi

    echo -e "${CYAN}检测到当前 config.json 中有 API Key: $(mask_key "$current_key")${NC}"
    echo -n "是否导入为 profile？输入名称（留空跳过）: "
    read -r name

    if [ -n "$name" ]; then
        cmd_add "$name" --key "$current_key"
    fi
}

# 主入口
main() {
    init

    if [ $# -eq 0 ]; then
        usage
        return
    fi

    local cmd="$1"
    shift

    case "$cmd" in
        list|ls)          cmd_list "$@" ;;
        current|cur)      cmd_current "$@" ;;
        add)              with_lock cmd_add "$@" ;;
        use|switch)       with_lock cmd_use "$@" ;;
        edit)             with_lock cmd_edit "$@" ;;
        test)             cmd_test "$@" ;;
        exec)             cmd_exec "$@" ;;
        rm|del|remove)    with_lock cmd_rm "$@" ;;
        rename|mv)        with_lock cmd_rename "$@" ;;
        show|info)        cmd_show "$@" ;;
        export)           cmd_export "$@" ;;
        import-file)      with_lock cmd_import_file "$@" ;;
        backup)           cmd_backup "$@" ;;
        update)           cmd_update "$@" ;;
        import)           cmd_import_current "$@" ;;
        help|-h|--help)   usage ;;
        *)
            echo -e "${RED}未知命令: $cmd${NC}"
            usage
            return 1
            ;;
    esac
}

main "$@"
