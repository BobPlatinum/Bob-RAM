#include "kernel/include/types.h"
#include "kernel/include/stat.h"
#include "kernel/include/fcntl.h"
#include "user.h"

// 共享内存测试程序
// 测试进程间通过共享内存进行通信

#define SHM_KEY 1234
#define SHM_SIZE 4096

int main(int argc, char *argv[])
{
  int shmid;
  char *shm_ptr;
  int pid;
  int i;

  printf("=== 共享内存 IPC 测试程序 ===\n");

  // 创建共享内存
  shmid = shmget(SHM_KEY, SHM_SIZE, IPC_CREAT);
  if (shmid < 0) {
    printf("shmget 失败!\n");
    exit(1);
  }
  printf("[父进程] 创建共享内存成功, shmid = %d\n", shmid);

  // 将共享内存附加到进程地址空间
  shm_ptr = (char*)shmat(shmid, 0, 0);
  if ((int)shm_ptr < 0) {
    printf("shmat 失败!\n");
    exit(1);
  }
  printf("[父进程] 附加共享内存成功, addr = %p\n", shm_ptr);

  // 父进程写入初始数据
  printf("[父进程] 写入初始数据...\n");
  for (i = 0; i < 10; i++) {
    shm_ptr[i] = 'A' + i;
  }
  shm_ptr[10] = '\0';
  printf("[父进程] 写入数据: %s\n", shm_ptr);

  // 创建子进程
  pid = fork();
  if (pid < 0) {
    printf("fork 失败!\n");
    exit(1);
  }

  if (pid == 0) {
    // 子进程
    printf("\n[子进程] 启动 (PID = %d)\n", getpid());

    // 等待一下，确保父进程已经写入数据
    sleep(1);

    // 子进程附加同一块共享内存
    char *child_shm = (char*)shmat(shmid, 0, 0);
    if ((int)child_shm < 0) {
      printf("[子进程] shmat 失败!\n");
      exit(1);
    }
    printf("[子进程] 附加共享内存成功, addr = %p\n", child_shm);

    // 读取父进程写入的数据
    printf("[子进程] 读取到父进程写入的数据: %s\n", child_shm);

    // 子进程修改数据
    printf("[子进程] 修改共享内存数据...\n");
    for (i = 0; i < 10; i++) {
      child_shm[i] = 'a' + i;
    }
    child_shm[10] = '\0';
    printf("[子进程] 修改后的数据: %s\n", child_shm);

    // 分离共享内存
    if (shmdt((uint64)child_shm) < 0) {
      printf("[子进程] shmdt 失败!\n");
    } else {
      printf("[子进程] 分离共享内存成功\n");
    }

    printf("[子进程] 退出\n");
    exit(0);
  }

  // 父进程等待子进程
  wait(0);

  // 父进程读取子进程修改后的数据
  printf("\n[父进程] 读取子进程修改后的数据: %s\n", shm_ptr);

  // 分离共享内存
  if (shmdt((uint64)shm_ptr) < 0) {
    printf("[父进程] shmdt 失败!\n");
  } else {
    printf("[父进程] 分离共享内存成功\n");
  }

  // 删除共享内存
  if (shmctl(shmid, SHM_RMID, 0) < 0) {
    printf("[父进程] shmctl 删除失败!\n");
  } else {
    printf("[父进程] 删除共享内存成功\n");
  }

  printf("\n=== 共享内存 IPC 测试完成 ===\n");
  exit(0);
}
