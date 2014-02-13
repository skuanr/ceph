
#include "mds/InoUtility.h"
#include "mds/CInode.h"
#include "common/Mutex.h"
#include "mds/inode_backtrace.h"


#define dout_subsys ceph_subsys_mds


void InoUtility::by_path(std::string const &path)
{

}


void InoUtility::by_id(inodeno_t const id)
{
  // Go get the backtrace object, then follow

  // Resolve inode ID to object ID
  // TODO: cope with fragmented directory, have to get multiple
  // objects;
  // TODO: what's suffix to get_object_name and when would I need it?
  object_t oid = CInode::get_object_name(id, frag_t(), "");
  object_locator_t oloc(mdsmap->get_metadata_pool());

  // RADOS xattr read to obtain backtrace
  bufferlist parent_bl;
  ObjectOperation rd_parent;

  // FIXME: this is a faff, can I use synchronous librados ops or something?
  int r;
  bool done = false;
  Cond cond;

  // We need a separate lock for use with SafeCond, because it
  // takes the lock in completion, and we already hold .lock
  // while dispatching messages causing completion.
  Mutex localLock("dump:lock");
  C_SafeCond *c = new C_SafeCond(&localLock, &cond, &done, &r);

  rd_parent.getxattr("parent", &parent_bl, NULL);

  dout(4) << "Sending xattr read for object " << oid << dendl;

  // Take the lock while doing remote ops.
  lock.Lock();
  objecter->read(oid, oloc, rd_parent, CEPH_NOSNAP, NULL, 0, c);
  lock.Unlock();

  // Wait for xattr read
  localLock.Lock();
  while (!done) {
    cond.Wait(localLock);
  }
  localLock.Unlock();
  if (r < 0) {
    dout(0) << "Error getting xattr: " << r << dendl;
    return;
  }

  inode_backtrace_t backtrace;
  ::decode(backtrace, parent_bl);

  JSONFormatter jf(true);
  jf.open_object_section("backtrace");
  backtrace.dump(&jf);
  jf.close_section();
  jf.flush(std::cout);
  std::cout << std::endl;

// // {"ino":1099511635765,"ancestors":[
//  {"dirino":1099511635187,"dname":"577","version":1156},
//  {"dirino":1,"dname":"3","version":52181}],"pool":1,"old_pools":[]
//}

  // Go get the root inode



  // Tell me about my ancestors
  std::string path = "/";
  std::vector<inode_backpointer_t>::reverse_iterator i;
  for (i = backtrace.ancestors.rbegin(); i != backtrace.ancestors.rend(); ++i) {
      // Resolve dirino+dname into a fragment ID
      path += i->dname + "/";

      //
  }

  std::cout << "Path: " << path << std::endl;

//
//  // Decode the inode object
//  inode_t ino;
//  ino.decode(bl);
//
//  // Print the object
//  JSONFormatter jf;
//  jf.open_object_section("inode");
//  ino.dump(&jf);
//  jf.close_section();
//  jf.flush(std::cout);
}
