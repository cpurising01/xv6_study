#include "kernel/types.h"
#include "user/user.h"

// 过滤器节点递归求解函数
void
solve(int listen_fd)
{
  int prime;
  // 1. 读取管道中流过来的第一个数，它必然是当前阶段发现的最小素数
  int n = read(listen_fd, &prime, sizeof(int));
  
  // 如果读取到 EOF (返回 0) 或发生错误，说明上一代已无数据，当前分支生命周期结束
  if (n <= 0) {
    close(listen_fd);
    exit(0);
  }
  
  // 2. 打印当前素数
  printf("prime %d\n", prime);

  // 3. 创建向右传递的管道
  int p[2];
  if (pipe(p) < 0) {
    fprintf(2, "pipe failed\n");
    exit(1);
  }

  // 4. 创建子进程成为右侧的下一个过滤节点
  int pid = fork();
  if (pid < 0) {
    fprintf(2, "fork failed\n");
    exit(1);
  }

  if (pid == 0) {
    // 【子进程】
    close(p[1]);         // 子进程无需往右侧管道写入
    close(listen_fd);    // 子进程无需读取左侧管道 (那是上一代的事)
    solve(p[0]);         // 递归地以右侧管道的读端作为新的输入源
  } else {
    // 【父进程】
    close(p[0]);         // 父进程无需读取右侧管道
    
    int num;
    // 5. 循环从左侧管道读出剩余的所有数
    while (read(listen_fd, &num, sizeof(int)) == sizeof(int)) {
      // 6. 进行素数筛过滤：凡是不能被当前素数整除的，传递到右侧管道
      if (num % prime != 0) {
        if (write(p[1], &num, sizeof(int)) != sizeof(int)) {
          fprintf(2, "write failed\n");
          exit(1);
        }
      }
    }
    
    // 7. 父进程清理持有的 FD，向右侧发送 EOF
    close(listen_fd);
    close(p[1]); // 必须关闭写端，否则下一代 read 将会永远阻塞导致死锁！
    
    // 8. 强同步等待下一代进程执行完毕后退出
    wait(0);
    exit(0);
  }
}

int
main(int argc, char *argv[])
{
  int p[2];
  
  // 1. 创建初始管道
  if (pipe(p) < 0) {
    fprintf(2, "pipe failed\n");
    exit(1);
  }

  // 2. 创建第一个子进程节点
  int pid = fork();
  if (pid < 0) {
    fprintf(2, "fork failed\n");
    exit(1);
  }

  if (pid == 0) {
    // 子进程进入过滤器链
    close(p[1]);
    solve(p[0]);
  } else {
    // 主进程负责喂入 2 至 35 的初始序列
    close(p[0]);
    for (int i = 2; i <= 35; i++) {
      if (write(p[1], &i, sizeof(int)) != sizeof(int)) {
        fprintf(2, "initial write failed\n");
        exit(1);
      }
    }
    // 喂入完毕，关闭写端发送 EOF 信号，并等待整个进程链退出
    close(p[1]);
    wait(0);
    exit(0);
  }
}