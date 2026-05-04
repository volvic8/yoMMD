# yoMMD 起動方法

## Windows での起動手順

このリポジトリは Windows では MSYS2 UCRT64 環境でビルドして起動する。

### 1. MSYS2 を用意する

MSYS2 をインストールしたあと、UCRT64 環境で以下を入れる。

```bash
pacman -Syu
pacman -S make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
```

### 2. サブモジュールを取得する

```bash
git submodule update --init --recursive
```

### 3. 依存ライブラリと本体をビルドする

リポジトリ直下で以下を実行する。

```bash
make build-submodule
make release -j4
```

ビルドが成功すると、実行ファイルは `release/yoMMD.exe` にできる。

## 設定ファイル

`yoMMD` は実行ファイルと同じ階層の `config.toml` を読む。
このリポジトリでは `release/config.toml` を置くとそのまま起動できる。

例:

```toml
model = "../input/model/HakosBaelz/HakosBaelz/PMX/HakosBaelz.pmx"
default-model-position = [0.0, 0.0]
default-camera-position = [0.0, 20.0, 50.0]
default-scale = 1.5

[[motion]]
path = ["../input/motion/Running.vmd"]
```

`model` には `.pmx` か `.pmd`、`motion.path` には `.vmd` を指定する。

## 起動

`release` ディレクトリで以下を実行する。

```bash
./yoMMD.exe
```

PowerShell から起動する場合は以下でもよい。

```powershell
.\release\yoMMD.exe
```

## この環境で確認した構成

- 実行ファイル: `release/yoMMD.exe`
- 設定ファイル: `release/config.toml`
- モデル: `input/model/HakosBaelz/HakosBaelz/PMX/HakosBaelz.pmx`
- モーション: `input/motion/Running.vmd`
