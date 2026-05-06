# yoMMD 起動メモ

## Windows でのビルド

PowerShell から次を実行する。

```powershell
.\build.ps1
```

`build.ps1` は MSYS2 UCRT64 環境を使って、以下をまとめて実行する。

```bash
make build-submodule
make release -j4
```

ビルド後の実行ファイルは `release/yoMMD.exe` に出力される。

## 起動方法

`yoMMD` は起動時に `config.toml` を探すため、単に `.\release\yoMMD.exe` を実行すると
カレントディレクトリ次第で `No config file found.` になることがある。

確実な起動方法は次のどちらか。

### 方法 1: `release` ディレクトリに移動して起動

```powershell
cd .\release
.\yoMMD.exe
```

### 方法 2: `--config` を付けて起動

```powershell
.\release\yoMMD.exe --config .\release\config.toml
```

普段はこちらがおすすめ。

## 設定ファイル

このリポジトリでは `release/config.toml` を使う。

例:

```toml
model = "../input/model/HakosBaelz/HakosBaelz/PMX/HakosBaelz.pmx"
default-model-position = [0.90, -0.80]
default-camera-position = [0.0, 20.0, 50.0]
default-scale = 0.38

[[motion]]
path = ["../input/motion/Running.vmd"]
```

`model` には `.pmx` または `.pmd`、`motion.path` には `.vmd` を指定する。

## この環境で確認したファイル

- 実行ファイル: `release/yoMMD.exe`
- 設定ファイル: `release/config.toml`
- ビルドスクリプト: `.\build.ps1`
 .\release\yoMMD.exe --config .\release\config.toml