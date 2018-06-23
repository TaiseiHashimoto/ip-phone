# ip-phone

## 環境
基本的にLinuxまたはMacを想定しています。Windows対応はかなりの修正が必要であり、現時点では考えていません。ただし、MinGW、Cygwin上では動作するようです。

## インストール方法
### ALSA SDKのインストール(Linuxのみ)
LinuxにはもともとOSSというデバイスドライバがあるのですが、古いのでPortAudioは対応していないようです。そこで代わりにALSAをインストールします。
```
sudo apt-get install libasound-dev
```

### PortAudioのインストール
1. [公式サイト](http://www.portaudio.com/)のDownloadから最新版のソースをダウンロード、解凍
2. 解凍したフォルダに移動し、ターミナル上で  
```
./configure  
make  
sudo make install
```

### GTK+3のインストール
GUIのライブラリとしてGTK+3を使います。また、GUIのデザイン用にGladeを使います。

- Ubuntu
```
sudo apt-get install libgtk-3-dev
sudo apt-get install glade
```
- Mac
```
brew install gtk+3
brew install glade
```

### (ようやく)ip-phoneのインストール
- Ver.3まで  
```
gcc -o <YOUR PROGRAME NAME> phone_v*.c -lportaudio
```
などとしてください。(\*にはバージョンが入ります)  
(bin内のファイルはGitに無視されるようにしてあります。)  
Ver.1, Ver.2はPortAudioを使用していないlportaudioは必要ありません。環境によっては(確かMinGW、Cygwin)インクルードパスを追加しないとコンパイルできません。make installの出力などからパスは推定できると思います。

- Ver.4  
Makefileを作ってあります。

## 実行
- Ver.3まで
```
<YOUR PROGRAME NAME> <TCP port> <UDP port> | bin/play.sh  
```

- Ver.4  
```
<YOUR PROGRAME NAME> <TCP port> | bin/play.sh 
```

"TCP port"と"UDP port"は自分のポートです。  


## コマンド
Ver.3まではコマンドラインで操作します。

- 電話を掛ける
```
call <IP address> <TCP port> <UDP port> | bin/play.sh 
```
"IP address","TCP port","UDP port"は相手のアドレス、ポートです。

- 電話を受ける
"anser?"と聞かれるので、 "yes"と入力、エンター

おそらくLinuxでは、通話を始めるとALSA関連のエラーが出ます(unable to open slaveなどなど)が、特に害はないので無視します。

- 終了する
```
quit
```

## 利用しているライブラリ
- [PortAudio](http://www.portaudio.com/)
- [GTK+](https://www.gtk.org/)
- [Glade](https://glade.gnome.org/)

## その他
- .gitignoreについて

pushするときは実行ファイルを含めないでください。binフォルダの中身は.gitignoreとplay.sh以外はGitに無視されるので、
```
gcc -o bin/phone phone_v*.c -lportaudio
```
などとしてbinフォルダ内に実行ファイルを作成すると良いでしょう。

- gladeについて

Macで利用すると、右クリックができませんでした。(環境によるかもしれません)  そのためLinuxでの利用をおすすめします。  
あと、Glade3.22になってからだいぶインターフェースが変わったようで、ネットで得られる情報がそのまま使えない場合があります。必要に応じて旧バージョンをインストールすると良いかもしれません。