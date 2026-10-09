# Windows mGBA: PokemonStart ROM64アクセス記録

`rom64_access_trace.lua` は、このmGBAフォークのQt版スクリプト画面で使うLuaファイルです。ROMを変更しません。後半32 MiB (`0x0A000000`～`0x0BFFFFFF`) へのCPUデータ読み出し・DMA読み出し・命令フェッチをCSVへ記録します。**このリポジトリを新たにビルドしたmGBAが必要で、既存の配布バイナリでは追加APIを使えません。**

1. Luaファイル先頭の `ROM64_SCENE` を場面名、`ROM64_OUTPUT` を出力先に変更します。Windowsでは例えば `C:/Users/名前/Desktop/rom64_access_trace.csv` のように `/` 区切りの絶対パスを指定できます。既存CSVがあれば追記します。
2. Windows版mGBAでROMを開き、ツールメニューのスクリプト画面からこの `.lua` を読み込みます。画面に `ROM64 trace active` と出ることを確認します。`watchpoint unavailable` と出た場合、そのビルドでは必要なLua・デバッガ機能が使えません。
3. 調べたい画面や戦闘を操作します。1場面ごとに `ROM64_SCENE` を変えてスクリプトを再読み込みすると区別できます。
4. スクリプト画面から停止するか、Luaコンソールで `rom64_trace_stop()` を実行します。100件ごとに保存し、10万件で自動停止します。異常終了時には直近100件未満が失われる可能性があります。
5. CSVを `/home/one/cfru-jp/pokemonstart/rom64-access-audit/aggregate.py` で集計できます。Windowsへ集計ツールをコピーした場合は、そのフォルダーで `python .\aggregate.py C:\path\to\rom64_access_trace.csv` を実行してください。

記録形式は `scene,kind,pc,address,width,notes` です。`kind` は `read` / `dma` / `fetch` です。読み出し・DMAの `pc` はmGBAが公開する現在のPC値で、厳密な命令位置と数バイトずれる場合があります。`fetch` の `pc` はフェッチ対象アドレスそのものです。観測したイベントを記録するため、同じアクセスが繰り返されれば複数行になります。

追加したLua APIは `C.WATCHPOINT_TYPE.FETCH`（`emu:setRangeWatchpoint` / `emu:setWatchpoint` で指定可能）と、監視コールバックの `info.accessSource` です。後者を `C.MEMORY_ACCESS_SOURCE.DMA` と比較するとDMA読み出しを区別できます。`FETCH` 通知の `info.address` はフェッチ対象、`info.width` は2または4です。フェッチされた命令が実行されたことまでは意味しません。

この監視はARMデバッガのデータロード経路と命令パイプラインのフェッチ通知を使用します。DMAはデータロードの `accessSource` で区別します。**別の直接メモリ経路やエミュレータ外の実機アクセスまで網羅するものではありません。** また広い範囲の監視と逐次CSV書き込みにより動作が遅くなる可能性があります。アクセスが記録されなくても「参照がない」証明にはなりません。ROMやセーブデータを共有する必要はありません。
