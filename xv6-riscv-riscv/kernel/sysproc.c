#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// ADDED
extern int use_dynamic_ticks;
extern uint64 total_context_switches;

uint64
sys_set_tick_mode(void)
{
  int mode;
  argint(0, &mode);
  
  // Set mode (0 = fixed tick interval, 1 = dynamic tick interval)
  use_dynamic_ticks = mode;
  
  return 0;
}

struct perf_metrics {
  uint64 total_ticks;
  uint64 context_switches;
  uint64 current_tick_interval;
};

uint64
sys_get_perf_metrics(void)
{
  uint64 addr;
  struct perf_metrics metrics;
  struct proc *p = myproc();
  struct cpu *c = mycpu();
  
  argaddr(0, &addr);
  
  // Fill the metrics structure
  acquire(&tickslock);
  metrics.total_ticks = ticks;
  release(&tickslock);
  
  metrics.context_switches = total_context_switches;
  metrics.current_tick_interval = c->current_tick_interval;
  
  // Copy metrics to user space
  if(copyout(p->pagetable, addr, (char *)&metrics, sizeof(metrics)) < 0)
    return -1;
  
  return 0;
}