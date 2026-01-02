# 内存管理修改说明1：请求分页式堆内存延迟分配（Lazy Allocation）

## 0. 背景与动机
当前内核中，用户态通过 sbrk(n) 扩展堆空间时，会在内核里立即：
- kalloc 分配物理页
- 在用户页表 pagetable 中映射
- 并在该项目中，还会在进程内核页表 kpagetable 中同步映射

问题：
- 对大 n（例如几十 MB、上百 MB）会产生大量页分配与页表操作，耗时明显；
- 很多程序会“先要一大块地址空间但只用其中少量”（稀疏数组/提前预留），导致物理内存被浪费；
- 本项目 QEMU 默认内存小、PHYSTOP 更小，导致大 sbrk 直接失败，影响可用性。

## 1. 改进点
采用 请求分页（Demand Paging）思想实现“延迟分配”：
- sbrk(n) 只调整进程逻辑大小 myproc()->sz，不立即分配物理页；
- 访问未映射页时触发缺页异常（page fault），由内核缺页处理程序分配物理页并映射。

这属于请求分页存储管理模式：缺页中断 → 分配/装入 → 更新页表 → 重新执行指令。

说明：本次改进不引入“页面置换/换出到磁盘”，但为后续加入 CLOCK/FIFO/LRU 等置换算法与工作集控制留下接口位置。

## 2. 设计约束（与现有内核结构对齐）
本项目与标准 xv6 的关键不同：
- 每进程存在 kpagetable（内核态运行时使用），并把用户页也同步映射进 kpagetable，便于 copyin2/copyout2 直接 memmove。
  因此：缺页时必须把新页同时映射到：
- p->pagetable（用户态）
- p->kpagetable（内核态）

## 3. 关键行为定义
### 3.1 sbrk(n)
- n > 0：仅 p->sz += n，返回 oldsz
- n < 0：调用 uvmdealloc(pagetable, kpagetable, oldsz, oldsz+n) 释放已映射页；未映射页也应允许“跳过”
- 若 newsz 超过 MAXUVA 或溢出，返回 -1

### 3.2 缺页处理（usertrap）
当 scause 为 13/15（load/store page fault）且：
- va < p->sz 且 va < MAXUVA：
    - 若是 COW 页：走 cow_alloc()
    - 否则：走 lazy_alloc()，分配新页并映射
- 否则：非法访问，kill 进程

### 3.3 系统调用读写用户缓冲区
典型情形：进程 sbrk 得到一段地址，但尚未触发缺页；随后把这段地址传给 read()/write()/pipe 等系统调用。
要求：内核 copyout/copyin/copyinstr（以及本项目的 copyout2/copyin2/copyinstr2）在遇到未映射页时，必须：
- 若地址在 p->sz 范围内：先 lazy_alloc 再继续拷贝
- 否则：失败返回 -1

### 3.4 fork/uvmcopy 与 COW 的协同
uvmcopy 必须允许父进程地址空间中存在“尚未映射的页”（lazy 空洞）：
- 若 walk 找不到 pte 或 pte 无效：跳过（子进程也保持未映射）
- 若是有效页：按原逻辑映射，并对可写页设置 COW 标记、清除写位、增加 refcount

### 3.5 vmunmap/uvmunmap 行为
释放虚拟地址区间时：
- 如果某页根本没映射（lazy 空洞），不应 panic，直接跳过即可

## 4. 涉及文件与改动概览
### kernel/sysproc.c
- 修改 sys_sbrk：去掉对 growproc 的正向分配，保留负向 uvmdealloc

### kernel/trap.c
- 在 usertrap 中新增：缺页异常 → COW 或 lazy_alloc
- 地址越界或 OOM → kill

### kernel/vm.c
- 新增 lazy_alloc(pagetable, kpagetable, va)
- 修改 vmunmap：未映射页不 panic
- 修改 uvmcopy：跳过未映射页
- 修改 copyout/copyin/copyinstr：遇到未映射页且在 sz 内 → lazy_alloc
- 修改 copyout2/copyin2/copyinstr2：不要直接 memmove；改成调用 copyout/copyin/copyinstr（从而自动支持 lazy/COW）

### xv6-user/lazytest.c（新增）
- 覆盖：大 sbrk 不触碰也能成功；远端页首次触碰触发缺页；read 写入未触碰页；fork 下未映射页语义；负 sbrk 收缩不崩溃

### Makefile
- UPROGS 增加 $U/_lazytest

## 5. 预期收益（显著效果）
- 大 sbrk 从“分配几十万页”变成 O(1) 调整 sz，延迟到真正访问时再分配；
- 物理内存占用与实际触碰页数一致，支持稀疏大地址空间；
- fork 在父进程存在大量“未触碰页”的情况下更轻量（uvmcopy 直接跳过）。

## 6. 风险与回归策略
风险点：
- copyout2/copyin2 若仍直接 memmove，会在内核态访问未映射页导致内核 trap/panic；
- vmunmap 若仍对未映射页 panic，负 sbrk 或进程退出释放会崩。
  回归：
- 恢复 sys_sbrk 使用 growproc 的旧实现；
- 去掉 lazy_alloc 分支。

## 7. 验证方式
- make run（QEMU）
- 在 shell 中运行：
    - lazytest
    - cowtest
    - usertests
      均应 PASS。
