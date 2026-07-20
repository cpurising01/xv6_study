#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

// 运行单个命令，其中包含从当前 stdin 行解析出的附加参数
void
run_xargs(char *cmd, char *prefix_argv[], int prefix_argc, char *line)
{
  char *new_argv[MAXARG];
  int i;
  
  // 1. 复制原命令及其初始参数
  for(i = 0; i < prefix_argc; i++){
    new_argv[i] = prefix_argv[i];
  }

  // 2. 切分 line 字符串并将其追加至参数列表中
  int idx = prefix_argc;
  char *p = line;
  while(*p != '\0'){
    // 跳过空白字符
    while(*p == ' ' || *p == '\t'){
      p++;
    }
    if(*p == '\0')
      break;

    // 防止参数个数溢出 MAXARG 限制
    if(idx >= MAXARG - 1){
      fprintf(2, "xargs: too many arguments\n");
      exit(1);
    }
    new_argv[idx++] = p;

    // 寻找到当前参数的结尾
    while(*p != '\0' && *p != ' ' && *p != '\t'){
      p++;
    }

    // 截断参数，构造合法的以 \0 结尾的字符串
    if(*p != '\0'){
      *p = '\0';
      p++;
    }
  }
  new_argv[idx] = 0; // 必须以 NULL 结尾

  // 如果这一行读取的数据未解析出任何新参数，则不执行任何命令
  if(idx == prefix_argc){
    return;
  }

  // 3. fork 并 exec 执行子命令
  int pid = fork();
  if(pid < 0){
    fprintf(2, "xargs: fork failed\n");
    exit(1);
  }
  if(pid == 0){
    exec(cmd, new_argv);
    fprintf(2, "xargs: exec %s failed\n", cmd);
    exit(1);
  } else {
    wait(0); // 父进程等待子进程执行完成
  }
}

int
main(int argc, char *argv[])
{
  if(argc < 2){
    fprintf(2, "Usage: xargs <command> [args...]\n");
    exit(1);
  }

  char *cmd = argv[1];
  char *prefix_argv[MAXARG];
  int prefix_argc = 0;

  // 将 xargs 后的命令及其参数提取作为 prefix_argv
  // 例如：xargs echo bye -> cmd = "echo", prefix_argv = {"echo", "bye"}
  int i;
  for(i = 1; i < argc; i++){
    if(prefix_argc >= MAXARG - 1){
      fprintf(2, "xargs: initial arguments too many\n");
      exit(1);
    }
    prefix_argv[prefix_argc++] = argv[i];
  }

  char line[1024];
  int n = 0;
  char ch;

  // 逐字符读取 stdin（描述符 0）
  while(read(0, &ch, 1) > 0){
    if(ch == '\n'){
      line[n] = '\0';
      run_xargs(cmd, prefix_argv, prefix_argc, line);
      n = 0;
    } else {
      if(n < sizeof(line) - 1){
        line[n++] = ch;
      }
    }
  }

  // 处理输入末尾可能残留且不带换行符的一行数据
  if(n > 0){
    line[n] = '\0';
    run_xargs(cmd, prefix_argv, prefix_argc, line);
  }

  exit(0);
}
