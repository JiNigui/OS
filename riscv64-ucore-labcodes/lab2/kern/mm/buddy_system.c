#include <pmm.h>
#include <list.h>
#include <string.h>
#include <assert.h>
#include <buddy_system.h>
#include <memlayout.h>

#define LEFT_LEAF(index)   ((index) << 1)//左孩子索引
#define RIGHT_LEAF(index)  (((index) << 1) + 1)//右孩子索引
#define PARENT(index)       ((index) >> 1)//父节点索引
#define MAX(a,b) (a < b ? b : a)

static unsigned int* buddy_page;//用于存储管理页的状态
static unsigned int buddy_page_num;//管理页的总数
static unsigned int useable_page_num;//可用的页数
static struct Page* useable_page_base;//可用页的基地址。

//初始化 Buddy System 的内存映射
static void buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);

    //计算可使用内存页数并管理内存页数
    useable_page_num = 1;//初始化 useable_page_num 为1，这是伙伴系统中最小的内存块大小（2的0次方）。
    for (int i = 1; i < 30 && useable_page_num + (useable_page_num >> 9) < n; i++, useable_page_num <<= 1);
    useable_page_num >>= 1;
    buddy_page_num = (useable_page_num >> 9) + 1;//计算管理内存所需的页数。每9个内存块（因为2的9次方是512，是页表可以管理的最大页数）需要一个管理单元，再加上1。

    //可使用内存页基址
    useable_page_base = base + buddy_page_num;

    //循环将管理内存页标记为保留状态，因为这些页用于内存管理，不应被分配出去。
    for (size_t i = 0; i != buddy_page_num; i++) {
        SetPageReserved(base + i);
    }

    //循环将可用内存页标记为未保留，并设置它们的属性和引用计数为0。
    for (size_t i = buddy_page_num; i != n; i++) {
        ClearPageReserved(base + i);
        SetPageProperty(base + i);
        set_page_ref(base + i, 0);
    }

    // 初始化管理页
    buddy_page = (unsigned int*)KADDR(page2pa(base));//KADDR 将物理地址转换为内核虚拟地址，page2pa 将页框架转换为物理地址。
    for (size_t i = useable_page_num; i < useable_page_num << 1; i++) {
        buddy_page[i] = 1;
    }//初始化管理内存页，将所有可用内存页标记为1，表示它们是空闲的。
    for (size_t i = useable_page_num - 1; i > 0; i--) {
        buddy_page[i] = buddy_page[i << 1] << 1;
    }//从下往上更新管理内存页，将每个节点的大小设置为其子节点大小的两倍，构建伙伴系统树结构的关键步骤。


}

//分配内存块
static struct Page* buddy_alloc_pages(size_t n) {
    assert(n > 0);//使用断言确保传入的内存块大小 n 大于0

    //查找合适的内存块
    unsigned int index = 1;
    while (1) {
        if (buddy_page[index << 1] >= n) {
            index = LEFT_LEAF(index);
        } else if (buddy_page[(index << 1) + 1] >= n) {
            index = RIGHT_LEAF(index);
        } else {
            break;
        }
    }

    //分配内存
    unsigned int size = buddy_page[index];//获取当前索引节点表示的内存块大小。
    buddy_page[index] = 0;

    struct Page* new_page = &useable_page_base[index * size - useable_page_num];
    for (struct Page* p = new_page; p != new_page + size; p++) {
        ClearPageProperty(p);
        set_page_ref(p, 0);
    }//遍历新分配的内存块中的每个页，清除它们的属性并设置引用计数为0。

    // 更新父节点
    index = PARENT(index);
    while (index > 0) {
        buddy_page[index] = MAX(buddy_page[LEFT_LEAF(index)], buddy_page[RIGHT_LEAF(index)]);
        index = PARENT(index);
    }//从当前节点开始，向上遍历树，更新每个父节点的内存块大小为两个孩子节点中较大的一个。
    //这是为了维护树的属性，确保每个节点的内存块大小是其子节点中较大的一个。

    return new_page;
}

//释放内存块
static void buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);

    for (struct Page *p = base; p != base + n; p++) {
        assert(!PageReserved(p) && !PageProperty(p));
        SetPageProperty(p);
        set_page_ref(p, 0);
    }//遍历要释放的内存块中的每个页，确保它们不是保留页，并且没有属性。然后设置属性，并把引用计数设置为0。

    unsigned int index = useable_page_num + (unsigned int)(base - useable_page_base), size = 1;
    //计算要释放的内存块在管理数组 buddy_page 中的索引。
    while (buddy_page[index] > 0) {
        index = index >> 1;
        size <<= 1;
    }//向上遍历管理数组，直到找到对应的空闲块的起始索引。同时，计算出这个空闲块的大小。

    buddy_page[index] = size;
    while ((index = index >> 1) > 0) {
        size <<= 1;
        if (buddy_page[LEFT_LEAF(index)] + buddy_page[RIGHT_LEAF(index)] == size) {
            buddy_page[index] = size;
        } else {
            buddy_page[index] = MAX(buddy_page[LEFT_LEAF(index)], buddy_page[RIGHT_LEAF(index)]);
        }//检查当前节点的左右孩子节点是否都是空闲的，
        //如果是，说明可以合并这两个孩子节点为一个更大的空闲块。如果不是，说明只能保留较大的那个孩子节点作为空闲块。
    }
}

//获取剩余的空闲页数
static size_t buddy_nr_free_pages(void) {
    return buddy_page[1];
}

//检查 Buddy System
//通过一系列的断言来检查伙伴系统的正确性，包括分配过大的页数、分配和释放不同大小的内存块、回收页后再次分配等
static void buddy_check(void) {
    size_t all_pages = nr_free_pages();
    struct Page* p0, *p1, *p2, *p3;

    //分配过大的页数
    assert(alloc_pages(all_pages + 1) == NULL);

    //分配两个组页
    p0 = alloc_pages(1);
    assert(p0 != NULL);
    p1 = alloc_pages(2);
    assert(p1 == p0 + 2);
    assert(!PageReserved(p0) && !PageProperty(p0));
    assert(!PageReserved(p1) && !PageProperty(p1));

    //再分配两个组页
    p2 = alloc_pages(1);
    assert(p2 == p0 + 1);
    p3 = alloc_pages(8);
    assert(p3 == p0 + 8);
    assert(!PageProperty(p3) && !PageProperty(p3 + 7) && PageProperty(p3 + 8));

    //回收页
    free_pages(p1, 2);
    assert(PageProperty(p1) && PageProperty(p1 + 1));
    assert(p1->ref == 0);
    free_pages(p0, 1);
    free_pages(p2, 1);

    //回收后再分配
    p2 = alloc_pages(3);
    assert(p2 == p0);
    free_pages(p2, 3);
    assert((p2 + 2)->ref == 0);
    assert(nr_free_pages() == all_pages >> 1);

    p1 = alloc_pages(129);
    assert(p1 == p0 + 256);
    free_pages(p1, 256);
    free_pages(p3, 8);
}

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = NULL,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};