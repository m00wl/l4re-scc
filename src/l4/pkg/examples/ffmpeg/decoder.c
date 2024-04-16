// Code based on FFplay, Copyright (c) 2003 Fabrice Bellard

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/pixdesc.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <semaphore.h>
#include <pthread.h>
#include <assert.h>
#include <sys/time.h>

static unsigned long long clk(void)
{
  struct timeval t;
  gettimeofday(&t, NULL);
  return t.tv_sec * 1000000 + t.tv_usec;
}

int main(int argc, char *argv[]) {

  if (argc != 2)
  {
    fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
    return 1;
  }

  AVFormatContext *pFormatContext = avformat_alloc_context();
  if (!pFormatContext)
  {
    fprintf(stderr, "Could not allocate memory for Format Context\n");
    return 1;
  }

  if (avformat_open_input(&pFormatContext, argv[1], NULL, NULL) != 0)
  {
    fprintf(stderr, "Could not open input file\n");
    return 1;
  }

  if (avformat_find_stream_info(pFormatContext,  NULL) < 0)
  {
    fprintf(stderr, "Could not get stream info\n");
    return 1;
  }

  av_dump_format(pFormatContext, 0, argv[1], 0);

  int videoStreamIndex = -1;

  for (unsigned i = 0; i < pFormatContext->nb_streams; i++)
  {
    switch (pFormatContext->streams[i]->codecpar->codec_type)
    {
      case AVMEDIA_TYPE_VIDEO: videoStreamIndex = i; break;
      case AVMEDIA_TYPE_AUDIO:
      case AVMEDIA_TYPE_UNKNOWN:
      case AVMEDIA_TYPE_DATA:
      case AVMEDIA_TYPE_SUBTITLE:
      case AVMEDIA_TYPE_ATTACHMENT:
      case AVMEDIA_TYPE_NB: break;
    }
  }

  if (videoStreamIndex == -1)
  {
    fprintf(stderr, "Did not find a video stream\n");
    return 1;
  }

  printf("videoStream = %d\n", videoStreamIndex);

  AVCodecParameters *pCodecParameters;
  pCodecParameters = pFormatContext->streams[videoStreamIndex]->codecpar;
  AVCodec const *pCodec = avcodec_find_decoder(pCodecParameters->codec_id);
  if (pCodec == NULL)
  {
    fprintf(stderr, "Unsupported codec!\n");
    return 1;
  }

  AVCodecContext *pCodecContext = avcodec_alloc_context3(pCodec);
  if (!pCodecContext)
  {
    fprintf(stderr, "Failed to allocated memory for AVCodecContext\n");
    return 1;
  }

  if (avcodec_parameters_to_context(pCodecContext, pCodecParameters) < 0)
  {
    fprintf(stderr, "Failed to copy codec params to codec context\n");
    return 1;
  }

  int active_threads = pCodecContext->thread_count;
  printf("Using %d thread%s for decoding\n",
         active_threads, active_threads > 1 ? "s" : "");

  if (avcodec_open2(pCodecContext, pCodec, NULL) < 0)
  {
    fprintf(stderr, "Failed to open codec through avcodec_open2\n");
    return 1;
  }

  AVFrame *pFrame = av_frame_alloc();
  if (!pFrame)
  {
    fprintf(stderr, "Failed to allocate memory for AVFrame\n");
    return 1;
  }

  AVPacket *pPacket = av_packet_alloc();
  if (!pPacket)
  {
    fprintf(stderr, "Failed to allocate memory for AVPacket\n");
    return 1;
  }

  sleep(50);
  printf("Start decoding\n");

  unsigned frame_count = 0;
  unsigned long long clock_start = clk();
  while (av_read_frame(pFormatContext, pPacket) >= 0)
  {
    if (pPacket->stream_index == videoStreamIndex)
    {
      int r;

      r = avcodec_send_packet(pCodecContext, pPacket);
      if (r)
      {
        fprintf(stderr, "Error while sending a packet to the decoder: %s\n",
                av_err2str(r));
        //return r;
      }

      r = avcodec_receive_frame(pCodecContext, pFrame);
      //if (r == AVERROR(EAGAIN) || r == AVERROR_EOF)
      //  break;
      //else if (r < 0)
      if (r)
      {
        fprintf(stderr, "Error while receiving a frame from the decoder: %s\n",
        av_err2str(r));
        //return r;
      }

      frame_count++;
      //printf("frame: %u\n", frame_count);
    }

    av_packet_unref(pPacket);
  }

  printf("Decoded %u frames in %f seconds.\n", frame_count,
         (clk() - clock_start) / 1000000.0);

  avformat_close_input(&pFormatContext);
  av_packet_free(&pPacket);
  av_frame_free(&pFrame);
  avcodec_free_context(&pCodecContext);

  return 0;
}
