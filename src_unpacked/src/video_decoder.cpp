#include "video_decoder.h"
#include "proc_capture.h"
#include <fstream>
#include <algorithm>
#include <cstdio>
#include <array>
#include <cmath>
#include <sstream>
#ifdef MSF_HAS_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}
#endif
namespace msf {
bool VideoDecoder::open(const std::string&p){
#ifdef MSF_HAS_FFMPEG
    AVFormatContext* fmt=nullptr;
    if(avformat_open_input(&fmt,p.c_str(),nullptr,nullptr)<0)return false;
    if(avformat_find_stream_info(fmt,nullptr)<0){avformat_close_input(&fmt);return false;}
    int si=av_find_best_stream(fmt,AVMEDIA_TYPE_VIDEO,-1,-1,nullptr,0);
    if(si<0){avformat_close_input(&fmt);return false;}
    AVStream* st=fmt->streams[si];
    const AVCodec* c=avcodec_find_decoder(st->codecpar->codec_id);
    if(!c){avformat_close_input(&fmt);return false;}
    AVCodecContext* cc=avcodec_alloc_context3(c);
    if(!cc||avcodec_parameters_to_context(cc,st->codecpar)<0){if(cc)avcodec_free_context(&cc);avformat_close_input(&fmt);return false;}
    if(avcodec_open2(cc,c,nullptr)<0){avcodec_free_context(&cc);avformat_close_input(&fmt);return false;}
    fmt_=fmt;codec_=cc;stream_=si;path_=p;
    info_.width=cc->width;info_.height=cc->height;
    AVRational fr=av_guess_frame_rate(fmt,st,nullptr);
    info_.fps=fr.den?av_q2d(fr):0;
    info_.duration=st->duration>0?st->duration*av_q2d(st->time_base):(fmt->duration>0?fmt->duration/(double)AV_TIME_BASE:0);
    return true;
#else
    std::ifstream f(p,std::ios::binary);if(!f)return false;
    path_=p; info_={};
    std::string q="ffprobe -v error -select_streams v:0 -show_entries stream=width,height,duration,r_frame_rate -of default=noprint_wrappers=1 \""+p+"\"";
    std::string out; if(!captureSilent(q,out))return false;
    std::istringstream ss(out);std::string line;while(std::getline(ss,line)){auto pos=line.find('=');if(pos==std::string::npos)continue;auto k=line.substr(0,pos),v=line.substr(pos+1);try{if(k=="width")info_.width=std::stoi(v);else if(k=="height")info_.height=std::stoi(v);else if(k=="duration")info_.duration=std::stod(v);}catch(...){}}
    return info_.width>0&&info_.height>0&&info_.duration>0;
#endif
}
bool VideoDecoder::info(VideoInfo&o)const{o=info_;return !path_.empty();}
bool VideoDecoder::frameAt(double seconds,int w,int h,VideoFrame&o){
    std::vector<double> ts{seconds};
    std::vector<VideoFrame> frames;
    if(!framesAt(ts,w,h,frames) || frames.empty()) return false;
    o=std::move(frames.front());
    return true;
}
bool VideoDecoder::frameAtColor(double seconds,int w,int h,ColorFrame&o){
    if(path_.empty() || w<=0 || h<=0 || !std::isfinite(seconds) || seconds<0) return false;
#ifdef MSF_HAS_FFMPEG
    if(!fmt_ || !codec_ || stream_<0) return false;
    AVFormatContext* fmt=reinterpret_cast<AVFormatContext*>(fmt_);
    AVCodecContext* cc=reinterpret_cast<AVCodecContext*>(codec_);
    AVStream* st=fmt->streams[stream_];
    int64_t ts=av_rescale_q((int64_t)(seconds*1000000.0),AV_TIME_BASE_Q,st->time_base);
    if(av_seek_frame(fmt,stream_,ts,AVSEEK_FLAG_BACKWARD)<0) return false;
    avcodec_flush_buffers(cc);
    AVPacket* pkt=av_packet_alloc(); AVFrame* fr=av_frame_alloc();
    if(!pkt||!fr){av_packet_free(&pkt);av_frame_free(&fr);return false;}
    bool got=false;
    while(!got && av_read_frame(fmt,pkt)>=0){
        if(pkt->stream_index==stream_ && avcodec_send_packet(cc,pkt)>=0){
            while(avcodec_receive_frame(cc,fr)>=0){
                double t=fr->best_effort_timestamp==AV_NOPTS_VALUE?seconds:fr->best_effort_timestamp*av_q2d(st->time_base);
                if(t+0.05<seconds) continue;
                AVFrame* dst=av_frame_alloc(); if(!dst) break;
                dst->format=AV_PIX_FMT_RGB24; dst->width=w; dst->height=h;
                SwsContext* sws=sws_getContext(fr->width,fr->height,(AVPixelFormat)fr->format,w,h,AV_PIX_FMT_RGB24,SWS_BILINEAR,nullptr,nullptr,nullptr);
                if(sws && av_frame_get_buffer(dst,1)>=0){
                    sws_scale(sws,fr->data,fr->linesize,0,fr->height,dst->data,dst->linesize);
                    o.timestamp=t; o.width=w; o.height=h; o.rgb.resize((size_t)w*h*3);
                    for(int y=0;y<h;++y) std::copy(dst->data[0]+y*dst->linesize[0],dst->data[0]+y*dst->linesize[0]+w*3,o.rgb.begin()+size_t(y)*w*3);
                    got=true;
                }
                if(sws)sws_freeContext(sws); av_frame_free(&dst);
                break;
            }
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt); av_frame_free(&fr);
    return got;
#else
    (void)o; return false;
#endif
}

bool VideoDecoder::framesAt(const std::vector<double>& seconds,int w,int h,std::vector<VideoFrame>& out){
    out.clear();
    if(path_.empty() || w<=0 || h<=0 || seconds.empty()) return false;
#ifdef MSF_HAS_FFMPEG
    if(!fmt_ || !codec_ || stream_<0) return false;
    std::vector<double> targets=seconds;
    targets.erase(std::remove_if(targets.begin(),targets.end(),[](double v){return !std::isfinite(v)||v<0;}),targets.end());
    if(targets.empty()) return false;
    std::sort(targets.begin(),targets.end());
    targets.erase(std::unique(targets.begin(),targets.end(),[](double a,double b){return std::abs(a-b)<1e-6;}),targets.end());
    AVFormatContext* fmt=reinterpret_cast<AVFormatContext*>(fmt_);
    AVCodecContext* cc=reinterpret_cast<AVCodecContext*>(codec_);
    AVStream* st=fmt->streams[stream_];
    int64_t ts=av_rescale_q((int64_t)(targets.front()*1000000.0),AV_TIME_BASE_Q,st->time_base);
    if(av_seek_frame(fmt,stream_,ts,AVSEEK_FLAG_BACKWARD)<0) return false;
    avcodec_flush_buffers(cc);
    AVPacket* pkt=av_packet_alloc(); AVFrame* fr=av_frame_alloc();
    if(!pkt||!fr){av_packet_free(&pkt);av_frame_free(&fr);return false;}
    std::size_t next=0;
    auto emitFrame=[&](AVFrame* src,double t)->bool{
        AVFrame* dst=av_frame_alloc(); if(!dst) return false;
        dst->format=AV_PIX_FMT_GRAY8; dst->width=w; dst->height=h;
        SwsContext* sws=sws_getContext(src->width,src->height,(AVPixelFormat)src->format,w,h,AV_PIX_FMT_GRAY8,SWS_BILINEAR,nullptr,nullptr,nullptr);
        if(!sws || av_frame_get_buffer(dst,1)<0){if(sws)sws_freeContext(sws);av_frame_free(&dst);return false;}
        sws_scale(sws,src->data,src->linesize,0,src->height,dst->data,dst->linesize);
        VideoFrame vf; vf.timestamp=t; vf.width=w; vf.height=h; vf.gray.resize((size_t)w*h);
        for(int y=0;y<h;++y) std::copy(dst->data[0]+y*dst->linesize[0],dst->data[0]+y*dst->linesize[0]+w,vf.gray.begin()+size_t(y)*w);
        sws_freeContext(sws); av_frame_free(&dst); out.push_back(std::move(vf)); return true;
    };
    while(next<targets.size() && av_read_frame(fmt,pkt)>=0){
        if(pkt->stream_index==stream_ && avcodec_send_packet(cc,pkt)>=0){
            while(avcodec_receive_frame(cc,fr)>=0){
                double t=fr->best_effort_timestamp==AV_NOPTS_VALUE?targets[next]:fr->best_effort_timestamp*av_q2d(st->time_base);
                while(next<targets.size() && t+0.05>=targets[next]){
                    if(!emitFrame(fr,t)) { av_packet_unref(pkt); av_packet_free(&pkt); av_frame_free(&fr); return !out.empty(); }
                    ++next;
                }
                if(next>=targets.size()) break;
            }
        }
        av_packet_unref(pkt);
        if(next>=targets.size()) break;
        // Once decoding has passed the last requested timestamp, there is no useful work left.
        if(next<targets.size() && !out.empty() && out.back().timestamp>targets.back()+0.05) break;
    }
    av_packet_free(&pkt); av_frame_free(&fr);
    return !out.empty();
#else
    // Fallback keeps the same API. Without linked FFmpeg, delegate each requested
    // timestamp to the command-line decoder (the optimized single-pass path is used
    // whenever FFmpeg is linked).
    for(double secondsAt:seconds){
        if(!std::isfinite(secondsAt)||secondsAt<0) continue;
        std::ostringstream cmd; cmd<<"ffmpeg -v error -ss "<<secondsAt<<" -i \""<<path_<<"\" -frames:v 1 -vf scale="<<w<<":"<<h<<",format=gray -f rawvideo pipe:1";
        std::string raw; if(!captureSilent(cmd.str(),raw)) continue;
        if(raw.size()!=(size_t)w*h) continue;
        std::vector<std::uint8_t> data(raw.begin(),raw.end());
        out.push_back({secondsAt,w,h,std::move(data)});
    }
    return !out.empty();
#endif
}

bool VideoDecoder::framesAt96Plus32(const std::vector<double>& seconds,std::vector<VideoFrame>& out96,std::vector<VideoFrame>& out32){
    out96.clear(); out32.clear();
    if(!framesAt(seconds,96,96,out96)) return false;
    out32.reserve(out96.size());
    for(const auto& f:out96){
        if(f.width!=96||f.height!=96||f.gray.size()!=(std::size_t)96*96) continue;
        VideoFrame d; d.timestamp=f.timestamp; d.width=32; d.height=32;
        d.gray.resize((std::size_t)32*32);
        for(int y=0;y<32;++y)for(int x=0;x<32;++x){
            unsigned s=0;
            for(int dy=0;dy<3;++dy)for(int dx=0;dx<3;++dx) s+=f.gray[(std::size_t)(y*3+dy)*96+x*3+dx];
            d.gray[(std::size_t)y*32+x]=(std::uint8_t)((s+4)/9);
        }
        out32.push_back(std::move(d));
    }
    return !out32.empty();
}

void VideoDecoder::close(){
#ifdef MSF_HAS_FFMPEG
    if(codec_){auto*p=reinterpret_cast<AVCodecContext*>(codec_);avcodec_free_context(&p);codec_=nullptr;}
    if(fmt_){auto*p=reinterpret_cast<AVFormatContext*>(fmt_);avformat_close_input(&p);fmt_=nullptr;}
#endif
    path_.clear();info_={};
}
}
