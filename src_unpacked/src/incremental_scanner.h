#pragma once
#include "database.h"
#include <vector>
namespace msf {
class IncrementalScanner {
public:
    ChangeSet classify(const std::vector<FileState>& current,
                       const std::vector<FileState>& previous) const;
};
}
