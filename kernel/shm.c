#include "include/types.h"
#include "include/param.h"
#include "include/memlayout.h"
#include "include/riscv.h"
#include "include/spinlock.h"
#include "include/proc.h"
#include "include/syscall.h"
#include "include/kalloc.h"
#include "include/vm.h"
#include "include/string.h"
#include "include/printf.h"

// 最大共享内存段数量
#define NSHM 16

// shmctl 命令
#define SHM_RMID 1  // 删除共享内存段

// 共享内存段描述符
struct shm_segment {
  int id;              // 共享内存标识符
  int key;             // 用户提供的键值
  uint64 pa;           // 物理地址
  uint64 size;         // 大小（字节）
  int ref_count;       // 附加进程计数
  int perm;            // 权限标志
  int used;            // 是否使用
};

// 全局共享内存表
struct {
  struct spinlock lock;
  struct shm_segment segments[NSHM];
  int next_id;
} shm_table;

// 初始化共享内存系统
void
shm_init(void)
{
  initlock(&shm_table.lock, "shm");
  for (int i = 0; i < NSHM; i++) {
    shm_table.segments[i].used = 0;
  }
  shm_table.next_id = 1;
}

// 根据 key 查找共享内存段
static struct shm_segment*
shm_lookup_key(int key)
{
  for (int i = 0; i < NSHM; i++) {
    if (shm_table.segments[i].used && shm_table.segments[i].key == key) {
      return &shm_table.segments[i];
    }
  }
  return 0;
}

// 根据 id 查找共享内存段
static struct shm_segment*
shm_lookup_id(int shmid)
{
  for (int i = 0; i < NSHM; i++) {
    if (shm_table.segments[i].used && shm_table.segments[i].id == shmid) {
      return &shm_table.segments[i];
    }
  }
  return 0;
}

// 分配一个新的共享内存段
static struct shm_segment*
shm_alloc(void)
{
  for (int i = 0; i < NSHM; i++) {
    if (!shm_table.segments[i].used) {
      return &shm_table.segments[i];
    }
  }
  return 0;
}

// shmget: 创建或获取共享内存段
// key: 键值，IPC_PRIVATE 表示创建新的
// size: 大小
// flag: 创建标志（如 IPC_CREAT）
int
do_shmget(int key, uint64 size, int flag)
{
  struct shm_segment *seg;

  acquire(&shm_table.lock);

  // 查找已存在的共享内存
  if (key != 0) {
    seg = shm_lookup_key(key);
    if (seg) {
      // 共享内存已存在，返回其 ID
      int id = seg->id;
      release(&shm_table.lock);
      return id;
    }
  }

  // 创建新的共享内存段
  seg = shm_alloc();
  if (!seg) {
    release(&shm_table.lock);
    return -1;  // 没有可用槽位
  }

  // 计算需要的页面数
  int npages = (size + PGSIZE - 1) / PGSIZE;
  if (npages == 0) npages = 1;

  // 分配物理内存
  char *mem = kalloc();
  if (!mem) {
    release(&shm_table.lock);
    return -1;  // 内存不足
  }
  memset(mem, 0, PGSIZE);

  // 初始化共享内存段
  seg->id = shm_table.next_id++;
  seg->key = key;
  seg->pa = (uint64)mem;
  seg->size = npages * PGSIZE;
  seg->ref_count = 0;
  seg->perm = flag;
  seg->used = 1;

  int shmid = seg->id;
  release(&shm_table.lock);

  return shmid;
}

// shmat: 将共享内存附加到进程地址空间
// shmid: 共享内存 ID
// addr: 期望的附加地址（0 表示由系统选择）
// flag: 标志（如 SHM_RDONLY）
void*
do_shmat(int shmid, uint64 addr, int flag)
{
  struct shm_segment *seg;
  struct proc *p = myproc();
  uint64 va;

  acquire(&shm_table.lock);

  seg = shm_lookup_id(shmid);
  if (!seg) {
    release(&shm_table.lock);
    return (void*)-1;
  }

  // 分配虚拟地址空间（附加到进程堆的末尾）
  va = PGROUNDUP(p->sz);

  // 将共享内存映射到进程地址空间
  // 使用 mappages 建立物理页到虚拟页的映射
  // 允许读写
  if (mappages(p->pagetable, va, seg->size, seg->pa,
               PTE_W | PTE_R | PTE_U) != 0) {
    release(&shm_table.lock);
    return (void*)-1;
  }

  // 更新进程内存大小
  p->sz += seg->size;

  // 增加引用计数
  seg->ref_count++;

  release(&shm_table.lock);

  return (void*)va;
}

// shmdt: 从进程地址空间分离共享内存
// addr: 共享内存附加的地址
int
do_shmdt(uint64 addr)
{
  struct proc *p = myproc();
  struct shm_segment *seg = 0;
  uint64 va = addr;
  int found = 0;

  acquire(&shm_table.lock);

  // 查找对应的共享内存段
  for (int i = 0; i < NSHM; i++) {
    if (shm_table.segments[i].used) {
      seg = &shm_table.segments[i];
      // 检查这个地址是否在我们的共享内存范围内
      // 简化：假设 addr 是附加时的返回地址
      // 需要找到对应的共享内存段
      pte_t *pte = walk(p->pagetable, va, 0);
      if (pte && (*pte & PTE_V)) {
        uint64 pa = PTE2PA(*pte);
        if (pa == seg->pa) {
          found = 1;
          break;
        }
      }
    }
  }

  if (!found || !seg) {
    release(&shm_table.lock);
    return -1;
  }

  // 解除映射
  vmunmap(p->pagetable, va, seg->size / PGSIZE, 0);

  // 更新进程内存大小
  if (p->sz > seg->size) {
    p->sz -= seg->size;
  } else {
    p->sz = 0;
  }

  // 减少引用计数
  seg->ref_count--;

  release(&shm_table.lock);

  return 0;
}

// shmctl: 控制操作
// shmid: 共享内存 ID
// cmd: 命令（SHM_RMID 删除）
// buf: 缓冲区（未使用）
int
do_shmctl(int shmid, int cmd, void *buf)
{
  struct shm_segment *seg;
  (void)buf;  // 未使用

  acquire(&shm_table.lock);

  seg = shm_lookup_id(shmid);
  if (!seg) {
    release(&shm_table.lock);
    return -1;
  }

  if (cmd == SHM_RMID) {
    // 删除共享内存段
    // 只有当没有进程附加时才能删除
    if (seg->ref_count > 0) {
      release(&shm_table.lock);
      return -1;
    }

    // 释放物理内存
    kfree((void*)seg->pa);

    // 标记为未使用
    seg->used = 0;
  }

  release(&shm_table.lock);

  return 0;
}
