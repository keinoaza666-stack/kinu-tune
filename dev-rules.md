# Kinu Tune の開発ルール(設計の約束)

コードを変える前に読むファイルです。ここには「守らないと別の所が壊れる約束」だけを書きます。ビルド手順・検証の記録・過去の経緯は、リポジトリの外の `verify-map.md` にあります。

約束を新しく見つけたら、その変更と同じコミットでここに書き足します。1つの約束につき「何を守るか」「必ず通す入口」「破るとどのテストで分かるか」を書きます。

## 1. 音程を書き換える処理は NotePitch だけを通す

- ノートの音程(`Note::midiNote`)と「ピッチを補正」の基準(`Note::lastNonMacroMidiNote`、歌った音程)は、`Source/Utils/NotePitch.h` の関数だけで書き換える
  - 編集で動かす(ドラッグ・矢印キー・ハモリ・吸着・傾き・スマートコントロール): `NotePitch::applyEdit`(元に戻す記録用の値は `sourceAfterEdit`)
  - 記録した値を戻す(元に戻す・やり直し・取り消し・読み込み・分割と結合): `NotePitch::restore`
  - 補正そのもの(ピッチを補正・ピッチセンター): `NotePitch::applyCorrection` と `setCorrectionSource`
  - 新しく歌った音程にする(ピッチの描き込み): `NotePitch::setSungPitch`
- 元に戻す動作(`Source/Undo/`)は、基準の値を省略できない作りにしてある。新しい動作を足す時も、前後の基準を必ず受け取る
- 破ると: テスト `SourceGuard_pitch-writes` が、直接書き換えている行を示して落ちる。基準の扱いの誤りは `VariAudioInspectorTests` が拾う
- この検査は1行ずつ読むので、メンバー関数ポインタ経由の呼び出しや、呼び出しを複数行に分けた書き方は見逃す。そういう書き方をしない

## 2. 再生以外で音を出す処理は AuditionOutput だけを通す

- ドラッグ中の試聴・鍵盤の試聴・範囲の試聴は、出口が1つ(プラグインではホストの試聴経路、単体アプリでは AudioEngine の試聴ループ)。`Source/UI/Main/AuditionOutput.h` の `startHostPreview` / `playLoop` / `stopLoop` / `stopHostPreview` だけで鳴らす・止める
- 鳴らし始める時は、出口を前の持ち主から取り上げる(`claim`。前の持ち主には `setOnTakenOver` で登録した処理が呼ばれる)。新しい持ち主もループを鳴らすなら前のループは置き換わり、そうでなければ(範囲の試聴)前のループは止める。再生・一時停止・停止は、先に `yieldToTransport` を呼ぶ
- 新しい種類の試聴を足す時は、`AuditionOutput::Source` の `Count` より前に種類を足し、ループを鳴らす種類なら `playsLoop` にも足す。取り上げられた時の処理を MainComponent の構築時に登録する
- ホストへの4つのコールバック(試聴範囲の開始・停止、ループの再生・停止)は AuditionOutput の中にしか無い。MainComponent から直接は呼べない
- 破ると: テスト `SourceGuard_audition-output` が、AuditionOutput と AudioEngine 以外から試聴ループを直接呼んでいる行を示して落ちる(1行ずつ読む検査なので、§1 と同じ書き方の制限がある)。受け渡しの誤りは `AuditionOutputTests`(単体アプリの経路を含む)と `AuditionHandoffTests`(本物の MainComponent)が拾う

## 3. プロジェクトは持たずに毎回問い合わせる

- プロジェクトが差し替わる経路は十数か所ある(ファイルを開く・スナップショットの復元・ARA など)。画面の部品にプロジェクトのポインタを持たせず、`getCurrentProject` で毎回問い合わせるか、`parameterPanel.onProjectBound` に乗せる
- 破ると: 自動テストは無い。キーが「-」になる、別のイベントの値が出る、といった症状で現れる

## 4. ノートのポインタは編集のたびに捨てる

- 元に戻す・やり直し・結合・分割の後はノートの一覧が作り直される。マウスを乗せたノートやハンドルのポインタは `PianoRollComponent::dropTransientNotePointers()` で捨てる(`MainComponent::afterHistoryChange` から呼ぶ)
- 破ると: 消えたノートを触って落ちる

## 5. キーとスケールの読み方

- ハモリとエディタースケールのキーは `Project::getPreferredScaleMode()` / `getScaleRootNote()` で読む。`getScaleMode()` はドラッグの吸着の設定で、クロマチックを返すことがある
- ユーザーがキーを変える入口は、左のインスペクタのエディタースケールだけ(`ParameterPanel` は未設定のルートを整える時にだけ書き込む)

## 6. インスペクタの表示の読み直し

- インスペクタは 1 秒に 8 回、表示を読み直す。プルダウンを読み直す時は `VariAudioInspector::showMenuValue` を通す(ポップアップを開いている間と、自分が最後に書いた値と違う間は触らない)。直接 `setSelectedId` すると、選んだ値が消える

## 7. Windows でも通る書き方

- キャプチャなしのラムダ `[]` の中でローカルの `constexpr` を読まない(Windows のコンパイラ MSVC が通さない)。`[&]` にする
- 破ると: GitHub の自動チェック(CI)の `windows-build` が落ちる
- Windows のプラグインは C++ の実行時ライブラリ(MSVC ランタイム)を静的にリンクする(`CMakeLists.txt` の `CMAKE_MSVC_RUNTIME_LIBRARY`。ターゲットを作る前に設定する)。共有のランタイムだと、DAW が自分のフォルダーに持っている古い `msvcp140.dll` を使うことになり、新しいコンパイラーで作った `std::mutex` が最初のロックで落ちる(一覧には出るが、挿した瞬間に落ちる)。ONNX Runtime の DLL は共有のランタイムのままだが、Microsoft が古いランタイムでも動くように作っている
- 破ると: CI の `windows-build` で `tools/windows/check-vst3.ps1` が落ちる(プラグインが `msvcp140.dll`・`vcruntime140.dll` を読み込む指定を持っている、または Visual C++ 14.29 のランタイムで作成できない)。Windows 版のリリース用ワークフローも、同じ検査に通らなければ zip を付けない

## 8. 文面を変えたらバイナリも作り直す

- `THIRD_PARTY_NOTICES.txt` と `Resources/lang/*.json` はプラグイン本体に埋め込まれる。変えたら Mac 版・Windows 版を作り直して配布物を差し替える

## 9. 配布するバイナリにビルドした Mac のパスを残さない

- リンクの時に、ビルドフォルダー内の ONNX Runtime の場所(ユーザー名を含む絶対パス)が、ライブラリを探す場所(rpath)として記録される。Mac のバンドルは `cmake/StripBuildRpaths.cmake` が絶対パスの rpath を消し、残っていればビルドを失敗させる
- 新しい Mac 用のターゲット(バンドル)を足したら、同じ処理を POST_BUILD に足す
- 確かめ方: `otool -l <バイナリ> | grep -A2 LC_RPATH` に `/` で始まるパスが無いこと。`strings` では rpath が出ないので、これで確かめる

## 10. 合成エンジンの設定はプラグインの中で1つを共有する

- プラグイン(`Source/Plugin`)が持つ `EditorController` は何個あっても、合成エンジン(AI リシンセシス / クラシック)の設定は1つの `SynthesisEnginePreference` を全員で共有する。作る時は必ず `Source/Plugin/RenderControllers.h` の `makeSharedEngineController`(処理側では `PitchNetAudioProcessor::makeRenderController`)を通す。`std::make_unique<EditorController>(false)` を直接書かない
- エンジンの読み書きは `EditorController::getSynthesisEngineType` / `setSynthesisEngineType` だけ。共有中はメンバー変数ではなく共有値を読み書きする。合成の直前に `IncrementalSynthesizer` へ渡す値もこの getter から取る
- エンジンを切り替える操作(右パネルのラジオ)は、設定を保存したあと `MainComponent::rerenderEditedNotes` を呼ぶ。編集が音に届いているノートの洗い出しは `Project::markEditedNotesDirty` に任せ、自前で `markDirty` を回さない
- 合成の開始と完了は `MainComponent::showRenderStatus` に通す(右パネルの状態行)。新しく合成を始める経路を足したら、開始と完了の両方で呼ぶ
- 破ると: テスト `SourceGuard_render-controllers` が、`Source/Plugin` の中で `std::make_unique<EditorController>(false)` と書いた行(`RenderControllers.h` 以外)を示して落ちる。この検査は `make_unique` にリテラル `false` を渡す綴りしか見ない。`new EditorController(false)`、変数を渡す書き方、`(true)`(プラグインの中で音声デバイス付きを作る誤り)、`(` と `false` を行で分けた書き方は通ってしまうので、そういう書き方をしない。共有の受け渡しは `SynthesisEngineShareTests` が拾う。状態行の呼び忘れは自動テストが無く、「合成中」のまま止まる、または前回の表示のままになる症状で現れる

## 11. 変更の進め方

- 不具合を直す時は、直す前に失敗するテストを足す
- コミットの前に、テスト全部(`ctest --test-dir build`)と pluginval(厳しさ 5)を通す。`pitchnet_ui` にリンクするテストは、UI を変えたら全体をビルドしてから流す
- GitHub に送ったら、自動チェック(CI)が通ったことを確かめる
- モデルの読み込み・推論・合成(FCPE・GAME・声の合成モデル・PSOLA)を変えた時は、`tests/ModelInferenceTests.cpp` が通ることを確かめる。これは CI の Windows ジョブでも走る唯一のモデル検査で、Windows で解析と合成が動く根拠はここだけにある。新しいモデルや合成エンジンを足したら、この検査にも段階を足す
- 新しく作るソースファイルの先頭には、Kinu Tune の著作権とライセンスの3行を書く(既存の新規ファイルと同じ文面)
- GitHub の URL を渡されて実装・移植する時、依存を新しく足す時、第三者のフォーク(例: Rowryu/kinu-tune)からコードを取り込む時は、§14 の安全確認を先に行い、3段階の判定を報告してから動かす

## 12. ONNX Runtime は読み込めた時だけ呼ぶ

- ONNX Runtime(AI の推論エンジン)は、プラグインに同梱したものだけを、`OnnxRuntime::initialise` が場所を指定して読み込む(macOS は `Frameworks/libonnxruntime.dylib`、Windows はプラグインと同じフォルダーの `onnxruntime.dll`)。`ORT_API_MANUAL_INIT` は全プラットフォームで定義してある。Windows の System32 にある古い `onnxruntime.dll` には決して切り替えない。プラグインは `onnxruntime.dll` から読み込み指定(import)を1つも持たない作りで、Windows のリンク時に出る LNK4199(遅延読み込みの対象が無い)はそのため。それでも遅延読み込みが起きた時に備えて、`OnnxRuntimeLoader.cpp` のフックが補助処理に DLL を探させない(探させると System32 の古いものを読む)
- 読み込めない時(Windows ではたいてい Visual C++ 再頒布可能パッケージが入っていない)は、API のポインターが空のままになる。この状態で `Ort::` を1つでも呼ぶと落ちる。`Ort::Env` を作る所・`Ort::GetAvailableProviders` など ONNX Runtime を呼ぶ所は、先に `OnnxRuntime::isAvailable()` を確かめる(今は FCPE・RMVPE・GAME・声の合成・AI リシンセシスを選べるかの判定 `getAiResynthesisSupport`・設定画面の6か所)。解析の入口 `EditorController::analyzeAudio` と、最初に開いたエディターは `OnnxRuntime::showUnavailableMessageOnce()` で直し方を見せる(1プロセスに1回だけ。ARA のホストはクリップごとに解析するので、毎回出すと何枚も重なる)。理由は記録(Windows はデバッガー出力)に出る
- 新しく ONNX Runtime を呼ぶ所を足したら、同じく先に確かめ、`tests/OnnxRuntimeAvailabilityTests.cpp` にその入口を足す
- 破ると: `OnnxRuntimeAvailabilityTests` が落ちる(API を空にした状態で各入口を通すので、確かめ忘れた所でテストが落ちる。ただし AI リシンセシスを選べるかの判定は Windows でしか ONNX Runtime を呼ばず、macOS のテストでは通らない。Windows の CI はこのテストを作らないので、この判定の確かめ忘れは自動では見つからない)。Windows では CI の `tools/windows/check-vst3.ps1` の、ランタイムの無いコンテナーでの検査が落ちる

## 13. インストーラーは zip の中身から作る

- Mac の `.pkg`(`packaging/macos/build-pkg.sh`)と Windows のインストーラー(`packaging/windows/KinuTune.iss`、`.github/workflows/windows-installer.yml`)は、配布する zip を展開したものから作る。インストーラーのためだけにビルドし直さない。zip とインストーラーの中身が同じだと言えるのはこのため
- Mac は利用者のホームの中(手で入れる手順と同じ場所)に入れる。管理者のパスワードが要らず、手で入れたものを上書きする。`/Library` に入れると、手で入れた `~/Library` のものと2つ並ぶ
- Mac の `.pkg` では、同じ識別子のバンドルが別の場所にあるとインストーラーがそこへ入れてしまう(relocation)。`build-pkg.sh` は `BundleIsRelocatable` を切っている。部品を足す時も同じ関数を通す
- macOS はこの Mac で作ったファイルに消せない属性(`com.apple.provenance`)を付け、`pkgbuild` はそれを「._」のファイルとして中身に入れる。`build-pkg.sh` は中身を cpio と mkbom で作り直して取り除き、仕上がった中身に残っていれば失敗する
- Windows の `AppId` は変えない。変えると新しい版が古い版を見つけられず、2つ入る
- Mac の `.pkg` にはライセンス文書(LICENSE・THIRD_PARTY_NOTICES.txt・MODIFICATIONS.md・licenses)も入れる(モデルと同じフォルダー)。zip と Windows のインストーラーにも入っている
- Windows のインストーラーはモデルを `%PROGRAMDATA%` に入れる。プラグインは `%APPDATA%\Keinoaza\Kinu Tune\models` が「フォルダーとして」あれば、ファイルが欠けていてもそちらだけを読む(ファイルごとに `%PROGRAMDATA%` へは戻らない)。README はインストーラーで入れる人に `%APPDATA%` 側を消すよう書いている。モデルを変える版を出す時は、この順番に気をつける。インストーラーはモデルのフォルダーを空にしない(利用者が置く rmvpe.onnx を消さないため)
- 破ると: Windows は `windows-installer.yml` の `tools/windows/check-installer.ps1`(まっさらなコンテナーで入れて、プラグインを作って、消す)が落ちる。ただしモデルを実際に読み込むところまでは確かめていない。Mac は自動の検査が無い。作り直したら、この Mac で `installer -pkg <pkg> -target CurrentUserHomeDirectory` で入れ、zip の中身と `diff -r` で同じこと、属性が付いていないことを確かめる

## 14. 外から取り込むコードは安全確認してから動かし、取り込んだものはハッシュかコミット ID で固定する

- 守ること: 外部リポジトリのコード・依存・GitHub Actions のアクションは、`~/.claude/skills/_shared/external-code-intake.md` の手順(空のフォルダーに取得→自動で走る仕掛けの洗い出し→`scan_external_code.sh`→依存の固定→3段階の判定を報告)を通してから、ビルド・インストール・テストに入れる。判定が「注意点あり」以上なら、動かすかどうかはユーザーが決める
- 必ず通す入口: 確認の記録は `verify-map.md`(確認日・コミットID・判定・注意点)と、ライセンスと出典は `THIRD_PARTY_NOTICES.txt` / `docs/source-ledger.md`
- CMake がダウンロードするファイル(ONNX Runtime・DirectML の NuGet・GAME モデル)の SHA-256 は `cmake/DependencyHashes.cmake` の表が正本で、ダウンロードは必ず表の値で検証する。`ONNXRUNTIME_VERSION`・`DIRECTML_VERSION`・`GAME_MODELS_VERSION` を上げる時は、新しいファイルを 2 回取得して値が一致することを見てから表も同じコミットで更新する。表に無い変種(Linux の GPU 版、存在しない CUDA 13 版)は `-DPITCHNET_ALLOW_UNPINNED_DOWNLOADS=ON` を付けない限り構成が止まる。`GAME_MODELS_URL` を差し替える時は `GAME_MODELS_SHA256` も渡す
- GitHub Actions の `uses:` は 40 桁のコミット ID で指定し、行末のコメントに版名を残す。タグ(`@v7`)やブランチ名では指定しない。新しい版に上げる時は `gh api repos/<owner>/<repo>/git/ref/tags/<版>` で ID を取る
- サブモジュール(JUCE・ARA_SDK・r8brain)はコミット ID で固定されている。進める時は `git submodule status` の変化をコミットの説明に書く
- 破ると: ダウンロードの中身が表と違えば、構成(configure)が「hash mismatch」で止まる。表の更新忘れも同じ所で止まる。アクションのタグ指定は自動テストが無く、`grep -rn 'uses:' .github/workflows | grep -v '@[0-9a-f]\{40\}'` で見つける(ローカルの `./.github/workflows/…` 参照は対象外)

## 15. AI リシンセシスを選べるかは1か所で決め、選べない選択肢を選択中のままにしない

- 守ること: AI リシンセシス(声の合成モデル)を選べるかは `getAiResynthesisSupport()` だけで決める。置き場所は既定のエンジンを決める `defaultSynthesisEngineType()` と同じ `Source/Audio/SynthesisEngineType.cpp`。答えは「使えない / CPU だけ / GPU など」の3つ。Windows の配布版は CPU 版(DirectML なし)なので「CPU だけ」になり、AI は選べる(説明文で遅いことを伝える)。配布の形を変える時(DirectML 版・Linux 版など)は、既定のエンジンが AI になる環境でこの関数が「使えない」を返さないことを確かめる
- 必ず通す入口: 答えを右パネルに渡すのは `ParameterPanel::setAiResynthesisSupport` だけ(`MainComponent` の構築時、エンジン変更の保存処理をつないだ後)。右パネルは「使えない」時に AI を選択中のままにせず、クラシックへ寄せて `onSynthesisEngineChanged` で知らせ、保存させる(`setSynthesisEngineInternal` の中で行うので、後から AI を渡されても同じ)。ボタンを押せなくするだけの書き方に戻さない。既定が AI のままボタンが押せず、クラシックを一度選ぶと戻れなくなる(2026-10-30 の Windows 報告)
- 破ると: `AiResynthesisChoiceTests` が落ちる(CPU だけの時にクラシックから AI へ戻せる / 使えない時は寄せて知らせる / 説明文が状態に合う)。Windows の配布版で AI を選べることは、CI の Windows ジョブが走らせる `ModelInferenceTests` の段階5が確かめる(Windows で「使えない」なら落ちる)。DirectML 版で「GPU など」になるかは自動テストが無い
