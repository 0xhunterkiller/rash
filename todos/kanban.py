#!/usr/bin/env python3
"""kanban.py — manage the rash todo board (todos/board.json) and render it to HTML.

board.json shape:
    {"high": [...], "med": [...], "low": [...], "done": [...]}
each card: {"id", "title", "body", "blocked_on": [ids]}

Commands:
    list              print the board
    ready             cards you can pick up now (open, no unfinished blockers)
    add COL TITLE     add a card (--body, --blocked-on)
    move ID COL       move a card to a column
    block ID          set (--on "M2,M5") or --clear a card's blockers
    render            write board.html (static, no JS)

Stdlib only; paths resolve relative to this file.
"""

import argparse
import html
import json
import re
import sys
from pathlib import Path

BASE = Path(__file__).resolve().parent
BOARD_JSON = BASE / "board.json"
BOARD_HTML = BASE / "board.html"

COLUMN_ORDER = ["high", "med", "low", "done"]
TITLES = {"high": "High priority", "med": "Medium priority",
          "low": "Low priority", "done": "Done"}


# ---------------------------------------------------------------- data io
def load_board():
    board = json.loads(BOARD_JSON.read_text()) if BOARD_JSON.exists() else {}
    for key in COLUMN_ORDER:
        board.setdefault(key, [])
    for col in COLUMN_ORDER:
        for card in board[col]:
            card.setdefault("blocked_on", [])
    return board


def save_board(board):
    BOARD_JSON.write_text(json.dumps(board, indent=2, ensure_ascii=False) + "\n")


def find_card(board, card_id):
    for col in COLUMN_ORDER:
        for card in board[col]:
            if card["id"] == card_id:
                return col, card
    return None, None


def next_id(board):
    existing = {c["id"] for col in COLUMN_ORDER for c in board[col]}
    n = 1
    while f"N{n}" in existing:
        n += 1
    return f"N{n}"


def parse_ids(raw):
    return [t for t in re.split(r"[,\s]+", (raw or "").strip()) if t]


def done_ids(board):
    return {c["id"] for c in board["done"]}


def open_blockers(board, card, done=None):
    done = done_ids(board) if done is None else done
    return [b for b in card.get("blocked_on", []) if b not in done]


# ------------------------------------------------------------- commands
def cmd_list(_):
    board = load_board()
    done = done_ids(board)
    for col in COLUMN_ORDER:
        cards = board[col]
        head = f"{TITLES[col]} ({len(cards)})"
        print(f"\n{head}\n{'-' * len(head)}")
        for c in cards:
            blk = open_blockers(board, c, done)
            tail = f"  ⛔ blocked on {', '.join(blk)}" if blk else ""
            print(f"  [{c['id']}] {c['title']}{tail}")
    print()


def cmd_ready(_):
    board = load_board()
    done = done_ids(board)
    ready = [(col, c) for col in COLUMN_ORDER if col != "done"
             for c in board[col] if not open_blockers(board, c, done)]
    if not ready:
        print("nothing ready — every open card is blocked")
        return
    print(f"\nReady to pick ({len(ready)}):")
    for col, c in ready:
        print(f"  [{c['id']}] ({col}) {c['title']}")
    print()


def cmd_add(args):
    board = load_board()
    card = {"id": next_id(board), "title": args.title,
            "body": args.body or "", "blocked_on": parse_ids(args.blocked_on)}
    board[args.col].append(card)
    save_board(board)
    print(f"added {card['id']} to {args.col}: {card['title']}")


def cmd_move(args):
    board = load_board()
    src, card = find_card(board, args.id)
    if card is None:
        sys.exit(f"no card with id '{args.id}'")
    board[src].remove(card)
    board[args.col].append(card)
    save_board(board)
    print(f"moved {args.id}: {src} -> {args.col}")


def cmd_block(args):
    board = load_board()
    _, card = find_card(board, args.id)
    if card is None:
        sys.exit(f"no card with id '{args.id}'")
    card["blocked_on"] = [] if args.clear else parse_ids(args.on)
    save_board(board)
    print(f"{args.id} blocked on: {card['blocked_on'] or '(nothing)'}")


# --------------------------------------------------------------- render
CSS = """
*{box-sizing:border-box}
body{margin:0;background:#f4f5f7;color:#172b4d;
  font:14px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{padding:16px 20px;border-bottom:1px solid #dfe1e6}
header h1{font-size:18px;margin:0}
.board{display:grid;grid-template-columns:repeat(4,1fr);gap:14px;padding:20px;align-items:start}
@media(max-width:820px){.board{grid-template-columns:1fr}}
h2{font-size:12px;text-transform:uppercase;letter-spacing:.06em;
  display:flex;justify-content:space-between;color:#5e6c84}
.card{background:#fff;border:1px solid #dfe1e6;border-left:4px solid #5e6c84;
  border-radius:8px;padding:10px 12px;margin-bottom:10px;box-shadow:0 1px 0 rgba(9,30,66,.12)}
.card.high{border-left-color:#de350b}.card.med{border-left-color:#ff8b00}
.card.low{border-left-color:#00875a}.card.done{border-left-color:#6554c0}
.card.blocked{border-left-color:#de350b}
.card.ready{box-shadow:0 0 0 2px #00875a inset,0 1px 0 rgba(9,30,66,.12)}
.id{font:600 11px ui-monospace,monospace;color:#5e6c84}
.badge{font-size:11px;font-weight:700;margin-left:6px}
.badge.ready{color:#00875a}.badge.blocked{color:#de350b}
.title{font-weight:600;margin:2px 0}
.blockedon{font-size:12px;margin-top:4px;color:#5e6c84}
.blockedon b{display:inline-block;font:600 11px ui-monospace,monospace;
  background:rgba(222,53,11,.14);color:#de350b;border-radius:4px;padding:0 5px;margin-left:4px}
summary{color:#0052cc;cursor:pointer;font-size:12px;margin-top:6px}
pre{white-space:pre-wrap;font:12px/1.4 ui-monospace,monospace;color:#5e6c84;margin:8px 0 0}
@media(prefers-color-scheme:dark){
  body{background:#1d2125;color:#c7d1db}header{border-color:#3c4149}
  .card{background:#282e33;border-color:#3c4149}}
"""


def render_card(col, card, done):
    esc = html.escape
    blk = open_blockers({"done": [{"id": i} for i in done]}, card, done)
    is_open = col != "done"
    cls = f"card {col}" + (" blocked" if is_open and blk else " ready" if is_open else "")
    badge = ""
    if is_open:
        badge = ('<span class="badge blocked">⛔ blocked</span>' if blk
                 else '<span class="badge ready">● ready</span>')
    parts = [f'<div class="{cls}">',
             f'<div class="id">{esc(card["id"])}{badge}</div>',
             f'<div class="title">{esc(card["title"])}</div>']
    if blk:
        chips = "".join(f"<b>{esc(b)}</b>" for b in blk)
        parts.append(f'<div class="blockedon">blocked on:{chips}</div>')
    if card.get("body", "").strip():
        parts.append(f"<details><summary>details</summary><pre>{esc(card['body'])}</pre></details>")
    parts.append("</div>")
    return "".join(parts)


def cmd_render(_):
    board = load_board()
    done = done_ids(board)
    cols = []
    for col in COLUMN_ORDER:
        cards = "".join(render_card(col, c, done) for c in board[col])
        cols.append(f'<div class="col"><h2><span>{TITLES[col]}</span>'
                    f'<span>{len(board[col])}</span></h2>{cards}</div>')
    page = (f"<!doctype html><html lang=en><head><meta charset=utf-8>"
            f'<meta name=viewport content="width=device-width,initial-scale=1">'
            f"<title>rash board</title><style>{CSS}</style></head><body>"
            f"<header><h1>rash board</h1></header>"
            f'<div class="board">{"".join(cols)}</div></body></html>')
    BOARD_HTML.write_text(page)
    print(f"wrote {BOARD_HTML}")


# ------------------------------------------------------------------ cli
def main(argv=None):
    p = argparse.ArgumentParser(description="manage the rash todo board")
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list").set_defaults(func=cmd_list)
    sub.add_parser("ready").set_defaults(func=cmd_ready)
    sub.add_parser("render").set_defaults(func=cmd_render)

    a = sub.add_parser("add")
    a.add_argument("col", choices=COLUMN_ORDER)
    a.add_argument("title")
    a.add_argument("--body", default="")
    a.add_argument("--blocked-on", dest="blocked_on", default="")
    a.set_defaults(func=cmd_add)

    m = sub.add_parser("move")
    m.add_argument("id")
    m.add_argument("col", choices=COLUMN_ORDER)
    m.set_defaults(func=cmd_move)

    b = sub.add_parser("block")
    b.add_argument("id")
    b.add_argument("--on", default="")
    b.add_argument("--clear", action="store_true")
    b.set_defaults(func=cmd_block)

    args = p.parse_args(argv)
    args.func(args)


if __name__ == "__main__":
    main()
