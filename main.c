#include <stdio.h>
#include <stdlib.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
int flush_encoder(AVFormatContext *ofmt_ctx,int stream_index){
  if (!(ofmt_ctx->streams[stream_index]->codec->codec->capabilities & AV_CODEC_CAP_DELAY)){
    return 0;
  }
  int got_fame = 0;
  AVPacket *pkt = av_packet_alloc();

  while(1)
  {
    pkt->data = NULL;
    pkt->size = 0;
    av_init_packet(pkt);
    int ret = avcodec_encode_audio2(ofmt_ctx->streams[stream_index]->codec,pkt,NULL,&got_fame);
    if (ret < 0){
      break;
    }
    if (got_fame == 0){
      break;
    }
    ret = av_write_frame(ofmt_ctx,pkt);
    if (ret < 0){
      break;
    }
  }
  av_packet_free(&pkt);
  return 0;
}
int main(int argc,char *argv[])
{
  if (argc != 3){
    printf("usage : Encode <input pcm data> <output aac>");
    return -1;
  }
  const char *input = argv[1];
  const char *output = argv[2];
  av_register_all();
  AVFormatContext *ofmt_ctx = avformat_alloc_context();
  AVOutputFormat *oformat = av_guess_format(NULL,output,NULL);
  if (oformat==NULL){
    av_log(NULL,AV_LOG_ERROR,"fail to find the output format\n");
    return -1;
  }
  if (avformat_alloc_output_context2(&ofmt_ctx,oformat,oformat->name,output) <0){
    av_log(NULL,AV_LOG_ERROR,"fail to alloc output context\n");
    return -1;
  }
  AVStream *out_stream = avformat_new_stream(ofmt_ctx,NULL);
  if (out_stream == NULL){
    av_log(NULL,AV_LOG_ERROR,"fail to create new stream\n");
    return -1;
  }
  AVCodecContext *pCodecCtx = out_stream->codec;
  pCodecCtx->codec_id = oformat->audio_codec;
  pCodecCtx->codec_type = AVMEDIA_TYPE_AUDIO;
  pCodecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP; //其他会出错
  pCodecCtx->channel_layout = AV_CH_LAYOUT_STEREO;
  pCodecCtx->channels = av_get_channel_layout_nb_channels(pCodecCtx->channel_layout);
  pCodecCtx->sample_rate = 44100;
  pCodecCtx->bit_rate = 128000;

  AVCodec *pCodec = avcodec_find_encoder(pCodecCtx->codec_id);
  if (pCodec == NULL){
    av_log(NULL,AV_LOG_ERROR,"fail to find codec\n");
    return -1;
  }
  if (avcodec_open2(pCodecCtx,pCodec,NULL) < 0){
    av_log(NULL,AV_LOG_ERROR,"fail to open codec\n");
    return -1;
  }
  av_dump_format(ofmt_ctx,0,output,1);
  if (avio_open(&ofmt_ctx->pb,output,AVIO_FLAG_WRITE) < 0){
    av_log(NULL,AV_LOG_ERROR,"fail to open output\n");
    return -1;
  }
  if (avformat_write_header(ofmt_ctx,NULL) < 0){
    av_log(NULL,AV_LOG_ERROR,"fail to write header");
    return -1;
  }
  FILE *fp = fopen(input,"rb");
  if(fp == NULL){
    printf("fail to open file\n");
    return -1;
  }
  AVFrame *pframe = av_frame_alloc();
  pframe->channels = pCodecCtx->channels;
  pframe->format = pCodecCtx->sample_fmt;
  pframe->nb_samples = pCodecCtx->frame_size;

  int size = av_samples_get_buffer_size(NULL,pCodecCtx->channels,pCodecCtx->frame_size,pCodecCtx->sample_fmt,1);
  uint8_t *out_buffer = (uint8_t*)av_malloc(size);
  avcodec_fill_audio_frame(pframe,pCodecCtx->channels,pCodecCtx->sample_fmt,(const uint8_t*)out_buffer,size,1);

  //新版本需要使用到转换参数，将读取的数据转换成输出的编码格式
  uint8_t  **data = (uint8_t**)av_calloc( pCodecCtx->channels,sizeof(*data) );
  av_samples_alloc(data,NULL,pCodecCtx->channels,pCodecCtx->frame_size,pCodecCtx->sample_fmt,1);

  SwrContext *pSwrCtx  = swr_alloc();
  swr_alloc_set_opts(pSwrCtx,pCodecCtx->channel_layout,pCodecCtx->sample_fmt,pCodecCtx->sample_rate,
      pCodecCtx->channel_layout,AV_SAMPLE_FMT_S16,44100,0,NULL);
  swr_init(pSwrCtx);
  AVPacket *pkt = av_packet_alloc();
  av_new_packet(pkt,size);
  pkt->data = NULL;
  pkt->size = 0;

  int count = 1;
  while(1){
    //读取的长度要 和原始数据的采样率，采样格式以及通道有关 如果size设置的不对，会导致音频错误
    size = pframe->nb_samples * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16) * pframe->channels;
    if (fread(out_buffer,1,size,fp) < 0){
      printf("fail to read raw data\n");
      return -1;
    }else if (feof(fp)){
      break;
    }
    
    swr_convert(pSwrCtx,data,pCodecCtx->frame_size,pframe->data,pframe->nb_samples);
    //转换后的数据大小与采样率和采样格式有关
    size = pCodecCtx->frame_size * av_get_bytes_per_sample(pCodecCtx->sample_fmt);
    memcpy(pframe->data[0],data[0],size);
    memcpy(pframe->data[1],data[1],size);
    pframe->pts = count * 100;
    //编码写入
    if (avcodec_send_frame(pCodecCtx,pframe) < 0){
      printf("fail to send frame\n");
      return -1;
    }
    //读取编码好的数据
    if (avcodec_receive_packet(pCodecCtx,pkt)  >= 0){
      pkt->stream_index = out_stream->index;
      av_log(NULL,AV_LOG_INFO,"write %d frame\n",count);
      av_write_frame(ofmt_ctx,pkt);
    }
    count++;
    av_packet_unref(pkt);
  }
  //刷新编码器的缓冲区
  flush_encoder(ofmt_ctx,out_stream->index);

  av_packet_free(&pkt);
  swr_free(&pSwrCtx);
  av_free(out_buffer);
  av_frame_free(&pframe);
  av_write_trailer(ofmt_ctx);
  avio_close(ofmt_ctx->pb);
  avformat_free_context(ofmt_ctx);
  return 0;
}