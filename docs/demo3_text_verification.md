# Demo 3 (Canvas2D テキスト総合検証) 確認手順

`-demo 3` で起動、または起動後 `3` キーで Demo 3 に切替。
ページ切替は `[` (前) / `]` (次) で 1〜5 を循環。
画面は **1280 × 720** 全体に描画される（HUD オーバーレイなし）。

## ブラウザ側の「正解」リファレンス

`docs/demo3_reference.html` を ブラウザで開くと、**同じ描画コードをブラウザの Canvas 2D API で実行した結果** が見られます（@font-face で同じ TTF/OTF を読み込み）。
jsengine 側と並べて見比べれば、サイズ・ベースライン・幅などのズレが一目で分かります。

`file://` で開くとブラウザのセキュリティで font 読み込みがブロックされる場合があるので、その場合は
```
cd D:/test/jsengine
python -m http.server 8000
```
として `http://localhost:8000/docs/demo3_reference.html` で開いてください。
ブラウザ側は ← / → / 1〜5 キー、または上部ボタンでページ切替できます。

ヘッダ（全ページ共通）:
- 左上 24,44 に青字 32px "Demo 3: Canvas2D Text"
- その下 24,72 に灰字 18px "Page N/5  ([ / ] to switch)  — タイトル"
- 86px 位置に横区切り線

---

## Page 1: Font family / size

### 上半分 — フォントファミリ一覧 (30px)
左 24px から下記の順で 48px 間隔の縦並び。各行が **同じ高さ・同じ x 位置で揃って** いること。

| 色             | テキスト                                                  |
|----------------|-----------------------------------------------------------|
| 白             | `OpenSans-Regular  AaBb 0123 jpqg — Latin baseline`        |
| 緑             | `OpenSans-Bold  AaBb 0123 jpqg — Bold` ※ 1 行目より明らかに太い |
| 青             | `Roboto-Regular  Roboto 0123 () [] {}`                     |
| オレンジ       | `RobotoMono-VariableFont_wght  mono = code(); /* x */` ※ 等幅 |
| 水色           | `NotoSansJP-Regular  日本語フォント あいうえお 漢字 0123`  |
| 桃色           | `NotoEmoji-Regular  😀👍🌟❤️🎉🔥` ※ カラー絵文字でなくモノクロ字形でも可 |

正しさのチェックポイント:
- **OpenSans-Bold が Regular より明確に太い** → font name の解決が family ごとに違うフォントを引いていること
- **RobotoMono が等幅** → `mono = code()` の全文字が同じ幅
- **日本語が文字化けせずに表示** → NotoSansJP の登録と FT loader 経路
- **絵文字が抜けずに何らか表示** → NotoEmoji が引かれている（FT は単色出力のため塗りつぶし字形になる）

### 下半分 — サイズスケール
横並びで 10 / 14 / 18 / 24 / 32 / 48 / 64 / 96 px の数字。
- 各数字の **下に水平線** (= `measureText().width` のガイド)。
- 線の長さが文字幅と一致して見えること。
- 96 → 64 → 48 → … が **線形にスケール** すること（96 が 10 の約 9.6 倍幅）。
- 最下に灰字 16px で `"10 / 14 / 18 / 24 / 32 / 48 / 64 / 96 px (横線は measureText().width)"`。

**よくある不具合パターン**:
- 全部が小さすぎる → `kPxToPt = 72/96` が適用されていない（旧バージョン）
- 横線が文字幅より明らかに長い／短い → `measureText` の advance 集計がズレている

---

## Page 2: textAlign / textBaseline

### 上 — textAlign（中央に縦ガイド線 x = 640）
中央の縦線 (灰) を基準に **3 行のテキスト**:

| 色 | textAlign | テキスト                       | 揃え結果                       |
|----|-----------|--------------------------------|--------------------------------|
| 緑 | left      | `left ← テキスト揃え`           | テキスト先頭が縦線に揃う       |
| 青 | center    | `center  中央揃え 🎯`           | テキストの中央が縦線に揃う     |
| 紫 | right     | `右揃え right →`                 | テキスト末尾が縦線に揃う       |

**チェックポイント**:
- 3 行とも縦線を起点にきれいに揃っているか
- 日本語と Latin 文字が混在しても各行のベースラインがズレないか
- 🎯 絵文字が center 行に同じ高さで並ぶか

### 下 — textBaseline（各行に水平ガイド線）
6 本の水平線が 32,450 から 44px 間隔で並ぶ。各線の上に **同じテキスト "ラベル — Apgy0 漢字"** を 32px で描画。

| 色   | textBaseline   | 期待される位置関係              |
|------|----------------|--------------------------------|
| 緑   | top            | 文字の **上端** が線に接する   |
| 明緑 | hanging        | top とほぼ同じ                 |
| 青   | middle         | 文字の **中心** が線に重なる   |
| 紫   | alphabetic     | 線が **ベースライン**（jpgy の出っ張りが下に出る）|
| 薄紫 | ideographic    | alphabetic とほぼ同じ          |
| 橙   | bottom         | 文字の **下端** が線に接する   |

**チェックポイント**:
- top: 'A' の上端が線にぴったり付く
- bottom: 'g' / 'y' の descender 含めた最下端が線に接する
- alphabetic: 'A' の下辺が線上、'g' / 'p' の下尻が線より下に出る
- 漢字も同じベースライン規則に従う

**不具合の見え方**:
- 全ベースラインで位置が同じ → `compute_text_offset` の dy が常に 0
- top と bottom が逆 → 符号反転

---

## Page 3: measureText() visualization

8 行のサンプル文字列。各行に以下:
- **半透明青の矩形** (= ascent から descent までの範囲)
- **濃いグレーの水平線** (= ベースライン)
- 白文字本体
- 右側に灰字でメトリクス値 `w=... a=... d=...`

サンプル行:
1. 22px OpenSans-Regular: `The quick brown fox jumps over the lazy dog`
2. 30px OpenSans-Bold: `Bold variation 太字`
3. 32px Roboto-Regular: `Roboto sample 0123 () [] {}`
4. 36px RobotoMono: `mono = code()`
5. 30px NotoSansJP-Regular: `日本語の文字列の幅を測ります`
6. 30px NotoSansSC-Regular: `中文字符串宽度测试`
7. 40px NotoEmoji-Regular: `😀👍❤️🔥🎉`
8. 48px OpenSans-Bold: `BIG 大きい`

**チェックポイント**:
- 矩形の幅が文字の見た目の幅と一致（左右が文字の端に近い）
- 矩形の高さが ascent + descent と一致（上辺が文字頂、下辺が descender 底）
- ベースラインが矩形の **下から descent 分上** の位置（≒ 'A' の下辺）
- 右側の `w` 値が直感的にもっともらしい（24px の "Hello" なら ~50 程度）
- 日本語・中文・絵文字も矩形が文字をきちんと囲っている

**不具合の見え方**:
- 矩形がテキストより明らかに **短い** → measureText が CSS px に戻す係数を欠いている
- 矩形がテキストより明らかに **長い** → スケール係数が逆方向
- 矩形が文字の上下にズレている → ascent/descent の符号取扱いミス

---

## Page 4: Multilingual + textLocale

5 行。各行は 32,160 から 88px 間隔。テキスト下に灰字 14px でメタ情報。

| 色     | font                     | locale | テキスト                                              |
|--------|--------------------------|--------|-------------------------------------------------------|
| 白     | 32px NotoSansJP-Regular  | ja-JP  | `日本語: 今日もコードを書こう。 (ja-JP)`              |
| 青     | 32px NotoSansSC-Regular  | zh-CN  | `简体中文: 今天也来写代码。`                          |
| 紫     | 32px NotoSansTC-Regular  | zh-TW  | `繁體中文: 今天也來寫程式碼。`                        |
| 橙     | 40px NotoEmoji-Regular   | -      | `😀👍🌟❤️🎉 🔥💖😊`                                    |
| 緑     | 30px OpenSans-Regular    | -      | `Mixed: Hello コード 你好 (fallback test)`            |

**チェックポイント**:
- 日本語、簡体字、繁体字がそれぞれ **異なるフォント由来の字形** で表示される
  （特に "今" や "码" などは SC / TC で形が微妙に違う）
- 絵文字行で 8 個の絵文字すべて表示される（FT loader はモノクロ字形を出力するため、塗りつぶしの形だけでも可）
- 最後の Mixed 行: OpenSans プライマリで Latin 表示、**`コード` `你好` は ThorVG FT の fallback** で他の登録済みフォント（NotoSansJP / SC）から拾われる
  - Mixed 行で日本語・中文部分が文字化け（豆腐 □）になっていたら fallback が効いていない

下部 16px 灰字 2 行で説明テキスト。

---

## Page 5: strokeText / transform / getImageData

### 左半分 — 縦並び 56px テキスト 3 種
1. 橙、`lineWidth=1`、`strokeText("Outline 1px ぬき文字", 32, 170)` — 細い輪郭のみ
2. 青、`lineWidth=4`、`strokeText("輪郭 4px 日本語", 32, 240)` (NotoSansJP) — 太い輪郭のみ
3. 緑塗り + 黒 2px ストロークを **同じ位置に重ねた** `Fill + Stroke 重ね` — 縁取り付き文字

### 右半分 — transform
- `W*0.62, 200` で **-30° 回転** した紫 54px の「回転テキスト」
- `W*0.7, 290` で **+15° 回転** した 60px の「🎉✨🌟」 (NotoEmoji)
- `W*0.62, 360` で **scaleX 1.6** の橙 36px の「scaleX 1.6x 横長」(横に引き伸ばされた文字)

**チェックポイント**:
- 輪郭文字に塗りが入っていない（中が透けて背景の黒が見える）
- Fill+Stroke が緑塗り+黒縁になっている
- 回転テキストが斜めになっている（破綻していない）
- scaleX 1.6 のテキストが等幅 1.0 のテキストより横に長く伸びている

### 下半分 — getImageData 検証ボックス
左下 `(32, 380)` から **500 × 140** の検証ボックス:
- 暗灰 (#222) 背景
- その内側 10px インセットに **赤 (#ff0000)** 矩形
- 中央に白色 60px NotoSansJP-Regular の `検査 CHECK` (`textAlign=center`, `textBaseline=middle`)

その下に灰字 16px で **3 行のサンプル結果**:
```
red bg @ (62,410) = rgba(255,0,0,255)
center (white) @ (282,450) = rgba(255,255,255,255)  ※ 白文字を踏んでいれば
red right @ (502,490) = rgba(255,0,0,255)
```
さらに下に説明行 `"白文字近傍は (255,255,255,255)、赤背景近傍は (255,0,0,255) に近いはず"`。

**チェックポイント**:
- 3 サンプルそれぞれの色値が期待値（赤 or 白）に **近い**
  - `red bg`: 完全に赤領域 → `(255,0,0,255)` ぴったり
  - `center (white)`: 文字の塗り部分を踏めば 255,255,255、踏まなければ 255,0,0（ボックス中央が "検" のどの字画に当たるか次第。ベタの白に乗っていれば一目で分かる）
  - `red right`: 完全に赤領域 → `(255,0,0,255)`
- Debug ビルド時のみ、ピクセル値が変わったタイミングで stderr に `Demo3 page5: ...` ログが流れる

最下に水色 16px で `measureText('Aj') @16px: w=... a=... d=...` を 1 行表示。
- `w` が 18 〜 24 程度、`a` が 12〜14、`d` が 3〜4 程度の値で **妥当な範囲** に収まっていればよい
- 全部 0 や負値だったら measureText が破綻している

---

## 全体の判定基準（重要度順）

1. **Page 1 のサイズスケール**: 24px と 48px が **見た目で 2 倍** の高さになっていること。72/96 スケールが効いていない場合、Release だけが「サイズが合わない」状態に逆戻りする。
2. **Page 3 の measureText 矩形**: テキスト幅と矩形幅が一致。これが合わないとレイアウト系ライブラリ（pixi.ui 等）が崩れる根本原因。
3. **Page 2 の textBaseline**: top と bottom と alphabetic で文字の位置が **明確にずれて見える** こと。固定値で動いていたら未実装。
4. **Page 4 の Mixed 行**: 日本語・中文部分が豆腐にならない → fallback が動いている。
5. **Page 5 の getImageData**: 赤背景サンプルが `(255,0,0,255)` を返す → flush → readback 経路が一貫している。

---

## 補足: Debug と Release の挙動差

前回セッションで「Debug は白帯、Release は文字出るがサイズ合わない」という症状があった件。
- 「サイズが合わない」 → 今回 `kPxToPt = 72/96` を適用したので Release で正しい size になるはず（Page 1 で確認）
- 「白帯」 → Debug 固有の挙動。今回直接の修正は入っていないので、まだ起きるなら canvas2d.cpp の Text オブジェクト寿命や状態残留を追う必要がある。Page 1 のフォントが全部出ていれば解消、フォント行のどれかが空白なら再現中。
