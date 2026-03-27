# tmux Cheatsheet

## Start tmux
```bash
tmux new -s sessionname
```

## Split panes
- **Ctrl+b** → **%** → vertical split
- **Ctrl+b** → **"** → horizontal split

## Navigate panes
- **Ctrl+b** → **arrow keys** (↑ ↓ ← →)
- **Ctrl+b** → **q** → press number to select pane

## Close pane
- **Ctrl+b** → **x**

## Exit tmux
- **Ctrl+d**

## Reattach session
```bash
tmux attach -t sessionname
```

## List sessions
```bash
tmux list-sessions
```

## Kill session
```bash
tmux kill-session -t sessionname
```
