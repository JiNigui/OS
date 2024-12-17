# lab5
## 一.实验目的
- 了解第一个用户进程创建过程
- 了解系统调用框架的实现机制
- 了解ucore如何实现系统调用sys_fork/sys_exec/sys_exit/sys_wait来进行进程管理
## 二.实验内容
### 练习1
1. do_execv函数调用load_icode（位于kern/process/proc.c中）来加载并解析一个处于内存中的ELF执行文件格式的应用程序。你需要补充load_icode的第6步，建立相应的用户内存空间来放置应用程序的代码段、数据段等，且要设置好proc_struct结构中的成员变量trapframe中的内容，确保在执行此进程后，能够从应用程序设定的起始执行地址开始执行。需设置正确的trapframe内容。
请在实验报告中简要说明你的设计实现过程。
```
    /* LAB5:EXERCISE1 YOUR CODE
     * should set tf->gpr.sp, tf->epc, tf->status
     * NOTICE: If we set trapframe correctly, then the user level process can return to USER MODE from kernel. So
     *          tf->gpr.sp should be user stack top (the value of sp)
     *          tf->epc should be entry point of user program (the value of sepc)
     *          tf->status should be appropriate for user program (the value of sstatus)
     *          hint: check meaning of SPP, SPIE in SSTATUS, use them by SSTATUS_SPP, SSTATUS_SPIE(defined in risv.h)
     */
    tf->gpr.sp = USTACKTOP;
    tf->epc = elf->e_entry;
    // 将SPP设置为0，这样我们就可以返回到用户模式（User Mode）
    // 将SPIE设置为1，这样我们就可以处理中断
    tf->status = (sstatus & ~SSTATUS_SPP) | SSTATUS_SPIE;
```
- 设置栈指针：将陷阱帧中的通用寄存器gpr.sp设置为用户栈的顶部地址（USTACKTOP）。这样，当用户程序执行时，它将能够正确地访问栈空间。
- 设置程序计数器：将陷阱帧中的程序计数器epc设置为ELF文件头中的入口点地址（e_entry）。这个地址是用户程序开始执行的地方。在load_icode函数中，通过struct elfhdr *elf = (struct elfhdr *)binary;获取了ELF文件的文件头，从而获得了入口点地址。
- 设置状态寄存器：配置陷阱帧中的状态寄存器status，特别是两个关键的状态位：SPP（Supervisor Previous Privilege）和SPIE（Supervisor Interrupt Enable）。
- - SPP：这个位表示在异常或中断发生之前处理器所处的特权级别。它有两个可能的值：
- - - 0：表示处理器在异常或中断之前处于用户模式（User Mode）。
- - - 1：表示处理器在异常或中断之前处于特权模式（Supervisor Mode）。 由于我们希望在处理完异常或中断后，能够通过sret指令返回到用户模式，因此SPP应该设置为0。
- - SPIE：这个位表示在异常或中断发生之前中断的使能状态。它也有两个可能的值：
- - - 0：表示处理器在异常或中断之前中断被禁用。
- - - 1：表示处理器在异常或中断之前中断被使能。 为了保证用户程序能够正常触发中断，我们需要使能中断，因此SPIE应该设置为1。
2. 请简要描述这个用户态进程被ucore选择占用CPU执行（RUNNING态）到具体执行应用程序第一条指令的整个经过。
- 准备执行环境：
- - 清空用户空间：通过do_execve函数清空用户进程的内存空间，为加载新的执行代码做准备。
- - 检查内存管理结构（mm）：如果mm不为空，表示这是一个用户进程，需要设置页表以转入内核态。
- - 释放内存：如果mm的引用数为1，意味着没有其他进程使用这块内存，当进程结束后，应释放其占用的内存和页表空间。
- 加载程序和建立用户环境：
- - 创建内存管理结构：使用mm_create为进程分配内存管理数据结构mm。
- - 设置页目录：通过setup_pgdir申请页目录表所需的内存，并复制内核页表内容以映射内核空间。
- - 解析ELF文件：加载ELF格式的程序，使用mm_map根据程序段信息建立虚拟内存区域（VMA）。
- - 分配物理内存：为程序段分配物理内存，并建立物理地址到虚拟地址的映射。
- - 设置用户栈：通过mm_map建立用户栈的VMA结构，栈位于用户虚空间顶端，大小为1MB。
- 更新虚拟内存空间：
- - 更新页目录：将mm->pgdir的值写入CR3寄存器，完成用户进程虚拟内存空间的更新。
- 建立执行现场：
- - 清空中断帧：清除当前进程的中断帧。
- - 设置中断帧：重新设置中断帧，确保执行iret指令后，CPU能切换到用户态，使用用户态的内存和栈，并从用户进程的第一条指令开始执行。
### 练习2
1. 创建子进程的函数do_fork在执行中将拷贝当前进程（即父进程）的用户内存地址空间中的合法内容到新进程中（子进程），完成内存资源的复制。具体是通过copy_range函数（位于kern/mm/pmm.c中）实现的，请补充copy_range的实现，确保能够正确执行。
请在实验报告中简要说明你的设计实现过程。
```
            /* LAB5:EXERCISE2 YOUR CODE
             * replicate content of page to npage, build the map of phy addr of
             * nage with the linear addr start
             *
             * Some Useful MACROs and DEFINEs, you can use them in below
             * implementation.
             * MACROs or Functions:
             *    page2kva(struct Page *page): return the kernel vritual addr of
             * memory which page managed (SEE pmm.h)
             *    page_insert: build the map of phy addr of an Page with the
             * linear addr la
             *    memcpy: typical memory copy function
             *
             * (1) find src_kvaddr: the kernel virtual address of page
             * (2) find dst_kvaddr: the kernel virtual address of npage
             * (3) memory copy from src_kvaddr to dst_kvaddr, size is PGSIZE
             * (4) build the map of phy addr of  nage with the linear addr start
             */

            void *src_kvaddr = page2kva(page);//找出page的内核虚拟地址
            void *dst_kvaddr = page2kva(npage);//找出npage的内核虚拟地址
            memcpy(dst_kvaddr, src_kvaddr, PGSIZE);//内存拷贝，从src_kvaddr到dst_kvaddr，大小为PGSIZE
            ret = page_insert(to, npage, start, perm);//建立npage的物理地址和线性地址start的映射
```
copy_range函数调用过程：
- 进程创建与内存复制：
do_fork()函数是创建新进程的起点，它调用copy_mm()来处理内存复制的工作。
- 内存管理复制：
copy_mm()函数负责复制内存管理结构。它使用互斥锁来防止多个进程同时修改内存，然后调用dup_mmap()来复制内存映射。
- 内存映射复制：
dup_mmap()函数接受两个参数：mm（新进程的内存管理结构）和oldmm（父进程的内存管理结构）。它在新进程中创建内存段，但具体的内存页复制工作由copy_range()函数完成。
- 内存页复制：
copy_range()函数在内存页级别上执行复制。它首先使用get_pte()获取源页表项，并验证其有效性。接着，在目标页表中为新页分配内存并创建页表项。

- - 转换源地址：将源page转换为源虚拟内存地址。
- - 转换目标地址：将目标page转换为目标虚拟内存地址。
- - 执行内存复制：使用memcpy()函数复制内存内容。
- - 更新页表：使用page_insert()函数更新目标进程的页表，确保新页的虚拟地址和物理地址正确映射，同时设置适当的权限。
2. 如何设计实现Copy on Write机制？给出概要设计，鼓励给出详细设计。
_Copy-on-write（简称COW）的基本概念是指如果有多个使用者对一个资源A（比如内存块）进行读操作，则每个使用者只需获得一个指向同一个资源A的指针，就可以该资源了。若某使用者需要对这个资源A进行写操作，系统会对该资源进行拷贝操作，从而使得该“写操作”使用者获得一个该资源A的“私有”拷贝—资源B，可对资源B进行写操作。该“写操作”使用者对资源B的改变对于其他的使用者而言是不可见的，因为其他使用者看到的还是资源A。_
- 资源共享：当多个进程需要读取相同的资源（例如内存页）时，它们可以共享对原始资源的访问，而不是各自拥有一个独立的副本。这可以通过copy_range函数中的share参数来实现。根据share参数的值，copy_range函数会决定是复制资源（dup）还是共享资源。
- 检测写操作：如果一个进程尝试写入一个共享资源，系统需要检测到这个写操作。这通常通过内存保护机制实现。当进程试图修改一个被标记为只读的内存区域时，硬件会触发一个异常。操作系统可以通过定义一个新的陷阱（trap）类型，并在trap.c文件的exception_handler函数中处理这个异常。
- 资源复制：一旦检测到写操作，系统会为该进程分配新的内存或磁盘空间，并将原始资源的内容复制到新分配的空间中。这个过程可以通过调用copy_range函数来完成。
- 更新指针：系统将更新尝试写入资源的进程的指针，使其指向新复制的资源。这样，该进程的写操作只会影响其私有的资源副本，而不会影响其他进程所看到的原始资源。

![copy on write](img\lab5\cow.png)
### 练习3
请在实验报告中简要说明你对 fork/exec/wait/exit函数的分析。
并回答如下问题：
- 请分析fork/exec/wait/exit的执行流程。重点关注哪些操作是在用户态完成，哪些是在内核态完成？内核态与用户态程序是如何交错执行的？内核态执行结果是如何返回给用户程序的？
- 请给出ucore中一个用户态进程的执行状态生命周期图（包执行状态，执行状态之间的变换关系，以及产生变换的事件或函数调用）。（字符方式画即可）
1. fork/exec/wait/exit函数的分析
- fork系统调用：
- - 用于创建新进程。调用链为：fork() -> SYS_fork -> do_fork() + wakeup_proc()。
- - 首先检查系统进程数量是否超过限制，如果是，则返回错误。
- - 使用alloc_proc()创建并初始化新的进程控制块。
- - 通过setup_kstack()为新进程设置内核栈。
- - copy_mm()用于复制或共享内存空间。
- - copy_thread()复制父进程的中断帧和上下文信息。
- - get_pid()分配一个唯一的进程ID。
- - 将新进程加入到哈希表和链表中，建立进程间的链接。
- - 返回新进程的PID。
- exec系统调用：
- - 用于启动新程序。调用链为：SYS_exec -> do_execve()。
- - 验证程序名称的地址和长度，合法则保存在栈中，否则返回错误。
- - 将页表基址cr3指向内核页表，释放进程的内存管理区域。
- - load_icode()加载程序代码到内存并建立新的内存映射，加载失败则报错。
- - set_proc_name()用于设置进程名称。
- wait系统调用：
- - 用于回收子进程资源。调用链为：SYS_wait -> do_wait()。
- - 检查返回码存储地址code_store是否合法。
- - 根据PID查找子进程，等待其状态变为僵尸状态。
- - 如果没有子进程或子进程状态不正确，返回错误或休眠后重试。
- - 子进程进入僵尸状态后，回收其资源。
- exit系统调用：
- - 用于结束当前进程。调用链为：SYS_exit -> exit()。
- - 释放进程的虚拟内存空间。
- - 将进程状态设置为僵尸状态。
- - 如果父进程正在等待，唤醒父进程。
- - 将当前进程的子进程转交给initproc。
- - 完成僵尸状态子进程的资源回收。
- - 调用调度函数，切换到其他进程执行。
2. 问题1
- fork系统调用：
- - 用户态：父进程执行fork()系统调用，请求创建一个子进程。
- - 内核态：操作系统内核负责复制父进程的资源，包括内存和文件描述符，以初始化子进程。
- - 用户态：子进程完成初始化后，从fork()调用返回，获取自己的进程ID（PID）。同时，父进程也从fork()调用返回，获得子进程的PID。
- exec系统调用：
- - 用户态：进程执行exec()系统调用，请求加载并启动一个新的程序。
- - 内核态：内核负责加载新程序的代码和数据段，进行必要的设置，如内存布局和环境变量。
- - 用户态：新程序的代码开始执行，替换掉原进程的代码空间。
- wait系统调用：
- - 用户态：父进程调用wait()或waitpid()系统调用，以等待子进程结束。
- - 内核态：内核检查子进程是否已经结束。如果子进程已退出，内核将子进程的退出状态传递给父进程；如果子进程还在运行，父进程将被挂起，直到子进程结束。
- - 用户态：父进程接收到子进程的退出状态后，可以进行清理或获取子进程的退出代码等后续处理。
- exit系统调用：
- - 用户态：进程通过exit()系统调用来通知内核它准备终止。
- - 内核态：内核开始清理进程资源，包括内存释放、文件描述符关闭等。
- - 用户态：进程完成清理后，正式退出，控制权返回给父进程或内核的调度器。
3. 问题2
![生命周期图](img\lab5\lifecycle.png)
### 扩展练习
1. 实现 Copy on Write （COW）机制

给出实现源码,测试用例和设计报告（包括在cow情况下的各种状态转换（类似有限状态自动机）的说明）。

这个扩展练习涉及到本实验和上一个实验“虚拟内存管理”。在ucore操作系统中，当一个用户父进程创建自己的子进程时，父进程会把其申请的用户空间设置为只读，子进程可共享父进程占用的用户内存空间中的页面（这就是一个共享的资源）。当其中任何一个进程修改此用户内存空间中的某页面时，ucore会通过page fault异常获知该操作，并完成拷贝内存页面，使得两个进程都有各自的内存页面。这样一个进程所做的修改不会被另外一个进程可见了。请在ucore中实现这样的COW机制。

由于COW实现比较复杂，容易引入bug，请参考 https://dirtycow.ninja/ 看看能否在ucore的COW实现中模拟这个错误和解决方案。需要有解释。

这是一个big challenge.

（暂未设计，只是进行理论分析）
2. 说明该用户程序是何时被预先加载到内存中的？与我们常用操作系统的加载有何区别，原因是什么？
- 用户程序的加载时机：
在ucore实验中，用户程序（如hello程序）是在系统启动时预先加载到内存中的。这是通过链接器（ld）在编译过程中将用户程序的执行代码与ucore内核代码链接在一起实现的。链接器会记录用户程序的起始位置和大小，使得它能够与ucore内核一起被引导加载程序（bootloader）加载进内存。
- 与常用操作系统的加载区别：
在我们常用的操作系统中，应用程序通常不会在系统启动时就被加载到内存。相反，当用户启动应用程序时，操作系统会根据需要动态地加载应用程序到内存中。这种加载方式被称为延迟加载（Lazy Loading）或按需加载（On-Demand Loading）。
- 原因：
- - 静态加载：ucore中的hello程序与内核一起静态加载，这是因为hello程序需要紧跟内核的初始化进程（init_proc）执行。由于ucore实验环境可能没有实现复杂的调度机制，hello程序在系统启动时就加载，以确保它能够及时执行。
- - 内存管理：静态加载允许ucore在系统启动时就准备好执行关键的用户程序，但这也可能意味着在启动时就会占用更多的内存。如果ucore的内核空间有限，一次性加载大量程序可能会导致内存不足。
- - 实验环境限制：在ucore实验环境中，由于没有实现用户态应用程序的动态调度，因此hello程序在系统启动时就加载到内存中，以便在不使用调度的情况下直接执行。
## 三.实验补充
- ELF文件加载：load_icode函数负责将ELF格式的程序加载到内存中。这涉及到解析ELF头，为程序的代码段、数据段和BSS段分配内存，并设置页表项。
- Trap Frame设置：在用户态进程开始执行前，需要正确设置trapframe，以便在进程切换回用户态时能够从正确的地址开始执行。
- Copy-on-Write (COW)：当父进程和子进程共享内存时，如果子进程尝试写入内存，操作系统会创建一个写时复制的私有副本，这样父进程的内存不会被改变。
- 进程状态转换：进程在不同的状态之间转换，如UNINIT、RUNNABLE、RUNNING、SLEEPING和ZOMBIE。这些状态转换由系统调用如fork、exec、wait和exit触发。
- 系统调用：用户程序通过系统调用与内核交互，如SYS_fork、SYS_exec和SYS_wait。这些调用在用户态发起，在内核态执行。
- 内核态与用户态：操作系统在内核态和用户态之间切换，以执行不同的任务。系统调用和硬件中断是触发状态切换的常见原因。