// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13
#define HASH(dev, blockno) ((((dev)<<27)|(blockno))%NBUCKET)

struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  struct bucket {
    struct spinlock lock;
    struct buf head;
  } bucket[NBUCKET];
} bcache;

void
binit(void)
{
  initlock(&bcache.lock, "bcache");

  for(int i = 0; i < NBUCKET; i++){
    initlock(&bcache.bucket[i].lock, "bcache.bucket");
    bcache.bucket[i].head.prev = &bcache.bucket[i].head;
    bcache.bucket[i].head.next = &bcache.bucket[i].head;
  }

  for(int i = 0; i < NBUF; i++){
    struct buf *b = &bcache.buf[i];
    int id = i % NBUCKET;
    b->next = bcache.bucket[id].head.next;
    b->prev = &bcache.bucket[id].head;
    initsleeplock(&b->lock, "buffer");
    bcache.bucket[id].head.next->prev = b;
    bcache.bucket[id].head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int hash = HASH(dev, blockno);

  acquire(&bcache.bucket[hash].lock);

  // Is the block already cached?
  for(b = bcache.bucket[hash].head.next;
      b != &bcache.bucket[hash].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucket[hash].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache.bucket[hash].lock);

  // Not cached. Serialize eviction.
  acquire(&bcache.lock);
  acquire(&bcache.bucket[hash].lock);

  // Double-check after acquiring global lock.
  for(b = bcache.bucket[hash].head.next;
      b != &bcache.bucket[hash].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucket[hash].lock);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Find LRU victim across all buckets.
  struct buf *victim = 0;
  int victim_bucket = -1;

  for(int i = 0; i < NBUCKET; i++){
    if(i == hash) continue;
    acquire(&bcache.bucket[i].lock);
    for(b = bcache.bucket[i].head.next;
        b != &bcache.bucket[i].head; b = b->next){
      if(b->refcnt == 0 && (!victim || b->timestamp < victim->timestamp)){
        victim = b;
        victim_bucket = i;
      }
    }
    release(&bcache.bucket[i].lock);
  }

  for(b = bcache.bucket[hash].head.next;
      b != &bcache.bucket[hash].head; b = b->next){
    if(b->refcnt == 0 && (!victim || b->timestamp < victim->timestamp)){
      victim = b;
      victim_bucket = hash;
    }
  }

  if(!victim)
    panic("bget: no buffers");

  // Remove victim from its current bucket (if different from target).
  if(victim_bucket != hash){
    acquire(&bcache.bucket[victim_bucket].lock);
    if(victim->refcnt != 0){
      release(&bcache.bucket[victim_bucket].lock);
      release(&bcache.bucket[hash].lock);
      release(&bcache.lock);
      return bget(dev, blockno);
    }
    victim->next->prev = victim->prev;
    victim->prev->next = victim->next;
    victim->next = bcache.bucket[hash].head.next;
    victim->prev = &bcache.bucket[hash].head;
    bcache.bucket[hash].head.next->prev = victim;
    bcache.bucket[hash].head.next = victim;
    release(&bcache.bucket[victim_bucket].lock);
  }

  victim->dev = dev;
  victim->blockno = blockno;
  victim->refcnt = 1;
  victim->valid = 0;
  victim->disk = 0;

  release(&bcache.bucket[hash].lock);
  release(&bcache.lock);

  acquiresleep(&victim->lock);
  return victim;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int hash = HASH(b->dev, b->blockno);
  acquire(&bcache.bucket[hash].lock);
  b->refcnt--;
  if(b->refcnt == 0){
    b->timestamp = ticks;
  }
  release(&bcache.bucket[hash].lock);
}

void
bpin(struct buf *b) {
  int hash = HASH(b->dev, b->blockno);
  acquire(&bcache.bucket[hash].lock);
  b->refcnt++;
  release(&bcache.bucket[hash].lock);
}

void
bunpin(struct buf *b) {
  int hash = HASH(b->dev, b->blockno);
  acquire(&bcache.bucket[hash].lock);
  b->refcnt--;
  release(&bcache.bucket[hash].lock);
}


