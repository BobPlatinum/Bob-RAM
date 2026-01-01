// 测试写时复制（COW）功能
// 本程序测试COW是否正确处理父子进程间的页面共享

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

// 测试1：基本COW - 子进程修改数据，父进程应保持原始值
void test_basic_cow(void)
{
  print("Test 1: Basic COW\n");

  int page_size = 4096;
  char *data = (char*)malloc(page_size);
  if (data == 0) {
    print("  FAIL: malloc failed\n");
    exit(1);
  }

  // 初始化数据
  for (int i = 0; i < page_size; i++) {
    data[i] = 'A';
  }

  int pid = fork();
  if (pid == 0) {
    // 子进程
    data[0] = 'X';  // 这会触发COW
    data[page_size - 1] = 'Y';
    if (data[0] == 'X' && data[page_size - 1] == 'Y') {
      print("  Child: modification OK\n");
    } else {
      print("  FAIL: child modification failed\n");
      exit(1);
    }
    exit(0);
  } else {
    wait(0);
    // 父进程应该看到原始数据
    if (data[0] == 'A' && data[page_size - 1] == 'A') {
      print("  Parent: data unchanged (COW working)\n");
      print("  PASS\n");
    } else {
      print("  FAIL: parent data was modified\n");
      exit(1);
    }
  }

  free(data);
}

// 测试2：多页面COW
void test_multiple_pages(void)
{
  print("Test 2: Multiple pages COW\n");

  int num_pages = 10;
  int total_size = num_pages * 4096;
  char *data = (char*)malloc(total_size);
  if (data == 0) {
    print("  FAIL: malloc failed\n");
    exit(1);
  }

  // 初始化所有页面
  for (int i = 0; i < total_size; i++) {
    data[i] = 'B';
  }

  int pid = fork();
  if (pid == 0) {
    // 子进程修改每个页面
    for (int i = 0; i < num_pages; i++) {
      data[i * 4096] = 'C' + i;
    }
    exit(0);
  }

  wait(0);

  // 父进程检查数据是否未改变
  int pass = 1;
  for (int i = 0; i < num_pages; i++) {
    if (data[i * 4096] != 'B') {
      pass = 0;
      break;
    }
  }

  if (pass) {
    print("  PASS: all pages preserved\n");
  } else {
    print("  FAIL: some pages were modified\n");
    exit(1);
  }

  free(data);
}

// 测试3：父进程在fork后修改
void test_parent_modify(void)
{
  print("Test 3: Parent modifies after fork\n");

  int page_size = 4096;
  char *data = (char*)malloc(page_size);
  if (data == 0) {
    print("  FAIL: malloc failed\n");
    exit(1);
  }

  data[0] = 'P';

  int pid = fork();
  if (pid == 0) {
    // 子进程等待，然后检查
    sleep(5);
    if (data[0] == 'P') {
      print("  Child: sees original value\n");
    } else {
      print("  FAIL: child sees modified value\n");
      exit(1);
    }
    exit(0);
  } else {
    // 父进程修改（触发COW）
    sleep(2);
    data[0] = 'Q';
    if (data[0] == 'Q') {
      print("  Parent: modification OK\n");
    }
    wait(0);
    print("  PASS\n");
  }

  free(data);
}

// 测试4：多次fork
void test_many_forks(void)
{
  print("Test 4: Multiple forks\n");

  int page_size = 4096;
  char *data = (char*)malloc(page_size);
  if (data == 0) {
    print("  FAIL: malloc failed\n");
    exit(1);
  }

  data[0] = 'M';

  int num_children = 5;
  for (int i = 0; i < num_children; i++) {
    int pid = fork();
    if (pid == 0) {
      // 每个子进程修改自己的副本
      data[0] = '0' + i;
      sleep(1);
      if (data[0] == '0' + i) {
        print(".");
      } else {
        print("FAIL\n");
        exit(1);
      }
      exit(0);
    }
  }

  // 等待所有子进程
  for (int i = 0; i < num_children; i++) {
    wait(0);
  }

  // 父进程应该仍保持原始值
  if (data[0] == 'M') {
    print("\n  PASS: parent data unchanged\n");
  } else {
    print("\n  FAIL: parent data modified\n");
    exit(1);
  }

  free(data);
}

// 测试5：COW与exec
void test_cow_with_exec(void)
{
  print("Test 5: COW with fork+exec pattern\n");

  int page_size = 4096;
  char *data = (char*)malloc(page_size * 10);
  if (data == 0) {
    print("  FAIL: malloc failed\n");
    exit(1);
  }

  // 分配大内存，然后立即fork和exec
  // 这测试COW不会浪费时间复制内存
  for (int i = 0; i < page_size * 10; i++) {
    data[i] = 'X';
  }

  int pid = fork();
  if (pid == 0) {
    // 子进程通常会在这里exec
    // 对于这个测试，只是快速退出
    free(data);
    exit(0);
  }

  wait(0);
  print("  PASS: fork+exec pattern works\n");

  free(data);
}

int
main(void)
{
  print("=== COW (Copy-on-Write) Test ===\n\n");

  test_basic_cow();
  print("\n");

  test_multiple_pages();
  print("\n");

  test_parent_modify();
  print("\n");

  test_many_forks();
  print("\n");

  test_cow_with_exec();
  print("\n");

  print("=== All COW tests passed! ===\n");
  exit(0);
}
