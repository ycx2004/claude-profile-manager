#!/usr/bin/env bash
# cc-cli 卸载脚本

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

INSTALL_BIN="$HOME/.local/bin/cc"
INSTALL_BASH_COMPLETION="$HOME/.bash_completion.d/cc"
INSTALL_ZSH_COMPLETION="$HOME/.zsh/completions/_cc"
PROFILES_DIR="$HOME/.cc-profiles"
BASHRC="$HOME/.bashrc"
ZSHRC="$HOME/.zshrc"
TARGET_SHELL="${CC_SHELL:-$(basename "${SHELL:-bash}")}"
RC_FILE="$BASHRC"
CC_MARKER="# cc-cli: Claude Code 账号切换工具"

detect_shell() {
    case "$TARGET_SHELL" in
        zsh)
            RC_FILE="$ZSHRC"
            ;;
        *)
            RC_FILE="$BASHRC"
            ;;
    esac
}

print_cleanup_hint() {
    local shown=false
    local rc_file

    for rc_file in "$BASHRC" "$ZSHRC"; do
        if grep -qF "$CC_MARKER" "$rc_file" 2>/dev/null; then
            if [ "$shown" = false ]; then
                echo -e "${YELLOW}请手动编辑以下 shell 配置文件，删除 cc-cli 相关区块：${NC}"
                echo ""
                shown=true
            fi
            echo -e "  ${BOLD}$rc_file${NC}"
        fi
    done

    if [ "$shown" = false ]; then
        echo -e "${YELLOW}请手动检查 $RC_FILE，删除 cc-cli 相关区块：${NC}"
        echo ""
        echo -e "  ${BOLD}$RC_FILE${NC}"
    fi

    echo ""
    echo -e "  ${CYAN}# cc-cli: Claude Code 账号切换工具${NC}"
    echo -e "  ${CYAN}[ -f \"\$HOME/.cc-profiles/env.sh\" ] && source ...${NC}"
    echo -e "  ${CYAN}[ -f \"\$HOME/.bash_completion.d/cc\" ] && source ...${NC}"
    echo -e "  ${CYAN}[ -f \"\$HOME/.zsh/completions/_cc\" ] && source ...${NC}"
    echo -e "  ${CYAN}cc() { ... }${NC}"
    echo ""
    echo -e "  提示: 搜索 ${BOLD}cc-cli${NC} 关键字即可定位"
}

detect_shell

echo -e "${BOLD}cc-cli 卸载程序${NC}"
echo -e "────────────────────────────"
echo ""

# 1. 删除可执行文件
if [ -f "$INSTALL_BIN" ]; then
    rm "$INSTALL_BIN"
    echo -e "  ${GREEN}✓${NC} 已删除 $INSTALL_BIN"
else
    echo -e "  ${YELLOW}○${NC} $INSTALL_BIN 不存在，跳过"
fi

# 2. 删除补全脚本
if [ -f "$INSTALL_BASH_COMPLETION" ]; then
    rm "$INSTALL_BASH_COMPLETION"
    echo -e "  ${GREEN}✓${NC} 已删除 $INSTALL_BASH_COMPLETION"
else
    echo -e "  ${YELLOW}○${NC} $INSTALL_BASH_COMPLETION 不存在，跳过"
fi

if [ -f "$INSTALL_ZSH_COMPLETION" ]; then
    rm "$INSTALL_ZSH_COMPLETION"
    echo -e "  ${GREEN}✓${NC} 已删除 $INSTALL_ZSH_COMPLETION"
else
    echo -e "  ${YELLOW}○${NC} $INSTALL_ZSH_COMPLETION 不存在，跳过"
fi

# 3. 提示清理 bashrc
echo ""
print_cleanup_hint

# 4. 询问是否删除数据目录
echo ""
if [ -d "$PROFILES_DIR" ]; then
    echo -ne "${YELLOW}是否删除配置数据目录 $PROFILES_DIR？(y/N): ${NC}"
    read -r confirm
    if [ "$confirm" = "y" ] || [ "$confirm" = "Y" ]; then
        rm -rf "$PROFILES_DIR"
        echo -e "  ${GREEN}✓${NC} 已删除 $PROFILES_DIR"
    else
        echo -e "  ${YELLOW}○${NC} 保留 $PROFILES_DIR"
    fi
fi

# 5. 清理环境变量
unset ANTHROPIC_API_KEY 2>/dev/null || true
unset ANTHROPIC_BASE_URL 2>/dev/null || true

echo ""
echo -e "────────────────────────────"
echo -e "${GREEN}${BOLD}✓ cc-cli 卸载完成${NC}"
echo -e "  请执行 ${BOLD}source $RC_FILE${NC} 或重开终端使更改生效"
echo ""
