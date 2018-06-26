#include <math.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <portaudio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "phone.h"

RecAndSendData_t RecAndSendData;   // rec_and_send()で使用
short Silent = DEFAULT_SILENT;    // 沈黙かどうかの閾値
static BellData_t BellData; 

/**
 *  終了時に後始末をする
 *    err : portaudioのエラーハンドリング用変数
 */
int done(PaError err) {
  const PaHostErrorInfo* herr;

  Pa_Terminate();

  if(err != paNoError) {
    if(err == paUnanticipatedHostError) {
      fprintf(stderr, " unanticipated host error.\n");
      herr = Pa_GetLastHostErrorInfo();
      if (herr) {
        fprintf( stderr, " Error number: %ld\n", herr->errorCode );
        if (herr->errorText) {
          fprintf(stderr, " Error text: %s\n", herr->errorText );
        }
      } else {
        fprintf(stderr, " Pa_GetLastHostErrorInfo() failed!\n" );
      }
    } else {
      fprintf(stderr, "portaudio: %s\n", Pa_GetErrorText( err ) );
    }
    err = 1;
  }

  fprintf(stderr, "bye\n");
  exit(err);
}

int determine_threshold() {
  int len = RecAndSendData.check_count / BLOCK_LEN;
  short *block_max = (short *) malloc(sizeof(short) * len);

  for (int i = 0; i < len; i++) {
    block_max[i] = 0;
    for (int j = 0; j < BLOCK_LEN; j++) {
      block_max[i] = max(block_max[i], abs(RecAndSendData.sbuf[i*BLOCK_LEN + j]));
    }
  }

  qsort(block_max, len, sizeof(short), compare_short);

  double low, high, sum;
  int sep = len/2;

  while(1) {
    sum = 0;
    for (int i = 0; i < sep; i++) {
      sum += block_max[i];
    }
    low = sum / sep;
    sum = 0;
    for (int i = sep; i < len; i++) {
      sum += block_max[i];
    }
    high = sum / (len - sep);

    int new_sep = 0;
    while (block_max[new_sep] < (low + high)/2)
      new_sep++;

    if (abs(sep - new_sep) < CONVERGE) break;
    sep = new_sep;
    if (sep <= 0 || sep >= len) break;  // 例外的、実際は起こらないと思われる
  }

  free(block_max);

  if (high / low >= SIGNIFICANT) {
    return (short)(low + high)/2;     // 平均値を新たな閾値とする
  }
  return -1;    // 閾値が更新されない場合
}

/**
 *  portaudioの設定
 *    audioStream : portaudioの入力用ストリーム
 */
void open_rec(PaStream **audioStream) {
  PaError err;
  PaStreamParameters inputParameters;
  
  inputParameters.device = Pa_GetDefaultInputDevice();
  if (inputParameters.device == paNoDevice) {
    fprintf(stderr,"Error: No input default device.\n");
    die(NULL, paNoError);
  }
  inputParameters.channelCount = 1;          // モノラル
  inputParameters.sampleFormat = paInt16;    // 16bit 整数
  inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;  // レイテンシの設定
  inputParameters.hostApiSpecificStreamInfo = NULL;  // デバイスドライバの設定

  err = Pa_OpenStream(audioStream,
                      &inputParameters,
                      NULL,               // outputは使用しない
                      SAMPLE_RATE,        // サンプリング周波数
                      paFramesPerBufferUnspecified,  // 自動的に最適なバッファサイズが設定される
                      paClipOff,          // クリップしたデータは使わない
                      rec_and_send,       // コールバック関数
                      NULL);
  if (err != paNoError) die(NULL, err);
}


/**
 *  録音し、UDPを用いて送信する　portaudioのコールバック関数
 *    inputBuffer : portaudioの入力バッファ　録音データをここから読み出せる
 *    outputBuffer : portaudioの出力バッファ(使用しない)
 *    framesPerBuffer : 1回のコールで扱うバッファのサイズ
 *    timeInfo : portaudioの時間に関する構造体へのポインタ(使用しない)
 *    statusFlag : portaudioの状態に関する構造体へのポインタ(使用しない)
 *    userData : コールバック関数に渡すデータ(使用しない)
 */
int rec_and_send(const void *inputBuffer, void *outputBuffer,
                        unsigned long framesPerBuffer,
                        const PaStreamCallbackTimeInfo *timeInfo,
                        PaStreamCallbackFlags statusFlags,
                        void *userData) {
  int ret;
  short *in = (short *)inputBuffer; // 16bit(2Byte)で符号化しているのでshort型にキャスト
  
  int silent = 1;   // 沈黙かどうかのフラグ(沈黙なら送らない)
  for (int i = 0; i < framesPerBuffer; i++) {
    if (abs(in[i]) > Silent) {    // 音量が無音の閾値より大きい場合
      silent = 0;                 // フラグを下ろす
      RecAndSendData.silent_frames = 0;    // 無音が連続しているフレーム数を0にリセット
    }
    RecAndSendData.sbuf[RecAndSendData.check_count++] = in[i];
  }

  if (silent) {
    /* 無音が連続しているフレーム数に今回のフレーム数を足すが、SILENT_FRAMESより
       大きい値にはしない(しても意味がないし、オーバーフローが起こりうる) */
    RecAndSendData.silent_frames = min(RecAndSendData.silent_frames + framesPerBuffer, SILENT_FRAMES);
  }

  // 今回無音でないか、無音が連続しているフレーム数がSILENT_FREMESより少ない場合
  if (!silent || RecAndSendData.silent_frames < SILENT_FRAMES) {
    ret = sendto(InetData.udp_sock, in, 2 * framesPerBuffer, 0, (struct sockaddr *)&InetData.ot_udp_addr, sizeof(struct sockaddr_in));
    if (ret == -1) {
      die("sendto", paNoError);
    }
  }

  in += framesPerBuffer;  // move pointer forward

  if (RecAndSendData.check_count >= SAMPLE_RATE) {  // 1秒に一回チェック
    int s = determine_threshold();
    if (s != -1) Silent = s;        // 閾値を更新
    RecAndSendData.check_count = 0;
  }

  return paContinue;
}


gboolean recv_and_play(GIOChannel *s, GIOCondition c, gpointer d) {
  short sbuf[SOUND_BUF];
  socklen_t ot_addr_len = sizeof(struct sockaddr_in);

  int n = recvfrom(InetData.udp_sock, sbuf, SOUND_BUF, 0, (struct sockaddr *)&InetData.ot_udp_addr, &ot_addr_len);
  // fprintf(stderr, "n = %d\n", );
  if (n == -1) die("recv", paNoError);
  write(1, sbuf, n);

  return TRUE;
}

void open_play_bell(PaStream **audioStream) {
  PaError err;
  PaStreamParameters outputParameters;
  
  outputParameters.device = Pa_GetDefaultOutputDevice();
  if (outputParameters.device == paNoDevice) {
    fprintf(stderr,"Error: No output default device.\n");
    die(NULL, paNoError);
  }
  outputParameters.channelCount = 1;          // モノラル
  outputParameters.sampleFormat = paInt16;    // 16bit 整数
  outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;  // レイテンシの設定
  outputParameters.hostApiSpecificStreamInfo = NULL;  // デバイスドライバの設定

  int fd = open("sound/call.raw", O_RDONLY);
  
  if(fd == -1) die("bell file open", paNoError);

  BellData.size_data = lseek(fd, 0, SEEK_END) / 2;
  
  int n = read(fd, &BellData.bell_data, BellData.size_data);
  if(n == -1) perror("read bell file");
  BellData.position = 0;
  
  err = Pa_OpenStream(audioStream,
                      NULL,
                      &outputParameters,               
                      SAMPLE_RATE,        // サンプリング周波数
                      paFramesPerBufferUnspecified,  // 自動的に最適なバッファサイズが設定される
                      paClipOff,          // クリップしたデータは使わない
                      play_bell,       // コールバック関数
                      NULL);
  if (err != paNoError) die(NULL, err);
}

int play_bell(const void *inputBuffer, void *outputBuffer,
                        unsigned long framesPerBuffer,
                        const PaStreamCallbackTimeInfo *timeInfo,
                        PaStreamCallbackFlags statusFlags,
                        void *userData) {
  
  short *out = (short *)outputBuffer; // 16bit(2Byte)で符号化しているのでshort型にキャスト
  int i;
  for(i = 0; i < framesPerBuffer; i++){
    *out++ = BellData.bell_data[BellData.position];
    BellData.position = (BellData.position + 1) % BellData.size_data;
  }
  return paContinue;
}
  

  
  
    
  


