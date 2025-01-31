#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

/*

There is a data structure which is called Red-Black tree structure.
Red-Black Tree: 
  A red-black tree is a binary search tree which has the following red-black properties:
    Every node is either red or black. Every leaf (NULL) is black.
    If a node is red, then both its children are black.

*/ 

struct RedBlack_Tree{
  struct spinlock lock;
  struct proc *root, *min_vruntime;
  int period, count ,weight;
} redBlackTree;

static struct RedBlack_Tree *tasks = &redBlackTree;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

static int latency = NPROC / 2, min_gran = 2;
// redblackinit
void rbTree_init(struct RedBlack_Tree *rb_tree, char *lock_name) {
  rb_tree->period = latency;
  rb_tree->root = 0;
  rb_tree->min_vruntime = 0;
  rb_tree->weight = 0;
  rb_tree->count = 0;
  initlock(&rb_tree->lock, lock_name);
}
// is_empty
int is_empty(struct RedBlack_Tree *tree) { return tree->count == 0; }

// is_full
int is_full(struct RedBlack_Tree *tree) { return tree->count == NPROC; }

// rotate_left
void rotate_left(struct RedBlack_Tree *tree, struct proc *positonProc) {
  struct proc *right_proc = positonProc->proc_right;
  
  positonProc->proc_right = right_proc->proc_left;
  if (right_proc->proc_left)
    right_proc->proc_left->proc_parent = positonProc;
  right_proc->proc_parent = positonProc->proc_parent;

  if (!positonProc->proc_parent)
    tree->root = right_proc;
  else if (positonProc->proc_parent->proc_left)
    positonProc->proc_parent->proc_left = right_proc;
  else 
    positonProc->proc_parent->proc_right = right_proc;

  right_proc->proc_left = positonProc;
  positonProc->proc_parent = right_proc;
}

// rotate_right
void rotate_right(struct RedBlack_Tree *tree, struct proc *positonProc) {
  struct proc *left_proc = positonProc->proc_left;
  
  positonProc->proc_left = left_proc->proc_right;
  if (left_proc->proc_right)
    left_proc->proc_right->proc_parent = positonProc;
  left_proc->proc_parent = positonProc->proc_parent;

  if (!positonProc->proc_parent)
    tree->root = left_proc;
  else if (positonProc->proc_parent->proc_right)
    positonProc->proc_parent->proc_right = left_proc;
  else 
    positonProc->proc_parent->proc_left = left_proc;

  left_proc->proc_right = positonProc;
  positonProc->proc_parent = left_proc;
}

// retrieve_grand_parent_process => retrive the grandparent of the passed process
struct proc *retrieve_grand_parent_process(struct proc* process) {
  if (process && process->proc_parent)
    return process->proc_parent->proc_parent;
  return 0;
}

// retrieve_uncle_process => retrive the uncle of the passed process
struct proc *retrieve_uncle_process(struct proc* process) {
  struct proc *grandParent = retrieve_grand_parent_process(process);
  if (grandParent) {
    if(process->proc_parent == grandParent->proc_left) {
      return grandParent->proc_right;
    } else {
      return grandParent->proc_left;
    }
  }
  return 0;
}

// set_min_vruntime
struct proc *set_min_vruntime(struct proc *traversingProcess) {
  if (!traversingProcess) {
    if (!traversingProcess->proc_left) 
      return set_min_vruntime(traversingProcess->proc_left);
    else
      return traversingProcess;
  }
  return 0;
}

// insert_process
struct proc *insert_process (
  struct proc *traversing_process,
  struct proc *inserting_process) {
    inserting_process->proc_color = RED;
    if (!traversing_process)
      return traversing_process;

    if (traversing_process->virtual_runtime <= inserting_process->virtual_runtime) {
      inserting_process->proc_parent = traversing_process;
      traversing_process->proc_right = insert_process(traversing_process->proc_right, inserting_process);
    } else {
      inserting_process->proc_parent = traversing_process;
      traversing_process->proc_left = insert_process(traversing_process->proc_left, inserting_process);
    }

    return traversing_process;
}

// insertion_cases
void insertion_cases(struct RedBlack_Tree *tree, 
  struct proc *redblack_proc, int cases) {
    struct proc *uncle;
    struct proc *grand_parent;
    switch (cases)
    {
    case 1:
      if(!redblack_proc->proc_parent) 
        redblack_proc->proc_color = BLACK;
      else
        insertion_cases(tree, redblack_proc, 2);
      break;
    
    case 2:
      if(!redblack_proc->proc_parent->proc_color == RED) 
        insertion_cases(tree, redblack_proc, 3);
      break;

    case 3:
      uncle = retrieve_uncle_process(redblack_proc);
      if(uncle && uncle->proc_color == RED) {
        redblack_proc->proc_parent->proc_color = BLACK;
        uncle->proc_color = BLACK;
        grand_parent = retrieve_grand_parent_process(redblack_proc);
        grand_parent->proc_color = RED;
        insertion_cases(tree, grand_parent, 1);
        grand_parent = 0;
      } else {
        insertion_cases(tree, redblack_proc, 4);
      }
      uncle = 0;
      break;

    case 4:
      grand_parent = retrieve_grand_parent_process(redblack_proc);
      if (redblack_proc == redblack_proc->proc_parent->proc_right &&
       redblack_proc->proc_parent == grand_parent->proc_left) {
        rotate_left(tree, grand_parent->proc_parent);
        redblack_proc = redblack_proc->proc_left;
       } else if (redblack_proc == redblack_proc->proc_parent->proc_left &&
        redblack_proc->proc_parent == grand_parent->proc_right) {
          rotate_right(tree, redblack_proc->proc_parent);
          redblack_proc = redblack_proc->proc_right;
       }
       insertion_cases(tree, redblack_proc, 5);
       grand_parent = 0;
       break;

    case 5:
      grand_parent = retrieve_grand_parent_process(redblack_proc);
      if (grand_parent) {
        grand_parent->proc_color = RED;
        redblack_proc->proc_parent->proc_color = BLACK;
        if (redblack_proc == redblack_proc->proc_parent->proc_left &&
         redblack_proc->proc_parent == grand_parent->proc_left) 
          rotate_right(tree, grand_parent);
        else if (redblack_proc == redblack_proc->proc_parent->proc_right && 
         redblack_proc->proc_parent == grand_parent->proc_right)
          rotate_left(tree, grand_parent);
      }
      grand_parent = 0;
      break;
    
    default:
      break;
    }
  return;
}

// calculate_weight
int calculate_weight(int nice) {
  double denom = 1.25;
  nice = nice > 30 ? 30 : nice;
  int i = 0;
  while (i < nice && nice > 0)
    denom = denom * 1.25;
  
  return (int) (1024/denom);
}
// insert_proc
void insert_proc(struct RedBlack_Tree *tree, struct proc *process) {
  acquire(&tree->lock);
  if(!is_full(tree)) {
    tree->root = insert_process(tree->root, process);
    if (!tree->count) 
      tree->root->proc_parent = 0;
    tree->count += 1;
    process->proc_weight = calculate_weight(process->nice);
    tree->weight += process->proc_weight;
    insertion_cases(tree, process, 1);
    if(!tree->min_vruntime || tree->min_vruntime->proc_left)
      tree->min_vruntime = set_min_vruntime(tree->root);
  }
  release(&tree->lock);
}

// retrieving_cases
void retrieve_cases (struct RedBlack_Tree *tree,
 struct proc *parent_process, struct proc *process, int cases) {
  struct proc *parent_prc;
  struct proc *child_prc;
  struct proc *sibiling_prc;

  switch (cases)
  {
  case 1:
    parent_prc = parent_process;
    child_prc = process;
    if (process == tree->root) {
      tree->root = child_prc;
      if(child_prc) {
        child_prc->proc_parent = 0;
        child_prc->proc_color = BLACK;
      }
    } else if (child_prc && !(process->proc_color == child_prc->proc_color)) {
      child_prc->proc_parent = parent_prc;
      parent_prc->proc_left = child_prc;
      child_prc->proc_color = BLACK;
    } else if (process->proc_color == RED)
      parent_prc->proc_left = child_prc;
    else {
      if (child_prc)
        child_prc->proc_parent = parent_prc;
      parent_prc->proc_left = child_prc;
      retrieve_cases(tree, parent_prc, child_prc, 2);
    }
    process->proc_parent = 0;
    process->proc_left = 0;
    process->proc_right = 0;
    parent_prc = 0;
    child_prc = 0;
    break;

  case 2:
    parent_prc = parent_process;
    child_prc = process;
    while(process != tree->root && (!process || process->proc_color == BLACK)) {
      if ( process == parent_prc->proc_left) {
        sibiling_prc = parent_prc->proc_right;
        if (sibiling_prc && sibiling_prc->proc_color == RED) {
          sibiling_prc->proc_color = BLACK;
          parent_prc->proc_color = RED;
          rotate_left(tree, parent_prc);
          sibiling_prc = parent_prc->proc_right;
        } 
        if ((!sibiling_prc->proc_left || sibiling_prc->proc_left->proc_color == BLACK) &&
            (!sibiling_prc->proc_right || sibiling_prc->proc_right->proc_color == BLACK)) {
              sibiling_prc->proc_color = RED;
              process = parent_prc;
              parent_prc = parent_prc->proc_parent;
            } else {
              if (!sibiling_prc->proc_right || sibiling_prc->proc_right->proc_color == BLACK) {
                if (sibiling_prc->proc_left)
                  sibiling_prc->proc_left->proc_color = BLACK;
                sibiling_prc->proc_color = RED;
                rotate_right(tree, parent_prc);
                sibiling_prc = parent_prc->proc_right;
              }
              sibiling_prc->proc_color = parent_prc->proc_color;
              parent_prc->proc_color = BLACK;
              sibiling_prc->proc_right->proc_color = BLACK;
              rotate_left(tree, parent_prc);
              process = tree->root;
            }
      }
    }
    if (process) 
      process->proc_color = BLACK;
    break;
  
  default:
    break;
  }
}

// retrieve_process
struct proc *retrieve_process(struct RedBlack_Tree *tree) {
  struct proc *found_process;
  acquire(&tree->lock);
  if (!is_empty(tree)) {
    if(tree->count > (latency / min_gran))
      tree->period = tree->count * min_gran;
    
    found_process = tree->min_vruntime;
    if(found_process->state != RUNNABLE) {
      release(&tree->lock);
      return 0;
    }
    retrieve_cases(tree, tree->min_vruntime->proc_parent, tree->min_vruntime, 1);
    tree->min_vruntime = set_min_vruntime(tree->root);
    found_process->max_exec_time = tree->period * found_process->proc_weight / tree->weight;
    tree->weight -= found_process->proc_weight;
  } else {
    found_process = 0;
  }

  release(&tree->lock);
  return found_process;
}

// check_preemption
int check_preemption(struct proc *current, struct proc *min_vruntime) {
  int proc_runtime = current->current_runtime;
  if((proc_runtime >= current->max_exec_time) && (proc_runtime >= min_gran))
    return 1;
  if (min_vruntime && min_vruntime->state == RUNNABLE &&
   current->virtual_runtime > min_vruntime->virtual_runtime)
   {
    if (proc_runtime && (proc_runtime >= min_gran))
      return 1;
   } else if( !proc_runtime ) {
    return 1;
   }
  return 0;
}

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
  rbTree_init(tasks, "tasks");
}
// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  // CFS (red-black tree)
  p->proc_left = 0;
  p->proc_right = 0;
  p->proc_parent = 0;
  p->virtual_runtime = 0;
  p->current_runtime = 0;
  p->max_exec_time = 0;
  p->nice = 0;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");
  p->state = RUNNABLE;

  // Insert allocated process into the red black tree, which will become runnable. 
  insert_proc(tasks, p);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  // Insert allocated process into the red black tree, which will become runnable. 
  insert_proc(tasks, np);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    p = retrieve_process(tasks);
    while (p) {
      if (p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release ptable.lock and then reacquire it
        // before jumping back to us.
        c->proc = p;
        switchuvm(p);
        p->state = RUNNING;

        swtch(&(c->scheduler), p->context);
        switchkvm();

      }
      p = retrieve_process(tasks);
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *curproc = myproc();
  acquire(&ptable.lock);  //DOC: yieldlock
  if(check_preemption(curproc, tasks->min_vruntime)) {
    curproc->state = RUNNABLE;
    curproc->virtual_runtime += curproc->current_runtime;
    curproc->current_runtime = 0;
    insert_proc(tasks, curproc);
    sched();
  }
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan){
      p->state = RUNNABLE;
      p->virtual_runtime += p->current_runtime;
      p->current_runtime = 0;
      insert_proc(tasks, p);  
    }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING){
        p->state = RUNNABLE;
        p->virtual_runtime += p->current_runtime;
        p->current_runtime = 0;
        insert_proc(tasks, p);  
      }
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
