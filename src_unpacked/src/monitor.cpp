#include "monitor.h"
#include "path_utils.h"
#include "crop_fingerprint.h"
#include "image_decoder.h"
#include "similarity.h"
#include "video_fingerprint.h"
#include "index_manager.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <cstdlib>
#include <sstream>
#include <cctype>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace msf {
namespace fs=std::filesystem;

static bool mediaFile(const fs::path& p);

SystemLoad SystemLoadMonitor::sample(){
    SystemLoad l{};
#ifdef _WIN32
    FILETIME idle{},kernel{},user{};
    if(GetSystemTimes(&idle,&kernel,&user)){
        ULARGE_INTEGER i{},k{},u{};
        i.LowPart=idle.dwLowDateTime;i.HighPart=idle.dwHighDateTime;
        k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
        u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;
        if(haveCpuSample_){
            auto dI=i.QuadPart-prevIdle_.QuadPart, dK=k.QuadPart-prevKernel_.QuadPart, dU=u.QuadPart-prevUser_.QuadPart;
            auto total=dK+dU; l.cpuPercent=total?100.0*double(total-dI)/double(total):0.0;
        }
        prevIdle_=i; prevKernel_=k; prevUser_=u; haveCpuSample_=true;
    }
    MEMORYSTATUSEX m{}; m.dwLength=sizeof(m); if(GlobalMemoryStatusEx(&m)) l.memoryPercent=(double)m.dwMemoryLoad;
    auto now=std::chrono::steady_clock::now();
    if(lastGpuSample_.time_since_epoch().count()==0 || now-lastGpuSample_>=std::chrono::seconds(3)){
        FILE* f=_popen("nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>NUL","r");
        if(f){ char buf[64]{}; if(fgets(buf,sizeof(buf),f)){ try{cachedGpuPercent_=std::stod(buf);}catch(...){cachedGpuPercent_=-1.0;} } _pclose(f); }
        lastGpuSample_=now;
    }
    l.gpuPercent=cachedGpuPercent_;
#else
    std::ifstream f("/proc/loadavg"); double one=0; if(f) f>>one;
    unsigned hc=std::max(1u,std::thread::hardware_concurrency()); l.cpuPercent=std::min(100.0,one*100.0/hc);
    std::ifstream m("/proc/meminfo"); std::string key; long long total=0,avail=0;
    while(m>>key){ long long v; std::string unit; m>>v>>unit; if(key=="MemTotal:") total=v; if(key=="MemAvailable:") avail=v; }
    if(total) l.memoryPercent=100.0*double(total-avail)/double(total);
#endif
    l.state=classify(l); l.critical=(l.state==LoadState::Critical); return l;
}
LoadState SystemLoadMonitor::classify(const SystemLoad& l){
    if(l.memoryPercent>=95 || l.cpuPercent>=92 || (l.gpuPercent>=0 && l.gpuPercent>=95)) return LoadState::Critical;
    const double gpu=l.gpuPercent<0?0:l.gpuPercent; const double peak=std::max(l.cpuPercent,gpu);
    if(peak>=80 || l.memoryPercent>=90) return LoadState::Heavy;
    if(peak>=55 || l.memoryPercent>=80) return LoadState::Busy;
    if(peak>=25 || l.memoryPercent>=65) return LoadState::Light;
    return LoadState::Idle;
}
bool SystemLoadMonitor::allowAnalysis(const ResourcePolicy& p,const SystemLoad& l) const{
    if(l.critical || l.state==LoadState::Critical || l.memoryPercent>=90) return false;
    const double reserve=(p.mode==ResourceMode::Gaming)?20.0:10.0;
    if(l.cpuPercent+reserve>std::max(5.0,static_cast<double>(p.cpuPercent))) return false;
    if(p.gpuEnabled && l.gpuPercent>=0 && l.gpuPercent+reserve>std::max(5.0,static_cast<double>(p.gpuPercent))) return false;
    return l.state==LoadState::Idle || (l.state==LoadState::Light && p.mode!=ResourceMode::Gaming);
}

bool StableFileDetector::isStable(const std::string& path,int stableSeconds){
    std::error_code ec; fs::path p(path); if(!fs::is_regular_file(p,ec)) return false; auto sz=fs::file_size(p,ec); if(ec)return false; auto mt=fs::last_write_time(p,ec);if(ec)return false;
    auto now=std::chrono::steady_clock::now(); std::lock_guard<std::mutex> g(mutex_); auto it=states_.find(path);
    if(it==states_.end()){states_[path]={sz,mt,now};return false;}
    if(it->second.size!=sz || it->second.modified!=mt){it->second={sz,mt,now};return false;}
    if(now-it->second.firstSeen>=std::chrono::seconds(std::max(0,stableSeconds))){
        states_.erase(it);
        return true;
    }
    return false;
}

MediaMonitor::MediaMonitor()=default;
MediaMonitor::~MediaMonitor(){stop();}
void MediaMonitor::setPolicy(const ResourcePolicy& p){std::lock_guard<std::mutex> g(mutex_);policy_=p;}
void MediaMonitor::setPaused(bool paused){
    paused_.store(paused);
    { std::lock_guard<std::mutex> g(mutex_); status_.paused=paused; }
    queueCv_.notify_one();
}
bool MediaMonitor::paused() const { return paused_.load(); }
MonitorStatus MediaMonitor::status() const { std::lock_guard<std::mutex> g(mutex_); auto s=status_; s.running=running_.load(); s.pending=pending_.size(); s.paused=paused_.load(); return s; }
void MediaMonitor::enqueuePath(const std::string& path, bool notify){
    std::error_code ec; if(!fs::is_regular_file(path,ec)) return;
    auto ext=path_from_utf8(path).extension().string(); std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return(char)std::tolower(c);});
    static const char* exts[]={".jpg",".jpeg",".png",".bmp",".gif",".webp",".tif",".tiff",".mp4",".mkv",".avi",".mov",".webm",".m4v",".wmv"};
    if(std::find(std::begin(exts),std::end(exts),ext)==std::end(exts)) return;
    if(notify){
        std::error_code ec;
        const auto candidate=fs::weakly_canonical(path_from_utf8(path),ec);
        if(!ec){
            for(const auto& root:config_.compareRoots){
                std::error_code rec; auto cr=fs::weakly_canonical(path_from_utf8(root),rec);
                if(!rec){
                    auto rel=fs::relative(candidate,cr,rec);
                    if(!rec && !rel.empty() && *rel.begin()!=".." && rel!=fs::path("..")){ notify=false; break; }
                    if(!rec && rel==fs::path(".")){ notify=false; break; }
                }
            }
        }
    }
    std::lock_guard<std::mutex> g(mutex_);
    auto [it, inserted] = pendingSet_.try_emplace(path, PendingFlags{});
    it->second.notify = it->second.notify || notify;
    it->second.removal = false; // a later create/modify event supersedes a stale removal
    if(inserted) {
        const auto due=std::chrono::steady_clock::now();
        pending_.push({path,it->second.notify,false,due,0});
    }
    queueCv_.notify_one();
}

void MediaMonitor::enqueueRemoval(const std::string& path, bool notify){
    std::lock_guard<std::mutex> g(mutex_);
    auto [it, inserted] = pendingSet_.try_emplace(path, PendingFlags{});
    it->second.removal = true;
    it->second.notify = it->second.notify || notify;
    if(inserted) {
        pending_.push({path,it->second.notify,true,std::chrono::steady_clock::now(),0});
    }
    queueCv_.notify_one();
}

bool MediaMonitor::start(const MonitorConfig& c,const ResourcePolicy& p,Callback cb){
    stop(); config_=c;policy_=p;callback_=std::move(cb);seen_.clear();
    paused_.store(false);
    { std::lock_guard<std::mutex> g(mutex_); status_=MonitorStatus{}; status_.running=true; status_.paused=false; }
    {std::lock_guard<std::mutex> g(mutex_); while(!pending_.empty())pending_.pop(); pendingSet_.clear(); retryCounts_.clear();}
    compareEngines_.clear();
    const auto appDir=config_.applicationDirectory.empty()?path_to_utf8(fs::current_path()):config_.applicationDirectory;
    for(const auto& root:config_.compareRoots){ auto e=std::make_unique<MediaSearchEngine>(); if(e->openIndexForRoot(root,appDir)){ e->setResourcePolicy(policy_); e->setExpensiveStageGuard([this]{ auto l=load_.sample(); { std::lock_guard<std::mutex> g(mutex_); status_.loadState=l.state; status_.cpuPercent=l.cpuPercent; status_.memoryPercent=l.memoryPercent; status_.gpuPercent=l.gpuPercent; } return load_.allowAnalysis(policy_,l); }); compareEngines_.push_back({root,std::move(e)}); } }
    running_=true;
#ifdef _WIN32
    for(const auto& root:c.watchRoots) watcherThreads_.emplace_back(&MediaMonitor::windowsWatchLoop,this,root,true);
    for(const auto& root:c.compareRoots) { if(std::find(c.watchRoots.begin(),c.watchRoots.end(),root)==c.watchRoots.end()) watcherThreads_.emplace_back(&MediaMonitor::windowsWatchLoop,this,root,false); }
#endif
    thread_=std::thread(&MediaMonitor::loop,this);return true;
}
void MediaMonitor::stop(){
    if(!running_.exchange(false)) return;
    queueCv_.notify_all();
#ifdef _WIN32
    stopWindowsWatchers();
#endif
    if(thread_.joinable())thread_.join();
#ifdef _WIN32
    for(auto& t:watcherThreads_) if(t.joinable()) t.join(); watcherThreads_.clear();
#endif
}
void MediaMonitor::emitEvent(MonitorEvent e){Callback cb;{std::lock_guard<std::mutex> g(mutex_);cb=callback_;}if(cb)cb(e);}

#ifdef _WIN32
void MediaMonitor::stopWindowsWatchers(){
    std::lock_guard<std::mutex> g(watcherMutex_);
    for(void* h:watcherHandles_) if(h) CancelIoEx((HANDLE)h,nullptr);
}
void MediaMonitor::windowsWatchLoop(const std::string& root, bool notify){
    int wlen=MultiByteToWideChar(CP_UTF8,0,root.c_str(),-1,nullptr,0); std::wstring wroot; if(wlen<=0) return; wroot.resize((std::size_t)wlen); MultiByteToWideChar(CP_UTF8,0,root.c_str(),-1,wroot.data(),wlen); HANDLE h=CreateFileW(wroot.c_str(),FILE_LIST_DIRECTORY,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OVERLAPPED,nullptr);
    if(h==INVALID_HANDLE_VALUE) return;
    HANDLE ev=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!ev){CloseHandle(h);return;}
    {std::lock_guard<std::mutex> g(watcherMutex_); if(!running_){CloseHandle(ev);CloseHandle(h);return;} watcherHandles_.push_back(h);}
    std::vector<unsigned char> buffer(64*1024);
    const auto resync=[&](){
        std::error_code ec;
        fs::recursive_directory_iterator it(path_from_utf8(root),fs::directory_options::skip_permission_denied,ec),end;
        for(;running_ && it!=end && !ec;it.increment(ec))
            if(it->is_regular_file(ec) && mediaFile(it->path())) enqueuePath(path_to_utf8(it->path()),notify);
    };
    while(running_){
        OVERLAPPED ov{}; ov.hEvent=ev; ResetEvent(ev); DWORD bytes=0;
        BOOL ok=ReadDirectoryChangesW(h,buffer.data(),(DWORD)buffer.size(),TRUE,FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_SIZE|FILE_NOTIFY_CHANGE_LAST_WRITE|FILE_NOTIFY_CHANGE_CREATION,&bytes,&ov,nullptr);
        if(!ok && GetLastError()!=ERROR_IO_PENDING) break;
        DWORD wait=WaitForSingleObject(ev,250);
        if(wait==WAIT_TIMEOUT){if(!running_)CancelIoEx(h,&ov);continue;}
        if(wait!=WAIT_OBJECT_0) break;
        if(!GetOverlappedResult(h,&ov,&bytes,FALSE)){
            const DWORD error=GetLastError();
            if(!running_) break;
            if(error==ERROR_NOTIFY_ENUM_DIR) resync();
            continue;
        }
        if(bytes==0){
            resync();
            continue;
        }
        DWORD offset=0;
        do{
            if(offset + sizeof(FILE_NOTIFY_INFORMATION) > bytes) break;
            auto* f=(FILE_NOTIFY_INFORMATION*)(buffer.data()+offset);
            if(f->FileNameLength > bytes-offset-sizeof(FILE_NOTIFY_INFORMATION)+sizeof(WCHAR)) break;
            std::wstring name(f->FileName,f->FileNameLength/sizeof(WCHAR));
            int rlen=MultiByteToWideChar(CP_UTF8,0,root.c_str(),-1,nullptr,0); if(rlen<=0) break; std::wstring wr((std::size_t)rlen,L'\0'); MultiByteToWideChar(CP_UTF8,0,root.c_str(),-1,wr.data(),rlen); fs::path p=fs::path(wr.c_str())/name;
            if(f->Action==FILE_ACTION_REMOVED || f->Action==FILE_ACTION_RENAMED_OLD_NAME) enqueueRemoval(path_to_utf8(p),false);
            else enqueuePath(path_to_utf8(p),notify);
            if(f->NextEntryOffset==0) break;
            if(f->NextEntryOffset > bytes-offset) break;
            offset+=f->NextEntryOffset;
        }while(offset<bytes);
    }
    {std::lock_guard<std::mutex> g(watcherMutex_); watcherHandles_.erase(std::remove(watcherHandles_.begin(),watcherHandles_.end(),(void*)h),watcherHandles_.end());}
    CloseHandle(ev); CloseHandle(h);
}

#endif

static bool isWithinRoot(const std::string& path,const std::string& root){ std::error_code ec1,ec2; auto p=fs::weakly_canonical(path_from_utf8(path),ec1); auto r=fs::weakly_canonical(path_from_utf8(root),ec2); if(ec1||ec2) return false; auto rel=fs::relative(p,r,ec1); if(ec1) return false; return rel.empty() || (rel!=fs::path("..") && *rel.begin()!=fs::path("..")); }
static std::uint64_t fileKey(const fs::path&p){std::error_code ec;auto sz=fs::file_size(p,ec);auto mt=fs::last_write_time(p,ec);if(ec)return 0;return (std::uint64_t)sz ^ (std::uint64_t)mt.time_since_epoch().count();}
static bool mediaFile(const fs::path&p){auto e=p.extension().string();std::transform(e.begin(),e.end(),e.begin(),[](unsigned char c){return(char)std::tolower(c);});return e==".jpg"||e==".jpeg"||e==".png"||e==".bmp"||e==".gif"||e==".webp"||e==".tif"||e==".tiff"||e==".mp4"||e==".mkv"||e==".avi"||e==".mov"||e==".webm"||e==".m4v"||e==".wmv";}

void MediaMonitor::loop(){
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
#endif
    MonitorEvent started; started.type=MonitorEvent::Type::Started; started.detail="Real-time monitor started"; emitEvent(started);
#ifndef _WIN32
    // Non-Windows fallback: seed the queue through polling.
    auto lastPoll=std::chrono::steady_clock::now()-std::chrono::seconds(config_.pollSeconds);
#endif
    while(running_){
#ifndef _WIN32
        auto now=std::chrono::steady_clock::now();
        if(now-lastPoll>=std::chrono::seconds(std::max(1,config_.pollSeconds))){
            for(const auto& root:config_.watchRoots){std::error_code ec;if(!fs::exists(path_from_utf8(root),ec))continue;fs::recursive_directory_iterator it(path_from_utf8(root),fs::directory_options::skip_permission_denied,ec),end;for(;it!=end&&!ec;it.increment(ec))if(it->is_regular_file(ec)&&mediaFile(it->path())){auto p=path_to_utf8(it->path());auto k=fileKey(it->path());auto s=seen_.find(p);if(s==seen_.end()||s->second!=k)enqueuePath(p,true);}}
            for(const auto& root:config_.compareRoots){if(std::find(config_.watchRoots.begin(),config_.watchRoots.end(),root)!=config_.watchRoots.end())continue;std::error_code ec;if(!fs::exists(path_from_utf8(root),ec))continue;fs::recursive_directory_iterator it(path_from_utf8(root),fs::directory_options::skip_permission_denied,ec),end;for(;it!=end&&!ec;it.increment(ec))if(it->is_regular_file(ec)&&mediaFile(it->path())){auto p=path_to_utf8(it->path());auto k=fileKey(it->path());auto s=seen_.find(p);if(s==seen_.end()||s->second!=k)enqueuePath(p,false);}}
            lastPoll=now;
        }
#endif
        PendingItem item; bool haveItem=false;
        {
            std::unique_lock<std::mutex> lk(mutex_);
            while(running_ && pending_.empty()) queueCv_.wait_for(lk,std::chrono::milliseconds(500));
            if(!running_) break;
            if(!pending_.empty()) {
                const auto now=std::chrono::steady_clock::now();
                item=pending_.front();
                if(item.due>now) { queueCv_.wait_until(lk,item.due); continue; }
                pending_.pop();
                auto fit=pendingSet_.find(item.path);
                if(fit!=pendingSet_.end()) {
                    item.notify=fit->second.notify;
                    item.removal=fit->second.removal;
                    pendingSet_.erase(fit);
                }
                haveItem=true;
            }
        }
        if(!haveItem) continue;
        if(!running_)break;
        if(paused_.load()){
            { std::lock_guard<std::mutex> g(mutex_);
              auto [it, inserted]=pendingSet_.try_emplace(item.path, PendingFlags{});
              it->second.notify=it->second.notify || item.notify;
              it->second.removal=it->second.removal || item.removal;
              if(inserted) pending_.push(item);
            }
            std::unique_lock<std::mutex> lk(mutex_);
            queueCv_.wait_for(lk,std::chrono::milliseconds(250),[this]{ return !running_.load() || !paused_.load(); });
            continue;
        }
        const std::string path=item.path;
        const auto analysisStarted=std::chrono::steady_clock::now();
        if(item.removal){ for(auto& ce:compareEngines_) if(isWithinRoot(path,ce.root)) ce.engine->removePath(path); seen_.erase(path); retryCounts_.erase(path); continue; }
        auto defer=[&](const std::string& detail, unsigned baseMs){
            const unsigned retry=++retryCounts_[path];
            if(config_.maxRetries>0 && retry>config_.maxRetries){
                { std::lock_guard<std::mutex> g(mutex_); ++status_.errors; status_.lastErrorPath=path; status_.lastError=detail; }
                MonitorEvent e;e.type=MonitorEvent::Type::Error;e.path=path;e.detail="Retry limit reached: "+detail;emitEvent(e);
                retryCounts_.erase(path);
                return;
            }
            const unsigned capped=std::min(30u,retry);
            const unsigned delay=std::min(30000u,baseMs*(1u<<std::min(5u,capped-1)));
            { std::lock_guard<std::mutex> g(mutex_);
              auto [it, inserted]=pendingSet_.try_emplace(path, PendingFlags{});
              it->second.notify=it->second.notify || item.notify;
              it->second.removal=false;
              if(inserted) pending_.push({path,it->second.notify,false,std::chrono::steady_clock::now()+std::chrono::milliseconds(delay),retry});
              ++status_.deferred;
              queueCv_.notify_one(); }
            MonitorEvent e;e.type=MonitorEvent::Type::Deferred;e.path=path;e.detail=detail;emitEvent(e);
        };
        if(!stable_.isStable(path,config_.stableSeconds)){defer("Waiting for file copy/write activity to settle",250); continue;}
        auto load=load_.sample(); { std::lock_guard<std::mutex> g(mutex_); status_.loadState=load.state; status_.cpuPercent=load.cpuPercent; status_.memoryPercent=load.memoryPercent; status_.gpuPercent=load.gpuPercent; } if(!load_.allowAnalysis(policy_,load)){defer("Analysis paused to protect foreground workload",500);continue;}
        auto ext=path_from_utf8(path).extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return(char)std::tolower(c);});
        const bool image=ext==".jpg"||ext==".jpeg"||ext==".png"||ext==".bmp"||ext==".gif"||ext==".webp"||ext==".tif"||ext==".tiff";
        std::uint64_t fp=0, mirrorFp=0; CropFingerprints crops{}; bool ok=false;if(image){ok=imagePipeline_.image(path,fp,&mirrorFp); ImageDecoder d; GrayImage original; if(ok&&d.decodePreserveAspect(path,128,original)) crops=cropFingerprints(original);}else{VideoFingerprint vf;if(videoEngine_.build(path,vf)){for(auto h:vf.hashes)fp^=h;for(auto h:vf.mirrorHashes)mirrorFp^=h;crops.a4x3=vf.crop4x3;crops.a1x1=vf.crop1x1;crops.a9x16=vf.crop9x16;crops.mirrorA4x3=vf.mirrorCrop4x3;crops.mirrorA1x1=vf.mirrorCrop1x1;crops.mirrorA9x16=vf.mirrorCrop9x16;ok=fp!=0;}}
        if(!ok){ { std::lock_guard<std::mutex> g(mutex_); ++status_.errors; status_.lastErrorPath=path; status_.lastError="Media fingerprinting failed"; } MonitorEvent e;e.type=MonitorEvent::Type::Error;e.path=path;e.detail="Media fingerprinting failed";emitEvent(e);continue;}
        const int kind=image?1:2;
        const double analysisMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-analysisStarted).count();
        { std::lock_guard<std::mutex> g(mutex_); ++status_.analyzed; status_.lastAnalysisMs=analysisMs; status_.averageAnalysisMs = status_.analyzed==1 ? analysisMs : (status_.averageAnalysisMs*double(status_.analyzed-1)+analysisMs)/double(status_.analyzed); status_.lastAnalysisKind=image?"image":"video"; }
        if(!item.notify){ std::error_code ec; auto sz=fs::file_size(path,ec); auto mt=fs::last_write_time(path,ec); auto mtv=ec?0LL:(std::int64_t)mt.time_since_epoch().count(); for(auto& ce:compareEngines_) if(isWithinRoot(path,ce.root)) ce.engine->upsertFingerprint(path,fp,kind,ec?0:(std::uint64_t)sz,mtv,mirrorFp,crops.a4x3,crops.a1x1,crops.a9x16,crops.mirrorA4x3,crops.mirrorA1x1,crops.mirrorA9x16); seen_[path]=fileKey(path); retryCounts_.erase(path); continue; }
        std::vector<MonitorMatch> matches;
        for(auto& ce:compareEngines_){auto ms=ce.engine->compareFingerprint(fp,kind,config_.thresholdPercent,path,mirrorFp,crops.a4x3,crops.a1x1,crops.a9x16,crops.mirrorA4x3,crops.mirrorA1x1,crops.mirrorA9x16);for(const auto&m:ms)matches.push_back({path,m.rightPath,m.percent});}
        std::sort(matches.begin(),matches.end(),[](const MonitorMatch&a,const MonitorMatch&b){return a.percent>b.percent;});
        { std::lock_guard<std::mutex> g(mutex_); if(!matches.empty()) ++status_.matches; }
        MonitorEvent e;e.path=path;if(matches.empty()){e.type=MonitorEvent::Type::Detected;e.detail="No existing match above threshold";}else{e.type=MonitorEvent::Type::Match;e.detail="Existing similar media found";e.matches=std::move(matches);}
        seen_[path]=fileKey(path);
        retryCounts_.erase(path);
        emitEvent(e);
    }
    { std::lock_guard<std::mutex> g(mutex_); status_.running=false; status_.pending=0; }
    MonitorEvent stopped;stopped.type=MonitorEvent::Type::Stopped;stopped.detail="Real-time monitor stopped";emitEvent(stopped);
}
}
