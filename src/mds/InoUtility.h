
#ifndef INO_UTILITY_H_
#define INO_UTILITY_H_

#include "mds/MDSUtility.h"


/**
 * Utility for inspecting inode/directory fragment
 * objects directly from RADOS (requires the objects
 * of interest are flushed to first class objects and
 * are not still waiting in the journal: flush your
 * journal before running this).
 */
class InoUtility : public MDSUtility {
public:
  void by_path(std::string const &path);
  void by_id(inodeno_t const id);
};

#endif // INO_UTILITY_H_
