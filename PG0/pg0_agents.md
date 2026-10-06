# PG0 / PG0.5 のプログラムを書く AI エージェント向けの説明

> 利用者の方へ: このファイル (AGENTS.md) を、PG0 のプログラム (.pg0) を置くフォルダーにコピーしてから、
> そのフォルダーで AI エージェント (Claude Code、Codex、Gemini CLI など) を起動してください。
> AGENTS.md を読まないエージェントには「AGENTS.md を読んで」と伝えてください。

PG0 はプログラミング学習用の小さな言語で、PG0.5 はその拡張です。構文は C や JavaScript に似ていますが、
ずっと小さく、違う点が多くあります。**知っている言語の感覚で書かず、この説明に従ってください。**
利用者は初心者や子どものことが多いので、プログラムは短く読みやすく書き、説明はやさしい言葉でしてください。

## 進め方

1. プログラムを `名前.pg0` に UTF-8 で書く (改行コードは問わない)。
2. 構文を調べる: `pg0cmd -c -j 名前.pg0`
3. 実行する: `pg0cmd -j --timeout=5000 名前.pg0`
4. JSON の `status`、`output`、`error`、`variables` を読み、直して 2 に戻る。
5. 動いたら、利用者に PG0 (pg0.exe) で開いて実行 (F5) してもらう。

- **実行して確かめる前に「できました」と言わない。** 終わらないプログラム (ゲームなど) があるので `--timeout` は必ず付ける。
- 利用者が同じファイルを pg0.exe で開いている場合、ファイルを書き換えると pg0.exe が自動で読み込み直し、
  変更された行を表示する (利用者が編集中のときは、読み込み直すかを利用者に確認する)。
- pg0.exe の実行モード (PG0 / PG0.5) は利用者の設定で決まる。PG0.5 の機能を使うプログラムは、
  `#import(...)` が無いなら先頭に `#option("pg0.5")` を書く (どちらかがあれば必ず PG0.5 で動く)。

## pg0cmd (コマンドラインでの実行)

pg0.exe と同じフォルダーにある。既定のインストール先は `C:\Program Files (x86)\pg0\pg0cmd.exe` で、
PATH には登録されないのでフルパスで実行する (見つからない場合は利用者に場所を聞く)。

```
pg0cmd [オプション] file.pg0 [スクリプトに渡す引数...]
```

| オプション | 意味 |
|---|---|
| `-c`, `--check` | 構文チェックだけを行う (実行しない) |
| `-j`, `--json` | 結果を JSON (UTF-8、1 行) で出力する |
| `--timeout=ミリ秒` | 実行時間の上限。過ぎると停止する |
| `--max-steps=数` | 実行ステップ数の上限 |
| `--input=ファイル` | `input()` が 1 行ずつ読むファイル (UTF-8)。無い場合は標準入力から読む |
| `-p` | PG0 モードで実行する (既定は PG0.5) |
| `-s` | 変数の宣言 (`var`) を必須にする |
| `--utf8` | `-j` を付けないときの入出力を UTF-8 にする (既定は Windows のコードページ) |
| `--headless` | 画面ライブラリのウィンドウを出さず、音も鳴らさない |
| `--screenshot=ファイル.png` | 終了時 (停止時) の画面ライブラリの画面を PNG で保存する |
| `--screen-input=ファイル` | 画面ライブラリへのキーとタッチの操作を書いたファイル |

オプションは `-` で始める (`/c` の形は Git Bash などでパスに変換される)。`-cj` のようにまとめて書ける。
終了コードは 0: 正常終了、1: エラー、2: 実行時間かステップ数の上限で停止。

### 実行結果 (`-j`)

```json
{"status":"error","mode":"PG0.5","output":"before","output_truncated":false,
 "error_output":"Error: [a.pg0]: 0 で除算をしました(4): b = a / 0\n",
 "result":null,"result_type":null,
 "error":{"message":"Error: [a.pg0]: 0 で除算をしました(4): b = a / 0","line":4,"phase":"runtime"},
 "variables":{"a":1,"b":0},"screen":null,
 "stats":{"steps":16,"elapsed_ms":0,"input_lines_used":0}}
```

| フィールド | 意味 |
|---|---|
| `status` | `ok`: 正常終了 / `error`: 構文エラーか実行時エラー / `timeout`、`step_limit`: 上限で停止 (終わらないゲームでは正常) |
| `mode` | 実際に動いたモード (`PG0.5` か `PG0`) |
| `output` | `print()`、`println()` の出力 |
| `error_output` | `error()` の出力とエラーメッセージ |
| `result`、`result_type` | `exit 値` (最上位の `return 値`) の値と型 (`integer`、`float`、`string`、`array`)。無ければ `null` |
| `error` | `null` か `{"message", "line", "phase"}`。`line` は 1 始まりの行番号、`phase` は `parse` (構文) か `runtime` (実行時) |
| `variables` | 終了時 (停止時、エラー時) のグローバル変数。名前とキーは小文字になる。ブロックや関数の中の変数は入らない |
| `screen` | 画面ライブラリを使っていれば `{"width", "height", "screenshots": [保存した PNG のパス]}` |
| `stats` | `steps`: 実行ステップ数、`elapsed_ms`: 実行時間、`input_lines_used`: `input()` が読んだ行数 |

配列は、キー付きの要素が無ければ JSON の配列、あればオブジェクト (キーの無い要素はインデックスがキー) になる。
`-c -j` の結果は `{"ok": true か false, "mode", "error"}`。

### 画面ライブラリを使うプログラムを確かめる

```
pg0cmd -j --headless --timeout=3000 --screenshot=end.png --screen-input=ops.txt game.pg0
```

- 時間は実時間で進む。3 秒動かして止め、止めたときの画面を `end.png` に保存する (`status` は `timeout` になる)。
- 見た目は PNG を見て確かめる。見ていない見た目を「確認した」と言わない。
- 動きは `variables` (位置や点数などのグローバル変数) でも確かめられる。
- 操作のファイルは 1 行に 1 つの操作を書く。`startScreen()` が呼ばれた時点から順に実行される。

| 操作 | 意味 |
|---|---|
| `wait ms` | 待つ |
| `key 名前 [ms]` | キーを押して離す (既定は 100 ミリ秒)。名前は `inKey()` と同じ (`ArrowLeft`、`a`、`Enter` など)。空白キーは `Space` |
| `keydown 名前` / `keyup 名前` | キーを押したままにする / 離す (同時押し用) |
| `tap x y` | タッチ (クリック) する。座標は `startScreen()` の座標 |
| `press x y ms` | 指定した時間タッチし続ける |
| `drag x1 y1 x2 y2 [ms]` | ドラッグする (既定は 300 ミリ秒) |
| `move x y` | タッチせずに位置だけ動かす |
| `screen ファイル.png` | その時点の画面を PNG で保存する |

`#` か `//` で始まる行はコメント。`--headless` を付けないとウィンドウが開いて前面に出るので、確認には付ける。

## 言語の要点 (PG0.5)

```
#import("lib/io.pg0")          // println を使う
total = 0                      // ブロックの後で使う変数は、先に最上位で作る
for (i = 1; i <= 10; i++) {
    if (i % 2 == 0) {
        total += i
    } else if (i == 9) {
        break
    }
}
println("合計: " + total)      // 文字列 + 数値 は文字列になる

function area(w, h = 1) {      // h は省略できる (既定値 1)
    return w * h
}
list[] = {3, 1, 2}             // 配列の初期化
user[] = {"name": "pg0", "age": 5}   // キー付き (連想配列)
println(length(list) + " " + user["name"] + " " + area(3, 4))
```

- 1 行に 1 文。1 行に複数書くときは `;` で区切る。コメントは `//` だけ。
- `if`、`else`、`while`、`for`、`do`、`switch`、`function` の本体は必ず `{ }` で囲む。
- 文: `if` / `else if` / `else`、`while`、`do { } while (式)`、`for (式1; 式2; 式3)`、
  `switch (式) { case 値: ... break  default: ... }`、`break`、`continue`、`return`、`exit`。
- 型: 整数 (32bit)、実数 (64bit)、文字列 (`"..."` か `'...'`)、配列。`isType(v)` は 0 整数、1 実数、2 文字列、3 配列。
- 真偽: `0`、`0.0`、`""` が偽、それ以外が真。比較と論理演算は `1` か `0` を返す。
- 演算子: `+ - * / %`、`== != < > <= >=`、`&& ||`、`!`、`++ --`、`+= -= *= /= %=`、
  ビット演算 `& | ^ ~ << >> <<< >>>`。
- 配列: 添字は 0 から。`a[5] = 1` で自動的に伸びる。`a[] = b[]` はコピー、`a[] = b[] + c[]` は連結。
  `m[1][0] = 3` で多次元。要素数は `length(a)`。キーは `getKey(a, i)` で取り出せる。
- 関数: 定義は最上位だけ (呼び出しより後に書いてもよい)。引数はコピーで渡る。
  `function f(&a)` のように `&` を付けると参照渡し。`return` が無ければ `0` を返す。
- `exit` はプログラムを終了する。`exit 値` で結果 (`result`) を返せる。
- プリプロセッサ: `#import("lib/io.pg0")` (ライブラリや別の .pg0 の取り込み)、
  `#option("pg0.5")`、`#option("strict")` (`var` を必須にする)。
  取り込んだ .pg0 の関数は呼べるが、変数は見えない。

### PG0 モード

PG0 は PG0.5 の一部だけを使う。型は整数だけ、文は代入と `if` / `else` と `while` と `exit` だけで、
関数は一つも無い (`print` も `input` も無い)。演算子は `+ - * / %`、比較、`&& ||`、`!` だけ。
`7 / 2` は `3` になる。結果は `exit 値` か変数で返し、`pg0cmd -p -j` の `result` と `variables` で確かめる。
利用者が「PG0 で」と言わない限り PG0.5 で書く。

## 間違えやすい点

1. **`{ }` の中で初めて使った変数は、そのブロックの中だけのもの。** ブロックを出ると消える。
   合計、結果、フラグなどは、ループや `if` の前に最上位で作っておく。

   ```
   if (a > b) { max = a } else { max = b }
   println(max)     // 0 が出る: max は if の中だけの変数だった。先に max = 0 と書く
   ```

2. 知らない変数はエラーにならず `0` になる。`true`、`false`、`null` は無い (書くと `0` の変数になる)。
   `#option("strict")` を書くと `var` で宣言していない変数がエラーになるので、打ち間違いを見つけられる
   (その場合は、すべての変数を `var a = 0` のように宣言する)。
3. 関数の中の代入は、同じ名前のグローバル変数が既にあればそれを書き換え、無ければ関数の中だけの変数になる。
   関数で使う作業用の変数は `var` で宣言し、共有する変数は関数を呼ぶ前に最上位で作る。
4. `print` は改行しない。`println` は `#import("lib/io.pg0")` が必要。
5. 整数どうしの `/` は割り切れなければ実数になる (`7 / 2` は `3.5`)。整数にするには `int(7 / 2)`。
   実数は小数点以下 16 桁で表示される (`3.5000000000000000`)。値が整数になる実数は整数に戻る (`6 / 3` は `2`)。
6. 整数は 32bit で、あふれると黙って折り返す (`2147483647 + 1` は `-2147483648`)。
7. 文字列に使える演算子は `+`、`==`、`!=` だけ。`<` などはエラーになる。
8. 文字列に `s[i]` は使えない (`s` が配列に変わって壊れる)。`code(s, i)`、`array(s)`、`substring()` を使う。
9. 条件式の中の `=` は構文エラー。比較は `==`。
10. 変数名、関数名、配列のキーは半角の大文字と小文字を区別しない (`Score` と `score` は同じ)。
    キーワードは小文字だけ。
11. 配列は代入と引数でコピーされる。`e = list[0]` はコピーなので、`e` を変えても `list[0]` は変わらない。
    要素は `list[0]["hp"] = 1` のように直接書き換える。関数で書き換えるなら `&` の引数にする。
12. `a[i]` は読むだけでも配列を伸ばす (`a[100]` を読むと 101 要素になる)。負の添字はエラー。
13. 配列の初期化に変数をそのまま並べると、変数名がキーになる (`{x, y}` は `{"x": 1, "y": 2}`)。
    ただのリストにするには `{x + 0, y + 0}` と書くか、`a[0] = x` と 1 つずつ入れる。
14. 式を次の行に続けられるのは、行末が演算子のときと、`( )`、`[ ]`、配列の初期化の `{ }` の中だけ。
    行頭に `+` を書いて続けることはできない (別の文になる)。
15. `input()` は入力が尽きると整数の `0` を返す (`isType(x) == 0` で分かる)。空行は `""`。
16. 無いもの: ブロックコメント `/* */`、三項演算子、`true` / `false`、指数表記 (`1e3`)、
    ブロックの中の関数定義、クラス、例外。
17. 画面のプログラムでは、ループ 1 回ごとに `sleep(ms)` を呼ぶ。角度はラジアン。
    `drawText` の (x, y) は文字の左上。`clearRect` は透明にする (背景色に戻る)。

## 標準関数 (PG0.5、取り込み不要)

| 関数 | 説明 |
|---|---|
| `print(v)` | 出力する (改行なし)。配列は `{1,"key":2}` の形で出る |
| `error(v)` | エラー出力に 1 行出す (実行は続く) |
| `input()` | 入力を 1 行読む (文字列)。数値にするには `number(input())` |
| `isType(v)` | 型 (0 整数、1 実数、2 文字列、3 配列) |
| `length(v)` | 配列の要素数、文字列の文字数 |
| `array(s)` / `string(a)` | 文字列を 1 文字ずつの配列に / 配列をつなげて文字列に |
| `number(s)` / `int(v)` | 文字列を数値に / 整数に変換 (小数点以下は切り捨て) |
| `code(s, i = 0)` / `char(c)` | 文字コードを取得 / 文字コードを 1 文字に |
| `getKey(a, i)` / `setKey(a, i, key)` | 配列の i 番目の要素のキーを取得 / 設定 |

## ライブラリ

- `#import("lib/io.pg0")`: `println(v)`、`wait(ms)`、`saveValue(key, value)`、`loadValue(key)`、
  `removeValue(key)`、`get_clipboard()`、`set_clipboard(text)`
- `#import("lib/math.pg0")`: `abs`、`sign`、`floor`、`ceil`、`round`、`sqrt`、`pow(base, exp)`、`exp`、`log`、
  `sin`、`cos`、`tan`、`atan`、`atan2(y, x)`、`hypot(x, y)`、`max(a, b, ...)`、`min(a, b, ...)`、
  `random()` (0 以上 1 未満。`random(seed)` で再現できる乱数列にする)
- `#import("lib/string.pg0")`: `trim(s)`、`to_lower(s)`、`to_upper(s)`、`substring(s, begin, length = -1)`、
  `in_string(s, search, from = 0)` (無ければ -1)、`split(s, separator)`、`str_match(pattern, s)` (`*` と `?`)
- `#import("lib/screen.pg0")`: 画面への描画、キーとタッチの入力、音

### 画面ライブラリ (lib/screen.pg0)

option は `{"color": "#ff0000", "fill": 1}` のようなキー付きの配列で、省略できる。
色は `"#rrggbb"`、`"red"` などの色名、`"rgb(0,128,0)"`。既定の色は黒、線の太さは 1。

| 関数 | 説明 |
|---|---|
| `startScreen(width, height, option)` | 画面を開始する (最初に呼ぶ)。option: `color` (背景色。既定は白)、`fit` |
| `sleep(ms)` | 待つ。アニメーションやゲームのループで毎回呼ぶ |
| `time()` / `timeString(time, format)` | 1970 年からのミリ秒 (実数) / 日時の文字列 (`"YYYY/MM/DD hh:mm:ss"`) |
| `startOffscreen()` / `endOffscreen()` | この間の描画をまとめて画面に出す (ちらつきを防ぐ) |
| `clearRect(x, y, w, h)` | 範囲を透明にする |
| `drawLine(x1, y1, x2, y2, option)` | 直線。option: `color`、`width` |
| `drawRect(x, y, w, h, option)` | 四角。option: `color`、`width`、`fill` |
| `drawCircle(x, y, radius, option)` | 円。option: `color`、`width`、`fill`、`radius_y`、`rotation`、`start`、`end`、`close` |
| `drawPolyline({{x, y}, ...}, option)` | 折れ線、多角形。option: `color`、`width`、`fill`、`close` |
| `drawText(text, x, y, option)` | 文字。option: `color`、`fontsize` (既定 30)、`fontstyle`、`fontface`、`fill`、`width` |
| `measureText(text, option)` | 文字の大きさ `{"width", "height"}` |
| `drawFill(x, y, color)` | つながった同じ色の範囲を塗りつぶす |
| `drawScroll(x, y)` | 画面をずらす (はみ出た分は反対側に出る) |
| `createImage(x, y, w, h, option)` / `drawImage(id, x, y, option)` | 画面の一部を画像にする / 描く。option: `width`、`height`、`angle`、`alpha` |
| `startMask(option)` / `endMask()` | 描いた範囲だけを残す (option の `destination` が `"out"` なら描いた範囲を透明にする) |
| `rgbToPoint(x, y)` | その位置の色 `{"r", "g", "b"}`。`rgbToHex(rgb)`、`hexToRgb(hex)` で変換 |
| `inTouch()` | `{"x", "y", "touch", "button", "pos"}`。`touch` が 1 ならタッチ (クリック) 中 |
| `inKey(key)` | キーが押されていれば 1。名前は JavaScript の `KeyboardEvent.key` (`"ArrowLeft"`、`"a"`、`" "`、`"Enter"`)。引数なしで押されているキーの配列 |
| `playSound(note, start, length, volume = 1)` | 音を鳴らす。note は周波数か `"C4"` などの音名 |
| `playMusic(notes, option)` / `bgm(notes, option)` / `stopSound()` | `{{"C4", 500}, {"D4", 500}}` を続けて鳴らす / 繰り返す / 止める |

ゲームやアニメーションの基本の形:

```
#import("lib/screen.pg0")
startScreen(640, 480)
x = 320                                   // ループの中で変える変数は先に作る
while (1) {
    if (inKey("ArrowLeft")) { x -= 4 }
    if (inKey("ArrowRight")) { x += 4 }
    t = inTouch()
    if (t["touch"]) { x = t["x"] }        // キーとタッチの両方で操作できるようにする
    startOffscreen()
    drawRect(0, 0, 640, 480, {"color": "#ffffff", "fill": 1})   // 前の絵を塗りつぶして消す
    drawCircle(x, 240, 20, {"color": "#0000ff", "fill": 1})
    endOffscreen()
    sleep(16)
}
```

## 詳しい資料

pg0cmd.exe と同じフォルダーに、言語仕様 (`pg0.txt`、`pg0.5.txt`)、ライブラリのリファレンス
(`pg0.5_lib.txt`、英語版 `pg0.5_lib_eng.txt`)、サンプル (`sample` フォルダー) がある。
この説明で足りないときは、推測せずにそれらを読む。
