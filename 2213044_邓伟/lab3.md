# lab3 缺页异常和页面置换
## 一，实验目的
了解虚拟内存的Page Fault异常处理实现
了解页替换算法在操作系统中的实现
学会如何使用多级页表，处理缺页异常（Page Fault），实现页面置换算法。
## 二.实验内容
本次实验是在实验二的基础上，借助于页表机制和实验一中涉及的中断异常处理机制，完成Page Fault异常处理和部分页面替换算法的实现，结合磁盘提供的缓存空间，从而能够支持虚存管理，提供一个比实际物理内存空间“更大”的虚拟内存空间给系统使用。这个实验与实际操作系统中的实现比较起来要简单，不过需要了解实验一和实验二的具体实现。实际操作系统系统中的虚拟内存管理设计与实现是相当复杂的，涉及到与进程管理系统、文件系统等的交叉访问。如果大家有余力，可以尝试完成扩展练习，实现LRU页替换算法。
## 三.实验过程
### 练习1：理解基于FIFO的页面替换算法
_描述FIFO页面置换算法下，一个页面从被换入到被换出的过程中，会经过代码里哪些函数/宏的处理（或者说，需要调用哪些函数/宏），并用简单的一两句话描述每个函数在过程中做了什么？（为了方便同学们完成练习，所以实际上我们的项目代码和实验指导的还是略有不同，例如我们将FIFO页面置换算法头文件的大部分代码放在了kern/mm/swap_fifo.c文件中，这点请同学们注意)_
_至少正确指出10个不同的函数分别做了什么？如果少于10个将酌情给分。我们认为只要函数原型不同，就算两个不同的函数。要求指出对执行过程有实际影响,删去后会导致输出结果不同的函数（例如assert）而不是cprintf这样的函数。如果你选择的函数不能完整地体现”从换入到换出“的过程，比如10个函数都是页面换入的时候调用的，或者解释功能的时候只解释了这10个函数在页面换入时的功能，那么也会扣除一定的分数_

（按照）算法执行顺序：
1. 缺页处理触发
当访问的页面不在物理内存中时，操作系统会触发缺页异常，随后执行以下步骤：
- do_pgfault()（函数）：
处理缺页异常。这是页面置换算法的入口点，负责协调后续的页面置换流程。
- assert()（宏）：
断言宏，用于检查do_pgfault()是否被正确调用，并确保传入的参数有效。
2. 检查页面是否存在
在进行页面置换之前，需要检查访问的虚拟地址是否合法，以及对应的页表项是否存在：
- find_vma()（函数）：
检查访问出错的虚拟地址是否在进程的虚拟地址空间内，即是否合法。
- get_pte()（函数）：
获取给定虚拟地址对应的页表项。如果页表项不存在，可能需要分配新的页表项。
3. 页面换入换出
如果页表项存在，但页面不在物理内存中，或者需要替换一个页面，执行以下步骤：
- _fifo_map_swappable()（函数）：
当页面被换入时，将新页面添加到FIFO队列的末尾。
- list_add()（宏）：
将页面的pra_page_link添加到pra_list_head链表的末尾。
- _fifo_swap_out_victim()（函数）：
选择FIFO队列中的第一个页面（即最老的页面）作为换出页面。
- list_del()（宏）：
从pra_list_head链表中删除选定的换出页面。
- le2page()（宏）：
将链表节点转换回页面结构体指针，以便进行页面操作。
4. 页面数据读写
页面换入时需要从磁盘读取数据，换出时需要将数据写入磁盘：
- swap_in()（函数）：
如果页表项存在但页面被换出，调用swap_in()从磁盘读取数据到内存中。
- swapfs_read()（函数）：
调用内存和硬盘的I/O接口，读取硬盘中相应的内容到一个内存的物理页，实现换入过程。
- swap_out()（函数）：
负责将页面从物理内存换出到磁盘上。
- swapfs_write()（函数）：
把要换出页面的内容保存到硬盘中。
5. 页表项管理
页面换入换出过程中，需要对页表项进行管理：
- page_insert()（函数）：
根据换入的页面和虚拟地址建立映射，并刷新TLB。
- pte（宏）：
操作页表条目，如设置或清除特定的页表项标志。
6. 页面释放与链表管理
页面换出后，需要释放页面并更新FIFO队列：
- free_page()（函数）：
释放页面结构体，将其返回到空闲页面列表中。
- tlb_invalidate()（函数）：
刷新TLB，确保地址转换的一致性。
### 练习2：深入理解不同分页模式的工作原理
_get_pte()函数（位于kern/mm/pmm.c）用于在页表中查找或创建页表项，从而实现对指定线性地址对应的物理页的访问和映射操作。这在操作系统中的分页机制下，是实现虚拟内存与物理内存之间映射关系非常重要的内容。_

_get_pte()函数中有两段形式类似的代码， 结合sv32，sv39，sv48的异同，解释这两段代码为什么如此相像。_

_目前get_pte()函数将页表项的查找和页表项的分配合并在一个函数里，你认为这种写法好吗？有没有必要把两个功能拆开？_
1. sv32，sv39，sv48的异同
- Sv32：使用32位虚拟地址和34位物理地址，页表项大小为4字节，每个页表有1024个页表项，页表大小为4KiB。
- Sv39：使用39位虚拟地址和56位物理地址，页表项大小为8字节，每个页表可以被保存在一个页内。
- Sv48：类似于Sv39，但使用四级页表，虚拟地址中每一级页号仍然是9位，物理地址为56位，最高一级页表的页号为17位。
2. get_pte()函数中两段代码相似的原因
``` 
 pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create) {
    pde_t *pdep1 = &pgdir[PDX1(la)];
    if (!(*pdep1 & PTE_V)) {
        struct Page *page;
        if (!create || (page = alloc_page()) == NULL) {
            return NULL;
        }
        set_page_ref(page, 1);
        uintptr_t pa = page2pa(page);
        memset(KADDR(pa), 0, PGSIZE);
        *pdep1 = pte_create(page2ppn(page), PTE_U | PTE_V);
    }
    pde_t *pdep0 = &((pde_t *)KADDR(PDE_ADDR(*pdep1)))[PDX0(la)];
    if (!(*pdep0 & PTE_V)) {
        struct Page *page;
        if (!create || (page = alloc_page()) == NULL) {
            return NULL;
        }
        set_page_ref(page, 1);
        uintptr_t pa = page2pa(page);
        memset(KADDR(pa), 0, PGSIZE);
        *pdep0 = pte_create(page2ppn(page), PTE_U | PTE_V);
    }
    return &((pte_t *)KADDR(PDE_ADDR(*pdep0)))[PTX(la)];
}
```
- 页目录项（PDE）的查找和创建：
- - pde_t *pdep1 = &pgdir[PDX1(la)];：计算虚拟地址la在第一级页目录中的索引PDX1(la)，并获取对应的页目录项。
- - if (!(*pdep1 & PTE_V))：检查该页目录项是否已存在（即是否已映射）。
- - 如果不存在且create标志为真，则分配一个新页面作为页表，并初始化它：
- - - struct Page *page = alloc_page();：分配一个新页面。
- - - set_page_ref(page, 1);：设置页面引用计数为1。
- - - memset(KADDR(pa), 0, PGSIZE);：将新分配的页表清零。
- - - *pdep1 = pte_create(page2ppn(page), PTE_U | PTE_V);：创建页目录项，映射新分配的页表。
- 页表项（PTE）的查找和创建：
- - pde_t *pdep0 = &((pde_t *)KADDR(PDE_ADDR(*pdep1)))[PDX0(la)];：计算虚拟地址la在第二级页目录中的索引PDX0(la)，并获取对应的页目录项。
- - if (!(*pdep0 & PTE_V))：检查该页目录项是否已存在。
- - 如果不存在且create标志为真，则分配一个新页面作为页表，并初始化它：
- - - struct Page *page = alloc_page();：分配一个新页面。
- - - set_page_ref(page, 1);：设置页面引用计数为1。
- - - memset(KADDR(pa), 0, PGSIZE);：将新分配的页表清零。
- - - *pdep0 = pte_create(page2ppn(page), PTE_U | PTE_V);：创建页目录项，映射新分配的页表。
- 从代码的分析来看，代码相似性的原因如下：
- - 分页机制的共性：无论是Sv32、Sv39还是Sv48，它们都采用了多级页表的映射方式。在get_pte()函数中，核心逻辑是根据虚拟地址找到对应的页目录项和页表项。不同之处在于页表项的索引计算方式和物理地址的长度，但基本的操作逻辑是一致的。
- - 代码复用：为了减少代码重复，get_pte()函数中的两段代码处理了不同级别的页表项查找和创建。这种设计使得代码更加简洁，易于维护。
3. get_pte()函数将页表项的查找和页表项的分配合并在一个函数里的写法分析
我认为这种写法虽然提升了性能，减少了开销，并且提升了代码的简洁性，还减少了状态不一致的风险，但是将这些功能写到一起却增加了错误定位和针对性的问题解决问题。
原因：
- 无法确定具体错误位置：
当get_pte()函数在合并实现中返回NULL时，这可能意味着在查找或分配过程中的任何一步出现了问题，如页目录项或页表项的分配失败。由于所有操作都在同一个函数内完成，无法直接知道是哪一步具体导致了问题。
- 缺乏中间状态反馈：
在分开实现中，可以在查找和分配的每个步骤中添加检查点和错误处理代码，这样可以获得更多的中间状态反馈，帮助定位问题。而在合并实现中，由于缺少这样的中间检查点，一旦出现问题，就需要更多的调试信息来确定具体的错误位置。
- 统一的错误处理路径：
在合并实现中，由于查找和分配是连续进行的，任何级别的缺失都可能导致相同的错误处理路径。这意味着无法根据错误的具体级别（页目录或页表）来采取不同的处理措施。
- 灵活性降低：
在分开实现中，可以在查找到页目录或页表项缺失时立即进行针对性的分配和弥补。例如，如果发现页目录项缺失，可以直接分配一个新的页表并更新页目录项。而在合并实现中，由于查找和分配是连续的，可能需要回溯或额外的逻辑来处理这种针对性的分配。
### 练习3：给未被映射的地址映射上物理页
1. 补充完成do_pgfault（mm/vmm.c）函数，给未被映射的地址映射上物理页。设置访问权限 的时候需要参考页面所在 VMA 的权限，同时需要注意映射物理页时需要操作内存控制 结构所指定的页表，而不是内核的页表。

VMA指的是struct vm_area_struct，它是一个数据结构，用于描述进程地址空间中的一段虚拟区域

代码补充:
```
int
do_pgfault(struct mm_struct *mm, uint_t error_code, uintptr_t addr) {
    int ret = -E_INVAL;
    //try to find a vma which include addr
    struct vma_struct *vma = find_vma(mm, addr);//调用find_vma函数来查找包含地址addr的虚拟内存区域（VMA）。mm是进程的内存管理结构，包含了进程的页表。

    pgfault_num++;
    //If the addr is in the range of a mm's vma?
    if (vma == NULL || vma->vm_start > addr) {
        cprintf("not valid addr %x, and  can not find it in vma\n", addr);
        goto failed;
    }

    /* IF (write an existed addr ) OR
     *    (write an non_existed addr && addr is writable) OR
     *    (read  an non_existed addr && addr is readable)
     * THEN
     *    continue process
     */
    uint32_t perm = PTE_U;
    if (vma->vm_flags & VM_WRITE) {
        perm |= (PTE_R | PTE_W);
    }//如果VMA的vm_flags标志中设置了VM_WRITE，则表示该区域可写，因此将perm的读权限PTE_R和写权限PTE_W位设置。
    addr = ROUNDDOWN(addr, PGSIZE);

    ret = -E_NO_MEM;

    pte_t *ptep=NULL;
    /*
    * Maybe you want help comment, BELOW comments can help you finish the code
    *
    * Some Useful MACROs and DEFINEs, you can use them in below implementation.
    * MACROs or Functions:
    *   get_pte : get an pte and return the kernel virtual address of this pte for la
    *             if the PT contians this pte didn't exist, alloc a page for PT (notice the 3th parameter '1')
    *   pgdir_alloc_page : call alloc_page & page_insert functions to allocate a page size memory & setup
    *             an addr map pa<--->la with linear address la and the PDT pgdir
    * DEFINES:
    *   VM_WRITE  : If vma->vm_flags & VM_WRITE == 1/0, then the vma is writable/non writable
    *   PTE_W           0x002                   // page table/directory entry flags bit : Writeable
    *   PTE_U           0x004                   // page table/directory entry flags bit : User can access
    * VARIABLES:
    *   mm->pgdir : the PDT of these vma
    *
    */


    ptep = get_pte(mm->pgdir, addr, 1);  //(1) try to find a pte, if pte's
                                         //PT(Page Table) isn't existed, then
                                         //create a PT.
    if (*ptep == 0) {//如果页表项ptep为0，说明还没有建立映射，需要分配物理页面并建立映射。
        if (pgdir_alloc_page(mm->pgdir, addr, perm) == NULL) {
            cprintf("pgdir_alloc_page in do_pgfault failed\n");
            goto failed;
        }
    } else {
        /*LAB3 EXERCISE 3: YOUR CODE
        * 请你根据以下信息提示，补充函数
        * 现在我们认为pte是一个交换条目，那我们应该从磁盘加载数据并放到带有phy addr的页面，
        * 并将phy addr与逻辑addr映射，触发交换管理器记录该页面的访问情况
        *
        *  一些有用的宏和定义，可能会对你接下来代码的编写产生帮助(显然是有帮助的)
        *  宏或函数:
        *    swap_in(mm, addr, &page) : 分配一个内存页，然后根据
        *    PTE中的swap条目的addr，找到磁盘页的地址，将磁盘页的内容读入这个内存页
        *    page_insert ： 建立一个Page的phy addr与线性addr la的映射
        *    swap_map_swappable ： 设置页面可交换
        */
        if (swap_init_ok) {
            struct Page *page = NULL;
            // 你要编写的内容在这里，请基于上文说明以及下文的英文注释完成代码编写
            //(1）According to the mm AND addr, try
            //to load the content of right disk page
            //into the memory which page managed.
            //(2) According to the mm,
            //addr AND page, setup the
            //map of phy addr <--->
            //logical addr
            //(3) make the page swappable.
            swap_in(mm, addr, &page); // 这里传入page的地址，以便swap_in可以修改它
            page_insert(mm->pgdir, page, addr, perm); // 建立物理地址和逻辑地址之间的映射
            swap_map_swappable(mm, addr, page, 1); // 设置页面可交换
            page->pra_vaddr = addr;
        } else {
            cprintf("no swap_init_ok but ptep is %x, failed\n", *ptep);
            goto failed;
        }
   }

   ret = 0;
failed:
    return ret;
}
```
_请在实验报告中简要说明你的设计实现过程。请回答如下问题：

2. 请描述页目录项（Page Directory Entry）和页表项（Page Table Entry）中组成部分对ucore实现页替换算法的潜在用处。
- Clock（时钟）算法
- - 使用的PTE和PDE标志位：

PTE_A（Accessed）：表示页面是否被访问过。

PTE_D（Dirty）：表示页面是否被修改过。

- - 具体实现：

Clock算法通过一个环形链表来维护内存中的页面，每个页面在链表中有一个节点。算法使用一个指针（称为“时钟指针”）来跟踪最老的页面。当需要替换页面时，从时钟指针开始顺时针遍历链表，直到找到一个访问位为0的页面进行替换。如果访问位为1，则将其清零并继续搜索。
- Extended Clock（扩展时钟）算法

- - 使用的PTE和PDE标志位：

PTE_A（Accessed）：表示页面是否被访问过。

PTE_D（Dirty）：表示页面是否被修改过。

- - 具体实现：

Extended Clock算法是Clock算法的扩展，它考虑了页面的脏位（Dirty bit）。在替换页面时，算法会优先考虑访问位为0且修改位为0的页面。如果找不到这样的页面，它会查找访问位为0但修改位为1的页面，并将这些页面写回磁盘后再进行替换。

实现Enhanced Clock算法时，需要一个环形链表和一个指针。指针指向下一个要考虑替换的页面。当页面被访问时，访问位PTE_A被设置为1。当页面被修改时，修改位PTE_D被设置为1。在替换页面时，算法会清除访问位为1的页面的PTE_A位，并继续搜索直到找到合适的页面进行替换。

3. 如果ucore的缺页服务例程在执行过程中访问内存，出现了页访问异常，请问硬件要做哪些事情？

- 保存当前指令的地址：处理器会将触发异常的线性地址保存到控制寄存器CR2（一个控制寄存器，其主要功能是用于报告页异常时的错误信息。）中。
- 压栈异常帧：处理器会自动将当前的某些寄存器状态压栈，这包括：
- - EFLAGS寄存器：保存处理器的状态标志。
- - CS（代码段寄存器）：保存当前的代码段选择子。
- - EIP（指令指针寄存器）：保存发生异常时的下一条指令的地址。
- - 错误码：对于某些异常，处理器还会压栈一个错误码，它提供了关于异常的额外信息。
- 触发异常中断：处理器会触发一个中断，将控制权转移到操作系统定义的异常处理例程（中断向量表中对应的处理函数）。
- 切换到内核模式：处理器从当前的模式（可能是用户模式）切换到内核模式，以便执行内核级别的异常处理代码。
- 调用缺页处理函数：操作系统的异常处理例程会识别出这是一个缺页异常，并调用缺页处理函数do_pgfault来处理。
- 更新页表：如果缺页处理函数成功找到或分配了物理页面，并建立了虚拟地址到物理地址的映射，它会更新页表项（PTE）。
- 恢复上下文：一旦缺页处理完成，操作系统会恢复之前保存的处理器状态，包括寄存器和程序计数器。
- 重新执行指令：处理器会重新执行之前触发缺页异常的指令，此时由于页表已经更新，所以可以正常访问内存。
4. 数据结构Page的全局变量（其实是一个数组）的每一项与页表中的页目录项和页表项有无对应关系？如果有，其对应关系是啥？
```
struct Page {
    int ref;                        // page frame's reference counter
    uint_t flags;        // array of flags that describe the status of the page frame
    uint_t visited;
    unsigned int property;    // the num of free block, used in first fit pm manager
    list_entry_t page_link;         // free list link
    list_entry_t pra_page_link;     // used for pra (page replace algorithm)
    uintptr_t pra_vaddr;            // used for pra (page replace algorithm)
};
```
- 引用计数器（ref）：
PTE：PTE中没有直接对应的字段，但可以通过操作系统的内存管理逻辑间接关联。引用计数器用于跟踪页面的使用情况，当页面被多个虚拟页面共享时，这个计数器会增加。操作系统需要确保在页面被写回磁盘或释放之前，页面不会被错误地释放。
- 标志数组（flags）：
PTE：PTE中有多个标志位，如存在位（P）、修改位（D）、访问位（A）等。flags字段可以用来存储操作系统特定的页面状态信息，而PTE中的标志位用于控制页面的访问权限和行为。
- 访问标志（visited）：
PTE：与PTE中的访问位（A）相对应。这个字段可以用来跟踪页面是否被访问过，这在某些页面替换算法（如LRU）中非常重要。
- 空闲列表链接（page_link）：
PDE/PTE：没有直接对应的字段。这个字段用于链接空闲页面，以便操作系统可以快速分配和回收内存页。页目录和页表项主要用于虚拟地址到物理地址的映射，而不直接管理空闲页面。
- 页面替换算法链接（pra_page_link）：
PDE/PTE：没有直接对应的字段。这个字段用于在页面替换算法中链接页面，例如在FIFO或LRU算法中。它允许操作系统跟踪页面的使用历史，以便在需要时替换掉合适的页面。
- 页面替换算法虚拟地址（pra_vaddr）：
PDE/PTE：没有直接对应的字段。这个字段存储页面替换算法中使用的虚拟地址，用于快速访问和替换页面。
### 练习4：补充完成Clock页替换算法
1. 代码补充
```
tatic int
_clock_init_mm(struct mm_struct *mm)
{     
     /*LAB3 EXERCISE 4: YOUR CODE*/ 
     // 初始化pra_list_head为空链表
     // 初始化当前指针curr_ptr指向pra_list_head，表示当前页面替换位置为链表头
     // 将mm的私有成员指针指向pra_list_head，用于后续的页面替换算法操作
     //cprintf(" mm->sm_priv %x in fifo_init_mm\n",mm->sm_priv);
     list_init(&pra_list_head);
     curr_ptr = &pra_list_head;
     mm->sm_priv = &pra_list_head;
     return 0;
}
```
pra_list_head 作为链表的头节点。
将内存管理结构mm的私有成员指针指向pra_list_head，这样可以通过mm结构体来访问FIFO队列。
```
static int
_clock_map_swappable(struct mm_struct *mm, uintptr_t addr, struct Page *page, int swap_in)
{
    list_entry_t *entry=&(page->pra_page_link);
 
    assert(entry != NULL && curr_ptr != NULL);
    //record the page access situlation
    /*LAB3 EXERCISE 4: YOUR CODE*/ 
    // link the most recent arrival page at the back of the pra_list_head qeueue.
    // 将页面page插入到页面链表pra_list_head的末尾
    // 将页面的visited标志置为1，表示该页面已被访问
    list_entry_t *head = (list_entry_t *)mm->sm_priv;
    list_add(head, entry);
    page->visited = 1;
    return 0;
}
```
list_entry_t *head = (list_entry_t *)mm->sm_priv;：从内存管理结构中获取页面链表的头节点。
将页面添加到链表的末尾。(这里细致一点可使用list_add_before或list_add_after函数，具体取决于链表的实现和页面链表的顺序要求。)
```
tatic int
_clock_swap_out_victim(struct mm_struct *mm, struct Page ** ptr_page, int in_tick)
{
     list_entry_t *head=(list_entry_t*) mm->sm_priv;
         assert(head != NULL);
     assert(in_tick==0);
     /* Select the victim */
     //(1)  unlink the  earliest arrival page in front of pra_list_head qeueue
     //(2)  set the addr of addr of this page to ptr_page
    while (1) {
        /*LAB3 EXERCISE 4: YOUR CODE*/ 
        // 编写代码
        // 遍历页面链表pra_list_head，查找最早未被访问的页面
        // 获取当前页面对应的Page结构指针
        // 如果当前页面未被访问，则将该页面从页面链表中删除，并将该页面指针赋值给ptr_page作为换出页面
        // 如果当前页面已被访问，则将visited标志置为0，表示该页面已被重新访问
        if (curr_ptr == head) {
            curr_ptr = list_prev(head); // 当curr_ptr指向头节点时，移动到链表的最后一个元素
            continue;
        }
        struct Page* curr_page = le2page(curr_ptr, pra_page_link);
        if (curr_page->visited == 0) {
            cprintf("curr_ptr %p\n", curr_ptr);
            *ptr_page = curr_page; // 在删除页面之前设置ptr_page
            list_del(curr_ptr); // 删除curr_ptr指向的页面
            return 0;
        }
        curr_page->visited = 0; // 标记页面为已访问
        curr_ptr = list_prev(curr_ptr); // 移动到前一个页面
    }
    return 0;
}
```
通过while循环遍历链表，查找最早未被访问的页面。
检查当前指针是否指向链表头节点。如果是，移动到链表的最后一个元素，继续循环。
然后将当前链表项转换为页面结构，紧接着检查页面是否未被访问。
如果页面未被访问，将其地址赋给ptr_page，这将是被换出的页面，并删除当前页面。
如果页面已被访问，将其visited标志置为0，表示该页面已被重新访问。
最后，移动到链表中的前一个页面。

2. Clock页替换算法与FIFO算法的不同
- 页面访问记录：

FIFO算法：FIFO（先进先出）算法不记录页面的访问历史，它仅根据页面进入内存的顺序来决定页面的替换。每个页面只有进入时间的记录，没有访问时间的记录.

Clock算法：Clock算法给每个页帧关联一个使用位（也称为引用位）。当页面被访问时，该位被设置为1，表示页面被引用过。这种记录方式允许算法在需要置换页面时，根据页面的访问情况来选择替换的页面.

- 页面替换策略：

FIFO算法：FIFO算法采用简单的队列机制，总是替换最早进入内存的页面。这种方法不考虑页面的使用频率和访问模式，可能导致频繁使用的页面被替换出去.

Clock算法：Clock算法使用一个循环链表来维护页面，每个页面有一个引用位。当需要置换页面时，Clock算法会扫描链表，跳过引用位为1的页面，直到找到一个引用位为0的页面进行替换。这样可以减少频繁访问页面的替换，提高页面命中率.

- 算法复杂度：

FIFO算法：FIFO算法的时间复杂度较低，因为它只需要维护一个队列，并且在队列头部进行页面替换，操作通常为O(1).

Clock算法：Clock算法的时间复杂度相对较高，因为它需要遍历链表来查找可以替换的页面。在最坏的情况下，可能需要遍历整个链表，时间复杂度为O(n)，其中n是内存中的页面数.

- 适用场景：

FIFO算法：FIFO算法适用于页面访问模式较为随机，且对算法实现简单性有要求的场景。由于其简单性，FIFO算法在资源有限的嵌入式系统或者对性能要求不高的场景中较为常见.

Clock算法：Clock算法适用于页面访问模式具有局部性特征的场景，尤其是在需要频繁页面置换的情况下。它可以减少频繁访问页面的替换，提高页面命中率，适用于对性能有一定要求的系统.
### 练习5：阅读代码和实现手册，理解页表映射方式相关知识（思考题）
1. 如果我们采用”一个大页“ 的页表映射方式，相比分级页表，有什么好处、优势，有什么坏处、风险？
- 好处与优势
- - 减少TLB失效情况：
大页技术可以减少Translation Lookaside Buffer（TLB）的失效情况。因为大页映射的内存区域更大，所以需要的页表项更少，从而减少了TLB缓存的压力，提高了TLB的命中率。
- - 减少页表内存消耗：
使用大页可以减少页表的内存消耗。由于页表项数量减少，整个页表的大小也随之减少，这对于内存资源是一种节省。
- - 减少缺页中断次数：
大页技术可以减少Page Fault（缺页中断）的次数。对于需要连续大块内存的应用，使用大页可以减少因多次小页映射导致的频繁缺页中断。
- - 提高内存访问性能：
对于占用大量内存的程序，大页可以提升性能，因为TLB miss和缺页中断的减少可以显著提高内存访问速度，性能提升可达50%左右。
- - 内存页不会swap到磁盘上：
大页内存的页面不会被swap到磁盘上，这对于需要保持数据在物理内存中的应用来说是一个优势。
- 坏处与风险
- - 内存内碎片问题：
使用大页可能导致内存内碎片问题。操作系统申请内存时总是申请一大块内存，哪怕实际只需要很小的内存，导致大页内存得不到充分利用；而且内存很快会被这些大页侵占。
- - 内存使用灵活性降低：
程序使用内存小，却申请了大页内存，会造成内存浪费，因为内存分配最小单位是页。对于小内存需求的程序，大页可能导致内存使用效率降低。
- - 特定使用方式要求：
大页内存的使用不像普通内存申请那么简单，需要借助Hugetlb文件系统来创建和管理，这增加了使用复杂性。
- - 内存分配和管理复杂性：
对于超过1GB的大页内存分配，需要在Linux的启动项中设置和挂载，这增加了系统配置和管理的复杂性。
- - 可能导致内存浪费：
在内存较小的系统中，如果不当使用大页内存，可能会导致大量内存被占用而未被充分利用，从而浪费宝贵的内存资源。

## 四.实验一些知识点补充
- 虚拟内存管理：
理解虚拟内存的概念，以及它如何提供比物理内存更大的地址空间。
- 页表和页表项：
掌握页表和页表项的结构，以及它们如何将虚拟地址映射到物理地址。
- 页面替换算法：
学习FIFO和Clock等页面替换算法的原理和实现，以及它们对系统性能的影响。
- 缺页异常处理：
理解缺页异常的概念，以及如何在操作系统中处理缺页异常。
- 内存分配器：
学习如何在操作系统中实现内存分配器，以及如何管理内存的分配和回收。
- 内核同步机制：
掌握内核同步机制，如锁和信号量，以及它们在多线程环境中的重要性。
- 系统调用：
理解系统调用的实现过程，以及它们如何允许用户空间程序请求操作系统服务。
- 中断和异常处理：
学习中断和异常处理机制，以及它们在操作系统中的作用。



