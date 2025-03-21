#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// Global flag to control tick behavior
int use_dynamic_ticks = 1; // Default to dynamic ticks

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];
extern struct proc proc[]; // added

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

// added
void update_proc_stats(void);
uint64 calculate_tick_interval(void);

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(killed(p))
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    printf("usertrap(): unexpected scause 0x%lx pid=%d\n", r_scause(), p->pid);
    printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if(killed(p))
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to userret in trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    // interrupt or trap from an unknown source
    printf("scause=0x%lx sepc=0x%lx stval=0x%lx\n", scause, r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

// Update process statistics based on state transitions
// Update process statistics based on state transitions
void
update_proc_stats(void)
{
  struct proc *p;
  uint64 current_ticks;
  
  acquire(&tickslock);
  current_ticks = ticks;
  release(&tickslock);
  
  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state != UNUSED) {
      uint64 elapsed = current_ticks - p->last_tick;
      
      // Update appropriate counters based on process state
      if(p->state == RUNNING) {
        p->run_ticks += elapsed;
      } else if(p->state == SLEEPING) {
        p->sleep_ticks += elapsed;
      } else if(p->state == RUNNABLE) {
        p->runnable_ticks += elapsed;
      }
      
      // Update last tick time
      p->last_tick = current_ticks;
      
      // Calculate CPU usage and I/O intensity periodically
      // We'll do this every 10 ticks to smooth out fluctuations
      if(current_ticks % 10 == 0) {
        uint64 total_ticks = p->run_ticks + p->sleep_ticks + p->runnable_ticks;
        
        if(total_ticks > 0) {
          // CPU usage = (run_ticks / total_ticks) * 10000
          p->cpu_usage = (p->run_ticks * 10000) / total_ticks;
          
          // I/O intensity = (sleep_ticks / total_ticks) * 10000
          // Note: This is an approximation, as not all sleep time is I/O
          // but in most cases, processes sleep when waiting for I/O
          p->io_intensity = (p->sleep_ticks * 10000) / total_ticks;
        }
      }
    }
    release(&p->lock);
  }
}

// This function calculates the dynamic tick interval based on system state 
//added
uint64 calculate_tick_interval(void)
{
  struct cpu *c = mycpu();
  struct proc *p;
  int total_procs = 0;
  int active_procs = 0;
  int high_cpu_procs = 0;
  int high_io_procs = 0;
  uint64 interval = DEFAULT_TICK_INTERVAL;
  
  // Count different types of processes
  for(p = proc; p < &proc[NPROC]; p++) {
    if(p->state != UNUSED) {
      total_procs++;
      
      if(p->state == RUNNING || p->state == RUNNABLE) {
        active_procs++;
      }
      
      if(p->cpu_usage > HIGH_CPU_THRESHOLD) {
        high_cpu_procs++;
      }
      
      if(p->io_intensity > HIGH_IO_THRESHOLD) {
        high_io_procs++;
      }
    }
  }
  
  // Adjust tick interval based on process behavior
  // 1. If many high-CPU processes, use shorter ticks to prevent monopolization
  // 2. If many I/O-intensive processes, use longer ticks to reduce overhead
  // 3. Balance between system load and fairness!!!!!
  
  if(high_cpu_procs > (active_procs / 2) && active_procs > 1) {
    // Shorter ticks when many CPU-bound processes are competing
    interval = MIN_TICK_INTERVAL + 
               (DEFAULT_TICK_INTERVAL - MIN_TICK_INTERVAL) * (1 - (float)high_cpu_procs/active_procs);
  } else if(high_io_procs > (active_procs / 2) && active_procs > 1) {
    // Longer ticks when many I/O-bound processes are running
    interval = DEFAULT_TICK_INTERVAL + 
               (MAX_TICK_INTERVAL - DEFAULT_TICK_INTERVAL) * ((float)high_io_procs/active_procs);
  } else if(active_procs <= 1) {
    // If only one active process or none, use longer interval
    interval = MAX_TICK_INTERVAL;
  } else {
    // Default case: adjust based on number of active processes
    // More processes -> shorter ticks for fairness
    interval = DEFAULT_TICK_INTERVAL - 
               (DEFAULT_TICK_INTERVAL - MIN_TICK_INTERVAL) * ((float)active_procs / NPROC);
    
    if(interval < MIN_TICK_INTERVAL)
      interval = MIN_TICK_INTERVAL;
  }
  
  // Save the calculated interval for metrics
  c->current_tick_interval = interval;
  
  return interval;
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  update_proc_stats();  // Update process statistics
  wakeup(&ticks);
  release(&tickslock);

  // Calculate the dynamic tick interval based on current system state
  uint64 interval;
  if (use_dynamic_ticks) {
    interval = calculate_tick_interval();
  } else {
    interval = DEFAULT_TICK_INTERVAL; // Fixed interval if dynamic is disabled
  }
  
  // ask for the next timer interrupt with the dynamic interval
  // this also clears the interrupt request
  w_stimecmp(r_time() + interval);
}

// void
// clockintr()
// {
//   if(cpuid() == 0){
//     acquire(&tickslock);
//     ticks++;
//     wakeup(&ticks);
//     release(&tickslock);
//   }

//   // ask for the next timer interrupt. this also clears
//   // the interrupt request. 1000000 is about a tenth
//   // of a second.
//   w_stimecmp(r_time() + 1000000);
// }

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if(scause == 0x8000000000000009L){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000005L){
    // timer interrupt.
    clockintr();
    return 2;
  } else {
    return 0;
  }
}

