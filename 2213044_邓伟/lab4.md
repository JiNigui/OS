# lab4：进程管理
## 一.实验目的
- 了解内核线程创建/执行的管理过程
- 了解内核线程的切换和基本调度过程
## 二.实验内容
实验2/3完成了物理和虚拟内存管理，这给创建内核线程（内核线程是一种特殊的进程）打下了提供内存管理的基础。当一个程序加载到内存中运行时，首先通过ucore OS的内存管理子系统分配合适的空间，然后就需要考虑如何分时使用CPU来“并发”执行多个程序，让每个运行的程序（这里用线程或进程表示）“感到”它们各自拥有“自己”的CPU。

本次实验将首先接触的是内核线程的管理。内核线程是一种特殊的进程，内核线程与用户进程的区别有两个：

内核线程只运行在内核态
用户进程会在在用户态和内核态交替运行
所有内核线程共用ucore内核内存空间，不需为每个内核线程维护单独的内存空间
而用户进程需要维护各自的用户内存空间
相关原理介绍可看附录B：【原理】进程/线程的属性与特征解析。

提前说明
需要注意的是，在ucore的调度和执行管理中，对线程和进程做了统一的处理。且由于ucore内核中的所有内核线程共享一个内核地址空间和其他资源，所以这些内核线程从属于同一个唯一的内核进程，即ucore内核本身。
## 三.实验过程
### 练习1：分配并初始化一个进程控制块（需要编码）
1. alloc_proc函数（位于kern/process/proc.c中）负责分配并返回一个新的struct proc_struct结构，用于存储新建立的内核线程的管理信息。ucore需要对这个结构进行最基本的初始化，你需要完成这个初始化过程。

_【提示】在alloc_proc函数的实现中，需要初始化的proc_struct结构中的成员变量至少包括：state/pid/runs/kstack/need_resched/parent/mm/context/tf/cr3/flags/name。_
请在实验报告中简要说明你的设计实现过程。
```
static struct proc_struct *
alloc_proc(void) {
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL) {
    //LAB4:EXERCISE1 YOUR CODE
    /*
     * below fields in proc_struct need to be initialized
     *       enum proc_state state;                      // Process state
     *       int pid;                                    // Process ID
     *       int runs;                                   // the running times of Proces
     *       uintptr_t kstack;                           // Process kernel stack
     *       volatile bool need_resched;                 // bool value: need to be rescheduled to release CPU?
     *       struct proc_struct *parent;                 // the parent process
     *       struct mm_struct *mm;                       // Process's memory management field
     *       struct context context;                     // Switch here to run process
     *       struct trapframe *tf;                       // Trap frame for current interrupt
     *       uintptr_t cr3;                              // CR3 register: the base addr of Page Directroy Table(PDT)
     *       uint32_t flags;                             // Process flag
     *       char name[PROC_NAME_LEN + 1];               // Process name
     */

        proc->state = PROC_UNINIT;                      //状态为未初始化
        proc->pid = -1;                                 //pid为未赋值
        proc->runs = 0;                                 //运行时间为0
        proc->kstack = 0;                               //除了idleproc其他线程的内核栈都要后续分配
        proc->need_resched = 0;                         //不需要调度切换线程
        proc->parent = NULL;                            //没有父线程
        proc->mm = NULL;                                //未分配内存
        memset(&(proc->context), 0, sizeof(struct context));//将上下文变量全部赋值为0，清空
        proc->tf = NULL;                                //初始化没有中断帧
        proc->cr3 = boot_cr3;                           //内核线程的cr3为boot_cr3，即页目录为内核页目录表
        proc->flags = 0;                                //标志位为0
        memset(proc->name, 0, PROC_NAME_LEN+1);         //将线程名变量全部赋值为0，清空
    }
    return proc;
}
```
注释中已有讲解。
2. 请回答如下问题：请说明proc_struct中struct context context和struct trapframe *tf成员变量含义和在本实验中的作用是啥？（提示通过看代码和编程调试可以判断出来）
```
struct proc_struct {
    enum proc_state state;                      // Process state
    int pid;                                    // Process ID
    int runs;                                   // the running times of Proces
    uintptr_t kstack;                           // Process kernel stack
    volatile bool need_resched;                 // bool value: need to be rescheduled to release CPU?
    struct proc_struct *parent;                 // the parent process
    struct mm_struct *mm;                       // Process's memory management field
    struct context context;                     // Switch here to run process
    struct trapframe *tf;                       // Trap frame for current interrupt
    uintptr_t cr3;                              // CR3 register: the base addr of Page Directroy Table(PDT)
    uint32_t flags;                             // Process flag
    char name[PROC_NAME_LEN + 1];               // Process name
    list_entry_t list_link;                     // Process link list 
    list_entry_t hash_link;                     // Process hash list
    int exit_code;                              // exit code (be sent to parent proc)
    uint32_t wait_state;                        // waiting state
    struct proc_struct *cptr, *yptr, *optr;     // relations between processes
    struct run_queue *rq;                       // running queue contains Process
    list_entry_t run_link;                      // the entry linked in run queue
    int time_slice;                             // time slice for occupying the CPU
    skew_heap_entry_t lab6_run_pool;            // FOR LAB6 ONLY: the entry in the run pool
    uint32_t lab6_stride;                       // FOR LAB6 ONLY: the current stride of the process
    uint32_t lab6_priority;                     // FOR LAB6 ONLY: the priority of process, set by lab6_set_priority(uint32_t)
    struct files_struct *filesp;                // the file related info(pwd, files_count, files_array, fs_semaphore) of process
};
```
- struct context context：这是一个保存进程上下文的结构体，它包含了CPU寄存条器的状态。在操作系统中，当进程被切换出去时，它的寄存器状态会被保存在这个结构体中，以便之后能够恢复进程的执行状态。在本实验中，这个结构体用于保存和恢复内核线程的执行状态。
- struct trapframe *tf：这是一个指向陷阱帧（trap frame）的指针，陷阱帧是在进程发生中断或异常时由硬件自动填充的一段内存区域，它保存了中断发生时的寄存器状态。在本实验中，tf用于传递中断发生时的上下文信息，以便在进程恢复执行时能够从正确的位置继续执行。
### 练习2：为新创建的内核线程分配资源（需要编码）
1. 创建一个内核线程需要分配和设置好很多资源。kernel_thread函数通过调用do_fork函数完成具体内核线程的创建工作。do_kernel函数会调用alloc_proc函数来分配并初始化一个进程控制块，但alloc_proc只是找到了一小块内存用以记录进程的必要信息，并没有实际分配这些资源。ucore一般通过do_fork实际创建新的内核线程。do_fork的作用是，创建当前内核线程的一个副本，它们的执行上下文、代码、数据都一样，但是存储位置不同。因此，我们实际需要"fork"的东西就是stack和trapframe。在这个过程中，需要给新内核线程分配资源，并且复制原进程的状态。你需要完成在kern/process/proc.c中的do_fork函数中的处理过程。它的大致执行步骤包括：
- 调用alloc_proc，首先获得一块用户信息块。
- 为进程分配一个内核栈。
- 复制原进程的内存管理信息到新进程（但内核线程不必做此事）
- 复制原进程上下文到新进程
- 将新进程添加到进程列表
- 唤醒新进程
- 返回新进程号
请在实验报告中简要说明你的设计实现过程。请回答如下问题：
```
int
do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf) {
    int ret = -E_NO_FREE_PROC;
    struct proc_struct *proc;
    if (nr_process >= MAX_PROCESS) {
        goto fork_out;
    }
    ret = -E_NO_MEM;
    //LAB4:EXERCISE2 YOUR CODE
    /*
     * Some Useful MACROs, Functions and DEFINEs, you can use them in below implementation.
     * MACROs or Functions:
     *   alloc_proc:   create a proc struct and init fields (lab4:exercise1)
     *   setup_kstack: alloc pages with size KSTACKPAGE as process kernel stack
     *   copy_mm:      process "proc" duplicate OR share process "current"'s mm according clone_flags
     *                 if clone_flags & CLONE_VM, then "share" ; else "duplicate"
     *   copy_thread:  setup the trapframe on the  process's kernel stack top and
     *                 setup the kernel entry point and stack of process
     *   hash_proc:    add proc into proc hash_list
     *   get_pid:      alloc a unique pid for process
     *   wakeup_proc:  set proc->state = PROC_RUNNABLE
     * VARIABLES:
     *   proc_list:    the process set's list
     *   nr_process:   the number of process set
     */

    //    1. call alloc_proc to allocate a proc_struct
    //    2. call setup_kstack to allocate a kernel stack for child process
    //    3. call copy_mm to dup OR share mm according clone_flag
    //    4. call copy_thread to setup tf & context in proc_struct
    //    5. insert proc_struct into hash_list && proc_list
    //    6. call wakeup_proc to make the new child process RUNNABLE
    //    7. set ret vaule using child proc's pid
    // 分配一个进程控制块
    proc = alloc_proc();
    if(proc==NULL)//分配失败
        goto fork_out;  

    // 设置当前进程为新进程的父进程
    proc->parent = current;

    // 为新进程分配内核栈
    if(setup_kstack(proc))
        goto bad_fork_cleanup_kstack;//跳转进行清理

    //复 制进程的内存布局信息，以确保新进程拥有与原进程相同的内存环境
    if(copy_mm(clone_flags,proc))
        goto bad_fork_cleanup_proc;//失败则进行清理
  
    // 复制原进程的上下文到新进程
    copy_thread(proc, stack, tf);

    //get_pid中的全局变量需要原子性的更改，禁止中断
    bool intr_flag;
    local_intr_save(intr_flag);

    // 为新进程分配一个唯一的进程号
    proc->pid = get_pid();

    // 将新进程添加到进程列表，并允许中断
    hash_proc(proc);
    list_add(&proc_list,&(proc->list_link));//将proc->list_link加到proc_list后
    nr_process ++;//更新进程数量计数器
    local_intr_save(intr_flag);

    // 唤醒新进程，进入可调度状态
    wakeup_proc(proc);
  
    // 返回新进程号pid
    ret = proc->pid;

fork_out:
    return ret;

bad_fork_cleanup_kstack:
    put_kstack(proc);
bad_fork_cleanup_proc:
    kfree(proc);
    goto fork_out;
}

```
注释中已有讲解。
2. 请说明ucore是否做到给每个新fork的线程一个唯一的id？请说明你的分析和理由。
```
static int
get_pid(void) {
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    if (++ last_pid >= MAX_PID) {
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe) {
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        while ((le = list_next(le)) != list) {
            proc = le2proc(le, list_link);
            if (proc->pid == last_pid) {
                if (++ last_pid >= next_safe) {
                    if (last_pid >= MAX_PID) {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid) {
                next_safe = proc->pid;
            }
        }
    }
    return last_pid;
}
```
①该函数在每次创建新的内核进程时被调用，以分配一个唯一的PID作为新进程的proc->pid字段。

以下是get_pid函数的详细工作流程：

- 函数中首先使用static_assert宏确保MAX_PID大于MAX_PROCESS，保证有足够的PID可用。
- 声明相关变量，如next_safe和last_pid，用于跟踪下一个安全的PID以及上一次分配的PID。
- 递增last_pid，如果达到MAX_PID，则将其重置，并跳转到inside。如果last_pid大于等于next_safe（last_pid已经被使用），则将next_safe设置为MAX_PID，进入repeat循环：遍历进程链表，检查当前进程的ID是否等于last_pid，如果相等，则递增last_pid。同时，更新next_safe为当前进程中最大的PID值。
- 当找到一个没有被其他进程使用的last_pid时，这个值就被返回作为新分配的PID，因此保证了唯一性。

②此外，get_pid函数在do_fork中被调用时，会关闭中断，确保get_pid是原子操作，进一步确保了PID分配的唯一性。
### 练习3：编写proc_run 函数（需要编码）
1. proc_run用于将指定的进程切换到CPU上运行。它的大致执行步骤包括：
- 检查要切换的进程是否与当前正在运行的进程相同，如果相同则不需要切换。
- 禁用中断。你可以使用/kern/sync/sync.h中定义好的宏local_intr_save(x)和local_intr_restore(x)来实现关、开中断。
- 切换当前进程为要运行的进程。
- 切换页表，以便使用新进程的地址空间。/libs/riscv.h中提供了lcr3(unsigned int cr3)函数，可实现修改CR3寄存器值的功能。
- 实现上下文切换。/kern/process中已经预先编写好了switch.S，其中定义了switch_to()函数。可实现两个进程的context切换。
- 允许中断。
请回答如下问题：

```
void
proc_run(struct proc_struct *proc) {
    if (proc != current) {
        // LAB4:EXERCISE3 YOUR CODE
        /*
        * Some Useful MACROs, Functions and DEFINEs, you can use them in below implementation.
        * MACROs or Functions:
        *   local_intr_save():        Disable interrupts
        *   local_intr_restore():     Enable Interrupts
        *   lcr3():                   Modify the value of CR3 register
        *   switch_to():              Context switching between two processes
        */
        bool intr_flag;
        local_intr_save(intr_flag);
        // 保存当前进程的上下文，并切换到新进程
        struct proc_struct * temp = current;
        current = proc;
        // 切换页表，以便使用新进程的地址空间
        // cause: 
        // 为了确保进程 A 不会访问到进程 B 的地址空间
        // 页目录表包含了虚拟地址到物理地址的映射关系,将当前进程的虚拟地址空间映射关系切换为新进程的映射关系.
        // 确保指令和数据的地址转换是基于新进程的页目录表进行的
        lcr3(current->cr3);// 修改 CR3 寄存器(CR3寄存器:页目录表（PDT）的基地址)，加载新页目录表的基地址
        // 上下文切换
        // cause:
        // 保存当前进程的信息,以便之后能够正确地恢复到当前进程
        // 将新进程的上下文信息加载到相应的寄存器和寄存器状态寄存器中，确保 CPU 开始执行新进程的代码
        // 禁用中断确保在切换期间不会被中断打断
        switch_to(&(temp->context),&(proc->context));
        // 恢复中断状态
        local_intr_restore(intr_flag);
    }
}
```
注释中已有讲解。
2. 在本实验的执行过程中，创建且运行了几个内核线程？
- idleproc（空闲进程）：

这是Ucore启动后创建的第一个内核线程。
它的主要职责是初始化操作系统的各个子系统，包括内存管理、进程调度、文件系统等。
在初始化完成后，idleproc会调用调度器，将控制权交给其他就绪状态的进程。
当系统中没有其他进程需要运行时，CPU会执行idleproc。它通常执行一个空循环，等待其他进程变为可运行状态。
idleproc是系统的核心，它确保了系统的稳定运行，并且在没有其他进程可运行时，它会继续占用CPU。
- initproc（初始化进程）：

在idleproc完成系统初始化并调用调度器后，initproc被创建并开始运行。
initproc的目的是执行实验中特定的功能，这些功能可能是用户程序的启动、系统服务的初始化等。
initproc可以创建新的用户进程或内核线程，或者执行其他需要在系统启动时完成的任务。
initproc是用户空间程序的起点，它可能会启动shell或其他用户级程序。
### 扩展练习 Challenge：
1. 说明语句local_intr_save(intr_flag);....local_intr_restore(intr_flag);是如何实现开关中断的？
在Ucore操作系统中，local_intr_save(intr_flag);....local_intr_restore(intr_flag);这对语句用于实现中断的开关，确保关键代码段的原子性，防止在执行过程中被中断打断。具体实现机制如下：

- local_intr_save(intr_flag);：这个语句会保存当前的中断状态到变量intr_flag中，并关闭中断。在RISC-V架构中，这通常是通过检查sstatus寄存器的SIE位（Supervisor Interrupt Enable）来实现的。如果SIE位为1，表示中断启用，那么会通过intr_disable()函数来禁用中断，并将intr_flag设置为1，表示中断之前是启用状态。
- local_intr_restore(intr_flag);：这个语句根据之前保存的intr_flag状态来恢复中断。如果intr_flag为1，表示中断之前是启用状态，那么会通过intr_enable()函数来重新启用中断。

这两个函数的实现依赖于特定的硬件和操作系统。在Ucore中，它们是通过操作RISC-V的sstatus寄存器中的SIE位来实现的。中断开启时设置该位为1，关闭时清除为0，从而控制中断的启用与禁用。
## 四.实验总结与知识点补充
1. 内核线程与用户进程的区别
内核线程：只运行在内核态，与系统其他部分紧密集成，用于执行内核任务。
用户进程：在用户态和内核态之间切换，运行用户级程序，与内核有较清晰的界限。
2. 进程控制块（proc_struct）
存储进程的状态信息，如PID、状态、内核栈、内存管理信息、上下文等。
初始化过程中，需要设置状态、PID、内核栈等关键信息。
3. 中断控制
使用local_intr_save和local_intr_restore宏来保护临界区，防止中断打断关键操作。
在RISC-V中，通过操作sstatus寄存器的SIE位来控制中断。
4. 进程调度
proc_run函数实现了进程调度的核心逻辑，包括上下文切换和页表切换。
上下文切换通过switch_to函数实现，保存当前进程状态并加载新进程状态。
5. 内存管理
内核线程共享内核内存空间，而用户进程需要独立的用户内存空间。
内存管理信息（如mm_struct）在进程间复制或共享，影响内存隔离和资源管理。
6. 进程创建
do_fork函数用于创建新进程，包括分配进程控制块、内核栈、复制内存管理信息等。
新进程的创建涉及到资源分配和状态初始化，是操作系统中的关键操作。
7. 进程状态转换
进程状态（如就绪、运行、阻塞）在进程生命周期中转换，由调度器管理。
状态转换涉及到进程调度算法和系统资源分配策略。
8. 进程间通信（IPC）
虽然本实验未涉及，但IPC是多进程系统中不可或缺的部分，用于进程间的数据交换和同步。
9. 同步与互斥
多进程环境中，同步和互斥机制（如信号量、互斥锁）用于协调进程间的操作，避免竞态条件。
10. 系统调用与用户态切换
用户进程通过系统调用来请求内核服务，涉及从用户态到内核态的切换。
系统调用的处理涉及到中断处理和进程上下文切换。



