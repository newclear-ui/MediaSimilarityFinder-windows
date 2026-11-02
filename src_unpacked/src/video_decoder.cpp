#include "video_decoder.h"
#include "path_utils.h"
#include "proc_capture.h"
#include <fstream>
#include <algorithm>
#include <cstdio>
#include <array>
#include <cmath>
#include <sstream>
#include <chrono>   // E-3B: the exact-sparse-seek executor is timed per stage
#ifdef MSF_HAS_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/error.h>
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
    // 0.9.4.167 (P1): normalize the stream-time origin. Sampling targets
    // are video-time (0-based); without this offset a nonzero start_time
    // shifts every selected sample (verified 2.5 s shift on an
    // output_ts_offset fixture in 0.9.4.166).
    info_.startOffsetSec=0;
    if(st->start_time!=AV_NOPTS_VALUE){
      const double off=st->start_time*av_q2d(st->time_base);
      if(std::isfinite(off)) info_.startOffsetSec=off;
    }
    return true;
#else
    std::ifstream f(path_from_utf8(p),std::ios::binary);if(!f)return false;
    path_=p; info_={};
    std::string q="ffprobe -v error -select_streams v:0 -show_entries stream=width,height,duration,r_frame_rate -of default=noprint_wrappers=1 \""+p+"\"";
    // Metadata class: 30 s budget (local probe, normally < 2 s).
    std::string out; if(!captureSilent(q,out,30000))return false;
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
    const double startOffC=info_.startOffsetSec;
    int64_t ts=av_rescale_q((int64_t)((seconds+startOffC)*1000000.0),AV_TIME_BASE_Q,st->time_base);
    if(av_seek_frame(fmt,stream_,ts,AVSEEK_FLAG_BACKWARD)<0) return false;
    avcodec_flush_buffers(cc);
    AVPacket* pkt=av_packet_alloc(); AVFrame* fr=av_frame_alloc();
    if(!pkt||!fr){av_packet_free(&pkt);av_frame_free(&fr);return false;}
    bool got=false;
    while(!got && av_read_frame(fmt,pkt)>=0){
        if(pkt->stream_index==stream_ && avcodec_send_packet(cc,pkt)>=0){
            while(avcodec_receive_frame(cc,fr)>=0){
                double t=fr->best_effort_timestamp==AV_NOPTS_VALUE?seconds:fr->best_effort_timestamp*av_q2d(st->time_base)-startOffC;
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

bool VideoDecoder::framesAt(const std::vector<double>& seconds,int w,int h,std::vector<VideoFrame>& out,
                      const std::atomic<bool>* cancel,std::size_t* decodedOut,std::size_t* errorOut,std::size_t* noptsOut){
    out.clear();
    if(errorOut) *errorOut=0;
    if(noptsOut) *noptsOut=0;
    if(path_.empty() || w<=0 || h<=0 || seconds.empty()) return false;
    if(cancel && cancel->load(std::memory_order_relaxed)) return false;
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
    // 0.9.4.167 (P1): normalize the stream-time origin. Targets are
    // video-time (0-based); seeking and frame times translate by the
    // recorded offset so containers with nonzero start_time sample the
    // same content as zero-start ones.
    const double startOff=info_.startOffsetSec;
    int64_t ts=av_rescale_q((int64_t)((targets.front()+startOff)*1000000.0),AV_TIME_BASE_Q,st->time_base);
    if(av_seek_frame(fmt,stream_,ts,AVSEEK_FLAG_BACKWARD)<0) return false;
    avcodec_flush_buffers(cc);
    AVPacket* pkt=av_packet_alloc(); AVFrame* fr=av_frame_alloc();
    if(!pkt||!fr){av_packet_free(&pkt);av_frame_free(&fr);return false;}
    std::size_t next=0;
    // 0.9.4.155: count every frame FFmpeg actually produces, not just the
    // ones matching a sample target. GPU-decode replacement and frame-
    // download savings cannot be measured from kept frames alone.
    bool cancelled=false;
    // 0.9.4.158 (P1): last actually-decoded frame time, for the tail rule
    // below. Updated for every received frame, including intermediates that
    // match no target (they still prove content existed at that time).
    double lastDecodedT=0; bool anyDecoded=false;
    if(decodedOut) *decodedOut=0;
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
    // 0.9.4.164 (P2): decode anomaly accounting. Rejected input packets
    // (including EAGAIN drops, which this sweep skips rather than draining
    // for -- a known frame-loss path, now visible) and non-EOF demuxer read
    // errors are counted, never silent. EAGAIN on receive is normal flow
    // control, not an error.
    int readRc=0;
    while(next<targets.size() && (readRc=av_read_frame(fmt,pkt))>=0){
        // Cooperative cancel between packet reads: a Stop (or the stall
        // watchdog) landing mid-sweep stops here instead of decoding to EOF.
        // 0.9.4.158 (P0): a cancelled sweep is a failure, never a partial
        // success. The flag is recorded so the tail below clears the
        // partial output instead of returning it as a usable sample set.
        if(cancel && cancel->load(std::memory_order_relaxed)){ av_packet_unref(pkt); cancelled=true; break; }
        if(pkt->stream_index==stream_){
            const int sent=avcodec_send_packet(cc,pkt);
            // 0.9.4.164 (P2): rejected input is counted, never silent.
            if(sent<0){ if(errorOut) ++(*errorOut); }
            else while(avcodec_receive_frame(cc,fr)>=0){
                if(decodedOut) ++(*decodedOut);
                // 0.9.4.166 (P1): a NOPTS frame carries no measured time; the
        // requested target is substituted and counted. Such samples hold
        // assumed, not observed, timing.
        double t=0; const bool nopts=fr->best_effort_timestamp==AV_NOPTS_VALUE;
        if(nopts){ t=targets[next]; if(noptsOut) ++(*noptsOut); }
        else t=fr->best_effort_timestamp*av_q2d(st->time_base)-startOff;
                lastDecodedT=t; anyDecoded=true;
                while(next<targets.size() && t+0.05>=targets[next]){
                    // 0.9.4.158 (P0): an emit failure is a sweep failure.
                    // Returning the frames gathered so far would let the
                    // caller fingerprint (and cache) an incomplete sample
                    // set as if it were complete.
                    if(!emitFrame(fr,t)) { av_packet_unref(pkt); av_packet_free(&pkt); av_frame_free(&fr); out.clear(); return false; }
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
    // 0.9.4.164 (P2): a non-EOF demuxer error ends the sweep like EOF does;
    // count it so truncated/errored reads are visible in telemetry.
    if(readRc<0 && readRc!=AVERROR_EOF){ if(errorOut) ++(*errorOut); }
    // 0.9.4.158 (P1): EOF flush/drain. B-frames (and any reordered frames)
    // held inside the decoder never surface from av_read_frame, so without
    // a NULL-packet drain the tail samples would silently go missing while
    // the sweep still reported success.
    if(!cancelled && next<targets.size()){
        if(avcodec_send_packet(cc,nullptr)<0){ if(errorOut) ++(*errorOut); }
        else {
            while(next<targets.size() && avcodec_receive_frame(cc,fr)>=0){
                if(decodedOut) ++(*decodedOut);
                // 0.9.4.166 (P1): a NOPTS frame carries no measured time; the
        // requested target is substituted and counted. Such samples hold
        // assumed, not observed, timing.
        double t=0; const bool nopts=fr->best_effort_timestamp==AV_NOPTS_VALUE;
        if(nopts){ t=targets[next]; if(noptsOut) ++(*noptsOut); }
        else t=fr->best_effort_timestamp*av_q2d(st->time_base)-startOff;
                lastDecodedT=t; anyDecoded=true;
                while(next<targets.size() && t+0.05>=targets[next]){
                    if(!emitFrame(fr,t)) { av_packet_free(&pkt); av_frame_free(&fr); out.clear(); return false; }
                    ++next;
                }
            }
        }
    }
    av_packet_free(&pkt); av_frame_free(&fr);
    // 0.9.4.158 (P0/P1) completeness contract: success means every requested
    // sample was produced and no cancel landed mid-sweep, EXCEPT trailing
    // targets within endpoint-quantization slack of the last decoded frame.
    // The plan always appends the exact duration endpoint, but the last
    // decodable frame precedes it by up to a frame interval (low-fps files:
    // tenths of a second), so demanding it would fail legitimate files.
    // Cancelled sweeps always fail with cleared output: no partial sample
    // set may reach fingerprinting or the caches. A truncated file (or
    // decoder errors) leaves missing targets FAR beyond the slack, so it
    // still fails with nothing stored.
    if(cancelled || (cancel && cancel->load(std::memory_order_relaxed))){ out.clear(); return false; }
    if(!anyDecoded || out.empty()){ out.clear(); return false; }
    if(next<targets.size()){
        // Tail tolerance: trailing targets within endpoint-quantization
        // slack of the last actually-decoded frame are excused (frame-grid /
        // muxer rounding at the duration endpoint). Anything else missing
        // means real content loss (truncation, decode errors) -> failure.
        double slack=0.5;
        if(info_.fps>0.5) slack=std::clamp(1.5/info_.fps,0.15,1.5);
        for(std::size_t k=next;k<targets.size();++k){
            if(!(targets[k]<=lastDecodedT+slack)){ out.clear(); return false; }
        }
    }
    for(std::size_t i=0;i<out.size();++i){
        if(!(out[i].timestamp+0.05>=targets[i])){ out.clear(); return false; }
        if(i>0 && out[i].timestamp+1e-6<out[i-1].timestamp){ out.clear(); return false; }
    }
    return true;
#else
    // Fallback keeps the same API. Without linked FFmpeg, delegate each requested
    // timestamp to the command-line decoder (the optimized single-pass path is used
    // whenever FFmpeg is linked).
    // 0.9.4.158: same completeness contract as the linked path (P0/P1),
    // including the endpoint tail tolerance (the CLI seek for a duration-
    // endpoint target yields no frame on low-fps files).
    std::size_t valid=0;
    std::vector<double> validStamps;
    std::size_t firstMissOrdinal=SIZE_MAX, lastOkOrdinal=0;
    double lastOkStamp=0; bool anyOk=false;
    for(double secondsAt:seconds){
        if(cancel && cancel->load(std::memory_order_relaxed)){ out.clear(); return false; }
        if(!std::isfinite(secondsAt)||secondsAt<0) continue;
        const std::size_t vi=valid++;
        validStamps.push_back(secondsAt);
        std::ostringstream cmd; cmd<<"ffmpeg -v error -ss "<<secondsAt<<" -i \""<<path_<<"\" -frames:v 1 -vf scale="<<w<<":"<<h<<",format=gray -f rawvideo pipe:1";
        // Decode class: 120 s budget (single frame, normally < 10 s).
        // 0.9.4.164 (P2): CLI/decode failures count like linked-path errors.
        std::string raw; if(!captureSilent(cmd.str(),raw,120000)){ firstMissOrdinal=std::min(firstMissOrdinal,vi); if(errorOut) ++(*errorOut); continue; }
        if(raw.size()!=(size_t)w*h){ firstMissOrdinal=std::min(firstMissOrdinal,vi); if(errorOut) ++(*errorOut); continue; }
        std::vector<std::uint8_t> data(raw.begin(),raw.end());
        out.push_back({secondsAt,w,h,std::move(data)});
        lastOkOrdinal=vi; lastOkStamp=secondsAt; anyOk=true;
        // No-FFmpeg fallback: each CLI decode yields at most one kept frame.
        if(decodedOut) ++(*decodedOut);
    }
    if(cancel && cancel->load(std::memory_order_relaxed)){ out.clear(); return false; }
    if(out.size()==valid) return !out.empty();
    // Partial CLI results fail, except trailing misses within endpoint slack
    // of the last produced sample. An interior gap means real content loss.
    if(!anyOk){ out.clear(); return false; }
    if(firstMissOrdinal<=lastOkOrdinal){ out.clear(); return false; }
    for(std::size_t k=0;k<validStamps.size();++k){
        if(k>lastOkOrdinal && !(validStamps[k]<=lastOkStamp+1.0)){ out.clear(); return false; }
    }
    return !out.empty();
#endif
}

bool VideoDecoder::framesAt96Plus32(const std::vector<double>& seconds,std::vector<VideoFrame>& out96,std::vector<VideoFrame>& out32,
                               const std::atomic<bool>* cancel,std::size_t* decodedOut,std::size_t* errorOut,std::size_t* noptsOut){
    out96.clear(); out32.clear();
    // 0.9.4.158: framesAt owns the completeness contract (including the
    // endpoint tail tolerance), so a true result is used as-is.
    if(!framesAt(seconds,96,96,out96,cancel,decodedOut,errorOut,noptsOut)){ out96.clear(); out32.clear(); return false; }
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
    // 0.9.4.158: the 32x32 set must stay 1:1 with the 96x96 sweep; a short
    // derived set is a sweep failure, not a smaller fingerprint.
    if(out32.size()!=out96.size() || out32.empty()){ out96.clear(); out32.clear(); return false; }
    return true;
}

bool VideoDecoder::framesAt96Plus32ExactSparse(const std::vector<double>& seconds,std::vector<VideoFrame>& out96,std::vector<VideoFrame>& out32,SparseSeekStats* st){
    // E-3B executor timing helper, file-local so it cannot be mistaken for a
    // shared API.
    const auto msSince=[](const std::chrono::steady_clock::time_point& t){
        return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();
    };
    out96.clear(); out32.clear();
    if(path_.empty()||seconds.empty()) return false;
#ifdef MSF_HAS_FFMPEG
    if(!fmt_ || !codec_ || stream_<0) return false;
    AVFormatContext* fmt=reinterpret_cast<AVFormatContext*>(fmt_);
    AVCodecContext* cc=reinterpret_cast<AVCodecContext*>(codec_);
    AVStream* strm=fmt->streams[stream_];
    const double tbSec=av_q2d(strm->time_base);
    const auto tAll=std::chrono::steady_clock::now();
    SparseSeekStats local;
    AVPacket* pkt=av_packet_alloc(); AVFrame* fr=av_frame_alloc();
    if(!pkt||!fr){av_packet_free(&pkt);av_frame_free(&fr);return false;}

    for(double target:seconds){
        // Seek to target - 0.05, not target. The predicate admits a frame up to
        // 0.05 s EARLIER than the target, so landing on a keyframe at or before
        // target is not enough to reach the frame production would pick. Landing
        // at or before target-0.05 guarantees it is reachable. This is the E-2B
        // fix; the predicate, tolerance and target are all unchanged.
        // 0.9.4.167 (P1): seek in stream time (video-time target + offset).
        const double offSec=info_.startOffsetSec;
        const int64_t backPts=av_rescale_q(
            (int64_t)((std::max(0.0,target-0.05)+offSec)*1000000.0),AV_TIME_BASE_Q,strm->time_base);
        const auto ts0=std::chrono::steady_clock::now();
        if(av_seek_frame(fmt,stream_,backPts,AVSEEK_FLAG_BACKWARD)<0){++local.seekFailures;break;}
        avcodec_flush_buffers(cc);
        ++local.seeks; local.seekMs+=msSince(ts0);
        VideoFrame vf; vf.timestamp=target; bool done=false,landingChecked=false;
        while(!done && av_read_frame(fmt,pkt)>=0){
            if(pkt->stream_index!=stream_){av_packet_unref(pkt);continue;}
            const auto td0=std::chrono::steady_clock::now();
            const int sent=avcodec_send_packet(cc,pkt);
            local.decodeMs+=msSince(td0);
            av_packet_unref(pkt);
            if(sent<0) continue;
            for(;;){
                const auto tr0=std::chrono::steady_clock::now();
                const int got=avcodec_receive_frame(cc,fr);
                local.decodeMs+=msSince(tr0);
                if(got<0) break;
                ++local.decoded;
                if(!landingChecked){
                    landingChecked=true;
                    // The executor contract. If the decoder's first output is
                    // later than what we asked for, production's frame may be
                    // unreachable and the whole result is untrustworthy.
                    if(fr->best_effort_timestamp>backPts) ++local.landingViolations;
                }
                // 0.9.4.167 (P1): compare in video time (stream PTS minus the
                // recorded offset), so offset containers select the same
                // content as zero-start ones. NOPTS falls back to the target.
                const double ft=fr->best_effort_timestamp==AV_NOPTS_VALUE?target:fr->best_effort_timestamp*tbSec-offSec;
                if(ft+0.05>=target){
                    const auto tc0=std::chrono::steady_clock::now();
                    AVFrame* dst=av_frame_alloc();
                    if(dst){
                        dst->format=AV_PIX_FMT_GRAY8; dst->width=96; dst->height=96;
                        SwsContext* sws=sws_getContext(fr->width,fr->height,(AVPixelFormat)fr->format,96,96,AV_PIX_FMT_GRAY8,SWS_BILINEAR,nullptr,nullptr,nullptr);
                        if(sws&&av_frame_get_buffer(dst,1)>=0){
                            sws_scale(sws,fr->data,fr->linesize,0,fr->height,dst->data,dst->linesize);
                            vf.width=96; vf.height=96; vf.gray.resize((size_t)96*96);
                            for(int y=0;y<96;++y) std::copy(dst->data[0]+y*dst->linesize[0],dst->data[0]+y*dst->linesize[0]+96,vf.gray.begin()+size_t(y)*96);
                        }
                        if(sws)sws_freeContext(sws); av_frame_free(&dst);
                    }
                    local.convertMs+=msSince(tc0);
                    out96.push_back(vf); ++local.emitted; done=true; break;
                }
            }
        }
    }
    av_packet_free(&pkt); av_frame_free(&fr);
    local.totalMs=msSince(tAll);
    if(st)*st=local;
    // The executor contract. A candidate is usable only when it is COMPLETE and
    // PROVABLY EQUAL to production, so both failure modes discard the result
    // rather than returning a smaller set of hashes.
    //
    // This is not defensive padding. A read that runs out of packets before a
    // target is reached leaves out96 short, and an earlier version returned
    // success for that truncated set ??which would have silently fingerprinted a
    // file with fewer samples than production and changed search results.
    if(local.landingViolations>0) { out96.clear(); out32.clear(); return false; }
    if(out96.size()!=seconds.size()){ out96.clear(); out32.clear(); return false; }
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
    // A partial 32x32 derivation would also be a wrong fingerprint, so the
    // counts are re-checked rather than trusting the skip above.
    return out32.size()==seconds.size();
#else
    (void)st;
    return false;  // no FFmpeg: exact sparse seek is unavailable, never faked
#endif
}

std::string VideoDecoder::codecName() const {
#ifdef MSF_HAS_FFMPEG
    if(!fmt_ || stream_<0) return std::string();
    AVFormatContext* fmt=reinterpret_cast<AVFormatContext*>(fmt_);
    if(stream_>=(int)fmt->nb_streams) return std::string();
    const AVCodec* dec=avcodec_find_decoder(fmt->streams[stream_]->codecpar->codec_id);
    return dec?std::string(dec->name):std::string();
#else
    return std::string();
#endif
}

double VideoDecoder::gopFramesFromIndex() const {
#ifdef MSF_HAS_FFMPEG
    if(!fmt_ || stream_<0) return -1.0;
    AVFormatContext* fmt=reinterpret_cast<AVFormatContext*>(fmt_);
    if(stream_>=(int)fmt->nb_streams) return -1.0;
    AVStream* st=fmt->streams[stream_];
    // Container index, not a decode: cheap enough to run on every file.
    const int entries=avformat_index_get_entries_count(st);
    if(entries<=0) return -1.0;
    // Prefer the real frame count when the container provides one, so the GOP
    // is in frames rather than in index entries.
    double frames = st->nb_frames>0 ? (double)st->nb_frames : 0.0;
    if(frames<=0.0){
        const double dur=info_.duration>0?info_.duration:0.0;
        if(dur>0.0 && info_.fps>0.0) frames=dur*info_.fps;
    }
    if(frames<=0.0) return -1.0;
    return frames/(double)entries;
#else
    return -1.0;
#endif
}

void VideoDecoder::close(){
#ifdef MSF_HAS_FFMPEG
    if(codec_){auto*p=reinterpret_cast<AVCodecContext*>(codec_);avcodec_free_context(&p);codec_=nullptr;}
    if(fmt_){auto*p=reinterpret_cast<AVFormatContext*>(fmt_);avformat_close_input(&p);fmt_=nullptr;}
#endif
    path_.clear();info_={};
}
}
