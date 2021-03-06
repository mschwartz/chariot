#include <dev/driver.h>
#include <errno.h>
#include <fs.h>
#include <fs/ext2.h>

#define EXT2_ADDR_PER_BLOCK(node) (node->block_size / sizeof(u32))

static int ilog2(int x) {
  /*
   * Find the leftmost 1. Use a method that is similar to
   * binary search.
   */
  int result = 0;
  result = (!!(x >> 16)) << 4;  // if > 16?
  // based on previous result, if > (result + 8)
  result = result + ((!!(x >> (result + 8))) << 3);
  result = result + ((!!(x >> (result + 4))) << 2);
  result = result + ((!!(x >> (result + 2))) << 1);
  result = result + (!!(x >> (result + 1)));
  return result;
}

struct block_reader {
  fs::ext2_inode &node;

  int last_path[4];

 public:
  block_reader(fs::ext2_inode &node) : node(node) {}
};

#define EXT2_NDIR_BLOCKS 12
#define EXT2_IND_BLOCK EXT2_NDIR_BLOCKS
#define EXT2_DIND_BLOCK (EXT2_IND_BLOCK + 1)
#define EXT2_TIND_BLOCK (EXT2_DIND_BLOCK + 1)
#define EXT2_N_BLOCKS (EXT2_TIND_BLOCK + 1)
/**
 * block_to_path - given a block number, solve the indirect path to it
 *
 * @node: the inode you are looking into. This argument exists to get metadata,
 *        and no IO occurs
 * @block: the block index to find the path of
 * @offsets: the indirect path array to fill in
 */
static int block_to_path(fs::inode *node, int i_block, int offsets[4],
                         int *boundary = nullptr) {
  int ptrs = EXT2_ADDR_PER_BLOCK(node);
  int ptrs_bits = ilog2(ptrs);

  // how many block pointers are in each indirect block
  const long direct_blocks = 12;  // 12 direct blocks
  const long indirect_blocks = ptrs;
  const long double_blocks = (1 << (ptrs_bits * 2));

  int n = 0;
  int final = 0;

  if (i_block < 0) {
    printk("[EXT2 WARN] %s: block < 0\n", __func__);
  } else if (i_block < direct_blocks) {
    offsets[n++] = i_block;
    final = direct_blocks;
  } else if ((i_block -= direct_blocks) < indirect_blocks) {
    offsets[n++] = EXT2_IND_BLOCK;
    offsets[n++] = i_block;
    final = ptrs;
  } else if ((i_block -= indirect_blocks) < double_blocks) {
    offsets[n++] = EXT2_DIND_BLOCK;
    offsets[n++] = i_block >> ptrs_bits;
    offsets[n++] = i_block & (ptrs - 1);
    final = ptrs;
  } else if (((i_block -= double_blocks) >> (ptrs_bits * 2)) < ptrs) {
    offsets[n++] = EXT2_TIND_BLOCK;
    offsets[n++] = i_block >> (ptrs_bits * 2);
    offsets[n++] = (i_block >> ptrs_bits) & (ptrs - 1);
    offsets[n++] = i_block & (ptrs - 1);
    final = ptrs;
  } else {
    printk("[EXT2 WARN] %s: block is too big!\n", __func__);
  }

  if (boundary) *boundary = final - 1 - (i_block & (ptrs - 1));
  return n;
}

fs::ext2_inode::ext2_inode(int type, u32 index) : fs::inode(type) {
  this->ino = index;
}

fs::ext2_inode::~ext2_inode() {
  auto p = priv<ext2_idata>();
  for (int i = 0; i < 4; i++) {
    if (p->blk_bufs[i] != nullptr) {
      kfree(p->blk_bufs[i]);
    }
  }
}

int fs::ext2_inode::touch(string name, int mode, fs::inode *&dst) {
  if (type != T_DIR) return -ENOTDIR;

  printk("ext2 touch\n");
  return -EINVAL;
}

int block_from_index(fs::inode &node, int i_block, int set_to = 0) {
  auto efs = (fs::ext2 *)node.fs;

  auto bsize = efs->blocksize;

  auto p = node.priv<fs::ext2_idata>();
  // start the inodeS
  auto table = (int *)p->block_pointers;
  int path[4];
  int n = block_to_path(&node, i_block, path);

  for (int i = 0; i < n - 1; i++) {
    int off = path[i];
    if (p->blk_bufs[i] == NULL || p->cached_path[i] != off) {
      if (p->blk_bufs[i] == NULL) p->blk_bufs[i] = (int *)kmalloc(bsize);
      if (!efs->read_block(table[off], p->blk_bufs[i])) {
        return 0;
      }
      p->cached_path[i] = off;
    }
    table = p->blk_bufs[i];
  }

  return table[path[n - 1]];
}

static int injest_info(fs::inode *ino, fs::ext2_inode_info &info) {
  auto efs = (fs::ext2 *)ino->fs;
  ino->size = info.size;
  ino->mode = info.type;
  ino->uid = info.uid;
  ino->gid = info.gid;
  ino->link_count = info.hardlinks;
  ino->atime = info.last_access;
  ino->ctime = info.create_time;
  ino->mtime = info.last_modif;
  ino->dtime = info.delete_time;

  ino->block_size = efs->blocksize;

  auto table = (u32 *)info.dbp;
  // copy the block pointers
  for (int i = 0; i < 15; i++) {
    ino->priv<fs::ext2_idata>()->block_pointers[i] = table[i];
  }

  if (ino->type == T_DIR) {
    // TODO: clear out the dirents
    efs->traverse_dir(ino->ino, [&](u32 nr, const char *name) -> bool {
      // register the resident entries
      ino->register_direntry(name, ENT_RES);
      return true;
    });
  } else if (ino->type == T_CHAR || ino->type == T_BLK) {
    // parse the major/minor
    unsigned dev = info.dbp[0];
    if (!dev) dev = info.dbp[1];
    ino->major = (dev & 0xfff00) >> 8;
    ino->minor = (dev & 0xff) | ((dev >> 12) & 0xfff00);
  }
  return 0;
}

int fs::ext2_inode::commit_info(void) {
  fs::ext2_inode_info info;

  auto efs = (fs::ext2 *)fs;

  efs->read_inode(info, ino);

  info.size = size;
  info.uid = uid;
  info.gid = gid;
  info.hardlinks = link_count;
  info.last_access = atime;
  info.create_time = ctime;
  mtime = info.last_modif = dev::RTC::now();

  info.type = (info.type & ~0xFFF) | (mode & 0xFFF);

  // ??
  info.delete_time = dtime;

  // copy the block pointers
  auto table = (u32 *)info.dbp;
  for (int i = 0; i < 15; i++) table[i] = priv<ext2_idata>()->block_pointers[i];

  efs->write_inode(info, ino);

  return 0;
}

#define UNIMPL() printk("[ext2] '%s' NOT IMPLEMENTED\n", __PRETTY_FUNCTION__)

static int ext2_seek(fs::file &, off_t, off_t) {
  return 0;  // allow seek
}

static ssize_t ext2_do_read_write(fs::file &f, char *buf, size_t nbytes,
                                  bool is_write) {
  auto efs = (fs::ext2 *)f.ino->fs;
  if (is_write) {
    // TODO: ensure blocks are avail
  }

  ssize_t offset = f.offset();
  if (offset > f.ino->size) return 0;

  // how many bytes have been read
  ssize_t nread = 0;

  // the size of a single block
  auto bsize = efs->blocksize;

  off_t first_blk_ind = offset / bsize;
  off_t last_blk_ind = (offset + nbytes) / bsize;
  off_t offset_into_first_block = offset % bsize;

  // TODO: lock the FS. We now want to own the efs->work_buf
  auto *given_buf = (u8 *)buf;

  int remaining_count = min((off_t)nbytes, (off_t)f.ino->size - offset);

  for (int bi = first_blk_ind; remaining_count && bi <= last_blk_ind; bi++) {
    u32 blk = block_from_index(*f.ino, bi);
    if (blk == 0) {
      printk("ext2fs: read_bytes: failed at lbi %u\n", bi);
      return -EIO;
    }

    auto *buf = (u8 *)efs->work_buf;
    efs->read_block(blk, buf);

    int offset_into_block = (bi == first_blk_ind) ? offset_into_first_block : 0;
    int num_bytes_to_copy = min(bsize - offset_into_block, remaining_count);

    if (is_write) {
      memcpy(buf + offset_into_block, given_buf, num_bytes_to_copy);
      efs->write_block(blk, buf);
    } else {
      memcpy(given_buf, buf + offset_into_block, num_bytes_to_copy);
    }
    remaining_count -= num_bytes_to_copy;
    nread += num_bytes_to_copy;
    given_buf += num_bytes_to_copy;
  }
  f.seek(nread, SEEK_CUR);
  return nread;
  return -ENOTIMPL;
}

static ssize_t ext2_read(fs::file &f, char *dst, size_t sz) {
  if (f.ino->type != T_FILE) return -EINVAL;
  return ext2_do_read_write(f, dst, sz, false);
}

static ssize_t ext2_write(fs::file &f, const char *src, size_t sz) {
  if (f.ino->type != T_FILE) return -EINVAL;
  return ext2_do_read_write(f, (char *)src, sz, true);
}

static int ext2_ioctl(fs::file &, unsigned int, off_t) {
  UNIMPL();
  return -ENOTIMPL;
}

static int ext2_open(fs::file &) { return 0; }
static void ext2_close(fs::file &) {}

static int ext2_mmap(fs::file &, struct mm::area &) {
  UNIMPL();
  return -ENOTIMPL;
}

static int ext2_resize(fs::file &, size_t) {
  UNIMPL();
  return -ENOTIMPL;
}

fs::ext2_idata::~ext2_idata() {
  for (int i = 0; i < 4; i++) {
    if (blk_bufs[i] != nullptr) {
      kfree(blk_bufs[i]);
    }
  }
}

static void ext2_destroy_priv(fs::inode &v) { delete v.priv<fs::ext2_idata>(); }

fs::file_operations ext2_file_ops{
    .seek = ext2_seek,
    .read = ext2_read,
    .write = ext2_write,
    .ioctl = ext2_ioctl,
    .open = ext2_open,
    .close = ext2_close,
    .mmap = ext2_mmap,
    .resize = ext2_resize,
    .destroy = ext2_destroy_priv,
};

static int ext2_create(fs::inode &, const char *name,
                       struct fs::file_ownership &) {
  UNIMPL();
  return -ENOTIMPL;
}

static int ext2_mkdir(fs::inode &, const char *name,
                      struct fs::file_ownership &) {
  UNIMPL();
  return -ENOTIMPL;
}

static int ext2_unlink(fs::inode &, const char *) {
  UNIMPL();
  return -ENOTIMPL;
}

static struct fs::inode *ext2_lookup(fs::inode &node, const char *needle) {
  if (node.type != T_DIR) panic("ext2_lookup on non-dir\n");

  bool found = false;
  auto efs = (fs::ext2 *)node.fs;
  u32 ent_inode_num;
  efs->traverse_dir(node.ino, [&](u32 ino, const char *name) -> bool {
    if (!strcmp(needle, name)) {
      ent_inode_num = ino;
      found = true;
      return false;
    }
    return true;
  });

  if (found) return efs->get_inode(ent_inode_num);
  return NULL;
}

static int ext2_mknod(fs::inode &, const char *name,
                      struct fs::file_ownership &, int major, int minor) {
  UNIMPL();
  return -ENOTIMPL;
}

static int ext2_walk(fs::inode &, func<bool(const string &)>) {
  UNIMPL();
  return -ENOTIMPL;
}

fs::dir_operations ext2_dir_ops{
    .create = ext2_create,
    .mkdir = ext2_mkdir,
    .unlink = ext2_unlink,
    .lookup = ext2_lookup,
    .mknod = ext2_mknod,
    .walk = ext2_walk,
};

fs::ext2_inode *fs::ext2_inode::create(ext2 *fs, u32 index) {
  fs::ext2_inode *ino = NULL;

  struct ext2_inode_info info;

  fs->read_inode(info, index);

  int ino_type = T_INVA;

  auto type = ((info.type) & 0xF000) >> 12;

  if (type == 0x1) ino_type = T_FIFO;
  if (type == 0x2) ino_type = T_CHAR;
  if (type == 0x4) ino_type = T_DIR;
  if (type == 0x6) ino_type = T_BLK;
  if (type == 0x8) ino_type = T_FILE;
  if (type == 0xA) ino_type = T_SYML;
  if (type == 0xC) ino_type = T_SOCK;

  ino = new ext2_inode(ino_type, index);
  ino->priv<ext2_idata>() = new ext2_idata();

  ino->dev.major = fs->disk->ino->major;
  ino->dev.minor = fs->disk->ino->minor;
  ino->fops = &ext2_file_ops;
  ino->dops = &ext2_dir_ops;
  ino->fs = fs;

  injest_info(ino, info);

  if (ino_type == T_CHAR || ino_type == T_BLK) {
    ino->fops = dev::get(ino->major);
    ino->dops = NULL;
  }

  return ino;
}
