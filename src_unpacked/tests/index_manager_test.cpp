#include "index_manager.h"
#include "media_search_engine.h"
#include <filesystem>
#include <fstream>
#include <iostream>

int main(){
    namespace fs=std::filesystem;
    const auto base=fs::temp_directory_path()/"msf_index_manager_test";
    const auto app=base/"app";
    const auto root1=base/"media1";
    const auto root2=base/"media2";
    fs::remove_all(base); fs::create_directories(app); fs::create_directories(root1); fs::create_directories(root2);

    msf::IndexPaths a,b,c;
    if(!msf::IndexManager::resolve(app,root1,a)) return 1;
    if(a.directory.parent_path()!=app/"Index") return 2;
    if(a.database!=a.directory/"index.sqlite" || a.videoCache!=a.directory/"video_cache.sqlite") return 3;
    if(fs::exists(root1/".msf")) return 4;
    if(!msf::IndexManager::resolve(app,root1,b) || a.directory!=b.directory) return 5;
    if(!msf::IndexManager::resolve(app,root2,c) || a.directory==c.directory) return 6;

    // NOTE: the reader must not stay open: Windows cannot replace/remove a
    // file while a handle to it is held (updateLastScan renames over it).
    std::string text;
    { std::ifstream meta(a.metadata); text.assign((std::istreambuf_iterator<char>(meta)),{}); }
    if(text.find("rootPath") == std::string::npos || text.find("lastScan") == std::string::npos || text.find("schemaVersion") == std::string::npos) return 7;

    // A real scan must only populate the application-owned Index directory.
    std::ofstream(root1/"a.txt") << "not media";
    // The application-owned Index directory must also be excluded when the application directory itself is scanned.
    std::ofstream(app/"sample.jpg") << "not image";
    msf::MediaSearchEngine engine;
    if(!engine.openIndexForRoot(root1.string(),app.string())) return 8;
    engine.scan(root1.string());
    msf::MediaSearchEngine parentEngine;
    if(!parentEngine.openIndexForRoot(app.string(),app.string())) return 11;
    parentEngine.scan(app.string());
    if(fs::exists(root1/".msf") || fs::exists(root1/"index.sqlite") || fs::exists(root1/"video_cache.sqlite")) return 9;
    if(!fs::exists(a.database) || !fs::exists(a.videoCache)) return 10;
    if(!msf::IndexManager::updateLastScan(a)) return 12;
    if(fs::exists(a.metadata.string()+".tmp")) return 13;

    std::cout << "index_manager=ok\n"
              << "central_index=ok\n"
              << "target_folder_untouched=ok\n";
    engine.close(); parentEngine.close(); // Windows cannot remove open database files.
    fs::remove_all(base);
    return 0;
}
