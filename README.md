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

### (ようやく)ip-phoneのインストール
時間がないのでMakefileなんて作っていません。
```
gcc -o <YOUR PROGRAME NAME> phone_v*.c -lportaudio
```
などとしてください。(\*にはバージョンが入ります)  
(bin内のファイルはGitに無視されるようにしてあります。)  
バージョン1,2はPortAudioを使用していないlportaudioは必要ありません。環境によっては(確かMinGW、Cygwin)インクルードパスを追加しないとコンパイルできません。make installの出力などからパスは推定できると思います。

## 実行
```
<YOUR PROGRAME NAME> <ip address> <TCP port> <UDP port> | bin/play.sh  
```
"TCP port"と"UDP port"は自分のポートです。  
おそらくLinuxではALSA関連のエラーが出ます(unable to open slaveなどなど)が、特に害はないので無視します。

## 利用しているライブラリ
- PortAudio
[公式サイト](http://www.portaudio.com/)

## その他
pushするときは実行ファイルを含めないでください。binフォルダの中身は.gitignoreとplay.sh以外はGitに無視されるので、
```
gcc -o bin/phone phone_v*.c -lportaudio
```
などとしてbinフォルダ内に実行ファイルを作成すると良いでしょう。
