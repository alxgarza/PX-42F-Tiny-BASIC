#!/usr/bin/env python3
"""Builds basic_manual.html, the PX-BASIC manual: one HTML file with its
pictures inside. The two keyboard maps and the keyword pages come from
src/console.c's own tables, so the manual says what the keys type; the
screenshots are the simulator's (tools/shots.sh makes them, in
build/shots); the style is tools/manual.css.

    tools/shots.sh && python3 tools/make_manual.py        (tools/shots.sh does both)
"""
import base64
import html
import os
import re
import sys

PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HERE = os.path.join(PROJ, 'build', 'shots')  # the screenshots

console = open(os.path.join(PROJ, 'src', 'console.c')).read()
style = open(os.path.join(PROJ, 'tools', 'manual.css')).read().strip()


def c_char_table(name):
    body = re.search(name + r'\[7\]\[6\] = \{(.*?)\};', console, re.S).group(1)
    rows = re.findall(r'\{([^{}]*)\}', body)
    table = []
    for row in rows:
        cells = []
        for tok in re.findall(r"'(?:\\.|[^'])'|(?<![\w'])0(?![\w'])", row):
            if tok == '0':
                cells.append(None)
            else:
                ch = tok[1:-1]
                cells.append({"\\'": "'", '\\"': '"'}.get(ch, ch))
        table.append(cells)
    assert len(table) == 7 and all(len(r) == 6 for r in table), name
    return table


def keyword_pages():
    body = re.search(r'keyword_pages\[2\]\[7\]\[6\] = \{(.*?)\n\};', console, re.S).group(1)
    pages = []
    for page in re.findall(r'\{\s*(\{.*?\}),\s*\}', body, re.S):
        rows = re.findall(r'\{([^{}]*)\}', page)
        table = []
        for row in rows:
            cells = []
            for tok in re.findall(r'"(?:\\.|[^"])*"|NULL', row):
                cells.append(None if tok == 'NULL' else tok[1:-1].replace('\\"', '"'))
            table.append(cells)
        pages.append(table)
    assert len(pages) == 2 and all(len(p) == 7 for p in pages)
    return pages


ALPHA = c_char_table('abc_keys')
NUM = c_char_table('num_keys')
PAGES = keyword_pages()

# What's printed on the calculator's keys, by place.
PRINTED = [
    ['&Sigma;+', '1/x', '&radic;x', 'LOG', 'LN', 'XEQ'],
    ['STO', 'RCL', 'R&darr;', 'SIN', 'COS', 'TAN'],
    ['ENTER', 'x&#8823;y', '+/&minus;', 'E', '&larr;'],
    ['&#9650;', '7', '8', '9', '&divide;'],
    ['&#9660;', '4', '5', '6', '&times;'],
    ['SHIFT', '1', '2', '3', '&minus;'],
    ['ON', '0', '.', 'R/S', '+'],
]
# The keys that do something else than type, by place.
SPECIAL = {(2, 0): 'enters; SHIFT: ALPHA', (2, 4): 'deletes', (3, 0): 'editor / left; SHIFT: history', (4, 0): 'editor / right; SHIFT: keywords',
           (5, 0): 'the other mode, once', (6, 0): 'stops; leaves (asks)'}


def span(r, c):
    return 's5' if r < 2 else 's6'


def keyboard(mode=None, codes=False, label=''):
    out = ['<div class="kbd-map" role="img" aria-label="%s">' % label]
    for r, row in enumerate(PRINTED):
        for c, printed in enumerate(row):
            cls = span(r, c)
            if codes:
                big = str((r + 1) * 10 + c + 1)
                small = printed
                note = ''
            else:
                ch = mode[r][c]
                if ch is None:
                    big = printed
                    small = ''
                    note = SPECIAL.get((r, c), '')
                    cls += ' fn'
                else:
                    big = html.escape('space' if ch == ' ' else ch)
                    small = printed
                    note = ''
            out.append('<div class="k %s"><b>%s</b><span>%s</span>%s</div>' % (
                cls, big, small, '<i>%s</i>' % note if note else ''))
    out.append('</div>')
    return '\n'.join(out)


def img(name, alt):
    data = base64.b64encode(open(os.path.join(HERE, name), 'rb').read()).decode()
    return '<img class="shot" src="data:image/png;base64,%s" alt="%s">' % (data, html.escape(alt))


def listing(name):
    return html.escape(open(os.path.join(PROJ, 'sim/drive/BIN/APPS/BASIC', name + '.BAS')).read().rstrip())


extra_css = """
.kbd-map { display: grid; grid-template-columns: repeat(30, 1fr); gap: 6px; max-width: 560px; margin: 10px 0 8px; }
.kbd-map .k { background: var(--key, #2b2c30); color: #f2f1ec; border-radius: 6px; padding: 6px 2px 4px; text-align: center; line-height: 1.15; min-height: 50px; }
.kbd-map .k b { display: block; font-size: 1.05rem; font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; }
.kbd-map .k span { display: block; font-size: 0.7rem; opacity: 0.75; }
.kbd-map .k i { display: block; font-style: normal; font-size: 0.62rem; color: #e0a24a; }
.kbd-map .fn { background: #4a4b52; }
.kbd-map .fn b { font-family: inherit; font-size: 0.85rem; }
.kbd-map .s5 { grid-column: span 5; } .kbd-map .s6 { grid-column: span 6; }
table.ref td:first-child { white-space: nowrap; }
"""

page = r'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>PX-BASIC</title>
STYLE
</head>
<body>
<main>

<header class="cover">
<h1>PX-BASIC</h1>
<p>The PX-42F calculator's BASIC, based on Tiny BASIC, with the calculator's own 34-digit decimal numbers.</p>
</header>

<nav class="toc">
<h2>Contents</h2>
<ol>
  <li><a href="#intro">What it is</a></li>
  <li><a href="#start">Getting started</a></li>
  <li><a href="#keys">Typing</a></li>
  <li><a href="#programs">Writing a program</a></li>
  <li><a href="#numbers">Numbers</a></li>
  <li><a href="#expressions">Expressions and functions</a></li>
  <li><a href="#statements">Statements</a></li>
  <li><a href="#screen">The screen, sound and keys</a></li>
  <li><a href="#files">Saving programs</a></li>
  <li><a href="#errors">Errors</a></li>
  <li><a href="#examples">Examples</a></li>
  <li><a href="#limits">Limits</a></li>
  <li><a href="#reference">Quick reference</a></li>
</ol>
</nav>

<!-- ============================================================ -->
<h2 id="intro">1. What it is</h2>

<p>PX-BASIC turns the PX-42F into a small computer you program in BASIC, the language of the first home computers. It follows Palo Alto Tiny BASIC, from 1976: numbered lines, <code>PRINT</code>, <code>INPUT</code>, <code>IF</code>, <code>GOTO</code>, <code>GOSUB</code>, <code>FOR</code>. To that it adds the calculator itself:</p>
<ul>
  <li><strong>Its numbers.</strong> 34-digit decimals, from the library Plus42 computes with. <code>0.1+0.2</code> is exactly <code>0.3</code>, and <code>SIN</code>, <code>LOG</code> and <code>SQR</code> have the calculator's precision.</li>
  <li><strong>Its screen, keys and speaker.</strong> Lines and points, the keys as input, the calculator's tones.</li>
  <li><strong>Its drive.</strong> Programs are saved as text files that a computer can open too.</li>
</ul>

<div class="shots">
<figure>IMG:a/run.png|A program typed and run: a table of square roots|<figcaption>A program and its run.</figcaption></figure>
<figure>IMG:a/spiral.png|A spiral drawn with LINE|<figcaption>Graphics (SPIRAL).</figcaption></figure>
<figure>IMG:a/sine.png|Sine and cosine plotted with PSET|<figcaption>Plotting (SINE).</figcaption></figure>
</div>

<!-- ============================================================ -->
<h2 id="start">2. Getting started</h2>

<h3>Installing</h3>
<ol>
  <li>Make sure the calculator's firmware is the PX-42F-Plus with app API version 3 or later.</li>
  <li>On the calculator, open <span class="mono">Settings &rarr; USB Drive &rarr; Connect to Computer</span>.</li>
  <li>Copy <code>BASIC.bin</code> into <code>BIN/APPS</code> on the drive named <code>PX-42F</code>. From the project folder, <code>make install</code> does it.</li>
  <li>Optionally, copy the example programs (<code>.BAS</code> files, in the project's <code>sim/drive/BIN/APPS/BASIC/</code>) into <code>BIN/APPS/BASIC</code> on the drive.</li>
  <li>Eject the drive and press <kbd>EXIT</kbd> on the calculator.</li>
</ol>

<h3>Starting and leaving</h3>
<p>Start it from <span class="mono">Settings &rarr; Games &amp; Apps &rarr; Apps &rarr; BASIC</span>. It shows how much room there is for a program, then the prompt, <code>&gt;</code>.</p>
<p>To leave, press <kbd>ON</kbd> at the prompt. The status bar asks <code>LEAVE BASIC? ENTER=YES</code>: <kbd>ENTER</kbd> (or <kbd>ON</kbd> again) goes back to the list of apps, and any other key stays in BASIC. Held for 2 seconds, <kbd>ON</kbd> turns the calculator off without asking, and the Sleep Timer does too.</p>
<figure>IMG:a/leave.png|The status bar asking LEAVE BASIC? ENTER=YES|<figcaption><kbd>ON</kbd> at the prompt asks first.</figcaption></figure>
<p>The program you were working on is saved when you leave, as <code>AUTOSAVE.BAS</code>, and comes back the next time you start BASIC.</p>

<h3>Your first program</h3>
<p>BASIC keeps lines that start with a number, in number order, as the program. A line without a number runs at once. Type these three lines, pressing <kbd>ENTER</kbd> after each. Use the keyword map (<kbd>SHIFT</kbd> <kbd>&#9660;</kbd>, below) for <code>FOR</code>, <code>PRINT</code> and <code>NEXT</code>, and <kbd>SHIFT</kbd> <kbd>ENTER</kbd> (ALPHA) to switch between letters and digits:</p>
<pre><code>10 FOR I=1 TO 5
20 PRINT I,SQR(I)
30 NEXT</code></pre>
<p>Then type <code>RUN</code> (or choose it from the keyword map): it prints each number and its square root. <code>LIST</code> shows the program again.</p>

<!-- ============================================================ -->
<h2 id="keys">3. Typing</h2>

<h3>The status bar</h3>
<p>The top right of the screen says what the keys do:</p>
<div class="table-wrap"><table>
<thead><tr><th>It says</th><th>Meaning</th></tr></thead>
<tbody>
<tr><td><code>ALPHA</code></td><td>The keys type letters. Every line typed at the prompt starts in ALPHA.</td></tr>
<tr><td><code>NUM</code></td><td>The keys type digits and symbols. A program's <code>INPUT</code> starts in NUM: it wants a number.</td></tr>
<tr><td><code>SHIFT</code> (next to it)</td><td>SHIFT is on: <kbd>ENTER</kbd> switches the mode (ALPHA); another key types from the other mode, once.</td></tr>
<tr><td><code>RUN</code></td><td>A program is running. <kbd>R/S</kbd> stops it.</td></tr>
</tbody>
</table></div>
<div class="shots">
<figure>IMG:a/alpha.png|ALPHA mode: IJ typed at the prompt|<figcaption>ALPHA: the second row's keys typed I and J.</figcaption></figure>
<figure>IMG:a/num.png|NUM mode after SHIFT ENTER|<figcaption><kbd>SHIFT</kbd> <kbd>ENTER</kbd> (ALPHA) switches to NUM.</figcaption></figure>
</div>

<h3>ALPHA: letters</h3>
<p>The letters are where the calculator's ALPHA keyboard has them, as on the DM42: A to F on the top row, G to L on the second, M N O next to <kbd>ENTER</kbd>, then P to Z, row by row. <code>=</code> is after Z, and the bottom row types <code>: . ?</code> and a space.</p>
KEYS_ALPHA

<h3>NUM: digits and symbols</h3>
<p>The digits and <code>+ &minus; &times; &divide;</code> are where they're printed (<code>*</code> and <code>/</code> for times and divide), with <code>.</code> and a space on the bottom row. The top rows type BASIC's symbols. <code>E</code>, on the calculator's <kbd>E</kbd> key, writes a number's exponent: <code>2E-3</code>.</p>
KEYS_NUM

<h3>The other keys</h3>
<div class="table-wrap"><table>
<thead><tr><th>Key</th><th>Does</th></tr></thead>
<tbody>
<tr><td><kbd>SHIFT</kbd> <kbd>ENTER</kbd></td><td>ALPHA, as printed below <kbd>ENTER</kbd>: switches between ALPHA and NUM.</td></tr>
<tr><td><kbd>SHIFT</kbd></td><td>With any other key, types from the other mode once: <kbd>SHIFT</kbd> <kbd>7</kbd> types 7 in ALPHA, <kbd>SHIFT</kbd> <kbd>&Sigma;+</kbd> types A in NUM.</td></tr>
<tr><td><kbd>&#9650;</kbd> <kbd>&#9660;</kbd></td><td>On an empty line: open the program editor (<a href="#editing">section 4</a>). On a line with text, or while editing a line: move the cursor left and right.</td></tr>
<tr><td><kbd>SHIFT</kbd> <kbd>&#9650;</kbd></td><td>The history: the last line typed at the prompt, in place of the one being typed, to run it again. Again: the one before, back through the last 8.</td></tr>
<tr><td><kbd>SHIFT</kbd> <kbd>&#9660;</kbd></td><td>The keyword map (below).</td></tr>
<tr><td><kbd>&larr;</kbd></td><td>Deletes the last character. <kbd>SHIFT</kbd> <kbd>&larr;</kbd> deletes the whole line.</td></tr>
<tr><td><kbd>ENTER</kbd></td><td>Enters the line.</td></tr>
<tr><td><kbd>R/S</kbd></td><td>While a program runs, or waits in <code>INPUT</code>: stops it. Typing: <code>?</code> in ALPHA, a space in NUM.</td></tr>
<tr><td><kbd>SHIFT</kbd> <kbd>R/S</kbd></td><td>At the prompt: <code>RUN</code>. It runs the program at once, in place of whatever is typed on the line.</td></tr>
<tr><td><kbd>ON</kbd></td><td>While a program runs or waits in <code>INPUT</code>: stops it, as <kbd>R/S</kbd> does. At the prompt: asks before leaving BASIC. Held 2 seconds: turns the calculator off.</td></tr>
</tbody>
</table></div>

<h3>The keyword map</h3>
<p>Typing <code>RETURN</code> letter by letter takes six keys. <kbd>SHIFT</kbd> <kbd>&#9660;</kbd> shows a map of BASIC's words laid over the keys: press a key to type its word, with the space or <code>(</code> after it. <kbd>&#9660;</kbd> again shows the second page, the functions. <kbd>&#9660;</kbd> once more, <kbd>ON</kbd> (shown as BACK), or a key with no word on it (an empty box), goes back without typing anything.</p>
<p><code>RUN</code> is on two keys of the first page: <kbd>ENTER</kbd>'s place, with the other commands, and <kbd>R/S</kbd>'s place on the bottom row, the same key as <kbd>SHIFT</kbd> <kbd>R/S</kbd>, which runs the program without the map.</p>
<div class="shots">
<figure>IMG:a/kw1.png|The keyword map, statements|<figcaption>Page 1: the statements and commands.</figcaption></figure>
<figure>IMG:a/kw2.png|The keyword map, functions|<figcaption>Page 2: the functions and operators.</figcaption></figure>
</div>

<h3>Typing from a computer</h3>
<p>With the calculator connected by USB, open its console port ("PX-42F Console") in any terminal program; the speed setting doesn't matter. A Mac lists it with <code>ls /dev/cu.usbmodem*</code> (the first of two); <code>screen /dev/cu.usbmodem2101</code> opens it. Then:</p>
<ul>
  <li>What you type there goes to BASIC as if typed on the keys, in any case: lowercase becomes uppercase, except inside quotes.</li>
  <li>Paste a whole program: each line goes in as typed.</li>
  <li>Everything the screen shows also goes to the terminal, so a long listing can be read there.</li>
  <li><kbd>Ctrl-C</kbd> stops a running program, as <kbd>R/S</kbd> does; at the prompt it clears the line.</li>
  <li>The arrow keys work as you'd expect: <kbd>&larr;</kbd> <kbd>&rarr;</kbd> move in the line, <kbd>Home</kbd> and <kbd>End</kbd> go to its ends, <kbd>Delete</kbd> deletes under the cursor, <kbd>&uarr;</kbd> <kbd>&darr;</kbd> step through the history. <kbd>Ctrl-A</kbd> <kbd>Ctrl-E</kbd> <kbd>Ctrl-B</kbd> <kbd>Ctrl-F</kbd> <kbd>Ctrl-P</kbd> <kbd>Ctrl-N</kbd> and <kbd>Ctrl-U</kbd> do the same, for terminals that don't send the arrows.</li>
  <li>Editing the middle of a line redraws it with ANSI codes (carriage return, clear to the end of the line, cursor back): any ordinary terminal handles them. If CoolTerm shows odd characters or never sends the arrows or Ctrl-C, set it to <em>Raw Mode</em> (Connection &rarr; Options &rarr; Terminal), which sends each key at once.</li>
</ul>

<!-- ============================================================ -->
<h2 id="programs">4. Writing a program</h2>

<ul>
  <li><strong>A line with a number</strong> (1 to 32767) goes into the program. Lines are kept in number order, whatever order they're typed in. Number them in tens (10, 20, 30) to leave room for lines in between.</li>
  <li><strong>The same number again</strong> replaces that line. <strong>The number alone</strong> deletes it.</li>
  <li><strong>A line without a number</strong> runs at once: <code>PRINT 2^64</code>, or <code>RUN</code>.</li>
  <li><strong>Several statements</strong> fit on one line, with <code>:</code> between them: <code>10 X=1:Y=2:PRINT X+Y</code>.</li>
  <li><strong>A line</strong> holds up to 80 characters, more than the screen's 31: the text wraps.</li>
</ul>
<div class="table-wrap"><table>
<thead><tr><th>Command</th><th>Does</th></tr></thead>
<tbody>
<tr><td><code>RUN</code></td><td>Runs the program from its first line, with every variable set to 0. <kbd>SHIFT</kbd> <kbd>R/S</kbd> does the same without typing it; so does the <kbd>R/S</kbd> key's place in the keyword map, which types <code>RUN</code> for <kbd>ENTER</kbd>.</td></tr>
<tr><td><code>LIST</code></td><td>Shows the whole program. <code>LIST 100</code>: from line 100 on. <code>LIST 100,200</code>: lines 100 to 200. <kbd>R/S</kbd> stops a long listing.</td></tr>
<tr><td><code>NEW</code></td><td>Erases the program and the variables.</td></tr>
<tr><td><code>GOTO 100</code></td><td>Typed at the prompt: runs from line 100 without clearing the variables.</td></tr>
</tbody>
</table></div>

<h3 id="editing">Editing the program</h3>
<p>You don't have to retype a line to change it. On the calculator, <kbd>&#9650;</kbd> or <kbd>&#9660;</kbd> at an empty prompt opens the program editor. It works in two steps, on one screen:</p>
<ol>
  <li><strong>The list.</strong> The program is shown a line to a row, the current line highlighted, and the header says <code>PROGRAM</code>. It opens on the line you last entered, or on the last line. <kbd>&#9650;</kbd> and <kbd>&#9660;</kbd> move the highlight, and the list scrolls; <kbd>SHIFT</kbd> with them moves a screenful. <kbd>ENTER</kbd> starts editing the highlighted line. <kbd>EXIT</kbd> (the <kbd>ON</kbd> key) leaves the editor, back to the prompt.</li>
  <li><strong>Editing a line.</strong> The line is edited where it is, in the list: all of its text, as many rows as it needs, with the rest of the program still shown around it. The header says <code>EDIT</code> and what the keys do. <kbd>&#9650;</kbd> moves the cursor left and <kbd>&#9660;</kbd> right (the cursor is the white block). What you type goes in at the cursor; <kbd>&larr;</kbd> deletes the character before it; <kbd>SHIFT</kbd> <kbd>&larr;</kbd> clears the line; <kbd>SHIFT</kbd> <kbd>&#9660;</kbd> is the keyword map; <kbd>SHIFT</kbd> <kbd>ENTER</kbd> switches ALPHA and NUM (the header then says <code>NUM</code>).
    <ul>
      <li><kbd>ENTER</kbd> <strong>accepts</strong> the change: the line replaces the old one.</li>
      <li><kbd>EXIT</kbd> <strong>discards</strong> it: the line stays as it was.</li>
    </ul>
    Either way you're back at the list, on the same line, to move to another one.</li>
</ol>
<div class="shots">
<figure>IMG:d/list.png|The program as a list, line 20 highlighted|<figcaption>1. The list: <kbd>&#9650;</kbd> <kbd>&#9650;</kbd> moved the highlight to line 20.</figcaption></figure>
<figure>IMG:d/edit.png|Line 20 being edited in place, the header saying EDIT|<figcaption>2. <kbd>ENTER</kbd>: line 20 is edited where it is, the rest still shown.</figcaption></figure>
<figure>IMG:d/cursor.png|The cursor moved to SQR|<figcaption>3. <kbd>&#9650;</kbd> six times moved the cursor to <code>SQR</code>.</figcaption></figure>
<figure>IMG:d/inserted.png|2* typed before SQR|<figcaption>4. <kbd>2</kbd> <kbd>*</kbd> went in at the cursor.</figcaption></figure>
<figure>IMG:d/accepted.png|Back in the list after ENTER|<figcaption>5. <kbd>ENTER</kbd> accepted it: the list again, on line 20.</figcaption></figure>
<figure>IMG:d/ran.png|The run, with the square roots doubled|<figcaption>6. <kbd>EXIT</kbd> left the editor; <code>RUN</code> shows the change.</figcaption></figure>
</div>
<ul>
  <li><strong>A new line from an old one:</strong> change the number while editing. The edited line goes in under the new number, and the old line stays.</li>
  <li><strong>Deleting a line:</strong> edit it down to just its number and press <kbd>ENTER</kbd>. The line is removed, and the highlight moves to its neighbour.</li>
  <li><strong>A line with no number</strong> (the number edited away) is dropped as if discarded: it would run as a command, which isn't what you meant.</li>
</ul>
<p>The history is on <kbd>SHIFT</kbd> <kbd>&#9650;</kbd>: it brings back the last command typed, for running it again. At a program's <code>INPUT</code>, <kbd>&#9650;</kbd> and <kbd>&#9660;</kbd> only move in what you've typed.</p>
<div class="shots">
<figure>IMG:b/input.png|A program asking for a radius and printing the area|<figcaption>A program with <code>INPUT</code>. <code>^</code> shows as &uarr;, the HP-42S's own character.</figcaption></figure>
<figure>IMG:b/list.png|R/S stops the program; LIST shows it|<figcaption><kbd>R/S</kbd> stopped it (<code>BREAK IN 10</code>); <code>LIST</code> shows it.</figcaption></figure>
</div>

<!-- ============================================================ -->
<h2 id="numbers">5. Numbers</h2>

<p>Every number is a decimal of up to 34 significant digits, from &plusmn;10<sup>&minus;6143</sup> to &plusmn;10<sup>6144</sup>, the same numbers Plus42 computes with. Because they're decimal, the sums that go wrong in most computer languages come out right: <code>0.1+0.2</code> is exactly 0.3. A fraction with no exact decimal still rounds, as on any calculator: <code>1/3</code> is 0.333&hellip;3, 34 threes.</p>

<h3>How PRINT shows them</h3>
<ul>
  <li>Rounded to 12 significant digits, like the HP-42S's display. <code>DIGITS n</code> changes that, from 1 to 34. After <code>DIGITS 34</code>, <code>PRINT SQR(2)</code> shows 1.414213562373095048801688724209698.</li>
  <li>No zeros at the end: <code>2.50</code> prints as <code>2.5</code>.</li>
  <li>With <code>E</code> when the number is too large for the digits, or below 0.000001: <code>1.5E-7</code>, <code>6.02214076E23</code>.</li>
  <li>The number is kept to all 34 digits; <code>DIGITS</code> only changes how it's shown.</li>
</ul>

<h3>Writing them</h3>
<p><code>12</code>, <code>-3.5</code>, <code>.25</code>, <code>2E-3</code>, <code>6.02214076E23</code>. An <code>E</code> is the exponent only with digits after it, so <code>2*E</code> is two times the variable E.</p>

<h3>Variables and the array</h3>
<ul>
  <li><strong>Variables:</strong> 26, <code>A</code> to <code>Z</code>, one letter each. They start at 0, and <code>RUN</code> sets them to 0.</li>
  <li><strong>The array:</strong> <code>@(0)</code> to <code>@(1023)</code>, for lists and tables: <code>@(I)=I*I</code>.</li>
</ul>

<!-- ============================================================ -->
<h2 id="expressions">6. Expressions and functions</h2>

<div class="table-wrap"><table>
<thead><tr><th>Operators, the first done first</th><th>Meaning</th></tr></thead>
<tbody>
<tr><td><code>( )</code></td><td>grouping</td></tr>
<tr><td><code>^</code></td><td>power, right to left: <code>2^3^2</code> is 2<sup>9</sup>. On the screen it shows as &uarr;.</td></tr>
<tr><td><code>-</code></td><td>minus: <code>-2^2</code> is &minus;4</td></tr>
<tr><td><code>*</code> <code>/</code> <code>MOD</code></td><td>times, divided by, and the remainder. <code>a MOD b</code> has the sign of <code>b</code>, as the HP-42S's MOD: <code>-7 MOD 3</code> is 2.</td></tr>
<tr><td><code>+</code> <code>-</code></td><td>plus, minus</td></tr>
<tr><td><code>= &lt;&gt; &lt; &gt; &lt;= &gt;=</code></td><td>comparisons: 1 if true, 0 if false</td></tr>
<tr><td><code>NOT</code></td><td>1 if 0, else 0</td></tr>
<tr><td><code>AND</code></td><td>1 if both are true (not 0)</td></tr>
<tr><td><code>OR</code></td><td>1 if either is true</td></tr>
</tbody>
</table></div>

<div class="table-wrap"><table class="ref">
<thead><tr><th>Function</th><th>Gives</th></tr></thead>
<tbody>
<tr><td><code>SIN(x) COS(x) TAN(x)</code></td><td>trigonometry, in radians, or in degrees after <code>DEG</code></td></tr>
<tr><td><code>ASN(x) ACS(x) ATN(x)</code></td><td>arcsine, arccosine, arctangent, in the same unit</td></tr>
<tr><td><code>SQR(x)</code></td><td>square root</td></tr>
<tr><td><code>EXP(x)</code>, <code>LOG(x)</code>, <code>LOG10(x)</code></td><td>e<sup>x</sup>, the natural logarithm, the base-10 logarithm</td></tr>
<tr><td><code>ABS(x)</code>, <code>SGN(x)</code></td><td>without its sign; &minus;1, 0 or 1</td></tr>
<tr><td><code>INT(x)</code></td><td>the whole number at or below x: <code>INT(-2.5)</code> is &minus;3</td></tr>
<tr><td><code>RND(n)</code></td><td>a random whole number from 1 to n. <code>RND(0)</code>: a random fraction from 0 to 1.</td></tr>
<tr><td><code>PI</code></td><td>&pi; to 34 digits</td></tr>
<tr><td><code>KEY()</code></td><td>a key pressed while the program runs (<a href="#screen">section 8</a>)</td></tr>
<tr><td><code>SIZE</code></td><td>bytes left for the program</td></tr>
</tbody>
</table></div>

<!-- ============================================================ -->
<h2 id="statements">7. Statements</h2>

<div class="table-wrap"><table class="ref">
<thead><tr><th>Statement</th><th>Does</th><th>Example</th></tr></thead>
<tbody>
<tr><td><code>PRINT</code> or <code>?</code></td><td>Shows numbers and "text". <code>;</code> between items keeps them together, <code>,</code> moves to the next column of 8. A <code>;</code> or <code>,</code> at the end keeps the line open for the next PRINT.</td><td><code>PRINT "X=";X,"Y=";Y</code></td></tr>
<tr><td><code>INPUT</code></td><td>Asks for numbers. Without a prompt it shows the variable's name: <code>R? </code>. Something that isn't a number gets <code>HOW?</code> and the question again.</td><td><code>INPUT "RADIUS? ",R</code></td></tr>
<tr><td><code>LET</code></td><td>Gives a variable a value. <code>LET</code> can be left out.</td><td><code>X=X+1</code>, <code>@(I)=0</code></td></tr>
<tr><td><code>IF</code> &hellip; <code>THEN</code></td><td>If the condition is true (not 0), does the rest of the line; if false, skips it, including statements after <code>:</code>. <code>THEN 100</code> goes to line 100.</td><td><code>IF X&gt;9 THEN PRINT "BIG":X=0</code></td></tr>
<tr><td><code>GOTO</code></td><td>Goes to a line. The number can be an expression.</td><td><code>GOTO 100+10*K</code></td></tr>
<tr><td><code>GOSUB</code>, <code>RETURN</code></td><td>Goes to a line, and <code>RETURN</code> comes back after the GOSUB.</td><td><code>GOSUB 500</code></td></tr>
<tr><td><code>FOR</code> &hellip; <code>TO</code> &hellip; <code>STEP</code>, <code>NEXT</code></td><td>Repeats the lines up to <code>NEXT</code>, the variable going from the first value to the last. <code>STEP</code> can be a fraction or negative; without it, 1.</td><td><code>FOR A=0 TO 1 STEP 0.1</code></td></tr>
<tr><td><code>END</code>, <code>STOP</code></td><td>Ends the program.</td><td></td></tr>
<tr><td><code>REM</code></td><td>A note: the rest of the line is ignored.</td><td><code>REM DRAW THE AXES</code></td></tr>
<tr><td><code>DEG</code>, <code>RAD</code></td><td>Angles in degrees, or radians (the default). <code>RUN</code> starts in radians, so a program that wants degrees says <code>DEG</code> itself.</td><td><code>DEG:PRINT SIN(30)</code></td></tr>
<tr><td><code>DIGITS</code></td><td>Digits PRINT shows, 1 to 34; 12 at first, and again at each <code>RUN</code>.</td><td><code>DIGITS 34</code></td></tr>
<tr><td><code>CLS</code>, <code>AT</code>, <code>PSET</code>, <code>LINE</code>, <code>BOX</code>, <code>TONE</code>, <code>WAIT</code></td><td colspan="2">The screen, sound and time: <a href="#screen">section 8</a>.</td></tr>
<tr><td><code>SAVE</code>, <code>LOAD</code>, <code>FILES</code>, <code>ERASE</code></td><td colspan="2">Files: <a href="#files">section 9</a>.</td></tr>
</tbody>
</table></div>

<!-- ============================================================ -->
<h2 id="screen">8. The screen, sound and keys</h2>

<h3>Text</h3>
<p>The text fills 9 lines of 31 characters under the status bar, and scrolls up when it reaches the bottom.</p>
<ul>
  <li><code>CLS</code> clears the screen and puts the text cursor at the top left.</li>
  <li><code>AT col,row</code> moves it, from 0,0 to 30,8, for the next PRINT.</li>
</ul>

<h3>Graphics</h3>
<p>The screen is 250 &times; 122 pixels, with x from 0 (left) to 249 and y from 0 (top) to 121. A colour of 1 is black (the default), 0 white, which erases.</p>
<div class="table-wrap"><table class="ref">
<thead><tr><th>Statement</th><th>Draws</th></tr></thead>
<tbody>
<tr><td><code>PSET x,y</code> or <code>PSET x,y,c</code></td><td>one pixel</td></tr>
<tr><td><code>LINE x1,y1,x2,y2</code> (<code>,c</code>)</td><td>a line</td></tr>
<tr><td><code>BOX x,y,w,h</code> (<code>,c</code>)</td><td>a filled rectangle <code>w</code> wide, <code>h</code> high, from its top-left corner</td></tr>
</tbody>
</table></div>
<p>Drawing and text share the screen. While a program runs the status bar isn't redrawn, so the program can use the whole screen; it comes back at the prompt.</p>

<h3>Sound and time</h3>
<ul>
  <li><code>TONE n</code> plays the calculator's tone <code>n</code>, 0 (low) to 9 (high), or 10 for the error sound. The program waits until it has played.</li>
  <li><code>WAIT ms</code> pauses that many milliseconds; <kbd>R/S</kbd> still stops the program during it.</li>
</ul>

<h3>Keys: KEY()</h3>
<p>While a program runs, the keys pressed (except <kbd>R/S</kbd> and <kbd>ON</kbd>, which stop it) are kept for <code>KEY()</code>. Each call gives the next one, in the order pressed, as its row and column from 1, or 0 when there's none left:</p>
KEYS_CODES
<pre><code>10 REM MOVE A BOX WITH 4 AND 6, R/S STOPS
20 CLS:X=120
30 BOX X,55,8,8
40 K=KEY()
50 IF K=0 THEN WAIT 20:GOTO 40
60 BOX X,55,8,8,0
70 IF K=52 THEN X=X-8
80 IF K=54 THEN X=X+8
90 GOTO 30</code></pre>
<p>Line 50 waits for a key, asleep, 20 ms at a time. Line 60 erases the box (colour 0) before it moves. A single pixel (<code>PSET</code>) is hard to see on the calculator's screen: for something that moves, draw a <code>BOX</code>.</p>

<!-- ============================================================ -->
<h2 id="files">9. Saving programs</h2>

<p>Programs are saved on the calculator's drive as plain text files, <code>NAME.BAS</code>, in the folder <code>/BIN/APPS/BASIC/</code>. A name has up to 20 letters, digits, <code>_</code> or <code>-</code>. The quotes can be left out: <code>LOAD RINGS3D</code> is <code>LOAD "RINGS3D"</code>. Lower case is the same name in capitals: <code>"Ring_3d"</code> is saved and listed as <code>RING_3D</code>.</p>
<div class="table-wrap"><table class="ref">
<thead><tr><th>Command</th><th>Does</th></tr></thead>
<tbody>
<tr><td><code>SAVE "NAME"</code></td><td>Saves the program, replacing a saved one of that name.</td></tr>
<tr><td><code>LOAD "NAME"</code></td><td>Loads a saved program, in place of the one in memory.</td></tr>
<tr><td><code>FILES</code></td><td>Lists the saved programs.</td></tr>
<tr><td><code>ERASE "NAME"</code></td><td>Deletes a saved program.</td></tr>
</tbody>
</table></div>
<p><strong>On a computer.</strong> In USB Drive mode the files are in <code>BIN/APPS/BASIC</code>. A <code>.BAS</code> file is the program as <code>LIST</code> shows it, so you can read and write programs in any text editor. Lines without a number are skipped when loading.</p>
<p><strong>AUTOSAVE.</strong> When you leave BASIC, the program in memory is saved as <code>AUTOSAVE.BAS</code> if it changed, and loaded again when BASIC starts. <code>FILES</code> doesn't list it.</p>

<!-- ============================================================ -->
<h2 id="errors">10. Errors</h2>

<p>The three messages of the original Tiny BASIC, plus BREAK. In a program each also says the line: <code>HOW? IN 120</code>.</p>
<div class="table-wrap"><table>
<thead><tr><th>Message</th><th>Means</th><th>For example</th></tr></thead>
<tbody>
<tr><td><code>WHAT?</code></td><td>It can't be read.</td><td>a misspelling (<code>PRIMT</code>), a missing <code>)</code> or <code>=</code></td></tr>
<tr><td><code>HOW?</code></td><td>It can't be done.</td><td>division by zero, <code>SQR(-1)</code>, <code>GOTO</code> a line that isn't there, <code>RETURN</code> without <code>GOSUB</code>, <code>@(5000)</code>, <code>LOAD</code> a file that isn't there</td></tr>
<tr><td><code>SORRY</code></td><td>There's no room.</td><td>the program is full (16 KB), or <code>GOSUB</code>s and <code>FOR</code>s nest more than 32 deep</td></tr>
<tr><td><code>BREAK</code></td><td><kbd>R/S</kbd>, <kbd>ON</kbd> or Ctrl-C stopped it.</td><td></td></tr>
</tbody>
</table></div>
<figure>IMG:b/errors.png|HOW?, WHAT? and HOW? IN 10 on the screen|<figcaption>An error at the prompt, and one in a program.</figcaption></figure>

<!-- ============================================================ -->
<h2 id="examples">11. Examples</h2>
<p>These come with the project, in <code>sim/drive/BIN/APPS/BASIC/</code>. Copy them to <code>BIN/APPS/BASIC</code> on the calculator's drive, then <code>LOAD "PRIMES"</code> and <code>RUN</code>.</p>

<h3>HELLO</h3>
<pre><code>LIST:HELLO</code></pre>
<h3>PRIMES</h3>
<pre><code>LIST:PRIMES</code></pre>
<figure>IMG:c/primes.png|The primes below 100|<figcaption>PRIMES.</figcaption></figure>
<h3>GUESS</h3>
<pre><code>LIST:GUESS</code></pre>
<figure>IMG:a/guess.png|A game of guessing a number|<figcaption>GUESS.</figcaption></figure>
<h3>SPIRAL</h3>
<pre><code>LIST:SPIRAL</code></pre>
<h3>SINE</h3>
<pre><code>LIST:SINE</code></pre>

<!-- ============================================================ -->
<h2 id="limits">12. Limits</h2>
<div class="table-wrap"><table>
<thead><tr><th></th><th>Limit</th></tr></thead>
<tbody>
<tr><td>Program</td><td>16,384 bytes: each line takes its text plus 3</td></tr>
<tr><td>A line</td><td>80 characters</td></tr>
<tr><td>Line numbers</td><td>1 to 32767</td></tr>
<tr><td>Variables</td><td>26, A to Z, and the array <code>@(0)</code> to <code>@(1023)</code></td></tr>
<tr><td>Numbers</td><td>34 significant digits, exponents to &plusmn;6144</td></tr>
<tr><td>GOSUB and FOR</td><td>32 deep each</td></tr>
<tr><td>Text</td><td>numbers only; strings appear in PRINT and as INPUT's prompt</td></tr>
</tbody>
</table></div>

<!-- ============================================================ -->
<h2 id="reference">13. Quick reference</h2>
<div class="table-wrap"><table class="ref">
<tbody>
<tr><th>Commands</th><td><code>RUN LIST NEW SAVE LOAD FILES ERASE</code></td></tr>
<tr><th>Statements</th><td><code>PRINT ? INPUT LET IF THEN GOTO GOSUB RETURN FOR TO STEP NEXT END STOP REM</code></td></tr>
<tr><th>The calculator</th><td><code>CLS AT PSET LINE BOX TONE WAIT DEG RAD DIGITS</code></td></tr>
<tr><th>Functions</th><td><code>SIN COS TAN ASN ACS ATN SQR EXP LOG LOG10 ABS SGN INT RND PI KEY SIZE</code></td></tr>
<tr><th>Operators</th><td><code>^ * / MOD + - = &lt;&gt; &lt; &gt; &lt;= &gt;= NOT AND OR</code></td></tr>
<tr><th>Keys</th><td><kbd>SHIFT</kbd> <kbd>ENTER</kbd> ALPHA/NUM &middot; <kbd>SHIFT</kbd> other mode once &middot; <kbd>&#9650;</kbd> <kbd>&#9660;</kbd> program editor, or move in a line &middot; <kbd>SHIFT</kbd> <kbd>&#9650;</kbd> history &middot; <kbd>SHIFT</kbd> <kbd>&#9660;</kbd> keywords &middot; <kbd>&larr;</kbd> delete &middot; <kbd>R/S</kbd> stop &middot; <kbd>SHIFT</kbd> <kbd>R/S</kbd> RUN &middot; <kbd>ON</kbd> stop, or leave (asks)</td></tr>
</tbody>
</table></div>

<footer>PX-BASIC &middot; based on Palo Alto Tiny BASIC by Li-Chen Wang (1976) &middot; needs PX-42F-Plus firmware with app API version 3.</footer>

</main>
</body>
</html>
'''

page = page.replace('STYLE', style.replace('</style>', extra_css + '</style>'))
page = page.replace('KEYS_ALPHA', keyboard(ALPHA, label='The keys in ALPHA mode'))
page = page.replace('KEYS_NUM', keyboard(NUM, label='The keys in NUM mode'))
page = page.replace('KEYS_CODES', keyboard(codes=True, label="Each key's KEY() number"))
page = re.sub(r'LIST:([A-Z]+)', lambda m: listing(m.group(1)), page)
page = re.sub(r'IMG:([^|]+)\|([^|]+)\|', lambda m: img(m.group(1), m.group(2)), page)
assert 'IMG:' not in page and 'KEYS_' not in page
out = os.path.join(PROJ, 'basic_manual.html')
open(out, 'w').write(page)
print(out, len(page))
