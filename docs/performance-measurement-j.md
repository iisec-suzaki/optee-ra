# 性能計測ログ (`RA_PERF`)

リモートアテステーション全体の処理時間と、主要イベント(ハッシュ計算、
CBOR/COSE エンコード、CAAM 署名、ブラックキー処理など)ごとの所要時間を
ログとして出力し、性能評価データを収集できます。

英語版は [performance-measurement.md](performance-measurement.md) を参照
してください。

## ログフォーマット

計測 1 件につき 1 行を出力します:

```
RA_PERF|<layer>|<event>|<duration_us>|key=<keymode>
```

| フィールド    | 意味                                                                       |
| ------------- | -------------------------------------------------------------------------- |
| `layer`       | `host`(Linux クライアント)、`ta`(ユーザ TA)、`pta`(OP-TEE コア内の PTA) |
| `event`       | 後述のログポイント一覧を参照                                                 |
| `duration_us` | 所要時間(マイクロ秒、整数)                                                 |
| `keymode`     | 実行時の鍵パス: `embedded`(組込みテストキー)/ `plain`(外部平文鍵)/ `black`(CAAM ブラックキー)/ `-`(非該当) |

クロックソースと出力先:

| Layer  | クロック                                         | 分解能  | 出力先                                  |
| ------ | ------------------------------------------------ | ------- | ---------------------------------------- |
| `host` | `clock_gettime(CLOCK_MONOTONIC)`                 | 約 1 µs | `optee_remote_attestation` の標準出力    |
| `ta`   | `TEE_GetSystemTime`                              | 約 1 ms | セキュアコンソール(`I/TA:` プレフィクス)|
| `pta`  | ARM ジェネリックタイマ(`CNTPCT`、周波数 `CNTFRQ`)| µs 未満 | セキュアコンソール(`I/TC:` プレフィクス)|

QEMU ではセキュアコンソールは `serial1.log`(`make check` 時)または
soc_term ウィンドウ、i.MX 8M Plus EVK ではデバッグ UART です。`dmesg` は
使用しません。PTA は実行ごとに擬似イベント `cntfrq` でカウンタ周波数を
1 回出力するので、tick→µs 換算の検証に使えます(i.MX8MP: 8 MHz、QEMU
virt: 約 62.5 MHz)。

## ログポイント一覧

### `host`(標準出力)

| イベント                 | 計測対象                                                            |
| ------------------------ | -------------------------------------------------------------------- |
| `teec_init`              | `TEEC_InitializeContext`(`/dev/tee0` オープン)                      |
| `teec_open_session`      | `TEEC_OpenSession` — 毎回 TA のロード/認証を含む                     |
| `veraison_new_session`   | relying party への HTTP ラウンドトリップ(`newSession`、nonce 受領)  |
| `evidence_get`           | `TEEC_InvokeCommand` — REE から見た secure 側エビデンス生成の全体     |
| `print_evidence`         | エビデンスの 16 進ダンプ出力(`total` から差し引けるように計測)       |
| `veraison_post_evidence` | HTTP ラウンドトリップ: エビデンス POST → アテステーション結果(EAR JWT)|
| `total`                  | アテステーション 1 回分全体(セッション開始 → 結果受領)              |
| `blackkey_generate`      | `--generate-blackkey` 全体(PTA 呼び出し 2 回: サイズ確認 + 取得)    |
| `blackkey_convert`       | `--convert-key` の呼び出し                                            |

### `ta`(セキュアコンソール、分解能約 1 ms)

| イベント     | 計測対象                                       |
| ------------ | ----------------------------------------------- |
| `pta_invoke` | `TEE_InvokeTACommand` による PTA 呼び出し往復   |
| `cmd_total`  | TA コマンド全体(パラメータ準備・コピーバック含む)|

### `pta`(セキュアコンソール、µs 分解能)

| イベント           | 計測対象                                                                     |
| ------------------ | ----------------------------------------------------------------------------- |
| `cntfrq`           | (擬似イベント)ジェネリックタイマ周波数 [Hz]。時間ではない                    |
| `ocotp_srk`        | signer-id 用 OCOTP SRK ハッシュのヒューズ読み出し(i.MX のみ)                |
| `ocotp_lifecycle`  | OCOTP ライフサイクル読み出し(i.MX のみ。内部で SRK ワードを再読する)        |
| `instance_id`      | PSA instance-id の導出(公開鍵の SHA-256)                                    |
| `hash_ta`          | ARoT 計測値: 呼び出し元 TA の読み取り専用メモリの SHA-256                     |
| `hash_tee`         | PRoT 計測値: OP-TEE コア `.text` + `.rodata` の SHA-256                       |
| `cbor_encode`      | PSA クレームの QCBOR エンコード                                               |
| `sign_key_setup`   | 署名鍵の確保とインポート(ブラックキー blob または組込み鍵 + 公開鍵)         |
| `sign_tbs_hash`    | COSE 署名対象(TBS)の SHA-256                                                |
| `sign_ecdsa`       | `crypto_acipher_ecc_sign` — **ブラックキー比較用ブラケット**(後述)          |
| `sign_verify`      | 署名のセルフチェック検証(組込み鍵パスのみ)                                  |
| `cose_sign1`       | COSE_Sign1 生成全体(上記 `sign_*` 4 イベントを含む)                         |
| `cmd_total`        | `GET_CBOR_EVIDENCE` PTA コマンド全体                                          |
| `perf_flush`       | バッファした RA_PERF 行の出力自体のコスト(注意事項参照)                     |
| `keypair_generate` | `crypto_acipher_gen_ecc_key`(i.MX では CAAM 鍵生成)。`--generate-blackkey` 1 回につき 2 回記録(サイズ確認 + 取得) |
| `blackkey_encap`   | `--convert-key` 内の `caam_key_black_encapsulation`(CCM)                    |
| `convert_total`    | `CONVERT_TO_BLACKKEY` PTA コマンド全体                                        |

## ログの有効化

ファームウェア側の計測コードは `CFG_REMOTE_ATTESTATION_PERF=y` のときだけ
コンパイルされます。デフォルト(リリースビルド)には影響しません。

**QEMU**(attester コンテナ内):

```bash
make check CFG_REMOTE_ATTESTATION_PTA=y CFG_REMOTE_ATTESTATION_PERF=y
```

**i.MX 8M Plus(Yocto)** — `local.conf` に追加:

```
CFG_REMOTE_ATTESTATION_PERF:pn-optee-os = "y"
CFG_REMOTE_ATTESTATION_PERF:pn-veraison-attestation = "y"
```

その後 `optee-os` を cleansstate から再ビルドし、`veraison-attestation`、
イメージのビルド、再署名を行ってください。注意: NXP の
`optee-os-common-fslc-imx.inc` は `CFG_TEE_CORE_LOG_LEVEL=0`(コアのトレース
を全てコンパイル時に除去)を強制するため、本リポジトリの bbappend は perf
フラグと同時にコアログレベルを 2 へ自動的に引き上げます — これがないと
`pta` の行は UART に一切出力されません。

**ホストクライアント** — ファームウェアフラグとは独立したランタイム指定:

```bash
optee_remote_attestation --perf          # または RA_PERF=1 optee_remote_attestation
```

データ収集向けの追加オプション:

| オプション    | 用途                                                                     |
| ------------- | ------------------------------------------------------------------------ |
| `--loop N`    | アテステーション全体を N 回繰り返す(反復ごとに `total` 行を出力)        |
| `--no-server` | 検証器との通信を省略しローカル乱数 nonce でアテステーションを実行 — relying party に到達できない環境や、ネットワーク変動と切り離して TEE 側だけを測りたい場合に有効 |

## 推奨計測プロトコル

1. 環境を固定して記録する: ボード(i.MX8MPEVK)、ビルド設定、`cntfrq` 行、
   ソフトウェアのバージョン、N。sub-ms のイベント(署名・ハッシュ)は DVFS で
   ぶれるので、**CPU ガバナを固定し実クロックを記録**する:

   ```bash
   for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
     echo performance > "$g" 2>/dev/null
   done
   cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
   cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq
   ```

   固定できない場合も、`scaling_governor` と `scaling_cur_freq` を読み取って
   記録すれば、後で DVFS による変動幅を論文で明示できる。
2. 設定ごとに 30 回以上繰り返し、初回(コールドキャッシュ・初回 TA ロード)
   は除外する:

```bash
# 検証器あり(エンドツーエンド):
optee_remote_attestation --perf --loop 31 | tee host-embedded.log

# TEE 側のみ(ネットワークなし):
optee_remote_attestation --perf --no-server --loop 31 | tee host-offline.log

# CAAM ブラックキー(i.MX のみ): 1 回生成してから計測
optee_remote_attestation --generate-blackkey --perf      # blackkey_generate を記録
optee_remote_attestation --perf --loop 31 --key-hex <FullKey> | tee host-black.log
```

3. `pta`/`ta` の行はセキュアコンソール(UART / `serial1.log`)から回収する。
4. 集計(イベント × 鍵パスごとの平均/最小/最大):

```bash
grep -h -o 'RA_PERF|.*' serial1.log host-*.log | awk -F'|' '
  { split($5, k, "="); id = $2 "|" $3 "|" k[2];
    n[id]++; s[id] += $4;
    if (min[id] == "" || $4 < min[id]) min[id] = $4;
    if ($4 > max[id]) max[id] = $4 }
  END { for (i in n) printf "%-44s n=%-3d mean=%10.1f min=%8d max=%8d\n",
        i, n[i], s[i]/n[i], min[i], max[i] }' | sort
```

この one-liner は**全イテレーションを集計**するため、`n` には手順 2 で除外
すべき初回(コールド)実行が含まれる。出力は「未トリム(n=N)」として扱うか、
集計前に各 `layer|event|keymode` の先頭 1 件を落とすこと(生ログはイテレーション
順を保持しているので `tail -n +2` 相当でトリム可)。なお `teec_init` /
`teec_open_session` はループ前に 1 回だけ計測されるので、各イテレーションの
集計には元々含まれない。

## CAAM 署名のブラックキー使用有無の比較

ブラックキーの使用有無は、同一の i.MX ビルド上での**ランタイム**の選択です:

* **ブラックキーあり**: `--key-hex <FullKey>` を指定(FullKey =
  `PubX(32) || PubY(32) || シリアライズ済み CAAM ブラックキー blob`。
  `--generate-blackkey` または `--convert-key` で取得)。ログは `key=black`。
* **ブラックキーなし**: 鍵引数を指定しない — 組込みテストキーを使用。
  ログは `key=embedded`。

比較には `pta|sign_ecdsa` イベント**のみ**を使ってください:

* `CFG_NXP_CAAM_ECC_DRV=y` の i.MX では**どちらのパスも** CAAM ハードウェア
  で ECDSA 署名を実行します。ブラックキーパスはそれに加えて鍵 blob の
  デカプセル化とエンジン内 CCM 鍵インポートのコストを払うため、この差分が
  まさに比較で分離したい量になります。(QEMU 実行はソフトウェア署名のみの
  第 3 のデータ点になります。)
* `sign_verify` は組込み鍵パス**のみ**で実行されます(署名のセルフチェック)。
  比較にバイアスを与えないよう別イベントとして計測しています。ブラックキー
  差分の評価に `cose_sign1` や `cmd_total` を使わないでください。

> **セキュリティに関する注意。** ブラックキーを使わない場合、署名鍵は
> OP-TEE 内で平文として扱われ(組込みテストキーの場合はバイナリにも含まれ)、
> CAAM による鍵保護は働きません。セキュアブートを起点とした鍵保護の保証が
> 成り立たないため、ブラックキーなしの数値は性能比較のためだけのものであり、
> 本番構成で使用してはいけません。

## 数値解釈上の注意

* PTA のタイムスタンプはすべて RA_PERF 行の出力**前**に取得され、PTA
  コマンドの最後に一括出力されます。コンソール書き込みは同期的なので、
  この出力コストは上位レイヤのブラケット(`ta|pta_invoke`、
  `host|evidence_get`、`host|total`)には**含まれます**。出力コスト自体は
  `pta|perf_flush` として報告されるため、レイヤ間の対応を取るときは差し引いて
  ください(最後の `perf_flush` 行 1 行分だけは計上されません)。
* 純粋なエビデンス生成時間には `pta|cmd_total` を使ってください。
  `host|evidence_get − ta|cmd_total` ≈ REE↔TEE 遷移 + libteec のオーバヘッド、
  `ta|pta_invoke − pta|cmd_total − pta|perf_flush` ≈ secure 側ディスパッチの
  オーバヘッドです。
* `host|total` はクライアントの冗長な表示を含みます。コンソールダンプ分は
  `print_evidence` として計測しているので差し引けます。
* `ta` レイヤの分解能は約 1 ms です(`TEE_GetSystemTime`。ユーザ TA は
  サイクルカウンタへ直接アクセスできません)。
* `hash_ta` は SHA-256 だけでなく、TA の読み取り専用リージョンの
  `qsort`(`memcmp` 比較。ASLR に対して順序を安定化)を含みます。
* `ocotp_lifecycle` は `ocotp_srk` が読んだ SRK ヒューズワードを内部で
  再読します。この重複は意図的にそのまま計測しています。
