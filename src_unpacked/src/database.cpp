#include "database.h"
#include <sqlite3.h>
#include <string>
namespace msf {
static sqlite3* D(void* p){return reinterpret_cast<sqlite3*>(p);}
static sqlite3_stmt* S(void* p){return reinterpret_cast<sqlite3_stmt*>(p);}

Database::~Database(){ finalizeStatements(); if(db_) sqlite3_close(D(db_)); }

bool Database::open(const std::string& p){
    finalizeStatements();
    if(db_){ sqlite3_close(D(db_)); db_=nullptr; }
    path_=p;
    const int rc=sqlite3_open(path_.c_str(),reinterpret_cast<sqlite3**>(&db_));
    if(rc!=SQLITE_OK){ if(db_){sqlite3_close(D(db_));db_=nullptr;} return false; }
    // Keep database operations responsive when the resident monitor and a foreground scan overlap.
    sqlite3_busy_timeout(D(db_),5000);
    return true;
}

bool Database::exec(const char* sql) const {
    char* e=nullptr;
    const int rc=sqlite3_exec(D(db_),sql,nullptr,nullptr,&e);
    sqlite3_free(e);
    return rc==SQLITE_OK;
}

void Database::finalizeStatements(){
    if(upsertStmt_) sqlite3_finalize(S(upsertStmt_));
    if(removeStmt_) sqlite3_finalize(S(removeStmt_));
    if(containsStmt_) sqlite3_finalize(S(containsStmt_));
    upsertStmt_=removeStmt_=containsStmt_=nullptr;
}

bool Database::prepareStatements(){
    finalizeStatements();
    const char* upsert=
      "INSERT INTO files(path,size,modified,quick_hash,fingerprint,kind,duration,mirror_fingerprint,"
      "crop_4x3,crop_1x1,crop_9x16,mirror_crop_4x3,mirror_crop_1x1,mirror_crop_9x16) "
      "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(path) DO UPDATE SET "
      "size=excluded.size,modified=excluded.modified,quick_hash=excluded.quick_hash,"
      "fingerprint=excluded.fingerprint,kind=excluded.kind,duration=excluded.duration,"
      "mirror_fingerprint=excluded.mirror_fingerprint,crop_4x3=excluded.crop_4x3,"
      "crop_1x1=excluded.crop_1x1,crop_9x16=excluded.crop_9x16,"
      "mirror_crop_4x3=excluded.mirror_crop_4x3,mirror_crop_1x1=excluded.mirror_crop_1x1,"
      "mirror_crop_9x16=excluded.mirror_crop_9x16";
    const char* remove="DELETE FROM files WHERE path=?";
    const char* contains="SELECT 1 FROM files WHERE path=? AND size=? AND modified=? LIMIT 1";
    sqlite3_stmt* a=nullptr;
    if(sqlite3_prepare_v2(D(db_),upsert,-1,&a,nullptr)!=SQLITE_OK) return false; upsertStmt_=a;
    if(sqlite3_prepare_v2(D(db_),remove,-1,&a,nullptr)!=SQLITE_OK){finalizeStatements();return false;} removeStmt_=a;
    if(sqlite3_prepare_v2(D(db_),contains,-1,&a,nullptr)!=SQLITE_OK){finalizeStatements();return false;} containsStmt_=a;
    return true;
}

bool Database::initialize(){
 if(!db_) return false;
 // WAL + NORMAL is appropriate for a local index: readers remain responsive while a scan/monitor writes.
 if(!exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA busy_timeout=5000; PRAGMA temp_store=MEMORY;")) return false;
 if(!exec("CREATE TABLE IF NOT EXISTS files(path TEXT PRIMARY KEY,size INTEGER NOT NULL,modified INTEGER NOT NULL,quick_hash TEXT NOT NULL,fingerprint INTEGER NOT NULL DEFAULT 0,kind INTEGER NOT NULL DEFAULT 0,duration REAL NOT NULL DEFAULT 0,mirror_fingerprint INTEGER NOT NULL DEFAULT 0,crop_4x3 INTEGER NOT NULL DEFAULT 0,crop_1x1 INTEGER NOT NULL DEFAULT 0,crop_9x16 INTEGER NOT NULL DEFAULT 0,mirror_crop_4x3 INTEGER NOT NULL DEFAULT 0,mirror_crop_1x1 INTEGER NOT NULL DEFAULT 0,mirror_crop_9x16 INTEGER NOT NULL DEFAULT 0); CREATE INDEX IF NOT EXISTS idx_files_modified ON files(modified);")) return false;
 // Migrate databases created before mirror-aware fingerprints.
 bool hasMirror=false; sqlite3_stmt* info=nullptr;
 if(sqlite3_prepare_v2(D(db_),"PRAGMA table_info(files)",-1,&info,nullptr)==SQLITE_OK){
   while(sqlite3_step(info)==SQLITE_ROW){ const auto* n=sqlite3_column_text(info,1); if(n && std::string(reinterpret_cast<const char*>(n))=="mirror_fingerprint"){hasMirror=true;break;} }
 }
 sqlite3_finalize(info);
 if(!hasMirror && !exec("ALTER TABLE files ADD COLUMN mirror_fingerprint INTEGER NOT NULL DEFAULT 0;")) return false;
 const char* cols[]={"crop_4x3","crop_1x1","crop_9x16","mirror_crop_4x3","mirror_crop_1x1","mirror_crop_9x16"};
 // One PRAGMA pass is enough for all legacy crop columns.
 bool present[6]={false,false,false,false,false,false};
 if(sqlite3_prepare_v2(D(db_),"PRAGMA table_info(files)",-1,&info,nullptr)==SQLITE_OK){
   while(sqlite3_step(info)==SQLITE_ROW){
     const auto* n=sqlite3_column_text(info,1); if(!n) continue;
     const std::string name(reinterpret_cast<const char*>(n));
     for(int i=0;i<6;++i) if(name==cols[i]) present[i]=true;
   }
 }
 sqlite3_finalize(info);
 for(int i=0;i<6;++i) if(!present[i]){
   const std::string q="ALTER TABLE files ADD COLUMN "+std::string(cols[i])+" INTEGER NOT NULL DEFAULT 0;";
   if(!exec(q.c_str())) return false;
 }
 return prepareStatements();
}

bool Database::beginTransaction(){return db_ && exec("BEGIN IMMEDIATE TRANSACTION;");}
bool Database::commitTransaction(){return db_ && exec("COMMIT;");}
bool Database::rollbackTransaction(){return db_ && exec("ROLLBACK;");}

bool Database::upsert(const FileState& x){
 if(!db_) return false;
 if(!upsertStmt_ && !prepareStatements()) return false;
 sqlite3_stmt* s=S(upsertStmt_);
 sqlite3_reset(s); sqlite3_clear_bindings(s);
 sqlite3_bind_text(s,1,x.path.c_str(),-1,SQLITE_TRANSIENT);
 sqlite3_bind_int64(s,2,(sqlite3_int64)x.size); sqlite3_bind_int64(s,3,(sqlite3_int64)x.modified);
 sqlite3_bind_text(s,4,x.quickHash.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_int64(s,5,(sqlite3_int64)x.fingerprint);
 sqlite3_bind_int(s,6,x.kind); sqlite3_bind_double(s,7,x.duration); sqlite3_bind_int64(s,8,(sqlite3_int64)x.mirrorFingerprint);
 sqlite3_bind_int64(s,9,(sqlite3_int64)x.crop4x3); sqlite3_bind_int64(s,10,(sqlite3_int64)x.crop1x1); sqlite3_bind_int64(s,11,(sqlite3_int64)x.crop9x16);
 sqlite3_bind_int64(s,12,(sqlite3_int64)x.mirrorCrop4x3); sqlite3_bind_int64(s,13,(sqlite3_int64)x.mirrorCrop1x1); sqlite3_bind_int64(s,14,(sqlite3_int64)x.mirrorCrop9x16);
 return sqlite3_step(s)==SQLITE_DONE;
}

bool Database::remove(const std::string& p){
 if(!db_) return false; if(!removeStmt_ && !prepareStatements()) return false;
 sqlite3_stmt* s=S(removeStmt_); sqlite3_reset(s); sqlite3_clear_bindings(s); sqlite3_bind_text(s,1,p.c_str(),-1,SQLITE_TRANSIENT);
 return sqlite3_step(s)==SQLITE_DONE;
}

bool Database::containsUnchanged(const FileState& x) const{
 if(!db_) return false;
 auto* self=const_cast<Database*>(this); if(!self->containsStmt_ && !self->prepareStatements()) return false;
 sqlite3_stmt* s=S(self->containsStmt_); sqlite3_reset(s); sqlite3_clear_bindings(s);
 sqlite3_bind_text(s,1,x.path.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_int64(s,2,(sqlite3_int64)x.size); sqlite3_bind_int64(s,3,(sqlite3_int64)x.modified);
 return sqlite3_step(s)==SQLITE_ROW;
}

std::vector<FileState> Database::all() const{
 std::vector<FileState> out; if(!db_) return out;
 sqlite3_stmt* s=nullptr;
 if(sqlite3_prepare_v2(D(db_),"SELECT path,size,modified,quick_hash,fingerprint,kind,duration,mirror_fingerprint,crop_4x3,crop_1x1,crop_9x16,mirror_crop_4x3,mirror_crop_1x1,mirror_crop_9x16 FROM files ORDER BY path",-1,&s,nullptr)!=SQLITE_OK) return out;
 while(sqlite3_step(s)==SQLITE_ROW){
   FileState x; x.path=(const char*)sqlite3_column_text(s,0); x.size=(std::uint64_t)sqlite3_column_int64(s,1); x.modified=(std::int64_t)sqlite3_column_int64(s,2);
   x.quickHash=(const char*)sqlite3_column_text(s,3); x.fingerprint=(std::uint64_t)sqlite3_column_int64(s,4); x.kind=sqlite3_column_int(s,5); x.duration=sqlite3_column_double(s,6);
   x.mirrorFingerprint=(std::uint64_t)sqlite3_column_int64(s,7); x.crop4x3=(std::uint64_t)sqlite3_column_int64(s,8); x.crop1x1=(std::uint64_t)sqlite3_column_int64(s,9); x.crop9x16=(std::uint64_t)sqlite3_column_int64(s,10);
   x.mirrorCrop4x3=(std::uint64_t)sqlite3_column_int64(s,11); x.mirrorCrop1x1=(std::uint64_t)sqlite3_column_int64(s,12); x.mirrorCrop9x16=(std::uint64_t)sqlite3_column_int64(s,13);
   out.push_back(std::move(x));
 }
 sqlite3_finalize(s); return out;
}
}
