// Code based on FFplay, Copyright (c) 2003 Fabrice Bellard

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/pixdesc.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/log.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <semaphore.h>
#include <pthread.h>
#include <assert.h>
#include <sys/time.h>
#include <math.h>

static unsigned long long clk(void)
{
  struct timeval t;
  gettimeofday(&t, NULL);
  return t.tv_sec * 1000000 + t.tv_usec;
}

void gblur(AVFrame *frame, uint8_t *out, int kernel_size, double sigma);
void gblur(AVFrame *frame, uint8_t *out, int kernel_size, double sigma)
{
  uint8_t *in = frame->data[0];

  double kernel[kernel_size][kernel_size];
  double sum = 0.0;

  for (int i = 0; i < kernel_size; i++)
  {
    for (int j = 0; j < kernel_size; j++)
    {
      int x = i - kernel_size / 2;
      int y = j - kernel_size / 2;
      kernel[i][j] = exp(-(x*x + y*y) / (2 * sigma*sigma))
                     / (2 * M_PI * sigma*sigma);
      sum += kernel[i][j];
    }
  }

  for (int i = 0; i < kernel_size; i++)
  {
    for (int j = 0; j < kernel_size; j++)
    {
      kernel[i][j] /= sum;
    }
  }

  for (int y = 0; y < frame->height; y++)
  {
    for (int x = 0; x < frame->width; x++)
    {
      double sum = 0.0;
      for (int i = 0; i < kernel_size; i++)
      {
        for (int j = 0; j < kernel_size; j++)
        {
          int x_idx = x - kernel_size / 2 + i;
          int y_idx = y - kernel_size / 2 + j;
          if (x_idx >= 0 && x_idx < frame->width && y_idx >= 0
              && y_idx < frame->height)
          {
            sum += in[y_idx * frame->linesize[0] + x_idx] * kernel[i][j];
          }
        }
      }
      out[y * frame->linesize[0] + x] = (uint8_t)sum;
    }
  }
}

void sobel(AVFrame *frame, uint8_t *out);
void sobel(AVFrame *frame, uint8_t *out)
{
  uint8_t *in = frame->data[0];

  int sobel_horizontal[3][3] = {{-1, 0, 1},
                                {-2, 0, 2},
                                {-1, 0, 1}};
  int sobel_vertical[3][3] = {{-1, -2, -1},
                              { 0,  0,  0},
                              { 1,  2,  1}};

  for (int y = 1; y < frame->height - 1; y++)
  {
    for (int x = 1; x < frame->width - 1; x++)
    {
      int gx = 0, gy = 0;
      for (int i = -1; i <= 1; i++)
      {
        for (int j = -1; j <= 1; j++)
        {
          gx += in[(y + i) * frame->linesize[0] + (x + j)]
                * sobel_horizontal[i + 1][j + 1];
        }
      }
      for (int i = -1; i <= 1; i++)
      {
        for (int j = -1; j <= 1; j++)
        {
          gy += in[(y + i) * frame->linesize[0] + (x + j)]
                * sobel_vertical[i + 1][j + 1];
        }
      }
      double magnitude = sqrt(gx * gx + gy * gy);
      magnitude = fmin(fmax(round(magnitude), 0), 255);
      out[y * frame->linesize[0] + x] = (uint8_t)magnitude;
    }
  }
}

void mm(AVFrame *frame, uint8_t *in1, uint8_t *in2);
void mm(AVFrame *frame, uint8_t *in1, uint8_t *in2)
{
  for (int y = 0; y < frame->height; y++)
  {
    uint8_t *row = frame->data[0] + y * frame->linesize[0];
    for (int x = 0; x < frame->width; x++)
    {
      row[x] = 0;
      for (int k = 0; k < frame->width; k++)
      {
        row[x] += in1[y * frame->linesize[0] + k]
                  * in2[x * frame->height + k];
      }
    }
  }
}

void mem(AVFrame *frame, uint64_t *mem);
void mem(AVFrame *frame, uint64_t *mem)
{
  int const N_READS = 16777216;

  uint64_t res = 0;
  int loc = 0;

  for (int i = 0; i < (16 * N_READS); i++)
  {
    uint64_t *p = mem + loc;
    asm volatile ( "" : "=m"(*p));
    res += *p;
    asm volatile ( "" : "=m"(*p));
    loc += 8;
    loc %= N_READS;
  }

  uint64_t *ptr = (uint64_t *)&((frame->data[0])[8]);
  *ptr = res;
}

int main(int argc, char *argv[])
{
  sleep(30);

  printf("start ffmpeg-bench-mem\n");

  if (argc != 2)
  {
    fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
    return 1;
  }

  av_log_set_level(AV_LOG_QUIET);

  AVFormatContext *pFormatContext = avformat_alloc_context();
  if (!pFormatContext)
  {
    fprintf(stderr, "Could not allocate memory for Format Context\n");
    return 1;
  }

  AVDictionary *options = NULL;
  av_dict_set(&options, "video_size", "3840x2160", 0);
  av_dict_set(&options, "pixel_format", "yuv420p", 0);

  if (avformat_open_input(&pFormatContext, argv[1], NULL, &options) != 0)
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

  //printf("videoStream = %d\n", videoStreamIndex);

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

  //int active_threads = pCodecContext->thread_count;
  //printf("Using %d thread%s for decoding\n",
  //    active_threads, active_threads > 1 ? "s" : "");

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

  //AVFrame *fFrame = av_frame_alloc();
  //AVFilterGraph *filter_graph = avfilter_graph_alloc();
  //if (!filter_graph)
  //{
  //  fprintf(stderr, "Failed to allocate filter graph\n");
  //  return 1;
  //}

  //int err;
  //char filters[512];
  //snprintf(filters, sizeof(filters), "buffer=video_size=%dx%d:pix_fmt=%d:time_base=1/50000,gblur=sigma=1,buffersink",
  //         pCodecContext->width, pCodecContext->height, pCodecContext->pix_fmt);
  //
  //AVFilterInOut *in = NULL;
  //AVFilterInOut *out = NULL;

  //err = avfilter_graph_parse(filter_graph, filters, in, out, NULL);
  //if (err < 0)
  //{
  //  fprintf(stderr, "Error parsing filter string\n");
  //  return 1;
  //}

  //err = avfilter_graph_config(filter_graph, NULL);
  //if (err < 0)
  //{
  //  fprintf(stderr, "Error configuring the filter graph\n");
  //  return 1;
  //}

  //AVFilterContext *bufferContext = filter_graph->filters[0];
  //AVFilterContext *buffersinkContext = filter_graph->filters[filter_graph->nb_filters-1];

  //printf("Start decoding\n");

  //struct SwsContext *pConvertContext = NULL;
  //enum AVPixelFormat pixformat = AV_PIX_FMT_RGBA;
  //unsigned const video_w = 3840;
  //unsigned const video_h = 2160;
  //void *fb_addr = NULL;
  //fb_addr = malloc(video_w * video_h * 2);
  //if (!fb_addr)
  //{
  //  fprintf(stderr, "Failed to allocate memory for framebuffer\n");
  //  return 1;
  //}

  unsigned frame_count = 0;
  unsigned long long clock_start = clk();

  uint64_t cycle1;
  asm volatile ("mrs %0, PMCCNTR_EL0" : "=r" (cycle1));

  while (av_read_frame(pFormatContext, pPacket) >= 0)
  {
    if (pPacket->stream_index == videoStreamIndex)
    {
      int r;

      r = avcodec_send_packet(pCodecContext, pPacket);
      if (r)
        fprintf(stderr, "Error while sending a packet to the decoder: %s\n",
            av_err2str(r));

      r = avcodec_receive_frame(pCodecContext, pFrame);
      if (r)
        fprintf(stderr, "Error while receiving a frame from the decoder: %s\n",
            av_err2str(r));

      //r = av_buffersrc_write_frame(bufferContext, pFrame);
      //if (r < 0)
      //{
      //  fprintf(stderr, "Error writing frame to buffer\n");
      //  return 1;
      //}

      //for (;;)
      //{
      //  r = av_buffersink_get_frame(buffersinkContext, fFrame);
      //  if (r == AVERROR(EAGAIN))
      //  {
      //    fprintf(stderr, "buffersink try again\n");
      //    //break;
      //    sleep(10);
      //    continue;
      //  }
      //  if (r == AVERROR_EOF /*|| r == AVERROR(EAGAIN)*/)
      //    break;
      //  if (r < 0)
      //  {
      //    fprintf(stderr, "Error getting frame from buffersink: %d\n", r);
      //    return 1;
      //  }
      //}

      size_t const FRAME_SIZE = pFrame->linesize[0] * pFrame->height
                                * sizeof(uint8_t);
      uint8_t *b = malloc(FRAME_SIZE);
      memset(b, 0x0, FRAME_SIZE);
      uint8_t *e = malloc(FRAME_SIZE);
      memset(e, 0x0, FRAME_SIZE);
      int const N_READS = 16777216;
      size_t const MEM_SIZE = N_READS * sizeof(uint64_t);
      uint64_t *m = malloc(MEM_SIZE);
      memset(m, 0x0, MEM_SIZE);

      gblur(pFrame, b, 5, 1.0);
      sobel(pFrame, e);
      mm(pFrame, b, e);
      mem(pFrame, m);

      free(m);
      free(e);
      free(b);

      frame_count++;
      //printf("frame: %u\n", frame_count);


      //pConvertContext = sws_getCachedContext(pConvertContext,
      //                                       pCodecContext->width,
      //                                       pCodecContext->height,
      //                                       pCodecContext->pix_fmt,
      //                                       video_w, video_h, pixformat, SWS_FAST_BILINEAR,
      //                                       NULL, NULL, NULL);

      //if (!pConvertContext)
      //{
      //  fprintf(stderr, "Failed to init conversion context\n");
      //  return 1;
      //}

      //uint8_t * const dstSlice[1] = { fb_addr };
      //const int dstStride[1] = { video_w * 2 };
      //sws_scale(pConvertContext, (uint8_t const * const *)pFrame->data, pFrame->linesize, 0, pCodecContext->height, dstSlice, dstStride);

    }

    av_packet_unref(pPacket);
  }

  uint64_t cycle2;
  asm volatile ("mrs %0, PMCCNTR_EL0" : "=r" (cycle2));

  printf("cycle delta: 0x%lX\n", cycle2-cycle1);

  printf("Decoded %u frames in %f seconds.\n", frame_count,
      (clk() - clock_start) / 1000000.0);

  avformat_close_input(&pFormatContext);
  av_packet_free(&pPacket);
  av_frame_free(&pFrame);
  avcodec_free_context(&pCodecContext);

  return 0;
}
