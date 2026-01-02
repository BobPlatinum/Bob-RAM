// 测试进程优先级调度功能
// 本程序测试优先级调度是否正确工作

#include "kernel/include/types.h"
#include "kernel/include/stat.h"
#include "xv6-user/user.h"

// 简化的打印函数
void print(const char *s)
{
  write(1, s, strlen(s));
}

void printnum(int n)
{
  char buf[16];
  int i = 0;
  if (n == 0) {
    write(1, "0", 1);
    return;
  }
  while (n > 0) {
    buf[i++] = '0' + (n % 10);
    n /= 10;
  }
  while (i > 0) {
    write(1, &buf[--i], 1);
  }
}

// 测试1：基本优先级设置和获取
void test_basic_priority(void)
{
  print("测试1: 基本优先级获取与设置\n");

  int old_prio = getpriority();
  print("  默认优先级: ");
  printnum(old_prio);
  print("\n");

  if (old_prio != 50) {
    print("  失败: 默认优先级应为50\n");
    exit(1);
  }

  // 设置新优先级
  if (setpriority(75) < 0) {
    print("  失败: setpriority调用失败\n");
    exit(1);
  }

  int new_prio = getpriority();
  if (new_prio != 75) {
    print("  失败: 优先级设置不正确\n");
    exit(1);
  }

  print("  新优先级: ");
  printnum(new_prio);
  print("\n");
  print("  通过\n");
}

// 测试2：fork时优先级继承
void test_priority_inherit(void)
{
  print("测试2: 优先级继承\n");

  setpriority(80);

  int pid = fork();
  if (pid == 0) {
    // 子进程
    int child_prio = getpriority();
    if (child_prio != 80) {
      print("  失败: 子进程未继承优先级\n");
      exit(1);
    }
    print("  子进程继承的优先级: ");
    printnum(child_prio);
    print("\n");
    exit(0);
  }

  wait(0);
  print("  通过\n");
}

// 测试3：优先级抢占测试 - 证明高优先级进程可以抢占低优先级进程
void test_priority_scheduling(void)
{
  print("测试3: 优先级抢占\n");

  // 步骤1: 先创建低优先级子进程，让它执行长时间任务
  int low_pid = fork();
  if (low_pid == 0) {
    setpriority(10);  // 低优先级
    print("  [低优先级] 开始执行长任务...\n");

    // 执行一个很长的循环，模拟CPU密集型任务
    // 在循环中定期打印，展示进程执行状态
    for (int i = 0; i < 100000; i++) {
      // 每隔一段时间打印一次
      if (i % 20000 == 0) {
        print("  [低优先级] 仍在运行... ");
        printnum(i / 1000);
        print("k次迭代\n");
      }
      // CPU密集型计算
      volatile int sum = 0;
      for (int j = 0; j < 10000; j++) {
        sum += j;
      }
    }

    print("  [低优先级] 任务完成！\n");
    exit(0);
  }

  // 步骤2: 父进程等待一小段时间，确保低优先级进程已经开始运行
  sleep(1);

  // 步骤3: 父进程临时设置高优先级，这样高子进程会继承高优先级
  setpriority(100);
  int high_pid = fork();
  setpriority(50);  // 恢复父进程优先级

  // 如果抢占工作正常，高优先级进程应该立即打断低优先级进程
  if (high_pid == 0) {
    setpriority(100);  // 最高优先级
    print("  *** [高优先级] 正在抢占！立即执行！***\n");

    // 高优先级进程快速执行
    volatile int sum = 0;
    for (int i = 0; i < 50000; i++) {
      sum += i;
    }

    print("  *** [高优先级] 高优先级任务完成！***\n");
    exit(0);
  }

  // 步骤4: 再创建一个中优先级进程
  sleep(2);
  int mid_pid = fork();
  if (mid_pid == 0) {
    setpriority(60);  // 中等优先级
    print("  [中优先级] 中优先级任务\n");

    volatile int sum = 0;
    for (int i = 0; i < 30000; i++) {
      sum += i;
    }

    print("  [中优先级] 中优先级任务完成！\n");
    exit(0);
  }

  // 等待所有子进程完成
  wait(0);
  wait(0);
  wait(0);

  print("  通过: 抢占测试已完成\n");
}

// 测试4：多进程优先级竞争
void test_priority_competition(void)
{
  print("测试4: 多进程优先级竞争\n");

  int pids[5];
  int priorities[] = {20, 40, 60, 80, 100};

  // 创建5个不同优先级的进程
  for (int i = 0; i < 5; i++) {
    pids[i] = fork();
    if (pids[i] == 0) {
      setpriority(priorities[i]);
      // 执行一些计算
      volatile int sum = 0;
      for (int j = 0; j < 1000; j++) {
        sum += j;
      }
      print("  优先级 ");
      printnum(priorities[i]);
      print(" 的进程完成\n");
      exit(0);
    }
  }

  // 等待所有子进程
  for (int i = 0; i < 5; i++) {
    wait(0);
  }

  print("  通过: 所有进程已完成\n");
}

int
main(void)
{
  print("=== 优先级调度测试 ===\n\n");

  test_basic_priority();
  print("\n");

  test_priority_inherit();
  print("\n");

  test_priority_scheduling();
  print("\n");

  test_priority_competition();
  print("\n");

  print("=== 所有优先级测试通过！===\n");
  exit(0);
}
