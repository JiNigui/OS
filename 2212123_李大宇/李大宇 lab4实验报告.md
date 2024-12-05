# 操作系统实验报告

## 实验信息
- **实验名称**：进程管理
- **姓名**：李大宇

## 一、实验目的
1. 了解内核线程创建/执行的管理过程。
2. 了解内核线程的切换和基本调度过程。

# 实验过程

## 1. 练习1：分配并初始化一个进程控制块（需要编码）

### alloc_proc函数

`alloc_proc` 函数负责分配并返回一个新的 `struct proc_struct` 结构，用于存储新建立的内核线程的管理信息。`ucore` 需要对这个结构进行最基本的初始化，你需要完成这个初始化过程。

#### 问题1
请说明 `proc_struct` 中 `struct context context` 和 `struct trapframe tf` 成员变量的含义和在本实验中的作用。

**回答**：
- **context**：进程上下文，用于进程切换。主要保存了前一个进程的现场（各个寄存器的状态）。在 uCore 中，所有的进程在内核中也是相对独立的。使用 `context` 保存寄存器的目的在于在内核态中能够进行上下文之间的切换。实际利用 `context` 进行上下文切换的函数是在 `kern/process/switch.S` 中定义的 `switch_to`。
  
- **tf**：中断帧的指针，总是指向内核栈的某个位置。当进程从用户空间跳到内核空间时，中断帧记录了进程在被中断前的状态。当内核需要跳回用户空间时，需要调整中断帧以恢复让进程继续执行的各寄存器值。此外，uCore 内核允许嵌套中断。因此为了保证嵌套中断发生时 `tf` 总是指向当前的 `trapframe`，uCore 在内核栈上维护了 `tf` 的链。

### 初始化代码

`alloc_proc` 函数主要是分配并且初始化一个 PCB 用于管理新进程的信息。`proc_struct` 结构的信息如下：

```c
struct proc_struct {
    enum proc_state state;              // 进程状态
    int pid;                            // 进程ID
    int runs;                           // 运行时间
    uintptr_t kstack;                   // 内核栈位置
    volatile bool need_resched;         // 是否需要调度
    struct proc_struct* parent;         // 父进程
    struct mm_struct* mm;               // 进程的虚拟内存
    struct context context;             // 进程上下文
    struct trapframe* tf;               // 当前中断帧的指针
    uintptr_t cr3;                      // 当前页表地址
    uint32_t flags;                     // 进程标志位
    char name[PROC_NAME_LEN + 1];       // 进程名字
    list_entry_t list_link;             // 进程链表
    list_entry_t hash_link;             // 哈希链表
};
```
在 alloc_proc 中我们对每个变量都进行初始化操作，代码如下：
```
static struct proc_struct* alloc_proc(void) {
    struct proc_struct* proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL) {
        proc->state = PROC_UNINIT;      // 给进程设置为未初始化状态
        proc->pid = -1;                 // 未初始化的进程，其 pid 为 -1
        proc->runs = 0;                 // 初始化时间片, 刚刚初始化的进程，运行时间一定为零
        proc->kstack = 0;               // 内核栈地址, 该进程分配的地址为 0，因为还没有执行，也没有被重定位
        proc->need_resched = 0;         // 不需要调度
        proc->parent = NULL;            // 父进程为空
        proc->mm = NULL;                // 虚拟内存为空
        memset(&(proc->context), 0, sizeof(struct context));  // 初始化上下文
        proc->tf = NULL;                // 中断帧指针为空
        proc->cr3 = boot_cr3;           // 页目录为内核页目录表的基址
        proc->flags = 0;                // 标志位为 0
        memset(proc->name, 0, PROC_NAME_LEN);  // 进程名为 0
    }
    return proc;
}
```

通过如上代码，完成了对分配得到的新进程的 PCB 的初始化操作。具体初始化如下：

- **state** 设置为未初始化状态；
- **pid** 设置为 -1，表示未初始化的进程；
- **runs** 初始化为 0，表示刚刚初始化的进程，运行时间为零；
- **kstack** 默认从 0 开始，表示该进程还没有执行，也没有被重定位；
- **need_resched** 初始化为 0，表示不需要调度；
- **parent** 设置为空，表示没有父进程；
- **mm** 设置为空，表示虚拟内存尚未分配；
- **context** 初始化为 0，表示上下文信息为空；
- **tf** 设置为空，表示中断帧指针为空；
- **cr3** 设置为 `boot_cr3`，即内核页目录表的基址；
- **flags** 设置为 0，表示标志位为空；
- **name** 初始化为 0，表示进程名为空。


## 2. 练习2：为新创建的内核线程分配资源（需要编码）

### 创建内核线程

创建一个内核线程需要分配和设置好很多资源。`kernel_thread` 函数通过调用 `do_fork` 函数完成具体内核线程的创建工作。`do_kernel` 函数会调用 `alloc_proc` 函数来分配并初始化一个进程控制块，但 `alloc_proc` 只是找到了一小块内存用以记录进程的必要信息，并没有实际分配这些资源。uCore 一般通过 `do_fork` 实际创建新的内核线程。`do_fork` 的作用是创建当前内核线程的一个副本，它们的执行上下文、代码、数据都一样，但是存储位置不同。因此，我们实际需要“fork”的东西就是 `stack` 和 `trapframe`。

### 一、do_fork函数的实现

1. **调用 `alloc_proc`**
   调用 `alloc_proc()` 函数申请内存块，如果失败，直接返回处理。
   
   ```c
   if ((proc = alloc_proc()) == NULL) {
       goto fork_out;
   }
   proc->parent = current;  // 将子进程的父节点设置为当前进程
   
   ```
#### 2. 为进程分配一个内核栈

调用 `setup_kstack()` 函数为进程分配一个内核栈。内核栈用于保存进程在内核态运行时的寄存器状态和其他必要的信息。内核栈对于每个进程都是独立的，它保证了进程在内核态下的执行环境是隔离的。如果 `setup_kstack()` 函数执行失败，则需要释放之前分配的资源，并返回错误。

```c
if (setup_kstack(proc)) {
    goto bad_fork_cleanup_proc;
}
```
#### 3. 复制原进程的内存管理信息到新进程（但内核线程不必做此事）

调用 `copy_mm()` 函数，复制父进程的内存信息到子进程。对于这个函数可以看到，进程 `proc` 是否复制还是共享当前进程 `current` 的内存空间，取决于 `clone_flags`。如果是 `clone_flags & CLONE_VM`（为真），那么子进程将与父进程共享同一虚拟地址空间；否则，子进程将获得一个新的虚拟地址空间，其内容是父进程的副本。
```c
if (copy_mm(clone_flags, proc)) {
    goto bad_fork_cleanup_kstack;
}
```
#### 4. 设置子进程的其他信息

设置子进程的其他相关信息，如进程名、PID 等。此外，还需要设置子进程的状态为 `PROC_UNINTERRUPTIBLE` 或 `PROC_RUNNING`，这取决于具体的实现需求。


```c
proc->pid = get_pid();  // 分配一个唯一的 PID
strcpy(proc->name, "child_process");  // 设置进程名
proc->state = PROC_UNINTERRUPTIBLE;  // 设置初始状态
```


#### 5. 初始化子进程的 trapframe

为了使子进程能够正确地开始执行，我们需要为其初始化一个 `trapframe`。`trapframe` 包含了子进程从用户态切换到内核态时需要的所有寄存器值。通常我们会设置 `trapframe` 的 eip（指令指针）指向子进程的入口点，并且设置 esp（堆栈指针）指向子进程的内核栈顶部。

```c
proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE) - 1;
*proc->tf = *current->tf;  // 复制当前进程的 trapframe
proc->tf->eip = func;  // 设置子进程的入口点
proc->tf->eax = arg;  // 设置传递给子进程的参数
```


#### 6. 插入子进程到进程列表中

将新创建的子进程插入到系统的进程列表中，以便调度器可以找到并调度它。
```c
hash_proc(proc);  // 插入到哈希表中
list_add(&proc->list_link, &proc_list);  // 插入到进程链表中
```

#### 7. 初始化子进程的 trapframe

最后，`do_fork` 函数返回新创建的子进程的 PID 给调用者。
```c
return proc->pid;
```

#### 错误处理
如果在上述过程中遇到任何错误，需要进行适当的清理操作，以避免内存泄漏或其他资源浪费。例如，如果 `copy_mm` 失败，则需要释放已经分配的内核栈，并销毁进程控制块。

```c
bad_fork_cleanup_kstack:
    free_kstack(proc);
bad_fork_cleanup_proc:
    kfree(proc);
fork_out:
    return -EAGAIN;
```

#### 总结

通过上述步骤，我们实现了 `do_fork` 函数，成功为新创建的内核线程分配了必要的资源，并设置了相应的初始化信息。这使得新创建的内核线程可以在系统中被正确调度和执行。

### 二、请说明 ucore 是否做到给每个新 fork 的线程一个唯一的 id？请说明你的分析和理由。

我们可以查看实验中获取进程id的函数：`get_pid(void)`。

这段代码通过维护一个静态变量 `last_pid` 来实现为每个新 `fork` 的线程分配一个唯一的 `id`。让我们逐步分析：

1. `last_pid` 是一个静态变量，它会记录上一个分配的 `pid`。
2. 当 `get_pid` 函数被调用时，首先检查是否 `last_pid` 超过了最大的 `pid` 值（`MAX_PID`）。如果超过了，将 `last_pid` 重新设置为 1，从头开始分配。
3. 如果 `last_pid` 没有超过最大值，就进入内部的循环结构。在循环中，它遍历进程列表，检查是否有其他进程已经使用了当前的 `last_pid`。如果发现有其他进程使用了相同的 `pid`，就将 `last_pid` 递增，并继续检查。
4. 如果没有找到其他进程使用当前的 `last_pid`，则说明 `last_pid` 是唯一的，函数返回该值。

这样，通过这个机制，每次调用 `get_pid` 都会尽力确保分配一个未被使用的唯一 `pid` 给新 `fork` 的线程。

## 3. 练习3：编写 `proc_run` 函数（需要编码）

`proc_run` 用于将指定的进程切换到 CPU 上运行。请回答如下问题：

### 3.1 问题1

根据文档的提示说明，我们编写的 `proc_run()` 函数如下：
```c
void proc_run(struct proc_struct *proc) {
    if (proc != current) {
        // LAB4:EXERCISE3
        /*
        * Some Useful MACROs, Functions and DEFINEs, you can use them in
        * below implementation.
        * MACROs or Functions:
        * local_intr_save(): Disable interrupts
        * local_intr_restore(): Enable Interrupts
        * lcr3(): Modify the value of CR3 register
        * switch_to(): Context switching between two processes
        */
        
        bool intr_flag;
        struct proc_struct *prev = current, *next = proc;
        
        local_intr_save(intr_flag); // Disable interrupts
        {
            current = proc;          // Set the current process to the new one
            lcr3(next->cr3);         // Set the CR3 register to the next process's page table
            switch_to(&(prev->context), &(next->context)); // Perform context switch
        }
        local_intr_restore(intr_flag); // Enable interrupts
    }
}
```

此函数的基本思路是：

1. 让 `current` 指向 `next` 内核线程 `initproc`。
2. 设置 `CR3` 寄存器的值为 `next` 内核线程 `initproc` 的页目录表起始地址 `next->cr3`，这实际上是完成进程间的页表切换。
3. 由 `switch_to` 函数完成具体的两个线程的执行现场切换，即切换各个寄存器，当 `switch_to` 函数执行完 `ret` 指令后，就切换到 `initproc` 执行了。

值得注意的是，这里我们使用 `local_intr_save()` 和 `local_intr_restore()`，作用分别是屏蔽中断和打开中断，以免进程切换时其他进程再进行调度，保护进程切换不会被中断。

### 3.2 问题2

在本实验中，创建且运行了 2 个内核线程：

1. **idleproc**：第一个内核进程，完成内核中各个子系统的初始化，之后立即调度，执行其他进程。
2. **initproc**：用于完成实验的功能而调度的内核进程。











### 4. 扩展练习 Challenge

#### `local_intr_save` 和 `local_intr_restore`
这两个宏函数分别调用了 `intr_save` 和 `intr_restore` 内联函数来实现中断的禁止与启用。`do{...}while(0)` 结构确保宏在语法结构上的一致性，不引起语法错误。

## 三、与参考答案的对比
由于本实验的代码逻辑较为固定，因此我们小组所完成的代码与参考答案差别不大。

## 四、实验中的知识点

### 4.1 进程与线程的关系

在计算机系统中，我们编写的源代码经过编译器的编译，会变成可执行文件，这种文件我们通常称之为 **程序**。而当一个程序被操作系统或用户启动，系统会为它分配资源、装载到内存中并开始执行，此时它就成为了一个 **进程**。

#### 程序与进程的区别

- **程序**：程序是一个静态的实体，它仅仅是磁盘中的一段存储文件，是一组按特定顺序排列的指令。程序本身并不执行，只有被加载到内存中，并通过操作系统的调度和管理，它才成为了一个进程。
  
- **进程**：进程则是一个 **动态的实体**，它是正在执行的程序。进程除了包含程序的代码外，还包括运行时的各种信息，如堆、栈、程序计数器（PC）、CPU寄存器的值等。进程的执行状态体现了程序在某一时刻的执行过程。

#### 进程和线程

如果我们只关注于“正在运行”的部分，进程中的一部分可以被剥离出来形成 **线程**。线程是操作系统调度的最小单位，它执行进程中的具体任务。线程与进程的关系可以总结如下：

- **一个进程**可以包含 **多个线程**。
- 线程之间共享进程的内存空间，通常它们有相同的代码、数据和资源，但是它们有各自独立的执行栈和CPU状态。
- 进程作为资源的管理单元，负责为其内部的线程分配资源。而线程则是执行单元，调度器根据线程的优先级和其他因素进行调度。

#### 进程与线程的对比

| 特性        | 进程                              | 线程                              |
|-------------|-----------------------------------|-----------------------------------|
| 独立性      | 进程是独立的，不共享内存           | 线程共享进程的内存空间            |
| 调度单位    | 操作系统调度的基本单位            | 操作系统调度的最小单位            |
| 资源消耗    | 进程的创建和销毁开销较大           | 线程的创建和销毁开销较小          |
| 进程间通信  | 进程间通信较复杂，需要使用IPC机制 | 线程间通信较为简单，共享内存即可 |

### 4.2 进程调度

进程调度是操作系统中的一项重要任务，负责在多个进程之间合理地分配 CPU 时间。进程调度的目标是提高系统的响应速度、吞吐量以及资源的利用率。

#### 调度的代价

在进行进程调度时，涉及到一些代价和开销，主要包括：

- **上下文切换的代价**：每次进程切换时，操作系统需要保存当前进程的上下文信息（如CPU寄存器的值、程序计数器等），并恢复下一个进程的上下文信息。这一过程被称为 **上下文切换**，上下文切换的代价通常很高，尤其是在多核系统上，每次切换都会消耗 CPU 时间。
  
- **权限切换的代价**：操作系统中的进程运行通常需要涉及权限切换。例如，从用户空间切换到内核空间时，操作系统需要进行权限验证和切换，代价较高。

因此，减少不必要的上下文切换和权限切换是提高系统性能的关键之一。

#### 优化调度的策略

为了优化进程调度的效率，操作系统采用了一些理论和技术，如：

- **纤程 (Fiber)**：纤程是用户级线程的一种实现方式，它由用户空间的程序进行调度，而不是操作系统的内核。纤程的切换开销通常比操作系统内核级线程小，但需要程序员显式控制。纤程通过简单的上下文切换机制来减少系统的调度开销。
  
- **ucontext**：`ucontext` 是一种用于协程和纤程的机制，允许程序保存和恢复执行上下文。它为线程的切换提供了一种更轻量级的方式，在某些场景下可以减少内核级的调度开销。
  
- **协程 (Coroutine)**：协程是一种更加轻量级的执行单元，它比线程更小，但也有类似的调度机制。协程允许函数暂停并在以后某个时间恢复执行，从而实现了更高效的任务切换。协程的调度开销几乎为零，因此在需要大量任务调度的场景中非常有用。

#### 进程调度的优化目标

1. **减少上下文切换的开销**：通过减少不必要的上下文切换，操作系统能够提高系统的响应速度，避免因频繁的切换而浪费大量的 CPU 时间。
  
2. **提高资源利用率**：进程调度的核心目标是合理分配 CPU 时间，以最大限度地利用系统资源，确保各个进程和线程能够得到及时的处理。

3. **快速切换**：采用诸如纤程、协程等轻量级线程技术，以快速、低开销的方式切换执行上下文，提升系统性能。

4. **程序员的限制**：操作系统在调度时，可以对程序员提出一定的设计要求，例如要求在某些情况下避免频繁的线程切换，或者要求使用协程等轻量级任务管理方式。

### 结语

进程和线程是操作系统中最基本的调度单元。理解它们的关系和调度机制，对于优化系统性能、提高资源利用率至关重要。通过合理的进程和线程管理，我们可以减少调度的开销，提升系统的响应性和吞吐量。

