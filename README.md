# ip-phone

## コンパイル  
gcc -o bin/<program name\>phone_v1.c  
として実行ファイルをbin内に入れてください。
(bin内のファイルはGitに無視されるようにしてあります。)  
後述のportaudioを利用する際は -lportaudio を忘れずに。(ライブラリのパスは環境によって異なります。ちゃんとインストールできていれば特に指定しなくても良いはず。)

## 外部ライブラリ
phone_v3以降はportaudio(オーディオI/O用のCライブラリ、libsoxよりドキュメントがしっかりしていて良さげ)を利用しています。  
portaudioについては [公式サイト](http://www.portaudio.com/)を参照してください。  
ファイルがダウンロードできたら、 ./configure & make でインストールできます。  
