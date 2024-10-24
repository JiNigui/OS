# lab2
## 一.实验目的
- 理解页表的建立和使用方法
- 理解物理内存的管理方法
- 理解页面分配算法
## 二.实验内容
实验一过后大家做出来了一个可以启动的系统，实验二主要涉及操作系统的物理内存管理。操作系统为了使用内存，还需高效地管理内存资源。本次实验我们会了解如何发现系统中的物理内存，然后学习如何建立对物理内存的初步管理，即了解连续物理内存管理，最后掌握页表相关的操作，即如何建立页表来实现虚拟内存到物理内存之间的映射，帮助我们对段页式内存管理机制有一个比较全面的了解。本次的实验主要是在实验一的基础上完成物理内存管理，并建立一个最简单的页表映射。
## 三.实验过程
### 练习1：理解first-fit连续物理内存分配算法
first-fit 连续物理内存分配算法是物理内存分配的一个很基础的方法，本实验需要理解它的实现过程。那么要仔细阅读手册并结合`kern/mm/default_pmm.c`中相关代码，认真分析default_init，default_init_memmap，default_alloc_pages， default_free_pages等相关函数，并描述程序在进行物理内存分配的过程以及各个函数的作用，以完成练习。
找到`kern/mm/default_pmm.c`文件，对其主要内容进行分析。

1. default_init()
这个函数用于初始化内存管理器。它初始化了一个空闲页面链表（free_list），并将空闲页面数（nr_free）设置为0。
```default_pmm.c
static void
default_init(void) {
    list_init(&free_list);  // 初始化空闲页面链表
    nr_free = 0;            // 初始化空闲页面数为0
}
```
2. default_init_memmap(struct Page *base, size_t n)
这个函数用于将一段物理内存映射到空闲页面链表中。它接受一个页面数组的基地址（base）和页面数（n），然后将这些页面标记为未使用，并加入到空闲页面链表中。
```default_pmm.c
struct Page {
    int ref;//页面的引用计数。                       
    uint64_t flags;//一个无符号64位整数，用于存储与页面相关的各种标志             
    unsigned int property;//一个无符号整数，用于存储页面的属性。         
    list_entry_t page_link;//一个链表条目，它用于将页面结构体连接到链表中。        
};

static void
default_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);//确保至少有一个页面需要被初始化
    struct Page *p = base;
    for (; p != base + n; p ++) {
        assert(PageReserved(p));//确保该页面已经被标记为保留
        p->flags = p->property = 0;//将标志位flags与页属性property均设为0
        set_page_ref(p, 0);//将当前页面的引用计数设置为0，表示当前页面没有被任何进程使用。
    }
    base->property = n;
    SetPageProperty(base);
    nr_free += n;
    // 将页面块加入到空闲列表中
    if (list_empty(&free_list)) {//检查空闲页面链表 free_list 是否为空。
        list_add(&free_list, &(base->page_link));//如果链表为空，将 base 页面添加到链表的头部。
    } else {//不为空，执行下面。
        // 插入到列表中，保持地址顺序
        list_entry_t* le = &free_list;
        while ((le = list_next(le)) != &free_list) {//遍历链表，找到正确的插入位置。
            struct Page* page = le2page(le, page_link);
            if (base < page) {
                list_add_before(le, &(base->page_link));
                break;
            } else if (list_next(le) == &free_list) {
                list_add(le, &(base->page_link));
            }
        }
    }
}

```
- Page结构体：
其中的flags前两位，分别为reserved，property。
文中的定义为:
```
#define PG_reserved                 0       // if this bit=1: the Page is reserved for kernel, cannot be used in alloc/free_pages; otherwise, this bit=0 
#define PG_property                 1       // if this bit=1: the Page is the head page of a free memory block(contains some continuous_addrress pages), and can be used in alloc_pages; if this bit=0: if the Page is the the head page of a free memory block, then this Page and the memory block is alloced. Or this Page isn't the head page.
```
- [ ] PG_reserved：如果该位为1，则表示该页面是为内核保留的，不能在 alloc_pages 和 free_pages 函数中使用；如果该位为0，则表示该页面可以被分配和释放。
- [ ] PG_property：如果该位为1，则表示该页面是空闲内存块的头部页面（包含一些连续地址的页面），并且可以在 alloc_pages 函数中使用；如果该位为0，则表示该页面是已分配的内存块的一部分，或者不是空闲内存块的头部页面。

- 另外的property成员表示其之后(包括自己)有多少个空闲页，因此赋为n。
- [ ] 在管理连续的空闲页面时，我们只需标记起始页面，为其设置属性`property`来指示整个空闲块包含的页面数`n`，并为其设置特殊的属性标志位。对于这个块中的其余页面，我们重置它们的所有标志和属性，并将引用计数归零，这样就可以确保只有空闲块的头部页面携带了整个块的信息，而块内的其他页面则保持清洁状态，随时准备被分配。
3. default_alloc_pages(size_t n)
当我们给定一个页数n需要进行内存分配时，该算法会从空闲页块链表中找到第一个适合大小的空闲页块，然后进行分配。
```
static struct Page *
default_alloc_pages(size_t n) {//需要分配的连续物理页面数量。
    assert(n > 0);
    if (n > nr_free) {
        return NULL;//接着检查请求的页面数是否超过了当前系统中的空闲页面总数 (nr_free)，如果是，则返回 NULL 表示无法满足请求。
    }
    struct Page *page = NULL;
    list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list) {//查找合适页面
        struct Page *p = le2page(le, page_link);
        if (p->property >= n) {
            page = p;
            break;
        }
    }
    if (page != NULL) {//如果找到一个足够大的页面，那么这个页面将成为要分配的页面。
        list_entry_t* prev = list_prev(&(page->page_link));
        list_del(&(page->page_link));//将这个页面从自由页面列表中移除，并记录下它的前一个节点，以便后续插入分割后的剩余页面。
        if (page->property > n) {//页面分割
            struct Page *p = page + n;//如果找到的页面大小大于请求的页面数 n，则需要将多余的页面分割出来。
            p->property = page->property - n;
            SetPageProperty(p);
            list_add(prev, &(p->page_link));
        }
        nr_free -= n;//分配完成后，更新系统中空闲页面的计数 nr_free，减少已经分配出去的页面数 n。接着调用 ClearPageProperty 函数来初始化或清除已分配页面的内容。
        ClearPageProperty(page);
    }
    return page;
}
```
4. default_free_pages(struct Page *base, size_t n)
此函数用于释放之前分配的一组连续的物理页面，并将它们重新加入到自由页面列表中。
```
static void
default_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p ++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        set_page_ref(p, 0);
    }//清除页面标志，然后将所有页面的标志位 flags 清零，并将引用计数 ref 设置为 0。
    base->property = n;
    SetPageProperty(base);
    nr_free += n;//更新空闲页面计数

    if (list_empty(&free_list)) {
        list_add(&free_list, &(base->page_link));
    } else {
        list_entry_t* le = &free_list;
        while ((le = list_next(le)) != &free_list) {
            struct Page* page = le2page(le, page_link);
            if (base < page) {
                list_add_before(le, &(base->page_link));
                break;
            } else if (list_next(le) == &free_list) {
                list_add(le, &(base->page_link));
            }//如果自由页面列表为空，则直接将释放的页面插入到列表的头部；
            //如果列表非空，则找到一个合适的插入位置，使得列表中的页面按地址顺序排列。
        }
    }

    list_entry_t* le = list_prev(&(base->page_link));
    if (le != &free_list) {
        p = le2page(le, page_link);
        if (p + p->property == base) {
            p->property += base->property;
            ClearPageProperty(base);
            list_del(&(base->page_link));
            base = p;
        }
    }

    le = list_next(&(base->page_link));
    if (le != &free_list) {
        p = le2page(le, page_link);
        if (base + base->property == p) {
            base->property += p->property;
            ClearPageProperty(p);
            list_del(&(p->page_link));
        }//合并相邻的空闲页面块
    }
}
```
- 问题：你的 first fit 算法是否有进一步的改进空间？
所谓First Fit算法就是当需要分配页面时，它会从空闲页块链表中找到第一个适合大小的空闲页块，然后进行分配。当释放页面时，它会将释放的页面添加回链表，并在必要时合并相邻的空闲页块，以最大限度地减少内存碎片。
- - Next-Fit 算法:
Next-Fit 算法是在 First-Fit 算法的基础上进行的一个小改进。Next-Fit 会记住上一次访问的位置，并从那里开始搜索下一个合适的空闲块。这样可以在一定程度上减少碎片化，尤其是在内存分配模式呈现某种规律的情况下。
- - Best-Fit 算法:
Best-Fit 算法会在空闲列表中寻找最适合的（即刚好够用的）空闲块。这种方法可以减少内部碎片，但可能会增加搜索的时间复杂度。此外，Best-Fit 容易导致外部碎片化的问题，因为随着时间的推移，较大的空闲块会被分割成许多较小的块。
- - Worst-Fit 算法:
Worst-Fit 算法选择最大的空闲块进行分配。这种方法可以减少外部碎片化，但可能会导致内部碎片化增加，因为它总是倾向于使用较大的空闲块。
- - Buddy System:
Buddy System 是一种特殊的内存分配算法，它通过将内存块分成大小为 2 的幂次方的“伙伴”块来进行分配和释放。这种方法非常适合于连续内存块的分配，因为它能够自动合并相邻的空闲块，并且分配和释放操作的时间复杂度相对较低。
- - 多级分配策略:
可以采用多级分配策略，例如，在分配内存时，首先尝试使用 Best-Fit 算法，如果找不到合适的块，则退回到 First-Fit 或 Worst-Fit 算法。这样的策略可以根据实际需求动态调整，以达到较好的平衡。
- - 内存压缩技术:
对于一些特定的应用场景，可以考虑使用内存压缩技术来减少所需的物理内存空间。这种方法在不影响性能的前提下，可以有效利用有限的物理内存资源。
- - 智能预测机制:
引入机器学习或智能预测机制来预测未来的内存使用模式，并据此优化内存分配策略，减少碎片化。
- - 改进的数据结构:
使用更高效的数据结构来存储和管理空闲块，例如使用平衡树（如 AVL 树或红黑树）来替代简单的链表，可以提高查找和插入的效率。
### 练习2：实现Best-Fit连续物理内存分配算法
对几个主要函数的改动如下（code之间的即为改动地方）：
1. static void best_fit_init_memmap(struct Page *****base, size_t n)
初始化内存映射，将指定范围内的页面标记为可用，并将它们添加到自由页面列表中。
```
static void
best_fit_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p ++) {
        assert(PageReserved(p));

        /*LAB2 EXERCISE 2: YOUR CODE*/ 
        // 清空当前页框的标志和属性信息，并将页框的引用计数设置为0
        //code
        p->flags = p->property = 0;
        set_page_ref(p, 0);
        //code
    }
    base->property = n;
    SetPageProperty(base);
    nr_free += n;
    if (list_empty(&free_list)) {
        list_add(&free_list, &(base->page_link));
    } else {
        list_entry_t* le = &free_list;
        while ((le = list_next(le)) != &free_list) {
            struct Page* page = le2page(le, page_link);
             /*LAB2 EXERCISE 2: YOUR CODE*/ 
            // 编写代码
            // 1、当base < page时，找到第一个大于base的页，将base插入到它前面，并退出循环
            // 2、当list_next(le) == &free_list时，若已经到达链表结尾，将base插入到链表尾部
            //code
            if (base < page){
                list_add_before(le, &(base->page_link));
                break;
            }
            else if (list_next(le) == &free_list){
                list_add_after(le, &(base->page_link));
            }  
            //code
        }
    }
}
```
2. static struct Page * best_fit_alloc_pages(size_t n)
根据 Best-fit 策略分配一组连续的物理页面。这个函数遍历自由页面列表，找到最适合的（即浪费最少的）页面块，并将其分配给请求者。
```
static struct Page *
best_fit_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > nr_free) {
        return NULL;
    }
    struct Page *page = NULL;
    list_entry_t *le = &free_list;
    size_t min_size = nr_free + 1;
     /*LAB2 EXERCISE 2: YOUR CODE*/ 
    // 下面的代码是first-fit的部分代码，请修改下面的代码改为best-fit
    // 遍历空闲链表，查找满足需求的空闲页框
    // 如果找到满足需求的页面，记录该页面以及当前找到的最小连续空闲页框数量
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        //code
        if (p->property >= n) {
            size_t waste = p->property - n;
            if (p->property >= n && p->property < min_size) {
            min_size = p->property;
            page = p;
        }
    }//code

    if (page != NULL) {
        list_entry_t* prev = list_prev(&(page->page_link));
        list_del(&(page->page_link));
        if (page->property > n) {
            struct Page *p = page + n;
            p->property = page->property - n;
            SetPageProperty(p);
            list_add(prev, &(p->page_link));
        }
        nr_free -= n;
        ClearPageProperty(page);
    }
    return page;
  }
}
```
3. static void best_fit_free_pages(struct Page *****base, size_t n)
释放一组连续的物理页面，并将它们重新加入到自由页面列表中。同时，这个函数还会尝试合并相邻的空闲页面块，以减少内存碎片。
```
static void
best_fit_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p ++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        set_page_ref(p, 0);
    }
    /*LAB2 EXERCISE 2: YOUR CODE*/ 
    // 编写代码
    // 具体来说就是设置当前页块的属性为释放的页块数、并将当前页块标记为已分配状态、最后增加nr_free的值
    //code
    base->property = n;
    SetPageProperty(base);
    nr_free += n;
    //code
    
    if (list_empty(&free_list)) {
        list_add(&free_list, &(base->page_link));
    } else {
        list_entry_t* le = &free_list;
        while ((le = list_next(le)) != &free_list) {
            struct Page* page = le2page(le, page_link);
            if (base < page) {
                list_add_before(le, &(base->page_link));
                break;
            } else if (list_next(le) == &free_list) {
                list_add(le, &(base->page_link));
            }
        }
    }

    list_entry_t* le = list_prev(&(base->page_link));
    if (le != &free_list) {
        p = le2page(le, page_link);
        /*LAB2 EXERCISE 2: YOUR CODE*/ 
         // 编写代码
        // 1、判断前面的空闲页块是否与当前页块是连续的，如果是连续的，则将当前页块合并到前面的空闲页块中
        // 2、首先更新前一个空闲页块的大小，加上当前页块的大小
        // 3、清除当前页块的属性标记，表示不再是空闲页块
        // 4、从链表中删除当前页块
        // 5、将指针指向前一个空闲页块，以便继续检查合并后的连续空闲页块
        //code
        if (base + base->property == p) {
            base->property += p->property;
            ClearPageProperty(p);
            list_del(&(p->page_link));
        }
        //code
    }

    le = list_next(&(base->page_link));
    if (le != &free_list) {
        p = le2page(le, page_link);
        if (base + base->property == p) {
            base->property += p->property;
            ClearPageProperty(p);
            list_del(&(p->page_link));
        }
    }
}

static size_t
best_fit_nr_free_pages(void) {
    return nr_free;
}
```
- 问题：你的 Best-Fit 算法是否有进一步的改进空间？
- - 内存碎片管理：
外部碎片化：Best-Fit 算法可以减少内部碎片，但仍然可能导致外部碎片化。外部碎片是指系统中有足够的总空闲空间，但由于分布得太散，无法满足单个大块的分配请求。
解决方案：引入内存紧缩（compaction）技术，将已分配的内存块移动，以合并分散的小块空闲空间。
- - 提高分配速度：
搜索时间：Best-Fit 算法需要遍历整个自由页面列表来寻找最合适的块，这可能导致较长的搜索时间。
解决方案：使用索引或其他数据结构（如平衡树）来快速定位最佳匹配的空闲块，减少每次分配时的搜索成本。
- - 数据结构优化：
维护有序列表：虽然 Best-Fit 算法通常按地址排序自由页面列表，但这可能不是最优的数据结构选择。
解决方案：考虑使用哈希表或其他高级数据结构来更快地检索和插入空闲块。
- - 内存预分配：
预分配策略：对于可预见的大内存需求，可以预先分配一定大小的内存块，以减少频繁分配带来的开销。
解决方案：引入内存池技术，预先分配一定大小的内存块供后续分配请求使用。
- - 多级分配：
混合策略：结合多种分配策略，比如先尝试 Best-Fit，如果找不到合适块，则退回到其他策略（如 First-Fit 或 Worst-Fit）。
解决方案：根据实际情况动态调整分配策略，以达到最好的效果。
- - 智能预测：
历史数据：利用历史数据来预测未来的内存使用模式，从而优化内存分配策略。
解决方案：引入机器学习模型或其他智能预测机制来辅助内存分配决策。
- - 内存压缩：
压缩技术：对于某些类型的数据，可以使用内存压缩技术来减少实际占用的物理内存。
解决方案：在分配内存之前，评估数据的压缩可能性，从而减少所需的实际物理内存空间。
- - 内存回收机制：
垃圾回收：对于长时间未使用的内存块，可以考虑定期进行垃圾回收。
解决方案：引入类似于垃圾回收机制的功能，定期检查并释放不再使用的内存块。
（部分方法与First-Fit的改进相似）
### 扩展练习1Challenge：buddy system（伙伴系统）分配算法
1. Buddy System 简介（代码的讲解，主要要放在注释中）
Buddy System 是一种经典的内存分配算法，被广泛应用于操作系统内核中进行内存管理。它的核心思想是将内存按照2的幂次法则进行划分，即每次分配或释放内存时，内存块的大小总是2的某个幂次方。这种划分方式使得内存的分配和回收变得高效，同时也减少了内存碎片的问题。
- 优点:
快速搜索合并（O(logN)时间复杂度）。
较低的外部碎片。
- 缺点:
内部碎片，因为按照2的幂次法则来划分内存块，可能会导致某些情况下内存利用不充分。
2. 运作机理
当操作系统启动时，会调用buddy_init_memmap函数来初始化伙伴系统的内存映射。这个函数会计算出可用内存的大小，并初始化管理数组buddy_page，将所有可用内存页标记为空闲。

当需要分配内存时，buddy_alloc_pages函数会被调用。这个函数会遍历管理数组，找到第一个大小足够的空闲块，并将其标记为已分配。同时，它会更新管理数组中的父节点状态，以反映内存块的分配情况。

当内存不再需要时，buddy_free_pages函数会被调用。这个函数会遍历要释放的内存块中的每个页，并尝试将当前空闲块与其相邻的空闲块合并，形成更大的空闲块。这个过程会一直进行，直到遇上未释放的相邻块或达到内存的上限。
### 扩展练习2Challenge：任意大小的内存单元slub分配算法
### 扩展练习3Challenge：硬件的可用物理内存范围的获取方法
1. BIOS中断调用：
在x86架构中，BIOS通过中断调用INT 15h提供获取内存大小的服务。具体来说，E820h函数号用于获取系统的内存映射，它会填充一个或多个描述内存区域的Memory Map Descriptor结构。
2. ACPI (Advanced Configuration and Power Interface)：
在现代的x86架构系统中，ACPI提供了一种标准化的方法来获取系统硬件信息，包括内存映射。RSDP（Root System Description Pointer）和SDT（System Description Table）结构中包含了关于内存布局的信息。
3. UEFI (Unified Extensible Firmware Interface)：
UEFI是BIOS的现代替代品，它提供了更加丰富和灵活的接口来获取系统信息，包括内存布局。UEFI的GetMemoryMap函数可以直接获取系统的物理内存布局。
4. 硬件探针（Memory Probe）：
在某些嵌入式系统或定制硬件中，操作系统可能需要通过直接读取硬件寄存器来探测可用的物理内存范围。
5. 设备树（Device Tree）：
在ARM和其他一些非x86架构中，设备树是一种用于描述硬件配置的高级数据结构。操作系统可以通过解析设备树来获取内存布局信息。
6. 引导加载程序（Bootloader）：
引导加载程序通常在操作系统内核之前运行，它负责初始化硬件和准备操作系统的启动环境。引导加载程序可以向操作系统内核提供关于可用物理内存范围的信息。
7. 架构特定的方法：
某些处理器架构可能提供特定的指令或机制来获取内存布局信息。例如，RISC-V架构中的misa寄存器包含了关于内存模型的信息。
8. 系统启动参数：
在某些情况下，系统启动时可能会传递一些参数，这些参数中可能包含了内存布局的信息。
9. 固件接口：
一些系统可能通过固件接口（如SeaBIOS、OpenHVM）来获取内存信息。

### 补充
1. 在RISC-V架构中，为了实现分页机制，需要建立虚拟内存和物理内存的页映射关系，这通常涉及创建和配置三级页表。以下是详细步骤和考虑因素：
- 确定需要映射的物理内存空间：
通常，所有的物理内存空间都需要建立页映射关系，以便操作系统可以管理内存并将其分配给进程。
- 具体的页映射关系：
页映射关系定义了虚拟页（VPN）和物理页帧（PPN）之间的对应关系。在Sv39模式下，39位虚拟地址空间通过三级页表转换为56位物理地址空间。
- 页目录表的起始地址：
页目录表的起始地址通常设置在页表寄存器satp中。在RISC-V中，satp寄存器包含模式字段、ASID（Address Space Identifier）和PPN（Physical Page Number），其中PPN是根页表的物理页号。
- 页表的起始地址和所需空间：
页表的起始地址应选择一个合适的物理地址，并且必须按照页表项大小（通常是8字节）对齐。每个页表需要的空间是4KB（512个页表项 * 8字节/项）。
- 设置页目录表项的内容：
页目录表项（PDE）需要包含下一级页表的物理页号（PPN）以及必要的权限和状态标志。
- 设置页表项的内容：
页表项（PTE）包含物理页帧的页号、权限标志（如读/写、执行、用户/超级用户模式访问权限）、有效位（V）、以及其他状态和控制标志（如脏位、访问位、全局位等）。
2. Bare模式：没有操作系统支持的情况下，直接在硬件上运行程序的模式
3. 进入虚拟内存访问方式，需要如下步骤：
分配页表所在内存空间并初始化页表；
设置好页基址寄存器（指向页表起始地址）；
刷新 TLB。
4. MMIO（Memory Mapped I/O，内存映射I/O）是一种计算机总线技术，它允许将I/O设备（如图形卡、声卡等）的寄存器映射到系统的内存地址空间中
5. DTB（Device Tree Blob）是一种用于在操作系统内核和硬件之间传递硬件布局信息的数据结构。它以二进制形式存在，由设备树源文件（DTS）通过设备树编译器（DTC）编译而成。
6. stap是控制地址翻译和保护的控制状态寄存器（CSR）
7. ASID（Address Space Identifier，地址空间标识符）是一种用于在虚拟内存系统中区分不同地址空间的机制。



